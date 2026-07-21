#pragma once

// ---------------------------------------------------------------------------
// IPressureSensor
// Pure C++ HAL interface for reading pressure sensors (e.g. ST LPS22HB).
// Zero heap — concrete driver instances are statically allocated or injected.
// ---------------------------------------------------------------------------
class IPressureSensor {
public:
    virtual ~IPressureSensor() = default;

    // Read current pressure in hectopascals (hPa).
    // Returns true on successful read, false on I2C / sensor fault.
    virtual bool readPressureHPa(float& pressureHPa) noexcept = 0;
};
