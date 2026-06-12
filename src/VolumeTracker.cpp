#include "syringe/VolumeTracker.hpp"

// ---------------------------------------------------------------------------
VolumeTracker::VolumeTracker(uint32_t nLPerStep) noexcept
    : nLPerStep_(nLPerStep)
{}

// ---------------------------------------------------------------------------
void VolumeTracker::addSteps(uint32_t steps) noexcept {
    steps_  += steps;
    accNL_  += steps * nLPerStep_;
}

// ---------------------------------------------------------------------------
void VolumeTracker::reset() noexcept {
    steps_ = 0U;
    accNL_ = 0U;
}

// ---------------------------------------------------------------------------
uint32_t VolumeTracker::volumeUL() const noexcept {
    return accNL_ / 1000U;   // nL → µL  (1 µL = 1000 nL)
}

// ---------------------------------------------------------------------------
uint32_t VolumeTracker::volumeNL() const noexcept {
    return accNL_;
}

// ---------------------------------------------------------------------------
uint32_t VolumeTracker::stepCount() const noexcept {
    return steps_;
}
