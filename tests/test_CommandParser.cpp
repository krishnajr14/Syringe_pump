#include <gtest/gtest.h>

// ─── Localized visibility override for target header ───
#define private public
#include "syringe/CommandParser.hpp"
#undef private

#include "syringe/PumpStateMachine.hpp"
#include "syringe/VolumeTracker.hpp"
#include "syringe/AlarmManager.hpp"
#include "StepperDriverStub.hpp"
#include "AlarmObserverStub.hpp"

// Static globals — zero heap
static StepperDriverStub  cp_stepper;
static AlarmObserverStub  cp_obs;
static VolumeTracker      cp_tracker{3U};
static AlarmManager       cp_alarms;

// Placement-new buffers
static uint8_t s_psmBuf   [sizeof(PumpStateMachine)] alignas(PumpStateMachine);
static uint8_t s_parserBuf[sizeof(CommandParser)]    alignas(CommandParser);

class CommandParserTest : public ::testing::Test {
protected:
    PumpStateMachine* psm    = nullptr;
    CommandParser* parser = nullptr;

    void SetUp() override {
        cp_stepper.resetAll();
        cp_obs.reset();
        cp_tracker.reset();
        cp_alarms = AlarmManager{};
        cp_alarms.registerObserver(&cp_obs);

        // Placement-new into static buffers (no heap)
        psm    = new (s_psmBuf)    PumpStateMachine{cp_stepper, cp_alarms,
                                                    cp_tracker, 10000U};
        parser = new (s_parserBuf) CommandParser{*psm};
    }
};

// ── Valid commands ─────────────────────────────────────────────────────────
TEST_F(CommandParserTest, Parse_START_TransitionsToPriming) {
    EXPECT_EQ(parser->parse("START"), ParseResult::OK);
    EXPECT_EQ(psm->currentState(), PumpState::PRIMING);
}

TEST_F(CommandParserTest, Parse_STOP_FromPriming_TransitionsToIdle) {
    parser->parse("START");
    EXPECT_EQ(parser->parse("STOP"), ParseResult::OK);
    EXPECT_EQ(psm->currentState(), PumpState::IDLE);
}

TEST_F(CommandParserTest, Parse_PAUSE_FromInfusing) {
    parser->parse("START");
    psm->handleEvent(PumpEvent::PRIMING_DONE);
    EXPECT_EQ(parser->parse("PAUSE"), ParseResult::OK);
    EXPECT_EQ(psm->currentState(), PumpState::PAUSED);
}

TEST_F(CommandParserTest, Parse_RESUME_FromPaused) {
    parser->parse("START");
    psm->handleEvent(PumpEvent::PRIMING_DONE);
    parser->parse("PAUSE");
    EXPECT_EQ(parser->parse("RESUME"), ParseResult::OK);
    EXPECT_EQ(psm->currentState(), PumpState::INFUSING);
}

TEST_F(CommandParserTest, Parse_CLEAR_ALARM_FromOcclusionAlarm) {
    parser->parse("START");
    psm->handleEvent(PumpEvent::PRIMING_DONE);
    psm->handleEvent(PumpEvent::OCCLUSION_DETECT);
    EXPECT_EQ(parser->parse("CLEAR_ALARM"), ParseResult::OK);
    EXPECT_EQ(psm->currentState(), PumpState::PAUSED);
}

TEST_F(CommandParserTest, Parse_SET_RATE_Valid) {
    // 250 mL over 60 seconds = 250000 uL/min
    EXPECT_EQ(parser->parse("SET_RATE 250 60"), ParseResult::OK);
    EXPECT_EQ(psm->getRate(), 250000U);
}

TEST_F(CommandParserTest, Parse_SET_RATE_LargeValue) {
    // 500 mL over 60 seconds = 500000 uL/min
    EXPECT_EQ(parser->parse("SET_RATE 500 60"), ParseResult::OK);
    EXPECT_EQ(psm->getRate(), 500000U);
}

TEST_F(CommandParserTest, Parse_SET_RATE_1_Valid) {
    // 1 mL over 60 seconds = 1000 uL/min
    EXPECT_EQ(parser->parse("SET_RATE 1 60"), ParseResult::OK);
    EXPECT_EQ(psm->getRate(), 1000U);
}

// ── Invalid / malformed commands ───────────────────────────────────────────
TEST_F(CommandParserTest, Parse_UnknownCommand_ReturnsError) {
    EXPECT_EQ(parser->parse("BLASTOFF"), ParseResult::ERR_UNKNOWN_CMD);
}

TEST_F(CommandParserTest, Parse_EmptyString_ReturnsEmpty) {
    EXPECT_EQ(parser->parse(""), ParseResult::ERR_EMPTY);
}

TEST_F(CommandParserTest, Parse_Null_ReturnsEmpty) {
    EXPECT_EQ(parser->parse(nullptr), ParseResult::ERR_EMPTY);
}

TEST_F(CommandParserTest, Parse_SET_RATE_Zero_Rejected) {
    EXPECT_EQ(parser->parse("SET_RATE 0"), ParseResult::ERR_BAD_PARAM);
}

