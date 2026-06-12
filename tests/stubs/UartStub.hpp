#pragma once
#include "syringe/hal/IUartDriver.hpp"
#include <array>
#include <cstring>

inline constexpr size_t UART_STUB_BUF = 256U;

// ---------------------------------------------------------------------------
// UartStub
// Captures TX bytes into a fixed buffer.
// Allows injecting RX bytes for CommandParser tests.
// No heap — instantiate as a static global in test files.
// ---------------------------------------------------------------------------
class UartStub final : public IUartDriver {
public:
    void transmit(const uint8_t* buf, size_t len) noexcept override {
        const size_t space = UART_STUB_BUF - txLen_;
        const size_t copy  = (len < space) ? len : space;
        memcpy(txBuf_.data() + txLen_, buf, copy);
        txLen_ += copy;
    }

    size_t receive(uint8_t* buf, size_t maxLen) noexcept override {
        const size_t copy = (rxAvail_ < maxLen) ? rxAvail_ : maxLen;
        memcpy(buf, rxBuf_.data(), copy);
        rxAvail_ = 0U;
        return copy;
    }

    void injectRx(const char* str) noexcept {
        const size_t len = strnlen(str, UART_STUB_BUF);
        memcpy(rxBuf_.data(), str, len);
        rxAvail_ = len;
    }

    const uint8_t* txData() const noexcept { return txBuf_.data(); }
    size_t         txLen()  const noexcept { return txLen_; }
    void           clearTx()     noexcept  { txLen_ = 0U; txBuf_.fill(0U); }

private:
    std::array<uint8_t, UART_STUB_BUF> txBuf_{};
    std::array<uint8_t, UART_STUB_BUF> rxBuf_{};
    size_t txLen_{0U};
    size_t rxAvail_{0U};
};
