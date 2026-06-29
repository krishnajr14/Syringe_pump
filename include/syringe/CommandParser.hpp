#pragma once

#include "syringe/PumpStateMachine.hpp"
#include <array>
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// ParseResult — outcome of parsing one complete command frame
// ---------------------------------------------------------------------------
enum class ParseResult : uint8_t {
    OK               = 0,
    ERR_UNKNOWN_CMD  = 1,
    ERR_BAD_PARAM    = 2,
    ERR_EMPTY        = 3,
    ERR_TOO_LONG     = 4
};

// ---------------------------------------------------------------------------
// CommandParser
// Parses ASCII command frames from UART and drives PumpStateMachine.
//
// Frame format (newline-terminated, CR optional):
//   "START\n"
//   "STOP\n"
//   "PAUSE\n"
//   "RESUME\n"
//   "SET_RATE <uint32>\n"   e.g. "SET_RATE 120\n"
//   "CLEAR_ALARM\n"
//
// Two usage modes:
//   1. feedByte(b) — feed one raw UART byte at a time (ISR / RX thread).
//      Returns ParseResult::OK (no action) for mid-frame bytes.
//      Returns the actual result when '\n' or '\r' received.
//
//   2. parse(str)  — parse a complete null-terminated string directly.
//      Used by unit tests to avoid byte-by-byte feeding.
//
// No heap. Internal buffer is std::array<char, CMD_BUF_SIZE>.
// ---------------------------------------------------------------------------

inline constexpr size_t CMD_BUF_SIZE = 32U;

class CommandParser {
public:
    explicit CommandParser(PumpStateMachine& psm) noexcept;

    // Feed one byte from UART RX.
    ParseResult feedByte(uint8_t byte) noexcept;

    // Parse a complete null-terminated command string (test entry point).
    ParseResult parse(const char* cmd) noexcept;

    bool lastCommandWasSetRate() const noexcept { return lastWasSetRate_; }

private:
    PumpStateMachine& psm_;
    std::array<char, CMD_BUF_SIZE> buf_{};
    uint8_t pos_{0};
    bool lastWasSetRate_{false};
    bool    overflow_{false};   // true = frame exceeded CMD_BUF_SIZE, discard until '\n'

    // Dispatch a fully-received, null-terminated command string.
    ParseResult dispatch(const char* cmd) noexcept;

    static bool     startsWith(const char* s,
                                const char* prefix) noexcept;
    static uint32_t parseUInt (const char* s,
                                bool& ok)    noexcept;
};
