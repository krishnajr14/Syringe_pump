#include <gtest/gtest.h>
#include "syringe/PumpStateMachine.hpp"
#include "syringe/VolumeTracker.hpp"
#include "syringe/AlarmManager.hpp"
#include "StepperDriverStub.hpp"
#include "AlarmObserverStub.hpp"

// ---------------------------------------------------------------------------
// Static instances — zero heap per project rules
// ---------------------------------------------------------------------------
static StepperDriverStub  g_stepper;
static AlarmObserverStub  g_obs;
static VolumeTracker      g_tracker{3U};
static AlarmManager       g_alarms;

// ---------------------------------------------------------------------------
class PsmTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_stepper.resetAll();
        g_obs.reset();
        g_tracker.reset();
        g_alarms = AlarmManager{};
        g_alarms.registerObserver(&g_obs);
    }

    // Helper: build a PSM with given target volume
    PumpStateMachine make(uint32_t targetUL = 10000U) {
        return PumpStateMachine{g_stepper, g_alarms, g_tracker, targetUL};
    }
};

// ── Initial state ────────────────────────────────────────────────────────
TEST_F(PsmTest, InitialState_IsIdle) {
    auto psm = make();
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, InitialState_StepperDisabled) {
    make();
    EXPECT_FALSE(g_stepper.isEnabled());
}

// ── Valid transitions ─────────────────────────────────────────────────────
TEST_F(PsmTest, Idle_Start_GoesToPriming) {
    auto psm = make();
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_START));
    EXPECT_EQ(psm.currentState(), PumpState::PRIMING);
}

TEST_F(PsmTest, Priming_PrimingDone_GoesToInfusing) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::PRIMING_DONE));
    EXPECT_EQ(psm.currentState(), PumpState::INFUSING);
}

TEST_F(PsmTest, Priming_Stop_GoesToIdle) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_STOP));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, Infusing_Pause_GoesToPaused) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_PAUSE));
    EXPECT_EQ(psm.currentState(), PumpState::PAUSED);
}

TEST_F(PsmTest, Infusing_Stop_GoesToIdle) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_STOP));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, Infusing_VolumeReached_GoesToComplete) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::VOLUME_REACHED));
    EXPECT_EQ(psm.currentState(), PumpState::COMPLETE);
}

TEST_F(PsmTest, Infusing_OcclusionDetect_GoesToAlarm) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::OCCLUSION_DETECT));
    EXPECT_EQ(psm.currentState(), PumpState::OCCLUSION_ALARM);
}

TEST_F(PsmTest, Paused_Resume_GoesToInfusing) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::CMD_PAUSE);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_RESUME));
    EXPECT_EQ(psm.currentState(), PumpState::INFUSING);
}

TEST_F(PsmTest, Paused_Stop_GoesToIdle) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::CMD_PAUSE);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_STOP));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, OcclusionAlarm_AlarmCleared_GoesToPaused) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::OCCLUSION_DETECT);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::ALARM_CLEARED));
    EXPECT_EQ(psm.currentState(), PumpState::PAUSED);
}

TEST_F(PsmTest, OcclusionAlarm_Stop_GoesToIdle) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::OCCLUSION_DETECT);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_STOP));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, Complete_Stop_GoesToIdle) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::VOLUME_REACHED);
    EXPECT_TRUE(psm.handleEvent(PumpEvent::CMD_STOP));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

// ── Invalid transitions ───────────────────────────────────────────────────
TEST_F(PsmTest, Invalid_PauseFromIdle) {
    auto psm = make();
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_PAUSE));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, Invalid_ResumeFromIdle) {
    auto psm = make();
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_RESUME));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, Invalid_StartFromInfusing) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_START));
    EXPECT_EQ(psm.currentState(), PumpState::INFUSING);
}

TEST_F(PsmTest, Invalid_OcclusionFromIdle) {
    auto psm = make();
    EXPECT_FALSE(psm.handleEvent(PumpEvent::OCCLUSION_DETECT));
}

TEST_F(PsmTest, Invalid_OcclusionFromPaused) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::CMD_PAUSE);
    EXPECT_FALSE(psm.handleEvent(PumpEvent::OCCLUSION_DETECT));
}

TEST_F(PsmTest, Invalid_ResumeFromPriming) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_RESUME));
}

TEST_F(PsmTest, Invalid_StartFromComplete) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::VOLUME_REACHED);
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_START));
}

TEST_F(PsmTest, Invalid_PauseFromAlarm) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::OCCLUSION_DETECT);
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_PAUSE));
}

// ── Stepper side-effects ──────────────────────────────────────────────────
TEST_F(PsmTest, StepperEnabled_OnPriming) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    EXPECT_TRUE(g_stepper.isEnabled());
    EXPECT_TRUE(g_stepper.isForward());
}

TEST_F(PsmTest, StepperEnabled_OnInfusing) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    EXPECT_TRUE(g_stepper.isEnabled());
}

TEST_F(PsmTest, StepperDisabled_OnPause) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::CMD_PAUSE);
    EXPECT_FALSE(g_stepper.isEnabled());
}

TEST_F(PsmTest, StepperDisabled_OnOcclusionAlarm) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::OCCLUSION_DETECT);
    EXPECT_FALSE(g_stepper.isEnabled());
}

TEST_F(PsmTest, StepperDisabled_OnComplete) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::VOLUME_REACHED);
    EXPECT_FALSE(g_stepper.isEnabled());
}

