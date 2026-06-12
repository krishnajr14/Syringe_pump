#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// VolumeTracker
// Converts TMC2209 microstep pulses to delivered volume.
//
// Fixed-point strategy (avoids float drift over millions of steps):
//   - Accumulate in nanoliters (uint32_t)
//   - Convert to µL only at read time  (volumeUL = accNL / 1000)
//   - 1 µL resolution guaranteed
//
// Default geometry (BD 10 mL syringe + M5 leadscrew + 256-microstep):
//   Syringe bore : 14.5 mm  → cross-section = π × 7.25² = 165.13 mm²
//   Lead per rev : 0.8 mm
//   Steps per rev: 200 full × 256 micro = 51 200 steps/rev
//   Travel/step  : 0.8 / 51200 = 15.625 nm
//   Volume/step  : 165.13 mm² × 15.625e-6 mm = 2.580e-3 µL = 2.58 nL
//   → nLPerStep constructor argument = 3  (conservative rounding)
//
// Pass a different nLPerStep to support other syringe sizes.
// ---------------------------------------------------------------------------
class VolumeTracker {
public:
    explicit VolumeTracker(uint32_t nLPerStep) noexcept;

    // Accumulate steps (called from PumpStateMachine::tick()).
    void addSteps(uint32_t steps) noexcept;

    // Reset all accumulators to zero (called on IDLE entry).
    void reset() noexcept;

    // Delivered volume in whole microliters (floor division).
    uint32_t volumeUL()  const noexcept;

    // Delivered volume in nanoliters (full precision).
    uint32_t volumeNL()  const noexcept;

    // Raw step count since last reset().
    uint32_t stepCount() const noexcept;

private:
    uint32_t nLPerStep_;
    uint32_t accNL_{0};
    uint32_t steps_{0};
};
