#include <gtest/gtest.h>
#include "syringe/VolumeTracker.hpp"

TEST(VolumeTracker, Initial_AllZero) {
    VolumeTracker vt{3U};
    EXPECT_EQ(vt.volumeUL(),  0U);
    EXPECT_EQ(vt.volumeNL(),  0U);
    EXPECT_EQ(vt.stepCount(), 0U);
}

TEST(VolumeTracker, OneStep_AccumulatesNL) {
    VolumeTracker vt{3U};
    vt.addSteps(1U);
    EXPECT_EQ(vt.volumeNL(), 3U);
    EXPECT_EQ(vt.volumeUL(), 0U);  // 3 nL < 1000 nL threshold
}

TEST(VolumeTracker, BelowULThreshold_ReturnsZeroUL) {
    VolumeTracker vt{3U};
    vt.addSteps(333U);   // 333 × 3 = 999 nL → 0 µL
    EXPECT_EQ(vt.volumeUL(), 0U);
    EXPECT_EQ(vt.volumeNL(), 999U);
}

TEST(VolumeTracker, CrossesULThreshold_Returns1UL) {
    VolumeTracker vt{3U};
    vt.addSteps(334U);   // 334 × 3 = 1002 nL → 1 µL
    EXPECT_EQ(vt.volumeUL(), 1U);
}

TEST(VolumeTracker, BulkSteps_CorrectNL) {
    VolumeTracker vt{3U};
    vt.addSteps(1000U);
    EXPECT_EQ(vt.volumeNL(), 3000U);
    EXPECT_EQ(vt.volumeUL(), 3U);
}

TEST(VolumeTracker, Reset_ClearsAll) {
    VolumeTracker vt{3U};
    vt.addSteps(5000U);
    vt.reset();
    EXPECT_EQ(vt.volumeNL(),  0U);
    EXPECT_EQ(vt.volumeUL(),  0U);
    EXPECT_EQ(vt.stepCount(), 0U);
}

TEST(VolumeTracker, StepCount_TracksIndependently) {
    VolumeTracker vt{3U};
    vt.addSteps(100U);
    vt.addSteps(50U);
    EXPECT_EQ(vt.stepCount(), 150U);
}

TEST(VolumeTracker, ZeroSteps_NoChange) {
    VolumeTracker vt{3U};
    vt.addSteps(100U);
    const uint32_t before = vt.volumeNL();
    vt.addSteps(0U);
    EXPECT_EQ(vt.volumeNL(), before);
}

TEST(VolumeTracker, HighNLPerStep_1ULPerStep) {
    VolumeTracker vt{1000U};   // 1000 nL = 1 µL per step
    vt.addSteps(1U);
    EXPECT_EQ(vt.volumeUL(), 1U);
    vt.addSteps(9U);
    EXPECT_EQ(vt.volumeUL(), 10U);
}

TEST(VolumeTracker, LargeAccumulation_9mL) {
    VolumeTracker vt{3U};
    vt.addSteps(3'000'000U);   // 9 000 000 nL = 9000 µL = 9 mL
    EXPECT_EQ(vt.volumeUL(), 9000U);
}

TEST(VolumeTracker, MultipleAddSteps_Cumulative) {
    VolumeTracker vt{3U};
    vt.addSteps(500U);
    vt.addSteps(500U);
    EXPECT_EQ(vt.volumeNL(), 3000U);
    EXPECT_EQ(vt.stepCount(), 1000U);
}

TEST(VolumeTracker, DifferentNLPerStep_Correct) {
    VolumeTracker vt{10U};   // 10 nL/step
    vt.addSteps(100U);
    EXPECT_EQ(vt.volumeNL(), 1000U);
    EXPECT_EQ(vt.volumeUL(), 1U);
}