// ── Alarm side-effects ────────────────────────────────────────────────────
TEST_F(PsmTest, OcclusionAlarm_Raised_OnAlarmEntry) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::OCCLUSION_DETECT);
    EXPECT_TRUE(g_alarms.isActive(AlarmType::OCCLUSION));
    EXPECT_EQ(g_obs.raiseCount(), 1U);
    EXPECT_EQ(g_obs.lastRaised(), AlarmType::OCCLUSION);
}

TEST_F(PsmTest, VolumeComplete_Alarm_Raised_OnCompleteEntry) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::VOLUME_REACHED);
    EXPECT_TRUE(g_alarms.isActive(AlarmType::VOLUME_COMPLETE));
    EXPECT_EQ(g_obs.lastRaised(), AlarmType::VOLUME_COMPLETE);
}

TEST_F(PsmTest, OcclusionAlarm_Cleared_OnIdleEntry) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::OCCLUSION_DETECT);
    psm.handleEvent(PumpEvent::CMD_STOP);
    EXPECT_FALSE(g_alarms.isActive(AlarmType::OCCLUSION));
}

// ── Rate ──────────────────────────────────────────────────────────────────
TEST_F(PsmTest, SetRate_StoredAndRetrieved) {
    auto psm = make();
    psm.setRate(250U);
    EXPECT_EQ(psm.getRate(), 250U);
}

TEST_F(PsmTest, DefaultRate_IsZero) {
    auto psm = make();
    EXPECT_EQ(psm.getRate(), 0U);
}

// ── Tick ──────────────────────────────────────────────────────────────────
TEST_F(PsmTest, Tick_InInfusing_AccumulatesSteps) {
    auto psm = make(10000U);
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.tick();
    psm.tick();
    EXPECT_EQ(g_tracker.stepCount(), 2U);
}

TEST_F(PsmTest, Tick_InPriming_DoesNotAccumulateVolume) {
    auto psm = make(10000U);
    psm.handleEvent(PumpEvent::CMD_START);
    for (int i = 0; i < 10; ++i) psm.tick();
    EXPECT_EQ(g_tracker.volumeNL(), 0U);
}

TEST_F(PsmTest, Tick_InIdle_NoStepping) {
    auto psm = make(10000U);
    psm.tick();
    EXPECT_EQ(g_stepper.steps(), 0U);
}

TEST_F(PsmTest, Tick_AutoTransition_ToComplete_WhenVolumeReached) {
    // Use 1000 nL/step so 1 step = 1 µL = target
    static VolumeTracker  localTracker{1000U};
    static AlarmManager   localAlarms;
    static AlarmObserverStub localObs;
    localTracker.reset();
    localObs.reset();
    localAlarms = AlarmManager{};
    localAlarms.registerObserver(&localObs);

    PumpStateMachine psm{g_stepper, localAlarms, localTracker, 1U};
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.tick();   // 1 step → 1000 nL = 1 µL ≥ target → COMPLETE

    EXPECT_EQ(psm.currentState(), PumpState::COMPLETE);
    EXPECT_EQ(localObs.raiseCount(), 1U);
    EXPECT_EQ(localObs.lastRaised(), AlarmType::VOLUME_COMPLETE);
}

TEST_F(PsmTest, Tick_AutoTransition_PrimingDone_AfterPrimingSteps) {
    auto psm = make(10000U);
    psm.handleEvent(PumpEvent::CMD_START);
    EXPECT_EQ(psm.currentState(), PumpState::PRIMING);
    // PRIMING_STEP_COUNT = 500
    for (int i = 0; i < 500; ++i) psm.tick();
    EXPECT_EQ(psm.currentState(), PumpState::INFUSING);
}

TEST_F(PsmTest, Branch_Idle_InvalidEvent_VolumeReached) {
    auto psm = make();
    // IDLE has no VOLUME_REACHED transition
    EXPECT_FALSE(psm.handleEvent(PumpEvent::VOLUME_REACHED));
    EXPECT_EQ(psm.currentState(), PumpState::IDLE);
}

TEST_F(PsmTest, Branch_Priming_InvalidEvent_Pause) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    // PRIMING has no CMD_PAUSE transition
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_PAUSE));
    EXPECT_EQ(psm.currentState(), PumpState::PRIMING);
}

TEST_F(PsmTest, Branch_Infusing_InvalidEvent_AlarmCleared) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    // INFUSING has no ALARM_CLEARED transition
    EXPECT_FALSE(psm.handleEvent(PumpEvent::ALARM_CLEARED));
    EXPECT_EQ(psm.currentState(), PumpState::INFUSING);
}

TEST_F(PsmTest, Branch_Paused_InvalidEvent_OcclusionDetect) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::CMD_PAUSE);
    // PAUSED has no OCCLUSION_DETECT transition
    EXPECT_FALSE(psm.handleEvent(PumpEvent::OCCLUSION_DETECT));
    EXPECT_EQ(psm.currentState(), PumpState::PAUSED);
}

TEST_F(PsmTest, Branch_OcclusionAlarm_InvalidEvent_Resume) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::OCCLUSION_DETECT);
    // OCCLUSION_ALARM has no CMD_RESUME transition
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_RESUME));
    EXPECT_EQ(psm.currentState(), PumpState::OCCLUSION_ALARM);
}

TEST_F(PsmTest, Branch_Complete_InvalidEvent_Pause) {
    auto psm = make();
    psm.handleEvent(PumpEvent::CMD_START);
    psm.handleEvent(PumpEvent::PRIMING_DONE);
    psm.handleEvent(PumpEvent::VOLUME_REACHED);
    // COMPLETE has no CMD_PAUSE transition
    EXPECT_FALSE(psm.handleEvent(PumpEvent::CMD_PAUSE));
    EXPECT_EQ(psm.currentState(), PumpState::COMPLETE);
}