#include <gtest/gtest.h>
#include "syringe/CommandParser.hpp"
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
    CommandParser*    parser = nullptr;

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
    EXPECT_EQ(parser->parse("SET_RATE 250"), ParseResult::OK);
    EXPECT_EQ(psm->getRate(), 250U);
}

TEST_F(CommandParserTest, Parse_SET_RATE_LargeValue) {
    EXPECT_EQ(parser->parse("SET_RATE 500"), ParseResult::OK);
    EXPECT_EQ(psm->getRate(), 500U);
}

TEST_F(CommandParserTest, Parse_SET_RATE_1_Valid) {
    EXPECT_EQ(parser->parse("SET_RATE 1"), ParseResult::OK);
    EXPECT_EQ(psm->getRate(), 1U);
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

TEST_F(CommandParserTest, Parse_LowercaseStart_UnknownCmd) {
    EXPECT_EQ(parser->parse("start"), ParseResult::ERR_UNKNOWN_CMD);
}

TEST_F(CommandParserTest, Parse_MixedCase_UnknownCmd) {
    EXPECT_EQ(parser->parse("Start"), ParseResult::ERR_UNKNOWN_CMD);
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
    // Fill past the buffer limit, then terminate with newline.
    // ERR_TOO_LONG is reported on the terminator, not mid-stream,
    // so the buffer stays clean for the next frame.
    for (int i = 0; i < 40; ++i) {
        parser->feedByte(static_cast<uint8_t>('A'));
    }
    ParseResult r = parser->feedByte(static_cast<uint8_t>('\n'));
    EXPECT_EQ(r, ParseResult::ERR_TOO_LONG);
}

TEST_F(CommandParserTest, FeedByte_AfterOverflow_AcceptsNewCommand) {
    // Overflow the buffer and terminate it.
    for (int i = 0; i < 40; ++i) {
        parser->feedByte(static_cast<uint8_t>('A'));
    }
    parser->feedByte(static_cast<uint8_t>('\n'));   // flush the overflow frame

    // Now send a valid command — buffer must be clean.
    const char* cmd = "START\n";
    ParseResult r = ParseResult::OK;
    for (size_t i = 0; cmd[i] != '\0'; ++i) {
        r = parser->feedByte(static_cast<uint8_t>(cmd[i]));
    }
    EXPECT_EQ(r, ParseResult::OK);
}

TEST_F(CommandParserTest, FeedByte_SET_RATE_Streaming) {
    const char* cmd = "SET_RATE 120\n";
    for (size_t i = 0; cmd[i] != '\0'; ++i) {
        parser->feedByte(static_cast<uint8_t>(cmd[i]));
    }
    EXPECT_EQ(psm->getRate(), 120U);
}
