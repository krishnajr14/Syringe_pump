/*
 * main.cpp — Zephyr firmware entry point
 *
 * THIS IS THE ONLY FILE THAT INCLUDES ZEPHYR HEADERS.
 * All business logic lives in include/ and src/ with zero Zephyr deps.
 *
 * Responsibilities:
 *   1. Construct concrete HAL implementations (stepper, UART, LED)
 *      using placement-new into static buffers (zero heap).
 *   2. Wire them into AlarmManager, VolumeTracker, PumpStateMachine,
 *      CommandParser.
 *   3. Spawn two Zephyr threads:
 *        pump_tick_thread  — calls psm.tick() every 200 µs
 *        uart_rx_thread    — feeds UART bytes to CommandParser
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>

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

    void enable() noexcept override {
        gpio_pin_set_dt(&en_, 0);   // active-low: 0 = enabled
    }
    void disable() noexcept override {
        gpio_pin_set_dt(&en_, 1);   // active-low: 1 = disabled
    }
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
    void     resetStepCount()      noexcept override { stepCount_ = 0U; }

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

// ============================================================
// Static storage — ALL objects placement-new'd, zero heap
// ============================================================
static constexpr uint32_t NL_PER_STEP   = 3U;       // ~2.58 nL/step rounded up
static constexpr uint32_t TARGET_VOL_UL = 10000U;   // 10 mL default

static VolumeTracker g_tracker{NL_PER_STEP};
static AlarmManager  g_alarms;

// Placement-new buffers (aligned storage, no heap)
static uint8_t buf_stepper [sizeof(ZephyrStepperDriver)] alignas(ZephyrStepperDriver);
static uint8_t buf_uartObs [sizeof(UartAlarmObserver)]   alignas(UartAlarmObserver);
static uint8_t buf_ledObs  [sizeof(LedAlarmObserver)]    alignas(LedAlarmObserver);
static uint8_t buf_psm     [sizeof(PumpStateMachine)]    alignas(PumpStateMachine);
static uint8_t buf_parser  [sizeof(CommandParser)]       alignas(CommandParser);

static ZephyrStepperDriver* g_stepper  = nullptr;
static PumpStateMachine*    g_psm      = nullptr;
static CommandParser*       g_parser   = nullptr;

// ============================================================
// Zephyr threads
// ============================================================

// Pump tick thread — steps motor every 200 µs
K_THREAD_STACK_DEFINE(pump_stack, 1024);
static struct k_thread pump_thread;

static void pump_tick_fn(void*, void*, void*) {
    while (true) {
        if (g_psm != nullptr) {
            g_psm->tick();
        }
        k_sleep(K_USEC(200U));   // 200 µs → max ~5 kHz step rate
    }
}

// UART RX thread — feeds bytes to CommandParser
K_THREAD_STACK_DEFINE(uart_stack, 512);
static struct k_thread uart_thread;

static void uart_rx_fn(void* arg, void*, void*) {
    const struct device* uart = static_cast<const struct device*>(arg);
    uint8_t byte = 0U;
    while (true) {
        if (uart_poll_in(uart, &byte) == 0) {
            if (g_parser != nullptr) {
                g_parser->feedByte(byte);
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
        GPIO_DT_SPEC_GET(DT_NODELABEL(step_pin), gpios);
    static const struct gpio_dt_spec dir_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(dir_pin), gpios);
    static const struct gpio_dt_spec en_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(en_pin), gpios);
    static const struct gpio_dt_spec led_spec =
        GPIO_DT_SPEC_GET(DT_NODELABEL(alarm_led), gpios);

    // Configure GPIO pins
    gpio_pin_configure_dt(&step_spec, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&dir_spec,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&en_spec,   GPIO_OUTPUT_ACTIVE);   // start disabled
    gpio_pin_configure_dt(&led_spec,  GPIO_OUTPUT_INACTIVE);

    // ── Construct HAL objects via placement-new (zero heap) ───────────────
    g_stepper = new (buf_stepper) ZephyrStepperDriver{step_spec, dir_spec, en_spec};

    auto* uartObs = new (buf_uartObs) UartAlarmObserver{uart};
    auto* ledObs  = new (buf_ledObs)  LedAlarmObserver{led_spec};

    g_alarms.registerObserver(uartObs);
    g_alarms.registerObserver(ledObs);

    g_psm    = new (buf_psm)    PumpStateMachine{*g_stepper, g_alarms,
                                                  g_tracker, TARGET_VOL_UL};
    g_parser = new (buf_parser) CommandParser{*g_psm};

    // ── Start threads ─────────────────────────────────────────────────────
    k_thread_create(&pump_thread,
                    pump_stack, K_THREAD_STACK_SIZEOF(pump_stack),
                    pump_tick_fn, nullptr, nullptr, nullptr,
                    K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    k_thread_name_set(&pump_thread, "pump_tick");

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
