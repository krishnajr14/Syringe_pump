#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// IStepperDriver
// Pure interface for a microstep motor driver (e.g. TMC2209).
// Business logic (PumpStateMachine) depends only on this interface —
// never on Zephyr GPIO calls directly.
//
// Concrete implementations:
//   Production : ZephyrStepperDriver  in zephyr/main.cpp
//   Test       : StepperDriverStub    in tests/stubs/
// ---------------------------------------------------------------------------
class IStepperDriver {
public:
    virtual ~IStepperDriver() = default;    // LCOV_EXCL_LINE

    // Enable driver output (TMC2209 EN pin active-low, handled by impl).
    virtual void enable() noexcept = 0;

    // Disable driver output — motor holds or freewheels per config.
    virtual void disable() noexcept = 0;

    // Pulse STEP line once. Each call = one microstep.
    virtual void step() noexcept = 0;

    // Set motor direction.
    // forward = true  → infuse (plunger advances)
    // forward = false → retract
    virtual void setDirection(bool forward) noexcept = 0;

    // Cumulative microstep count since last resetStepCount().
    virtual uint32_t getStepCount() const noexcept = 0;

    // Reset step counter to zero.
    virtual void resetStepCount() noexcept = 0;
};
