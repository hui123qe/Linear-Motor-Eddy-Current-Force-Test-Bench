#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <array>
#include <cstddef>

inline constexpr std::size_t kSafetyLightCount = 4;

enum class SafetyState
{
    Normal,
    Triggered,
    Unknown
};

enum class SafetyPointState
{
    Unknown,
    Normal,
    Triggered,
    Unconfigured
};

struct SafetyPointStatus
{
    SafetyPointState state = SafetyPointState::Unknown;
    int rawValue = 0;
};

struct SafetyStatusSnapshot
{
    SafetyPointStatus hardwareEmergencyStop;
    bool acsplProgramError = false;
    std::array<SafetyPointStatus, kSafetyLightCount> safetyLights{};
    SafetyPointStatus safetyDoorLock;
    QDateTime sampledAtUtc;
    QString detail;
};

Q_DECLARE_METATYPE(SafetyState)
Q_DECLARE_METATYPE(SafetyPointState)
Q_DECLARE_METATYPE(SafetyPointStatus)
Q_DECLARE_METATYPE(SafetyStatusSnapshot)
