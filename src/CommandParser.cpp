#include "syringe/CommandParser.hpp"
#include <cstring>
#include <cctype>

// ---------------------------------------------------------------------------
CommandParser::CommandParser(PumpStateMachine& psm) noexcept
    : psm_(psm)
{
    buf_.fill('\0');
}

// ---------------------------------------------------------------------------
// feedByte — state machine for byte-at-a-time UART ingestion.
//
// Overflow strategy:
//   Once a frame exceeds CMD_BUF_SIZE-1 bytes, set overflow_ = true and
//   silently swallow all remaining bytes until the next '\n'/'\r'.
//   On the terminator, return ERR_TOO_LONG and reset cleanly.
//   This ensures the buffer is always valid and the next frame is accepted.
// ---------------------------------------------------------------------------
ParseResult CommandParser::feedByte(uint8_t byte) noexcept {
    if (byte == '\n' || byte == '\r') {
        if (overflow_) {
            // Overflowed frame complete — discard, reset, report error.
            overflow_ = false;
            pos_      = 0U;
            buf_.fill('\0');
            return ParseResult::ERR_TOO_LONG;
        }
        buf_[pos_] = '\0';
        ParseResult r = (pos_ == 0U)
            ? ParseResult::ERR_EMPTY
            : dispatch(buf_.data());
        pos_ = 0U;
        buf_.fill('\0');
        return r;
    }

    // Non-terminator byte.
    if (overflow_) {
        return ParseResult::OK;   // Silently swallow — frame already overflowed.
    }
    if (pos_ >= CMD_BUF_SIZE - 1U) {
        overflow_ = true;         // Buffer full — mark overflow, swallow this byte.
        return ParseResult::OK;
    }
    buf_[pos_++] = static_cast<char>(byte);
    return ParseResult::OK;
}

// ---------------------------------------------------------------------------
ParseResult CommandParser::parse(const char* cmd) noexcept {
    if (cmd == nullptr)                        return ParseResult::ERR_EMPTY;
    const size_t len = strnlen(cmd, CMD_BUF_SIZE);
    if (len == 0U)                             return ParseResult::ERR_EMPTY;
    if (len >= CMD_BUF_SIZE)                   return ParseResult::ERR_TOO_LONG;
    return dispatch(cmd);
}

// ---------------------------------------------------------------------------
ParseResult CommandParser::dispatch(const char* cmd) noexcept {
    if (strcmp(cmd, "START") == 0) {
        psm_.handleEvent(PumpEvent::CMD_START);
        return ParseResult::OK;
    }
    if (strcmp(cmd, "STOP") == 0) {
        psm_.handleEvent(PumpEvent::CMD_STOP);
        return ParseResult::OK;
    }
    if (strcmp(cmd, "PAUSE") == 0) {
        psm_.handleEvent(PumpEvent::CMD_PAUSE);
        return ParseResult::OK;
    }
    if (strcmp(cmd, "RESUME") == 0) {
        psm_.handleEvent(PumpEvent::CMD_RESUME);
        return ParseResult::OK;
    }
    if (strcmp(cmd, "CLEAR_ALARM") == 0) {
        psm_.handleEvent(PumpEvent::ALARM_CLEARED);
        return ParseResult::OK;
    }
    if (startsWith(cmd, "SET_RATE ")) {
        bool ok = false;
        const uint32_t rate = parseUInt(cmd + 9U, ok);
        if (!ok || rate == 0U) return ParseResult::ERR_BAD_PARAM;
        psm_.setRate(rate);
        return ParseResult::OK;
    }
    return ParseResult::ERR_UNKNOWN_CMD;
}

// ---------------------------------------------------------------------------
bool CommandParser::startsWith(const char* s, const char* prefix) noexcept {
    while (*prefix != '\0') {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
uint32_t CommandParser::parseUInt(const char* s, bool& ok) noexcept {
    // Removed s == nullptr check because it's guaranteed non-null by internal callers
    if (!isdigit(static_cast<unsigned char>(*s))) {
        ok = false;
        return 0U;
    }
    uint32_t result = 0U;
    while (isdigit(static_cast<unsigned char>(*s))) {
        result = result * 10U + static_cast<uint32_t>(*s - '0');
        ++s;
    }
    ok = true;
    return result;
}
