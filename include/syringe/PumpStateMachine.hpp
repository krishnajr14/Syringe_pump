#pragma once

#include "syringe/hal/IStepperDriver.hpp"
#include "syringe/AlarmManager.hpp"
#include "syringe/VolumeTracker.hpp"
#include <cstdint>

// ---------------------------------------------------------------------------
// PumpState — IEC 60601-2-24 Table 101 states
// ---------------------------------------------------------------------------
enum class PumpState : uint8_t {
    IDLE             = 0,
    PRIMING          = 1,
    INFUSING         = 2,
    PAUSED           = 3,
    OCCLUSION_ALARM  = 4,
    COMPLETE         = 5
};

// ---------------------------------------------------------------------------
// PumpEvent — inputs that drive state transitions
// ---------------------------------------------------------------------------
enum class PumpEvent : uint8_t {
    CMD_START         = 0,   // UART: "START"
    CMD_STOP          = 1,   // UART: "STOP"
    CMD_PAUSE         = 2,   // UART: "PAUSE"
    CMD_RESUME        = 3,   // UART: "RESUME"
    PRIMING_DONE      = 4,   // Internal: priming step count reached
    VOLUME_REACHED    = 5,   // Internal: tracker.volumeUL() >= target
    OCCLUSION_DETECT  = 6,   // Internal: simulated / endstop triggered
    ALARM_CLEARED     = 7    // UART: "CLEAR_ALARM"
};

// ---------------------------------------------------------------------------
// PumpStateMachine
// State pattern implementation. Each state has defined entry actions.
// Transition table enforces IEC 60601-2-24 Table 101 validity.
//
// Dependencies injected at construction (no ownership):
//   IStepperDriver& — step / enable / disable motor
//   AlarmManager&   — raise / clear alarms
//   VolumeTracker&  — accumulate delivered volume
//
// tick() must be called periodically (e.g. every 200 µs from a Zephyr
// thread). It steps the motor and auto-fires VOLUME_REACHED when done.
// ---------------------------------------------------------------------------
class PumpStateMachine {
public:
    PumpStateMachine(IStepperDriver& stepper,
                     AlarmManager&   alarms,
                     VolumeTracker&  tracker,
                     uint32_t        targetVolumeUL) noexcept;

    // Process an event. Returns true if transition was valid, false if
    // the event is illegal in the current state (state unchanged).
    bool handleEvent(PumpEvent event) noexcept;

    // Periodic tick — drives stepper and checks completion.
    // Safe to call from any state; no-op in IDLE/PAUSED/ALARM/COMPLETE.
    void tick() noexcept;

    // Set infusion rate (µL/min). Stored; used by Zephyr layer for
    // inter-step delay calculation.
    void     setRate(uint32_t uLperMin) noexcept;
    uint32_t getRate()                  const noexcept;

    PumpState currentState() const noexcept;

private:
    IStepperDriver& stepper_;
    AlarmManager&   alarms_;
    VolumeTracker&  tracker_;

    PumpState state_{PumpState::IDLE};
    uint32_t  targetVolumeUL_;
    uint32_t  rateULperMin_{0};
    uint32_t  primingSteps_{0};
    uint32_t stepIntervalUs_{0U};
    uint32_t tickAccumulatorUs_{0U};

    // Number of microsteps performed during priming (air purge).
    static constexpr uint32_t PRIMING_STEP_COUNT = 500U;

    // Validate transition per IEC 60601-2-24 Table 101.
    bool isValidTransition(PumpState from, PumpEvent event) const noexcept;

    // Apply a validated transition: update state_, call entry action.
    void transitionTo(PumpState next) noexcept;

    // Entry actions — hardware side-effects on state entry.
    void enterIdle()            noexcept;
    void enterPriming()         noexcept;
    void enterInfusing()        noexcept;
    void enterPaused()          noexcept;
    void enterOcclusionAlarm()  noexcept;
    void enterComplete()        noexcept;
};
