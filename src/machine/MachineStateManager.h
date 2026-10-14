#pragma once

#include "../safety/SafetyTypes.h"

#include <QMutex>
#include <QObject>
#include <QString>

#include <atomic>

class SafetyMonitor;

enum class MachineState
{
    Error,
    Idle,
    Running,
    Resetting
};

Q_DECLARE_METATYPE(MachineState)

class MachineStateManager final : public QObject
{
    Q_OBJECT

public:
    static MachineStateManager& instance();

    void initialize(SafetyMonitor* safetyMonitor);
    void shutdown();

    [[nodiscard]] MachineState state() const;
    [[nodiscard]] QString reason() const;
    [[nodiscard]] SafetyState safetyState() const;
    [[nodiscard]] quint64 commandEpoch() const;
    [[nodiscard]] bool canSubmitCommand(
        MachineState requiredState,
        QString* errorMessage = nullptr) const;
    [[nodiscard]] bool commandAllowed(
        quint64 submittedEpoch,
        MachineState requiredState) const;

    [[nodiscard]] bool beginExecution(
        const QString& reason,
        QString* errorMessage = nullptr);
    void updateRunningReason(const QString& reason);
    void completeExecution(const QString& reason);

    [[nodiscard]] bool beginReset(QString* errorMessage = nullptr);
    void completeReset();
    void failReset(const QString& reason);
    void enterFault(const QString& reason);

signals:
    void machineStateChanged(MachineState state, const QString& reason);

private:
    MachineStateManager();

    void handleSafetyStateChanged(SafetyState state, const QString& reason);
    [[nodiscard]] bool transitionFrom(MachineState expectedState,
                                      MachineState nextState,
                                      const QString& reason,
                                      bool invalidateCommands,
                                      bool requireSafetyNormal);
    void transitionTo(MachineState state,
                      const QString& reason,
                      bool invalidateCommands);
    void publishStateChange(MachineState state, const QString& reason);

    mutable QMutex mutex_;
    SafetyMonitor* safetyMonitor_ = nullptr;
    QString reason_ = QStringLiteral("任务空闲");
    std::atomic<MachineState> state_{MachineState::Idle};
    std::atomic<SafetyState> safetyState_{SafetyState::Unknown};
    std::atomic<quint64> commandEpoch_{1};
    bool initialized_ = false;
};
