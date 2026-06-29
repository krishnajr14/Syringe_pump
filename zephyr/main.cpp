/*
 * main.cpp — Zephyr firmware entry point
 *
 * THIS IS THE ONLY FILE THAT INCLUDES ZEPHYR HEADERS.
 * All business logic lives in include/ and src/ with zero Zephyr deps.
 *
 * Responsibilities:
 *   1. Construct concrete HAL implementations (stepper, UART, LED, buzzer)
 *      using placement-new into static buffers (zero heap).
 *   2. Wire them into AlarmManager, VolumeTracker, PumpStateMachine,
 *      CommandParser.
 *   3. Spawn two Zephyr threads:
 *        pump_tick_thread  — calls psm.tick() every 200 µs
 *        uart_rx_thread    — feeds UART bytes to CommandParser
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <cstring>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>

// Mutex protecting g_psm and g_parser from concurrent access
// between pump_tick_thread and uart_rx_thread.
K_MUTEX_DEFINE(psm_mutex);


// Pure C++ business logic — no Zephyr inside these headers
#include "syringe/PumpStateMachine.hpp"
#include "syringe/VolumeTracker.hpp"
#include "syringe/AlarmManager.hpp"
#include "syringe/CommandParser.hpp"
#include "syringe/hal/IStepperDriver.hpp"
#include "syringe/hal/IAlarmObserver.hpp"

// ============================================================
// Concrete HAL implementations (Zephyr-specific)
// ============================================================

// ------------------------------------------------------------
// ZephyrStepperDriver
// Drives TMC2209 via GPIO bit-bang.
// EN pin is active-low on TMC2209 — enable() sets it LOW.
// ------------------------------------------------------------
class ZephyrStepperDriver final : public IStepperDriver {
public:
    ZephyrStepperDriver(const struct gpio_dt_spec step,
                        const struct gpio_dt_spec dir,
                        const struct gpio_dt_spec en) noexcept
        : step_(step), dir_(dir), en_(en)
    {}

    void enable()  noexcept override { gpio_pin_set_dt(&en_, 1); }
    void disable() noexcept override { gpio_pin_set_dt(&en_, 0); }

    void step() noexcept override {
        gpio_pin_set_dt(&step_, 1);
        k_busy_wait(2U);            // 2 µs STEP pulse HIGH
        gpio_pin_set_dt(&step_, 0);
        ++stepCount_;
    }

    void setDirection(bool fwd) noexcept override {
        gpio_pin_set_dt(&dir_, fwd ? 1 : 0);
    }

    uint32_t getStepCount()  const noexcept override { return stepCount_; }
    void     resetStepCount()      noexcept override { stepCount_ = 0U;   }

private:
    struct gpio_dt_spec step_;
    struct gpio_dt_spec dir_;
    struct gpio_dt_spec en_;
    uint32_t stepCount_{0U};
};

// ------------------------------------------------------------
// UartAlarmObserver
// Sends a human-readable alarm string over UART on every alarm edge.
// ------------------------------------------------------------
class UartAlarmObserver final : public IAlarmObserver {
public:
    explicit UartAlarmObserver(const struct device* uart) noexcept
        : uart_(uart) {}

    void onAlarm(AlarmType type) noexcept override {
        const char* msg = nullptr;
        switch (type) {
            case AlarmType::OCCLUSION:       msg = "ALARM:OCCLUSION\r\n";       break;
            case AlarmType::VOLUME_COMPLETE: msg = "ALARM:VOLUME_COMPLETE\r\n"; break;
            case AlarmType::POWER_FAULT:     msg = "ALARM:POWER_FAULT\r\n";     break;
            default:                         msg = "ALARM:UNKNOWN\r\n";          break;
        }
        txString(msg);
    }

    void onAlarmCleared(AlarmType /*type*/) noexcept override {
        txString("ALARM:CLEARED\r\n");
    }

private:
    const struct device* uart_;

    void txString(const char* s) noexcept {
        while (s && *s) {
            uart_poll_out(uart_, *s++);
        }
    }
};

// ------------------------------------------------------------
// LedAlarmObserver
// Turns the alarm LED on/off on every alarm edge.
// ------------------------------------------------------------
class LedAlarmObserver final : public IAlarmObserver {
public:
    explicit LedAlarmObserver(const struct gpio_dt_spec led) noexcept
        : led_(led) {}

    void onAlarm(AlarmType /*type*/) noexcept override {
        gpio_pin_set_dt(&led_, 1);
    }
    void onAlarmCleared(AlarmType /*type*/) noexcept override {
        gpio_pin_set_dt(&led_, 0);
    }

private:
    struct gpio_dt_spec led_;
};

// ------------------------------------------------------------
// BuzzerAlarmObserver
// Beeps 3 times on alarm, silences on clear.
// ------------------------------------------------------------
class BuzzerAlarmObserver final : public IAlarmObserver {
public:
    explicit BuzzerAlarmObserver(const struct gpio_dt_spec buzzer) noexcept
        : buzzer_(buzzer) {}

