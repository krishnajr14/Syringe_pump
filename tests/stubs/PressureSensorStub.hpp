#pragma once
#include "syringe/hal/IPressureSensor.hpp"

// ---------------------------------------------------------------------------
// PressureSensorStub
// Concrete stub for IPressureSensor used in unit tests.
// Zero heap — instantiate as a static global or local object in test files.
// ---------------------------------------------------------------------------
class PressureSensorStub final : public IPressureSensor {
public:
    void setPressure(float pressureHPa) noexcept {
        pressureHPa_ = pressureHPa;
        readSuccess_ = true;
    }

    void setReadFailure(bool fail) noexcept {
        readSuccess_ = !fail;
    }

    bool readPressureHPa(float& pressureHPa) noexcept override {
        if (!readSuccess_) {
            return false;
        }
        pressureHPa = pressureHPa_;
        return true;
    }

private:
    float pressureHPa_{1013.25f};
    bool readSuccess_{true};
};
