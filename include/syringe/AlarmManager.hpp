#pragma once

#include "syringe/hal/IAlarmObserver.hpp"
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
// ---------------------------------------------------------------------------

inline constexpr size_t MAX_ALARM_OBSERVERS = 4U;

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

private:
    std::array<IAlarmObserver*, MAX_ALARM_OBSERVERS> observers_{};
    uint8_t count_{0};
    uint8_t activeAlarms_{0};   // bitmask — bit N set = AlarmType(N) active

    static constexpr uint8_t bit(AlarmType t) noexcept {
        return static_cast<uint8_t>(1U << static_cast<uint8_t>(t));
    }
};
