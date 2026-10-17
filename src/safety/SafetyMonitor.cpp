#include "SafetyMonitor.h"

#include "../logging/AppLogger.h"
#include "../motion/AcsClient.h"

#include <QDateTime>
#include <QMutexLocker>

SafetyMonitor& SafetyMonitor::instance()
{
    static SafetyMonitor monitor;
    return monitor;
}

SafetyMonitor::SafetyMonitor()
{
    qRegisterMetaType<SafetyState>("SafetyState");
    qRegisterMetaType<SafetyPointState>("SafetyPointState");
    qRegisterMetaType<SafetyPointStatus>("SafetyPointStatus");
    qRegisterMetaType<SafetyStatusSnapshot>("SafetyStatusSnapshot");
}

void SafetyMonitor::initialize(AcsClient* client)
{
    if (initialized_ || client == nullptr) {
        return;
    }

    client_ = client;
    connect(client_,
            &AcsClient::safetyStatusSampled,
            this,
            &SafetyMonitor::acceptSafetyStatusSample,
            Qt::DirectConnection);
    connect(client_,
            &AcsClient::safetyStatusReadFailed,
            this,
            &SafetyMonitor::reportMonitoringFailure,
            Qt::DirectConnection);
    connect(client_,
            &AcsClient::connectionChanged,
            this,
            [this](bool connected, const QString& message) {
                if (!connected) {
                    reportMonitoringFailure(
                        message.isEmpty()
                            ? QStringLiteral("ACS 连接断开，安全回路状态未知")
                            : message);
                }
            },
            Qt::DirectConnection);

    initialized_ = true;
    qCInfo(logSafety) << "安全回路监测已绑定 ACS 客户端";
}

void SafetyMonitor::shutdown()
{
    if (client_ != nullptr) {
        disconnect(client_, nullptr, this, nullptr);
    }
    client_ = nullptr;
    initialized_ = false;
    state_.store(SafetyState::Unknown, std::memory_order_release);
    qCInfo(logSafety) << "安全回路监测已关闭";
}

SafetyState SafetyMonitor::state() const
{
    return state_.load(std::memory_order_acquire);
}

SafetyStatusSnapshot SafetyMonitor::snapshot() const
{
    const QMutexLocker locker(&mutex_);
    return snapshot_;
}

void SafetyMonitor::acceptSafetyStatusSample(
    const SafetyStatusSnapshot& snapshot)
{
    const SafetyState nextState = stateFromSnapshot(snapshot);
    const QString nextReason = stateReason(nextState, snapshot);
    bool snapshotChanged = false;
    bool stateChanged = false;
    {
        const QMutexLocker locker(&mutex_);
        snapshotChanged = snapshotDiffersFromCurrent(snapshot);
        stateChanged = !hasState_ || state_.load(std::memory_order_relaxed) != nextState
                       || stateReason_ != nextReason;
        snapshot_ = snapshot;
        stateReason_ = nextReason;
        hasSnapshot_ = true;
        hasState_ = true;
        state_.store(nextState, std::memory_order_release);
    }

    emit safetyStatusObserved(snapshot);
    if (snapshotChanged) {
        emit safetySnapshotChanged(snapshot);
    }
    if (stateChanged) {
        emit safetyStateChanged(nextState, nextReason);
    }
}

void SafetyMonitor::reportMonitoringFailure(const QString& message)
{
    SafetyStatusSnapshot failedStatus = snapshot();
    failedStatus.hardwareEmergencyStop.state = SafetyPointState::Unknown;
    failedStatus.acsplProgramError = false;
    for (SafetyPointStatus& light : failedStatus.safetyLights) {
        light.state = SafetyPointState::Unknown;
    }
    failedStatus.safetyDoorLock.state = SafetyPointState::Unknown;
    failedStatus.sampledAtUtc = QDateTime::currentDateTimeUtc();
    failedStatus.detail = message;
    acceptSafetyStatusSample(failedStatus);
}

bool SafetyMonitor::snapshotDiffersFromCurrent(
    const SafetyStatusSnapshot& snapshot) const
{
    if (!hasSnapshot_
        || snapshot_.hardwareEmergencyStop.state
               != snapshot.hardwareEmergencyStop.state
        || snapshot_.hardwareEmergencyStop.rawValue
               != snapshot.hardwareEmergencyStop.rawValue
        || snapshot_.acsplProgramError != snapshot.acsplProgramError
        || snapshot_.detail != snapshot.detail) {
        return true;
    }

    for (std::size_t index = 0; index < snapshot.safetyLights.size(); ++index) {
        if (snapshot_.safetyLights.at(index).state
                != snapshot.safetyLights.at(index).state
            || snapshot_.safetyLights.at(index).rawValue
                   != snapshot.safetyLights.at(index).rawValue) {
            return true;
        }
    }

    return snapshot_.safetyDoorLock.state != snapshot.safetyDoorLock.state
           || snapshot_.safetyDoorLock.rawValue
                  != snapshot.safetyDoorLock.rawValue;
}

SafetyState SafetyMonitor::stateFromSnapshot(
    const SafetyStatusSnapshot& snapshot)
{
    if (snapshot.acsplProgramError) {
        return SafetyState::Triggered;
    }

    switch (snapshot.hardwareEmergencyStop.state) {
    case SafetyPointState::Triggered:
        return SafetyState::Triggered;
    case SafetyPointState::Normal:
        return SafetyState::Normal;
    case SafetyPointState::Unknown:
    case SafetyPointState::Unconfigured:
        return SafetyState::Unknown;
    }
    return SafetyState::Unknown;
}

QString SafetyMonitor::stateReason(
    SafetyState state,
    const SafetyStatusSnapshot& snapshot)
{
    switch (state) {
    case SafetyState::Normal:
        return QStringLiteral("硬件安全回路正常");
    case SafetyState::Triggered:
        if (snapshot.acsplProgramError) {
            return QStringLiteral("ACSPL+ Program Error");
        }
        return QStringLiteral("ACS 硬件急停已触发");
    case SafetyState::Unknown:
        return snapshot.detail.isEmpty()
                   ? QStringLiteral("ACS 硬件安全回路状态未知")
                   : snapshot.detail;
    }
    return QStringLiteral("ACS 硬件安全回路状态未知");
}