TEST_F(CommandParserTest, Parse_SET_RATE_NoNumber_Rejected) {
    EXPECT_EQ(parser->parse("SET_RATE "), ParseResult::ERR_BAD_PARAM);
}

TEST_F(CommandParserTest, Parse_SET_RATE_Letters_Rejected) {
    EXPECT_EQ(parser->parse("SET_RATE abc"), ParseResult::ERR_BAD_PARAM);
}

TEST_F(CommandParserTest, Parse_TooLong_Rejected) {
    EXPECT_EQ(parser->parse("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"),
              ParseResult::ERR_TOO_LONG);
}

// ── feedByte streaming ─────────────────────────────────────────────────────
TEST_F(CommandParserTest, FeedByte_START_Newline_Works) {
    const char* cmd = "START\n";
    ParseResult r = ParseResult::OK;
    for (size_t i = 0; cmd[i] != '\0'; ++i) {
        r = parser->feedByte(static_cast<uint8_t>(cmd[i]));
    }
    EXPECT_EQ(r, ParseResult::OK);
    EXPECT_EQ(psm->currentState(), PumpState::PRIMING);
}

TEST_F(CommandParserTest, FeedByte_CR_AlsoTerminates) {
    parser->parse("START");
    const char* cmd = "STOP\r";
    for (size_t i = 0; cmd[i] != '\0'; ++i) {
        parser->feedByte(static_cast<uint8_t>(cmd[i]));
    }
    EXPECT_EQ(psm->currentState(), PumpState::IDLE);
}

TEST_F(CommandParserTest, FeedByte_EmptyLine_ReturnsEmpty) {
    EXPECT_EQ(parser->feedByte('\n'), ParseResult::ERR_EMPTY);
}

TEST_F(CommandParserTest, FeedByte_Overflow_ReturnsTooLong) {
    for (int i = 0; i < 40; ++i) {
        parser->feedByte(static_cast<uint8_t>('A'));
    }
    ParseResult r = parser->feedByte(static_cast<uint8_t>('\n'));
    EXPECT_EQ(r, ParseResult::ERR_TOO_LONG);
}

TEST_F(CommandParserTest, FeedByte_AfterOverflow_AcceptsNewCommand) {
    for (int i = 0; i < 40; ++i) {
        parser->feedByte(static_cast<uint8_t>('A'));
    }
    parser->feedByte(static_cast<uint8_t>('\n'));

    const char* cmd = "START\n";
    ParseResult r = ParseResult::OK;
    for (size_t i = 0; cmd[i] != '\0'; ++i) {
        r = parser->feedByte(static_cast<uint8_t>(cmd[i]));
    }
    EXPECT_EQ(r, ParseResult::OK);
}

TEST_F(CommandParserTest, FeedByte_SET_RATE_Streaming) {
    const char* cmd = "SET_RATE 120 60\n";
    for (size_t i = 0; cmd[i] != '\0'; ++i) {
        parser->feedByte(static_cast<uint8_t>(cmd[i]));
    }
    EXPECT_EQ(psm->getRate(), 120000U);
}

// ── Coverage Additions: States & Edge Branches ─────────────────────────────
TEST_F(CommandParserTest, Parse_SimOcclusion_Command_Coverage) {
    parser->parse("START"); 
    psm->handleEvent(PumpEvent::PRIMING_DONE); 
    ASSERT_EQ(psm->currentState(), PumpState::INFUSING);

    EXPECT_EQ(parser->parse("SIM_OCCLUSION"), ParseResult::OK);
    EXPECT_EQ(psm->currentState(), PumpState::OCCLUSION_ALARM);
}

TEST_F(CommandParserTest, Parse_SET_RATE_Branch_Edges) {
    // Triggers *ptr == '\0' by omitting duration parameter
    EXPECT_EQ(parser->parse("SET_RATE 10 "), ParseResult::ERR_BAD_PARAM);

    // Triggers volume == 0 condition
    EXPECT_EQ(parser->parse("SET_RATE 0 60"), ParseResult::ERR_BAD_PARAM);

    // Triggers duration == 0 condition
    EXPECT_EQ(parser->parse("SET_RATE 10 0"), ParseResult::ERR_BAD_PARAM);

    // Triggers calculated_ul_min == 0 floor division condition
    EXPECT_EQ(parser->parse("SET_RATE 1 3600000"), ParseResult::ERR_BAD_PARAM);

    // Triggers !isdigit trailing parameter condition
    EXPECT_EQ(parser->parse("SET_RATE 10 a"), ParseResult::ERR_BAD_PARAM);

    // Triggers ptr == nullptr condition inside the internal static helper
    bool ok = false;
    EXPECT_EQ(CommandParser::parseUInt(nullptr, ok), 0U);
}

TEST_F(CommandParserTest, Internal_ParseUInt_FullCoverage) {
    bool ok = false;
    
    EXPECT_EQ(CommandParser::parseUInt("9876", ok), 9876U);
    EXPECT_TRUE(ok);
    
    EXPECT_EQ(CommandParser::parseUInt("xyz", ok), 0U);
    EXPECT_FALSE(ok);
}