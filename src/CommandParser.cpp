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
ParseResult CommandParser::feedByte(uint8_t byte) noexcept {
    if (byte == '\n' || byte == '\r') {
        if (overflow_) {
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

    if (overflow_) {
        return ParseResult::OK;
    }
    if (pos_ >= CMD_BUF_SIZE - 1U) {
        overflow_ = true;
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
    lastWasSetRate_ = false;

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
    if (strcmp(cmd, "SIM_OCCLUSION") == 0) {
        psm_.handleEvent(PumpEvent::OCCLUSION_DETECT);
        return ParseResult::OK;
    }
    
    // ── TWO-PARAMETER SET_RATE PARSING (Seconds Variant) ───────────────────
    if (startsWith(cmd, "SET_RATE ")) {
        const char* ptr = cmd + 9U;
        
        // 1. Parse Volume (mL)
        if (ptr == nullptr || !isdigit(static_cast<unsigned char>(*ptr))) return ParseResult::ERR_BAD_PARAM;
        uint32_t volume_ml = 0U;
        while (isdigit(static_cast<unsigned char>(*ptr))) {
            volume_ml = volume_ml * 10U + static_cast<uint32_t>(*ptr - '0');
            ++ptr;
        }
        
        // 2. Skip spaces
        while (*ptr == ' ') {
            ++ptr;
        }
        
        // 3. Parse Duration (seconds)
        if (*ptr == '\0' || !isdigit(static_cast<unsigned char>(*ptr))) return ParseResult::ERR_BAD_PARAM;
        uint32_t duration_sec = 0U;
        while (isdigit(static_cast<unsigned char>(*ptr))) {
            duration_sec = duration_sec * 10U + static_cast<uint32_t>(*ptr - '0');
            ++ptr;
        }

        // 4. Validation
        if (volume_ml == 0U || duration_sec == 0U) return ParseResult::ERR_BAD_PARAM;
        
        // 5. Compute Flow Rate directly in uL/minute (Steps per minute)
        // Formula: ((Volume_ml * 1000) / duration_sec) * 60 seconds
        uint32_t calculated_ul_min = ((volume_ml * 1000U) * 60U) / duration_sec;
        
        if (calculated_ul_min == 0U) return ParseResult::ERR_BAD_PARAM;

        // Pass calculated values safely down
        psm_.setRate(calculated_ul_min);
        lastWasSetRate_ = true;
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
    if (s == nullptr || !isdigit(static_cast<unsigned char>(*s))) {
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