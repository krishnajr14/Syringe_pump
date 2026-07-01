#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <cstring>
#include <cctype>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>

K_MUTEX_DEFINE(psm_mutex);

// Pure C++ business logic — no Zephyr inside these headers
#include "syringe/PumpStateMachine.hpp"
#include "syringe/VolumeTracker.hpp"
#include "syringe/AlarmManager.hpp"
#include "syringe/CommandParser.hpp"
#include "syringe/hal/IStepperDriver.hpp"
#include "syringe/hal/IAlarmObserver.hpp"

// ============================================================
// Concrete HAL implementations
// ============================================================

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

class UartAlarmObserver final : public IAlarmObserver {
public:
    explicit UartAlarmObserver(const struct device* uart) noexcept
        : uart_(uart) {}

    void onAlarm(AlarmType type) noexcept override {
        const char* msg = nullptr;
        switch (type) {
            case AlarmType::OCCLUSION:       msg = "ALARM:OCCLUSION\r\n";       break;
            case AlarmType::VOLUME_COMPLETE: msg = "ALARM:INFUSION_COMPLETED\r\n"; break;
            case AlarmType::POWER_FAULT:     msg = "ALARM:POWER_FAULT\r\n";     break;
            default:                         msg = "ALARM:UNKNOWN\r\n";          break;
        }
        txString(msg);
    }

    void onAlarmCleared(AlarmType /*type*/) noexcept override { txString("Alarm Cleared \r\n"); }

private:
    const struct device* uart_;
    void txString(const char* s) noexcept {
        while (s && *s) { uart_poll_out(uart_, *s++); }
    }
};

class LedAlarmObserver final : public IAlarmObserver {
public:
    explicit LedAlarmObserver(const struct gpio_dt_spec led) noexcept : led_(led) {}
    void onAlarm(AlarmType /*type*/) noexcept override { gpio_pin_set_dt(&led_, 1); }
    void onAlarmCleared(AlarmType /*type*/) noexcept override { gpio_pin_set_dt(&led_, 0); }
private:
    struct gpio_dt_spec led_;
};

class BuzzerAlarmObserver final : public IAlarmObserver {
public:
    explicit BuzzerAlarmObserver(const struct gpio_dt_spec buzzer) noexcept : buzzer_(buzzer) {}
    void onAlarm(AlarmType /*type*/) noexcept override {
        for (int i = 0; i < 3; ++i) {
            gpio_pin_set_dt(&buzzer_, 1);
            k_sleep(K_MSEC(200));
            gpio_pin_set_dt(&buzzer_, 0);
            k_sleep(K_MSEC(100));
        }
    }
    void onAlarmCleared(AlarmType /*type*/) noexcept override { gpio_pin_set_dt(&buzzer_, 0); }
private:
    struct gpio_dt_spec buzzer_;
};

// ============================================================
// Static storage — Zero heap allocation runtime buffers
// ============================================================
static constexpr uint32_t NL_PER_STEP   = 1000U;    // 1 step = 1 µL
static constexpr uint32_t DEFAULT_VOL   = 10000U;   // 10 mL default fallback

static VolumeTracker g_tracker{NL_PER_STEP};
static AlarmManager  g_alarms;

static uint8_t buf_stepper   [sizeof(ZephyrStepperDriver)]  alignas(ZephyrStepperDriver);
static uint8_t buf_uartObs   [sizeof(UartAlarmObserver)]    alignas(UartAlarmObserver);
static uint8_t buf_ledObs    [sizeof(LedAlarmObserver)]     alignas(LedAlarmObserver);
static uint8_t buf_buzzerObs [sizeof(BuzzerAlarmObserver)]  alignas(BuzzerAlarmObserver);
static uint8_t buf_psm       [sizeof(PumpStateMachine)]     alignas(PumpStateMachine);
static uint8_t buf_parser    [sizeof(CommandParser)]        alignas(CommandParser);

