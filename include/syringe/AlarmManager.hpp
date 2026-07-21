#pragma once

#include "syringe/hal/IAlarmObserver.hpp"
#include "syringe/hal/IPressureSensor.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// AlarmManager
// Observer subject. Maintains a fixed-size table of IAlarmObserver*
// (no heap — std::array with explicit count).
//
// Design decisions:
//   - MAX_ALARM_OBSERVERS = 4  (UART + LED + buzzer + 1 spare)
//   - Alarm state tracked as a bitmask of AlarmType values
//   - raise() is idempotent: raising an active alarm does not re-notify
//   - clear() is idempotent: clearing an inactive alarm does nothing
//   - Occlusion threshold: 50 hPa above base pressure value
// ---------------------------------------------------------------------------

inline constexpr size_t MAX_ALARM_OBSERVERS = 4U;
inline constexpr float OCCLUSION_PRESSURE_THRESHOLD_HPA = 50.0f;
inline constexpr float DEFAULT_BASE_PRESSURE_HPA = 1013.25f;

class AlarmManager {
public:
    AlarmManager() noexcept;

    // Register an observer. Returns false if table full or obs == nullptr.
    bool registerObserver(IAlarmObserver* obs) noexcept;

    // Raise an alarm. Notifies all observers only on inactive→active edge.
    void raise(AlarmType type) noexcept;

    // Clear an alarm. Notifies all observers only on active→inactive edge.
    void clear(AlarmType type) noexcept;

    // Query alarm state.
    bool    isActive(AlarmType type)  const noexcept;
    uint8_t observerCount()           const noexcept;

    // Pressure baseline and occlusion checking
    void  setBasePressure(float baseHPa) noexcept;
    float getBasePressure() const noexcept;
    bool  isBasePressureSet() const noexcept;
    void  resetBasePressure() noexcept;

    // Evaluates current pressure against base pressure + 50 hPa threshold.
    // If threshold exceeded, calls raise(AlarmType::OCCLUSION) and returns true.
    bool checkPressureOcclusion(float currentHPa) noexcept;

    // Polls IPressureSensor, updates base pressure if not set, and evaluates threshold.
    bool checkPressureSensor(IPressureSensor& sensor) noexcept;

private:
    std::array<IAlarmObserver*, MAX_ALARM_OBSERVERS> observers_{};
    uint8_t count_{0};
    uint8_t activeAlarms_{0};   // bitmask — bit N set = AlarmType(N) active

    float basePressureHPa_{DEFAULT_BASE_PRESSURE_HPA};
    bool  basePressureSet_{false};

    static constexpr uint8_t bit(AlarmType t) noexcept {
        return static_cast<uint8_t>(1U << static_cast<uint8_t>(t));
    }
};

