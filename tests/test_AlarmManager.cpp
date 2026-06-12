#include <gtest/gtest.h>
#include "syringe/AlarmManager.hpp"
#include "AlarmObserverStub.hpp"

// Static stubs — zero heap
static AlarmObserverStub s_obs1;
static AlarmObserverStub s_obs2;
static AlarmObserverStub s_obs3;
static AlarmObserverStub s_obs4;
static AlarmObserverStub s_obs5;   // used for overflow test

class AlarmManagerTest : public ::testing::Test {
protected:
    AlarmManager am;
    void SetUp() override {
        s_obs1.reset(); s_obs2.reset();
        s_obs3.reset(); s_obs4.reset(); s_obs5.reset();
        am = AlarmManager{};
    }
};

// ── Registration ──────────────────────────────────────────────────────────
TEST_F(AlarmManagerTest, Register_FirstObserver_Succeeds) {
    EXPECT_TRUE(am.registerObserver(&s_obs1));
    EXPECT_EQ(am.observerCount(), 1U);
}

TEST_F(AlarmManagerTest, Register_Null_Fails) {
    EXPECT_FALSE(am.registerObserver(nullptr));
    EXPECT_EQ(am.observerCount(), 0U);
}

TEST_F(AlarmManagerTest, Register_FourObservers_AllSucceed) {
    EXPECT_TRUE(am.registerObserver(&s_obs1));
    EXPECT_TRUE(am.registerObserver(&s_obs2));
    EXPECT_TRUE(am.registerObserver(&s_obs3));
    EXPECT_TRUE(am.registerObserver(&s_obs4));
    EXPECT_EQ(am.observerCount(), 4U);
}

TEST_F(AlarmManagerTest, Register_FifthObserver_Fails) {
    am.registerObserver(&s_obs1);
    am.registerObserver(&s_obs2);
    am.registerObserver(&s_obs3);
    am.registerObserver(&s_obs4);
    EXPECT_FALSE(am.registerObserver(&s_obs5));
    EXPECT_EQ(am.observerCount(), 4U);
}

// ── Raise ─────────────────────────────────────────────────────────────────
TEST_F(AlarmManagerTest, Raise_NotifiesAllObservers) {
    am.registerObserver(&s_obs1);
    am.registerObserver(&s_obs2);
    am.raise(AlarmType::OCCLUSION);
    EXPECT_EQ(s_obs1.raiseCount(), 1U);
    EXPECT_EQ(s_obs2.raiseCount(), 1U);
}

TEST_F(AlarmManagerTest, Raise_SetsActiveFlag) {
    am.registerObserver(&s_obs1);
    EXPECT_FALSE(am.isActive(AlarmType::OCCLUSION));
    am.raise(AlarmType::OCCLUSION);
    EXPECT_TRUE(am.isActive(AlarmType::OCCLUSION));
}

TEST_F(AlarmManagerTest, Raise_Idempotent_NoDoubleNotify) {
    am.registerObserver(&s_obs1);
    am.raise(AlarmType::OCCLUSION);
    am.raise(AlarmType::OCCLUSION);   // second call — already active
    EXPECT_EQ(s_obs1.raiseCount(), 1U);
}

TEST_F(AlarmManagerTest, Raise_CorrectAlarmType_Delivered) {
    am.registerObserver(&s_obs1);
    am.raise(AlarmType::VOLUME_COMPLETE);
    EXPECT_EQ(s_obs1.lastRaised(), AlarmType::VOLUME_COMPLETE);
}

// ── Clear ─────────────────────────────────────────────────────────────────
TEST_F(AlarmManagerTest, Clear_NotifiesAllObservers) {
    am.registerObserver(&s_obs1);
    am.registerObserver(&s_obs2);
    am.raise(AlarmType::OCCLUSION);
    am.clear(AlarmType::OCCLUSION);
    EXPECT_EQ(s_obs1.clearCount(), 1U);
    EXPECT_EQ(s_obs2.clearCount(), 1U);
}

TEST_F(AlarmManagerTest, Clear_ClearsActiveFlag) {
    am.registerObserver(&s_obs1);
    am.raise(AlarmType::OCCLUSION);
    am.clear(AlarmType::OCCLUSION);
    EXPECT_FALSE(am.isActive(AlarmType::OCCLUSION));
}

TEST_F(AlarmManagerTest, Clear_Idempotent_NoNotifyIfNotActive) {
    am.registerObserver(&s_obs1);
    am.clear(AlarmType::OCCLUSION);   // never raised
    EXPECT_EQ(s_obs1.clearCount(), 0U);
}

// ── Multiple alarm types ──────────────────────────────────────────────────
TEST_F(AlarmManagerTest, MultipleTypes_Independent) {
    am.registerObserver(&s_obs1);
    am.raise(AlarmType::OCCLUSION);
    am.raise(AlarmType::VOLUME_COMPLETE);
    EXPECT_TRUE(am.isActive(AlarmType::OCCLUSION));
    EXPECT_TRUE(am.isActive(AlarmType::VOLUME_COMPLETE));
    am.clear(AlarmType::OCCLUSION);
    EXPECT_FALSE(am.isActive(AlarmType::OCCLUSION));
    EXPECT_TRUE(am.isActive(AlarmType::VOLUME_COMPLETE));
}

TEST_F(AlarmManagerTest, ThreeObservers_AllNotified_OnRaise) {
    am.registerObserver(&s_obs1);
    am.registerObserver(&s_obs2);
    am.registerObserver(&s_obs3);
    am.raise(AlarmType::POWER_FAULT);
    EXPECT_EQ(s_obs1.raiseCount(), 1U);
    EXPECT_EQ(s_obs2.raiseCount(), 1U);
    EXPECT_EQ(s_obs3.raiseCount(), 1U);
}

TEST_F(AlarmManagerTest, RaiseAndClear_TotalNotificationCount) {
    am.registerObserver(&s_obs1);
    am.raise(AlarmType::OCCLUSION);
    am.raise(AlarmType::VOLUME_COMPLETE);
    am.clear(AlarmType::OCCLUSION);
    EXPECT_EQ(s_obs1.raiseCount(), 2U);
    EXPECT_EQ(s_obs1.clearCount(), 1U);
}

TEST_F(AlarmManagerTest, PowerFault_RaisedAndCleared) {
    am.registerObserver(&s_obs1);
    am.raise(AlarmType::POWER_FAULT);
    EXPECT_TRUE(am.isActive(AlarmType::POWER_FAULT));
    am.clear(AlarmType::POWER_FAULT);
    EXPECT_FALSE(am.isActive(AlarmType::POWER_FAULT));
    EXPECT_EQ(s_obs1.raiseCount(), 1U);
    EXPECT_EQ(s_obs1.clearCount(), 1U);
}
