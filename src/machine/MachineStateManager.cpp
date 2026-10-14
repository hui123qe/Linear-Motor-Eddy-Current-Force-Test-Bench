#include "MachineStateManager.h"

#include "../logging/AppLogger.h"
#include "../safety/SafetyMonitor.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QString machineStateText(MachineState state)
{
    switch (state) {
    case MachineState::Error:
        return QStringLiteral("故障");
    case MachineState::Idle:
        return QStringLiteral("空闲");
    case MachineState::Running:
        return QStringLiteral("运行");
    case MachineState::Resetting:
        return QStringLiteral("复位中");
    }
    return QStringLiteral("未知");
}

} // namespace

MachineStateManager& MachineStateManager::instance()
{
    static MachineStateManager manager;
    return manager;
}

MachineStateManager::MachineStateManager()
{
    qRegisterMetaType<MachineState>("MachineState");
}

void MachineStateManager::initialize(SafetyMonitor* safetyMonitor)
{
    if (initialized_ || safetyMonitor == nullptr) {
        return;
    }

    safetyMonitor_ = safetyMonitor;
    safetyState_.store(safetyMonitor_->state(), std::memory_order_release);
    connect(safetyMonitor_,
            &SafetyMonitor::safetyStateChanged,
            this,
            &MachineStateManager::handleSafetyStateChanged,
            Qt::DirectConnection);
    initialized_ = true;
    qCInfo(logSafety) << "机器状态管理器已绑定安全回路监测";
}

void MachineStateManager::shutdown()
{
    if (safetyMonitor_ != nullptr) {
        disconnect(safetyMonitor_, nullptr, this, nullptr);
    }
    safetyMonitor_ = nullptr;
    initialized_ = false;
    safetyState_.store(SafetyState::Unknown, std::memory_order_release);
    commandEpoch_.fetch_add(1, std::memory_order_acq_rel);
}

MachineState MachineStateManager::state() const
{
    return state_.load(std::memory_order_acquire);
}

QString MachineStateManager::reason() const
{
    const QMutexLocker locker(&mutex_);
    return reason_;
}

SafetyState MachineStateManager::safetyState() const
{
    return safetyState_.load(std::memory_order_acquire);
}

quint64 MachineStateManager::commandEpoch() const
{
    return commandEpoch_.load(std::memory_order_acquire);
}

bool MachineStateManager::canSubmitCommand(
    MachineState requiredState,
    QString* errorMessage) const
{
    const SafetyState currentSafetyState = safetyState();
    if (currentSafetyState != SafetyState::Normal) {
        setError(errorMessage,
                 currentSafetyState == SafetyState::Triggered
                     ? QStringLiteral("安全回路已触发，控制命令被拒绝。")
                     : QStringLiteral("安全回路状态未知，控制命令被拒绝。"));
        return false;
    }

    const MachineState currentState = state();
    if (currentState != requiredState) {
        setError(errorMessage,
                 QStringLiteral("机器当前处于%1状态，不能执行该控制命令。")
                     .arg(machineStateText(currentState)));
        return false;
    }
    return true;
}

bool MachineStateManager::commandAllowed(
    quint64 submittedEpoch,
    MachineState requiredState) const
{
    return submittedEpoch != 0
           && submittedEpoch == commandEpoch()
           && safetyState() == SafetyState::Normal
           && state() == requiredState;
}

bool MachineStateManager::beginExecution(
    const QString& reason,
    QString* errorMessage)
{
    if (transitionFrom(MachineState::Idle,
                       MachineState::Running,
                       reason,
                       true,
                       true)) {
        return true;
    }
    if (canSubmitCommand(MachineState::Idle, errorMessage)) {
        setError(errorMessage,
                 QStringLiteral("机器状态在启动过程中发生变化。"));
    }
    return false;
}

void MachineStateManager::updateRunningReason(const QString& reason)
{
    static_cast<void>(transitionFrom(MachineState::Running,
                                     MachineState::Running,
                                     reason,
                                     false,
                                     false));
}

