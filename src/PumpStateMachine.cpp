#include "syringe/PumpStateMachine.hpp"

// ---------------------------------------------------------------------------
PumpStateMachine::PumpStateMachine(IStepperDriver& stepper,
                                   AlarmManager&   alarms,
                                   VolumeTracker&  tracker,
                                   uint32_t        targetVolumeUL) noexcept
    : stepper_(stepper)
    , alarms_(alarms)
    , tracker_(tracker)
    , targetVolumeUL_(targetVolumeUL)
{}

// ---------------------------------------------------------------------------
// Transition validity table — IEC 60601-2-24 Table 101
// Returns true only for transitions explicitly permitted by the standard.
// ---------------------------------------------------------------------------
bool PumpStateMachine::isValidTransition(PumpState  from,
                                          PumpEvent  event) const noexcept {
    switch (from) { // LCOV_EXCL_LINE
        case PumpState::IDLE:
            return event == PumpEvent::CMD_START;

        case PumpState::PRIMING:
            return event == PumpEvent::PRIMING_DONE
                || event == PumpEvent::CMD_STOP;

        case PumpState::INFUSING:
            return event == PumpEvent::CMD_PAUSE
                || event == PumpEvent::CMD_STOP
                || event == PumpEvent::VOLUME_REACHED
                || event == PumpEvent::OCCLUSION_DETECT;

        case PumpState::PAUSED:
            return event == PumpEvent::CMD_RESUME
                || event == PumpEvent::CMD_STOP;

        case PumpState::OCCLUSION_ALARM:
            return event == PumpEvent::ALARM_CLEARED
                || event == PumpEvent::CMD_STOP;

        case PumpState::COMPLETE:
            return event == PumpEvent::CMD_STOP;   // reset path back to IDLE

        default:    // LCOV_EXCL_LINE
            return false;   // LCOV_EXCL_LINE
    }
}

// ---------------------------------------------------------------------------
bool PumpStateMachine::handleEvent(PumpEvent event) noexcept {
    if (!isValidTransition(state_, event)) {
        return false;
    }

    switch (state_) { // LCOV_EXCL_LINE
        case PumpState::IDLE:
            transitionTo(PumpState::PRIMING); // Guaranteed to be CMD_START
            break;

        case PumpState::PRIMING:
            if (event == PumpEvent::PRIMING_DONE)
                transitionTo(PumpState::INFUSING);
            else 
                transitionTo(PumpState::IDLE); // Guaranteed to be CMD_STOP
            break;

        case PumpState::INFUSING:
            if (event == PumpEvent::CMD_PAUSE)         transitionTo(PumpState::PAUSED);
            else if (event == PumpEvent::CMD_STOP)    transitionTo(PumpState::IDLE);
            else if (event == PumpEvent::VOLUME_REACHED) transitionTo(PumpState::COMPLETE);
            else                                      transitionTo(PumpState::OCCLUSION_ALARM); 
            break;

        case PumpState::PAUSED:
            if (event == PumpEvent::CMD_RESUME)
                transitionTo(PumpState::INFUSING);
            else
                transitionTo(PumpState::IDLE);
            break;

        case PumpState::OCCLUSION_ALARM:
            if (event == PumpEvent::ALARM_CLEARED)
                transitionTo(PumpState::PAUSED);
            else
                transitionTo(PumpState::IDLE);
            break;

        case PumpState::COMPLETE:
            transitionTo(PumpState::IDLE); // Guaranteed to be CMD_STOP
            break;

        default: break; // LCOV_EXCL_LINE
    }
    return true;
}

// ---------------------------------------------------------------------------
// tick() — called every 200 µs from Zephyr pump thread.
// Drives the motor one microstep per call while PRIMING or INFUSING.
// Auto-fires PRIMING_DONE and VOLUME_REACHED when thresholds are hit.
// ---------------------------------------------------------------------------
void PumpStateMachine::tick() noexcept {
    if (state_ == PumpState::PRIMING) {
        stepper_.step();
        ++primingSteps_;
        if (primingSteps_ >= PRIMING_STEP_COUNT) {
            handleEvent(PumpEvent::PRIMING_DONE);
        }
        return;
    }

    if (state_ == PumpState::INFUSING) {
        if (stepIntervalUs_ == 0U) return;   // no rate set — do nothing

        tickAccumulatorUs_ += 200U;           // each tick = 200 µs

        if (tickAccumulatorUs_ >= stepIntervalUs_) {
            tickAccumulatorUs_ = 0U;
            stepper_.step();
            tracker_.addSteps(1U);
            if (tracker_.volumeUL() >= targetVolumeUL_) {
                handleEvent(PumpEvent::VOLUME_REACHED);
            }
        }
    }
}

// ---------------------------------------------------------------------------
void PumpStateMachine::setRate(uint32_t uLperMin) noexcept {
    rateULperMin_ = uLperMin;
    if (uLperMin == 0U) {
        stepIntervalUs_ = 0U;
        return;
    }
    // 1 step = 1 µL
    // µs per step = 60,000,000 / uLperMin
    stepIntervalUs_ = 60'000'000U / uLperMin;
}

uint32_t PumpStateMachine::getRate() const noexcept {
    return rateULperMin_;
}

PumpState PumpStateMachine::currentState() const noexcept {
    return state_;
}

// ---------------------------------------------------------------------------
// State entry actions
// ---------------------------------------------------------------------------
void PumpStateMachine::transitionTo(PumpState next) noexcept {
    state_ = next;
    switch (next) { // LCOV_EXCL_LINE
        case PumpState::IDLE:            enterIdle();           break;
        case PumpState::PRIMING:         enterPriming();        break;
        case PumpState::INFUSING:        enterInfusing();       break;
        case PumpState::PAUSED:          enterPaused();         break;
        case PumpState::OCCLUSION_ALARM: enterOcclusionAlarm(); break;
        case PumpState::COMPLETE:        enterComplete();       break;
        default:    // LCOV_EXCL_LINE
            break;  // LCOV_EXCL_LINE
    }
}

void PumpStateMachine::enterIdle() noexcept {
    stepper_.disable();
    stepper_.resetStepCount();
    tracker_.reset();
    primingSteps_ = 0U;
    alarms_.clear(AlarmType::OCCLUSION);
    alarms_.clear(AlarmType::VOLUME_COMPLETE);
}

void PumpStateMachine::enterPriming() noexcept {
    primingSteps_ = 0U;
    stepper_.setDirection(true);   // forward = infuse direction
    stepper_.enable();
}

void PumpStateMachine::enterInfusing() noexcept {
    tickAccumulatorUs_ = 0U;   // add this line
    stepper_.setDirection(true);
    stepper_.enable();
}

void PumpStateMachine::enterPaused() noexcept {
    stepper_.disable();
}

void PumpStateMachine::enterOcclusionAlarm() noexcept {
    stepper_.disable();
    alarms_.raise(AlarmType::OCCLUSION);
}

void PumpStateMachine::enterComplete() noexcept {
    stepper_.disable();
    alarms_.raise(AlarmType::VOLUME_COMPLETE);
}
