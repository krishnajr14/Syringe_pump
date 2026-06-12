#pragma once

// ---------------------------------------------------------------------------
// IGpioPin
// Pure interface for a single digital output pin.
// Used by alarm observers (LED) without pulling in Zephyr GPIO headers.
// ---------------------------------------------------------------------------
class IGpioPin {
public:
    virtual ~IGpioPin() = default;  // LCOV_EXCL_LINE

    virtual void setHigh() noexcept = 0;
    virtual void setLow()  noexcept = 0;
    virtual bool read()    noexcept = 0;
};