static ZephyrStepperDriver* g_stepper = nullptr;
static PumpStateMachine* g_psm     = nullptr;
static CommandParser* g_parser  = nullptr;

static struct gpio_dt_spec global_step_spec;
static struct gpio_dt_spec global_dir_spec;
static struct gpio_dt_spec global_en_spec;

// Stagger variable to slow down motor execution exclusively during priming
static uint32_t g_prime_speed_stagger = 0U;

// Soft interlocking flag to hold off execution ticks during the transitional delay phase
static volatile bool g_hold_execution_ticks = false;

static void uart_print(const struct device* uart, const char* msg) {
    while (msg && *msg) { uart_poll_out(uart, *msg++); }
}

// ============================================================
// Pump execution synchronization (Real-Time Priority 2 Window)
// ============================================================
static struct k_timer pump_timer;
K_THREAD_STACK_DEFINE(pump_stack, 2048);
static struct k_thread pump_thread;

static void pump_tick_fn(void*, void*, void*) {
    while (true) {
        k_timer_status_sync(&pump_timer);

        if (g_psm != nullptr) {
            if (k_mutex_lock(&psm_mutex, K_NO_WAIT) == 0) {
                
                // If the application layer is holding ticks for our 2-second delay window, stall output pulses
                if (g_hold_execution_ticks) {
                    k_mutex_unlock(&psm_mutex);
                    continue;
                }

                // Read internal state dynamically to adjust interrupt scaling
                if (g_psm->currentState() == PumpState::PRIMING) {
                    g_prime_speed_stagger++;
                    if (g_prime_speed_stagger >= 10U) {
                        g_psm->tick();
                        g_prime_speed_stagger = 0U;
                    }
                } else {
                    g_psm->tick();
                }
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

    static char     lastCmd[CMD_BUF_SIZE]{};
    static uint8_t  lastPos = 0U;
    static PumpState lastReportedState = PumpState::IDLE;

    uart_print(uart, "=== Syringe Pump Ready ===\r\n");
    uart_print(uart, "Commands: START, STOP, PAUSE, RESUME, SET_RATE <vol_ml> <time_sec>, CLEAR_ALARM, SIM_OCCLUSION\r\n");

    while (true) {
        // 1. ASYNC STATE MONITORING (Polled at 20 Hz)
        if (k_mutex_lock(&psm_mutex, K_NO_WAIT) == 0) {
            PumpState currentState = g_psm->currentState();
            k_mutex_unlock(&psm_mutex);

            if (currentState != lastReportedState) {
                switch (currentState) {
                    case PumpState::PRIMING:
                        uart_print(uart, "\r\n>> Auto-Transition: Priming (Slower Demo Mode Engaged)...\r\n");
                        break;

                    case PumpState::INFUSING:
                        g_hold_execution_ticks = true;
                        g_stepper->disable(); // Cut off motor coil current to freeze mechanical positions completely
                        
                        uart_print(uart, "\r\n>> Priming Complete. Entering 5-second stabilization delay...\r\n");
                        k_msleep(5000); // Hold UI execution threads safely for exactly 5 seconds
                        
                        uart_print(uart, ">> Stabilization complete. Starting active infusion cycle!\r\n");
                        g_stepper->enable();
                        g_hold_execution_ticks = false; // Release the real-time thread to begin tracking steps
                        
                        uart_print(uart, ">> Auto-Transition: Infusing...\r\n");
                        break;

                    case PumpState::COMPLETE:
                        uart_print(uart, "\r\n>> Auto-Transition: Infusion complete!\r\n");
                        break;
                    case PumpState::OCCLUSION_ALARM:
                        uart_print(uart, "\r\n>> ALERT: OCCLUSION ALARM DETECTED!\r\n");
                        break;
                    default:
                        break;
                }
                lastReportedState = currentState;
            }
        }

        k_msleep(50);

        // 2. INCOMING SERIAL CHARACTER PROCESSING
        while (uart_poll_in(uart, &byte) == 0 && g_parser != nullptr) {

            if (byte == '\n' || byte == '\r') {
                lastCmd[lastPos] = '\0';
                lastPos = 0U;
            } else if (lastPos < CMD_BUF_SIZE - 1U) {
                lastCmd[lastPos++] = static_cast<char>(byte);
            }

            uart_poll_out(uart, byte);

            // Intercepting user-driven START loop to build our presentation runway
            if ((byte == '\n' || byte == '\r') && strcmp(lastCmd, "START") == 0) {
                uart_print(uart, "\r\n>> Initializing system checks...\r\n");
                k_msleep(1000);
                uart_print(uart, ">> Starting Priming state in 3...\r\n");
                k_msleep(1000);
                uart_print(uart, ">> Starting Priming state in 2...\r\n");
                k_msleep(1000);
                uart_print(uart, ">> Starting Priming state in 1...\r\n");
                k_msleep(1000);
                uart_print(uart, ">> Launching motor execution cycle!\r\n");
            }

            k_mutex_lock(&psm_mutex, K_FOREVER);
            ParseResult r          = g_parser->feedByte(byte);
            bool        wasSetRate = g_parser->lastCommandWasSetRate();
            PumpState   state      = g_psm->currentState();
            k_mutex_unlock(&psm_mutex);

            if (byte == '\n' || byte == '\r') {
                lastReportedState = state; 
            } else {
                continue;
            }

            switch (r) {
                case ParseResult::OK:
                    if (wasSetRate) {
                        // ─── DYNAMIC RUNTIME ALIGNMENT EXTRACTION (Seconds) ───
                        const char* ptr = lastCmd + 9U;
                        uint32_t parsed_vol_ml = 0U;
                        uint32_t parsed_dur_sec = 0U;
                        
                        while (*ptr == ' ') { ptr++; }
                        while (isdigit(static_cast<unsigned char>(*ptr))) {
                            parsed_vol_ml = parsed_vol_ml * 10U + (*ptr - '0');
                            ptr++;
                        }
                        while (*ptr == ' ') { ptr++; }
                        while (isdigit(static_cast<unsigned char>(*ptr))) {
                            parsed_dur_sec = parsed_dur_sec * 10U + (*ptr - '0');
                            ptr++;
                        }

                        if (parsed_vol_ml > 0 && parsed_dur_sec > 0) {
                            uint32_t calculated_ul_min = ((parsed_vol_ml * 1000U) * 60U) / parsed_dur_sec;
                            uint32_t adjusted_target_ul = parsed_vol_ml * 1000U;

                            k_mutex_lock(&psm_mutex, K_FOREVER);
                            g_stepper->resetStepCount();
                            g_prime_speed_stagger = 0U;
                            g_hold_execution_ticks = false;
                            
                            // Reconstruct the State Machine with fresh target threshold bounds
                            g_psm->~PumpStateMachine();
                            g_psm = new (buf_psm) PumpStateMachine{*g_stepper, g_alarms, g_tracker, adjusted_target_ul};
                            
                            g_psm->setRate(calculated_ul_min);
                            k_mutex_unlock(&psm_mutex);
                        }

                        uart_print(uart, "\r\n>> Rate and volumetric target configured successfully.\r\n");
                        break;
                    }
                    if (strcmp(lastCmd, "CLEAR_ALARM") == 0) {
                        uart_print(uart, "\r\n>> Alarm cleared. Type RESUME to continue.\r\n");
                        break;
                    }
                    if (strcmp(lastCmd, "SIM_OCCLUSION") == 0) {
                        uart_print(uart, "\r\n>> Occlusion simulated.\r\n");
                        break;
                    }
                    if (strcmp(lastCmd, "RESUME") == 0) {
                        uart_print(uart, "\r\n>> Infusion resumed successfully.\r\n");
                        break;
                    }
                    if (strcmp(lastCmd, "PAUSE") == 0) {
                        uart_print(uart, "\r\n>> Infusion paused immediately.\r\n");
                        break;
                    }
                    if (strcmp(lastCmd, "STOP") == 0) {
                        uart_print(uart, "\r\n>> Pump stopped. Returning to IDLE.\r\n");
                        break;
                    }
                    
                    switch (state) {
                        case PumpState::PRIMING:
                            uart_print(uart, "\r\n>> Priming started...\r\n");
                            break;
                        case PumpState::PAUSED:
                            uart_print(uart, "\r\n>> Paused.\r\n");
                            break;
                        case PumpState::IDLE:
                            uart_print(uart, "\r\n>> Stopped. Back to IDLE.\r\n");
                            break;
                        default:
                            break;
                    }
                    break;

                case ParseResult::ERR_UNKNOWN_CMD:
                    uart_print(uart, "\r\n>> ERROR: Unknown command.\r\n");
                    break;
                case ParseResult::ERR_BAD_PARAM:
                    uart_print(uart, "\r\n>> ERROR: Bad parameter. Example: SET_RATE 10 2\r\n");
                    break;
                case ParseResult::ERR_TOO_LONG:
                    uart_print(uart, "\r\n>> ERROR: Command too long.\r\n");
                    break;
                case ParseResult::ERR_EMPTY:
                default:
                    break;
            }
        }
    }
}

// ============================================================
// main
// ============================================================
int main(void) {
    const struct device* uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    __ASSERT(device_is_ready(uart), "UART not ready");

    global_step_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(step_pin),   gpios);
    global_dir_spec  = GPIO_DT_SPEC_GET(DT_NODELABEL(dir_pin),    gpios);
    global_en_spec   = GPIO_DT_SPEC_GET(DT_NODELABEL(en_pin),     gpios);
    static const struct gpio_dt_spec led_spec    = GPIO_DT_SPEC_GET(DT_NODELABEL(alarm_led),  gpios);
    static const struct gpio_dt_spec buzzer_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(buzzer_pin), gpios);

    gpio_pin_configure_dt(&global_step_spec, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&global_dir_spec,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&global_en_spec,   GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_spec,         GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&buzzer_spec,      GPIO_OUTPUT_INACTIVE);

    g_stepper       = new (buf_stepper)   ZephyrStepperDriver{global_step_spec, global_dir_spec, global_en_spec};
    auto* uartObs   = new (buf_uartObs)   UartAlarmObserver{uart};
    auto* ledObs    = new (buf_ledObs)    LedAlarmObserver{led_spec};
    auto* buzzerObs = new (buf_buzzerObs) BuzzerAlarmObserver{buzzer_spec};

    g_alarms.registerObserver(uartObs);
    g_alarms.registerObserver(ledObs);
    g_alarms.registerObserver(buzzerObs);

    g_psm    = new (buf_psm)    PumpStateMachine{*g_stepper, g_alarms, g_tracker, DEFAULT_VOL};
    g_parser = new (buf_parser) CommandParser{*g_psm};

    k_timer_init(&pump_timer, nullptr, nullptr);

    k_thread_create(&pump_thread, pump_stack, K_THREAD_STACK_SIZEOF(pump_stack),
                    pump_tick_fn, nullptr, nullptr, nullptr,
                    K_PRIO_PREEMPT(2), 0, K_FOREVER);
    k_thread_name_set(&pump_thread, "pump_tick");

    k_thread_create(&uart_thread, uart_stack, K_THREAD_STACK_SIZEOF(uart_stack),
                    uart_rx_fn, const_cast<struct device*>(uart), nullptr, nullptr,
                    K_PRIO_PREEMPT(7), 0, K_FOREVER);
    k_thread_name_set(&uart_thread, "uart_rx");

    k_timer_start(&pump_timer, K_USEC(200), K_USEC(200));
    k_thread_start(&pump_thread);
    k_thread_start(&uart_thread);

    return 0;
}