#pragma once

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// IUartDriver
// Pure interface for UART transmit / receive.
// Concrete production impl wraps Zephyr uart_poll_out / uart_poll_in.
// Test stub captures TX bytes into a fixed buffer.
// ---------------------------------------------------------------------------
class IUartDriver {
public:
    virtual ~IUartDriver() = default;

    // Transmit len bytes from buf. Blocking until all bytes enqueued.
    virtual void transmit(const uint8_t* buf, size_t len) noexcept = 0;

    // Receive up to maxLen bytes into buf.
    // Returns number of bytes actually read (0 if none available).
    virtual size_t receive(uint8_t* buf, size_t maxLen) noexcept = 0;
};