void MachineStateManager::completeExecution(const QString& reason)
{
    static_cast<void>(transitionFrom(MachineState::Running,
                                     MachineState::Idle,
                                     reason,
                                     true,
                                     true));
}

bool MachineStateManager::beginReset(QString* errorMessage)
{
    {
        const QMutexLocker locker(&mutex_);
        const MachineState currentState =
            state_.load(std::memory_order_relaxed);
        if (currentState != MachineState::Idle
            && currentState != MachineState::Error) {
            setError(errorMessage,
                     QStringLiteral("只有空闲或故障状态可以执行复位。"));
            return false;
        }
        if (safetyState_.load(std::memory_order_acquire)
            != SafetyState::Normal) {
            setError(errorMessage,
                     QStringLiteral("安全回路尚未恢复正常，不能执行复位。"));
            return false;
        }

        commandEpoch_.fetch_add(1, std::memory_order_acq_rel);
        reason_ = QStringLiteral("正在复位");
        state_.store(MachineState::Resetting, std::memory_order_release);
    }

    publishStateChange(MachineState::Resetting, QStringLiteral("正在复位"));
    return true;
}

void MachineStateManager::completeReset()
{
    static_cast<void>(transitionFrom(MachineState::Resetting,
                                     MachineState::Idle,
                                     QStringLiteral("复位完成"),
                                     true,
                                     true));
}

void MachineStateManager::failReset(const QString& reason)
{
    enterFault(reason);
}

void MachineStateManager::enterFault(const QString& reason)
{
    const bool alreadyInError = state() == MachineState::Error;
    transitionTo(MachineState::Error, reason, !alreadyInError);
}

void MachineStateManager::handleSafetyStateChanged(
    SafetyState state,
    const QString& reason)
{
    safetyState_.store(state, std::memory_order_release);
    if (state == SafetyState::Normal) {
        return;
    }

    enterFault(reason.isEmpty()
                   ? QStringLiteral("安全回路状态异常")
                   : reason);
}

bool MachineStateManager::transitionFrom(
    MachineState expectedState,
    MachineState nextState,
    const QString& reason,
    bool invalidateCommands,
    bool requireSafetyNormal)
{
    {
        const QMutexLocker locker(&mutex_);
        if (state_.load(std::memory_order_relaxed) != expectedState
            || (requireSafetyNormal
                && safetyState_.load(std::memory_order_acquire)
                       != SafetyState::Normal)) {
            return false;
        }
        if (expectedState == nextState && reason_ == reason) {
            return true;
        }
        if (invalidateCommands) {
            commandEpoch_.fetch_add(1, std::memory_order_acq_rel);
        }
        reason_ = reason;
        state_.store(nextState, std::memory_order_release);
    }

    qCInfo(logApplication).noquote()
        << "机器状态变更：" << machineStateText(nextState)
        << "原因：" << reason;
    publishStateChange(nextState, reason);
    return true;
}

void MachineStateManager::transitionTo(
    MachineState nextState,
    const QString& reason,
    bool invalidateCommands)
{
    bool changed = false;
    {
        const QMutexLocker locker(&mutex_);
        const MachineState previousState = state_.load(std::memory_order_relaxed);
        changed = previousState != nextState || reason_ != reason;
        if (!changed) {
            return;
        }
        if (invalidateCommands) {
            commandEpoch_.fetch_add(1, std::memory_order_acq_rel);
        }
        reason_ = reason;
        state_.store(nextState, std::memory_order_release);
    }

    qCInfo(logApplication).noquote()
        << "机器状态变更：" << machineStateText(nextState)
        << "原因：" << reason;
    publishStateChange(nextState, reason);
}

void MachineStateManager::publishStateChange(
    MachineState state,
    const QString& reason)
{
    if (QThread::currentThread() == thread()) {
        emit machineStateChanged(state, reason);
        return;
    }

    QMetaObject::invokeMethod(
        this,
        [this, state, reason] {
            emit machineStateChanged(state, reason);
        },
        Qt::QueuedConnection);
}
