#include "syringe/AlarmManager.hpp"

// ---------------------------------------------------------------------------
AlarmManager::AlarmManager() noexcept {
    observers_.fill(nullptr);
}

// ---------------------------------------------------------------------------
bool AlarmManager::registerObserver(IAlarmObserver* obs) noexcept {
    if (obs == nullptr || count_ >= MAX_ALARM_OBSERVERS) {
        return false;
    }
    observers_[count_++] = obs;
    return true;
}

// ---------------------------------------------------------------------------
void AlarmManager::raise(AlarmType type) noexcept {
    const uint8_t b = bit(type);
    if (activeAlarms_ & b) {
        return;   // Already active — do not re-notify (idempotent).
    }
    activeAlarms_ |= b;
    for (uint8_t i = 0; i < count_; ++i) {
        observers_[i]->onAlarm(type);
    }
}

// ---------------------------------------------------------------------------
void AlarmManager::clear(AlarmType type) noexcept {
    const uint8_t b = bit(type);
    if (!(activeAlarms_ & b)) {
        return;   // Not active — nothing to clear (idempotent).
    }
    activeAlarms_ &= static_cast<uint8_t>(~b);
    for (uint8_t i = 0; i < count_; ++i) {
        observers_[i]->onAlarmCleared(type);
    }
}

// ---------------------------------------------------------------------------
bool AlarmManager::isActive(AlarmType type) const noexcept {
    return (activeAlarms_ & bit(type)) != 0U;
}

// ---------------------------------------------------------------------------
uint8_t AlarmManager::observerCount() const noexcept {
    return count_;
}