    void onAlarm(AlarmType /*type*/) noexcept override {
        for (int i = 0; i < 3; ++i) {
            gpio_pin_set_dt(&buzzer_, 1);
            k_sleep(K_MSEC(200));
            gpio_pin_set_dt(&buzzer_, 0);
            k_sleep(K_MSEC(100));
        }
    }

    void onAlarmCleared(AlarmType /*type*/) noexcept override {
        gpio_pin_set_dt(&buzzer_, 0);
    }

private:
    struct gpio_dt_spec buzzer_;
};

// ============================================================
// Static storage — ALL objects placement-new'd, zero heap
// ============================================================
static constexpr uint32_t NL_PER_STEP   = 1000U;    // 1 step = 1 µL = 1000 nL
static constexpr uint32_t TARGET_VOL_UL = 10000U;   // 10 mL default

static VolumeTracker g_tracker{NL_PER_STEP};
static AlarmManager  g_alarms;

// Placement-new buffers (aligned storage, no heap)
static uint8_t buf_stepper   [sizeof(ZephyrStepperDriver)]  alignas(ZephyrStepperDriver);
static uint8_t buf_uartObs   [sizeof(UartAlarmObserver)]    alignas(UartAlarmObserver);
static uint8_t buf_ledObs    [sizeof(LedAlarmObserver)]     alignas(LedAlarmObserver);
static uint8_t buf_buzzerObs [sizeof(BuzzerAlarmObserver)]  alignas(BuzzerAlarmObserver);
static uint8_t buf_psm       [sizeof(PumpStateMachine)]     alignas(PumpStateMachine);
static uint8_t buf_parser    [sizeof(CommandParser)]        alignas(CommandParser);

static ZephyrStepperDriver* g_stepper = nullptr;
static PumpStateMachine*    g_psm     = nullptr;
static CommandParser*       g_parser  = nullptr;

// ============================================================
// Helper — print string over UART (used before threads start too)
// ============================================================
static void uart_print(const struct device* uart, const char* msg) {
    while (msg && *msg) {
        uart_poll_out(uart, *msg++);
    }
}

// ============================================================
// Pump tick timer — used for precise 200µs synchronisation
static struct k_timer pump_timer;

// Pump tick thread — wakes exactly every 200µs via timer sync
K_THREAD_STACK_DEFINE(pump_stack, 2048);
static struct k_thread pump_thread;

static void pump_tick_fn(void*, void*, void*) {
    k_timer_start(&pump_timer, K_USEC(200), K_USEC(200));

    while (true) {
        k_timer_status_sync(&pump_timer);

        if (g_psm != nullptr) {
            // Try to lock; if busy, skip this tick to keep real-time stability
            if (k_mutex_lock(&psm_mutex, K_NO_WAIT) == 0) {
                g_psm->tick();
                k_mutex_unlock(&psm_mutex);
            }
        }
    }
}

// ============================================================
// uart_rx_thread — feeds bytes to CommandParser, prints replies
// ============================================================
K_THREAD_STACK_DEFINE(uart_stack, 512);
static struct k_thread uart_thread;

