#pragma once

#include "SafetyTypes.h"

#include <QMutex>
#include <QObject>

#include <atomic>

class AcsClient;

class SafetyMonitor final : public QObject
{
    Q_OBJECT

public:
    static SafetyMonitor& instance();

    void initialize(AcsClient* client);
    void shutdown();

    [[nodiscard]] SafetyState state() const;
    [[nodiscard]] SafetyStatusSnapshot snapshot() const;

signals:
    void safetyStateChanged(SafetyState state, const QString& reason);
    void safetySnapshotChanged(const SafetyStatusSnapshot& snapshot);
    void safetyStatusObserved(const SafetyStatusSnapshot& snapshot);

private slots:
    void acceptSafetyStatusSample(const SafetyStatusSnapshot& snapshot);
    void reportMonitoringFailure(const QString& message);

private:
    SafetyMonitor();

    [[nodiscard]] bool snapshotDiffersFromCurrent(
        const SafetyStatusSnapshot& snapshot) const;
    [[nodiscard]] static SafetyState stateFromSnapshot(
        const SafetyStatusSnapshot& snapshot);
    [[nodiscard]] static QString stateReason(
        SafetyState state,
        const SafetyStatusSnapshot& snapshot);

    mutable QMutex mutex_;
    AcsClient* client_ = nullptr;
    SafetyStatusSnapshot snapshot_;
    QString stateReason_;
    std::atomic<SafetyState> state_{SafetyState::Unknown};
    bool initialized_ = false;
    bool hasSnapshot_ = false;
    bool hasState_ = false;
};
