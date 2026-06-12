#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// AlarmType
// All alarm codes referenced by IEC 60601-2-24 Table 101.
// ---------------------------------------------------------------------------
enum class AlarmType : uint8_t {
    OCCLUSION        = 0,   // Downstream tube blocked mid-infusion
    VOLUME_COMPLETE  = 1,   // Target volume fully delivered
    POWER_FAULT      = 2    // Supply voltage out of range
};

// ---------------------------------------------------------------------------
// IAlarmObserver
// Pure interface. Implement once per notification channel
// (UART, LED, buzzer). Register with AlarmManager::registerObserver().
// Never allocates — concrete implementations are static globals or
// placement-new'd into static buffers.
// ---------------------------------------------------------------------------
class IAlarmObserver {
public:
    virtual ~IAlarmObserver() = default;    // LCOV_EXCL_LINE

    // Called when an alarm transitions inactive → active.
    virtual void onAlarm(AlarmType type) noexcept = 0;

    // Called when an alarm transitions active → inactive.
    virtual void onAlarmCleared(AlarmType type) noexcept = 0;
};