static void uart_rx_fn(void* arg, void*, void*) {
    const struct device* uart = static_cast<const struct device*>(arg);
    uint8_t byte = 0U;

    // Buffer to track the last complete command for context-specific replies
    static char     lastCmd[CMD_BUF_SIZE]{};
    static uint8_t  lastPos = 0U;

    uart_print(uart, "=== Syringe Pump Ready ===\r\n");
    uart_print(uart, "Commands: START, STOP, PAUSE, RESUME, SET_RATE <n>, CLEAR_ALARM, SIM_OCCLUSION\r\n");

    while (true) {
        if (uart_poll_in(uart, &byte) == 0 && g_parser != nullptr) {

            // Build lastCmd buffer — mirrors feedByte frame logic
            if (byte == '\n' || byte == '\r') {
                lastCmd[lastPos] = '\0';
                lastPos = 0U;
            } else if (lastPos < CMD_BUF_SIZE - 1U) {
                lastCmd[lastPos++] = static_cast<char>(byte);
            }

            // Feed byte to parser and capture state — both under mutex
            k_mutex_lock(&psm_mutex, K_FOREVER);
            ParseResult r          = g_parser->feedByte(byte);
            bool        wasSetRate = g_parser->lastCommandWasSetRate();
            PumpState   state      = g_psm->currentState();
            k_mutex_unlock(&psm_mutex);

            // Echo the byte back so you can see what you typed
            uart_poll_out(uart, byte);

            // Only reply when full command frame received (newline/CR)
            if (byte != '\n' && byte != '\r') {
                k_yield();
                continue;
            }

            switch (r) {
                case ParseResult::OK:
                    // SET_RATE is state-independent — check first
                    if (wasSetRate) {
                        uart_print(uart, ">> Rate set.\r\n");
                        break;
                    }
                    // CLEAR_ALARM always transitions to PAUSED
                    if (strcmp(lastCmd, "CLEAR_ALARM") == 0) {
                        uart_print(uart, ">> Alarm cleared. Type RESUME to continue.\r\n");
                        break;
                    }
                    // SIM_OCCLUSION reply
                    if (strcmp(lastCmd, "SIM_OCCLUSION") == 0) {
                        uart_print(uart, ">> Occlusion simulated.\r\n");
                        break;
                    }
                    // All other commands — reply based on resulting state
                    switch (state) {
                        case PumpState::PRIMING:
                            uart_print(uart, ">> Priming started...\r\n");
                            break;
                        case PumpState::INFUSING:
                            uart_print(uart, ">> Infusing...\r\n");
                            break;
                        case PumpState::PAUSED:
                            uart_print(uart, ">> Paused.\r\n");
                            break;
                        case PumpState::IDLE:
                            uart_print(uart, ">> Stopped. Back to IDLE.\r\n");
                            break;
                        case PumpState::OCCLUSION_ALARM:
                            uart_print(uart, ">> OCCLUSION ALARM! Type CLEAR_ALARM after fixing.\r\n");
                            break;
                        case PumpState::COMPLETE:
                            uart_print(uart, ">> Infusion complete!\r\n");
                            break;
                        default:
                            break;
                    }
                    break;

                case ParseResult::ERR_UNKNOWN_CMD:
                    uart_print(uart, ">> ERROR: Unknown command.\r\n");
                    break;
                case ParseResult::ERR_BAD_PARAM:
                    uart_print(uart, ">> ERROR: Bad parameter. Example: SET_RATE 120\r\n");
                    break;
                case ParseResult::ERR_EMPTY:
                    break;   // ignore empty lines silently
                case ParseResult::ERR_TOO_LONG:
                    uart_print(uart, ">> ERROR: Command too long.\r\n");
                    break;
                default:
                    break;
            }
        }
        k_yield();
    }
}

// ============================================================
// main
// ============================================================
int main(void) {
    // ── UART (ST-LINK VCP, USART2) ───────────────────────────────────────
    const struct device* uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    __ASSERT(device_is_ready(uart), "UART not ready");

    // ── GPIO specs from device tree overlay ──────────────────────────────
    static const struct gpio_dt_spec step_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(step_pin),   gpios);
    static const struct gpio_dt_spec dir_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(dir_pin),    gpios);
    static const struct gpio_dt_spec en_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(en_pin),     gpios);
    static const struct gpio_dt_spec led_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(alarm_led),  gpios);
    static const struct gpio_dt_spec buzzer_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(buzzer_pin), gpios);

    // Configure all GPIO pins
    gpio_pin_configure_dt(&step_spec,   GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&dir_spec,    GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&en_spec,     GPIO_OUTPUT_INACTIVE);    // start disabled (active-low)
    gpio_pin_configure_dt(&led_spec,    GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&buzzer_spec, GPIO_OUTPUT_INACTIVE);

    // ── Construct all HAL objects via placement-new (zero heap) ──────────
    g_stepper       = new (buf_stepper)   ZephyrStepperDriver{step_spec, dir_spec, en_spec};
    auto* uartObs   = new (buf_uartObs)   UartAlarmObserver{uart};
    auto* ledObs    = new (buf_ledObs)    LedAlarmObserver{led_spec};
    auto* buzzerObs = new (buf_buzzerObs) BuzzerAlarmObserver{buzzer_spec};

    // Register all three observers into AlarmManager
    g_alarms.registerObserver(uartObs);
    g_alarms.registerObserver(ledObs);
    g_alarms.registerObserver(buzzerObs);

    g_psm    = new (buf_psm)    PumpStateMachine{*g_stepper, g_alarms,
                                                  g_tracker, TARGET_VOL_UL};
    g_parser = new (buf_parser) CommandParser{*g_psm};

    // ── Start threads ─────────────────────────────────────────────────────
    // Start hardware timer — exact 200µs interval
    // Init timer (no callback — used only for sync)
    k_timer_init(&pump_timer, nullptr, nullptr);

    k_thread_create(&pump_thread,
                    pump_stack, K_THREAD_STACK_SIZEOF(pump_stack),
                    pump_tick_fn, nullptr, nullptr, nullptr,
                    K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    k_thread_name_set(&pump_thread, "pump_tick");

    k_timer_start(&pump_timer, K_USEC(200), K_USEC(200));

    k_thread_create(&uart_thread,
                    uart_stack, K_THREAD_STACK_SIZEOF(uart_stack),
                    uart_rx_fn,
                    const_cast<struct device*>(uart),
                    nullptr, nullptr,
                    K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
    k_thread_name_set(&uart_thread, "uart_rx");

    // main() returns — Zephyr idle thread takes over
    return 0;
}