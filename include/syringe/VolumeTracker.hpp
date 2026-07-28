#pragma once

#include <cstdint>
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
