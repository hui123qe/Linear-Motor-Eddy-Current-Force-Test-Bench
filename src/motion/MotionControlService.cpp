#include "MotionControlService.h"

#include "../config/TestParameters.h"
#include "../logging/AppLogger.h"
#include "../machine/MachineStateManager.h"

#include <QMetaObject>

#include <cmath>

namespace {

void setError(QString* errorMessage, const QString& message)
{
    qCWarning(logMotion).noquote() << message;
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool isActiveState(int state)
{
    return state == 10 || state == 20 || state == 30
           || state == 40 || state == 50 || state == 55
           || state == 60 || state == 70 || state == 75;
}

} // namespace

MotionControlService& MotionControlService::instance()
{
    static MotionControlService service;
    return service;
}

MotionControlService::MotionControlService()
    : client_(new AcsClient)
{
    qRegisterMetaType<AcsMotionStatus>("AcsMotionStatus");
    qRegisterMetaType<AcsSensorReadings>("AcsSensorReadings");
    qRegisterMetaType<MachineMode>("MachineMode");
    qRegisterMetaType<MaintenanceCommand>("MaintenanceCommand");
    qRegisterMetaType<ProtectionStopResult>();

    client_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, client_, &QObject::deleteLater);
    connect(client_,
            &AcsClient::connectionChanged,
            this,
            [this](bool connected, const QString& message) {
                connected_ = connected;
                if (!connected) { protectionStopped_ = false; }
                if (!connected) {
                    zeroMovePending_ = false;
                    maintenanceCommandPending_ = false;
                    forceTarePending_ = false;
                    currentStatus_ = AcsMotionStatus{};
                    hasStatusSnapshot_ = false;
                    emit homeDoneChanged(false);
                    emit homeRunningChanged(false);
                }
                if (connected) {
                    qCInfo(logMotion).noquote() << message;
                } else {
                    qCWarning(logMotion).noquote() << message;
                }
                emit connectionChanged(connected, message);
                if (!connected) {
                    // 断线先通知业务终止，缓存复位不得作为记录完成事件发布。
                    emit motionStateChanged(0, 0);
                    emit axisEnabledChanged(false);
                }
            });
    connect(client_,
            &AcsClient::statusChanged,
            this,
            [this](const AcsMotionStatus& status) {
                const AcsMotionStatus previous = currentStatus_;
                const bool firstSnapshot = !hasStatusSnapshot_;
                currentStatus_ = status;
                hasStatusSnapshot_ = true;
                // 同一快照先处理故障和采集边界，再发布完成计数。
                if (firstSnapshot || status.state != previous.state
                    || status.errorCode != previous.errorCode) {
                    qCInfo(logMotion)
                        << "ACS 状态变化：state=" << status.state
                        << "errorCode=" << status.errorCode;
                    emit motionStateChanged(status.state, status.errorCode);
                }
                if (status.state != previous.state
                    && (status.state == 55 || status.state == 75)) {
                    emit recordProcessingEntered(status.state);
                }
                if (firstSnapshot || status.currentCount != previous.currentCount) {
                    qCInfo(logMotion)
                        << "ACS 完成记录计数变化：previous=" << previous.currentCount
                        << "completedCount=" << status.currentCount;
                    emit completedRecordCountChanged(status.currentCount);
                }
                if (firstSnapshot || status.axisEnabled != previous.axisEnabled) {
                    emit axisEnabledChanged(status.axisEnabled);
                }

                if (firstSnapshot || (status.bufferRunning != previous.bufferRunning&&status.bufferRunning  ==0)) {
                    emit zeroPositionReached();
                }

                if (firstSnapshot || status.homeDone != previous.homeDone) {
                    emit homeDoneChanged(status.homeDone);
                }

                if (firstSnapshot || status.homeRunning != previous.homeRunning) {
                    emit homeRunningChanged(status.homeRunning);
                }
                
                // 反馈持续发布，保证重连后即使位置不变也能恢复显示。
                emit positionFeedbackChanged(status.feedbackPositionMillimeters);
                emit velocityFeedbackChanged(status.feedbackVelocityMillimetersPerSecond);
            });
    connect(client_,
            &AcsClient::sensorReadingsChanged,
            this,
            &MotionControlService::sensorReadingsChanged);
    connect(client_,
            &AcsClient::controllerResetCompleted,
            this,
            [this] {
                zeroMovePending_ = false;
                maintenanceCommandPending_ = false;
                protectionStopped_ = false;
                emit controllerResetCompleted();
            });
    connect(client_,
            &AcsClient::controllerResetFailed,
            this,
            [this](const QString& message) {
                maintenanceCommandPending_ = false;
                emit controllerResetFailed(message);
            });
    connect(client_,
            &AcsClient::emergencyStopCompleted,
            this,
            [this] {
                zeroMovePending_ = false;
                maintenanceCommandPending_ = false;
                forceTarePending_ = false;
                emit emergencyStopCompleted();
            });
    connect(client_,
            &AcsClient::emergencyStopFailed,
            this,
            &MotionControlService::emergencyStopFailed);
    connect(client_, &AcsClient::protectionStopResult, this,
            &MotionControlService::protectionStopResult, Qt::DirectConnection);
    connect(client_, &AcsClient::protectionStopResult, this, [this](const ProtectionStopResult& result) {
        qCInfo(logMotion) << "故障停止反馈" << result.occurrenceId
                         << "state=" << static_cast<int>(result.state) << result.detail;
        protectionStopped_ = result.state == ProtectionStopState::Confirmed;
        if (protectionStopped_) {
            zeroMovePending_ = false;
            maintenanceCommandPending_ = false;
            forceTarePending_ = false;
        }
    });
    connect(client_,
            &AcsClient::forceTareStarted,
            this,
            [this] {
                forceTarePending_ = false;
                emit forceTareStarted();
            });
    connect(client_,
            &AcsClient::forceTareFailed,
            this,
            [this](const QString& message) {
                forceTarePending_ = false;
                emit forceTareFailed(message);
            });
    connect(client_,
            &AcsClient::maintenanceCommandCompleted,
            this,
            [this](MaintenanceCommand command) {
                maintenanceCommandPending_ = false;
                emit maintenanceCommandCompleted(command);
            });
    connect(client_,
            &AcsClient::maintenanceCommandFailed,
            this,
            [this](MaintenanceCommand command, const QString& message) {
                maintenanceCommandPending_ = false;
                if (command == MaintenanceCommand::HomeAxis
                    && currentStatus_.homeRunning) {
                    currentStatus_.homeRunning = false;
                    emit homeRunningChanged(false);
                }
                emit maintenanceCommandFailed(command, message);
            });
    connect(client_, &AcsClient::zeroPositionReached, this, [this] {
        if (zeroMovePending_) {
            zeroMovePending_ = false;
            emit zeroPositionReached();
        }
    });
    connect(client_,
            &AcsClient::startRequestWritten,
            this,
            [this] {
                // AcsClient 已在写入启动请求前清零控制器计数。
                // 同步比较基线，下一轮即使与上一轮计数相同也能发布变化。
                currentStatus_.currentCount = 0;
                emit startRequestWritten();
            });
    connect(client_,
            &AcsClient::stopRequestWritten,
            this,
            &MotionControlService::stopRequestWritten);
    connect(client_,
            &AcsClient::commandFailed,
            this,
            &MotionControlService::commandFailed);

    MachineStateManager& machineStateManager = MachineStateManager::instance();
    connect(&machineStateManager,
            &MachineStateManager::machineStateChanged,
            this,
            [this](MachineState state, const QString& reason) {
                if (state == MachineState::Error) {
                    maintenanceCommandPending_ = false;
                    forceTarePending_ = false;
                    if (currentStatus_.homeRunning) {
                        currentStatus_.homeRunning = false;
                        emit homeRunningChanged(false);
                    }
                    emit safetyInhibitionChanged(true, reason);
                } else if (state == MachineState::Idle) {
                    emit safetyInhibitionChanged(false, reason);
                }
            });

    workerThread_.setObjectName(QStringLiteral("AcsMotionThread"));
}

MotionControlService::~MotionControlService()
{
    shutdown();
}

void MotionControlService::initialize()
{
    if (initialized_) {
        return;
    }

    initialized_ = true;
    workerThread_.start();
    qCInfo(logMotion) << "ACS 电机服务线程已启动";
}

void MotionControlService::shutdown()
{
    if (!workerThread_.isRunning()) {
        return;
    }

    qCInfo(logMotion) << "正在关闭 ACS 电机服务";
    if (connected_) {
        QMetaObject::invokeMethod(
            client_, &AcsClient::haltAxis, Qt::BlockingQueuedConnection);
    }
    QMetaObject::invokeMethod(
        client_, &AcsClient::disconnectController, Qt::BlockingQueuedConnection);
    workerThread_.quit();
    workerThread_.wait();
    connected_ = false;
    qCInfo(logMotion) << "ACS 电机服务已关闭";
}

void MotionControlService::connectController()
{
    if (!workerThread_.isRunning()) {
        const QString message = QStringLiteral("ACS 电机服务尚未初始化。");
        qCWarning(logMotion).noquote() << message;
        emit commandFailed(message);
        return;
    }

    qCInfo(logMotion) << "请求连接 ACS 控制器";
    QMetaObject::invokeMethod(
        client_, &AcsClient::connectController, Qt::QueuedConnection);
}

bool MotionControlService::disconnectController(QString* errorMessage)
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage, QStringLiteral("ACS 电机服务尚未初始化。"));
        return false;
    }
    if (maintenanceCommandPending_
        || isActiveState(currentStatus_.state)) {
        setError(
            errorMessage,
            QStringLiteral("维修命令正在处理或自动测试正在运行，请先停止后再断开 ACS 控制器。"));
        return false;
    }

    qCInfo(logMotion) << "请求断开 ACS 控制器";
    QMetaObject::invokeMethod(
        client_, &AcsClient::disconnectController, Qt::QueuedConnection);
    return true;
}

bool MotionControlService::emergencyStop(QString* errorMessage)
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage,
                 QStringLiteral("ACS 电机服务尚未初始化，不能执行软件急停。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage,
                 QStringLiteral("ACS 控制器未连接，不能执行软件急停。"));
        return false;
    }

    qCCritical(logMotion) << "请求执行软件急停：ACS Kill All";
    QMetaObject::invokeMethod(
        client_, &AcsClient::emergencyStop, Qt::QueuedConnection);
    return true;
}

void MotionControlService::tareForceSensor()
{
    if (!workerThread_.isRunning()) {
        const QString message =
            QStringLiteral("ACS 电机服务尚未初始化，不能执行力传感器去皮。");
        qCWarning(logMotion).noquote() << message;
        emit forceTareFailed(message);
        return;
    }
    if (!connected_) {
        const QString message =
            QStringLiteral("ACS 控制器未连接，不能执行力传感器去皮。");
        qCWarning(logMotion).noquote() << message;
        emit forceTareFailed(message);
        return;
    }
    if (!hasStatusSnapshot_) {
        const QString message =
            QStringLiteral("尚未读取到 ACS 状态，不能执行力传感器去皮。");
        qCWarning(logMotion).noquote() << message;
        emit forceTareFailed(message);
        return;
    }
    if (forceTarePending_) {
        const QString message =
            QStringLiteral("力传感器去皮请求正在处理中，请勿重复操作。");
        qCWarning(logMotion).noquote() << message;
        emit forceTareFailed(message);
        return;
    }
    if (maintenanceCommandPending_ || currentStatus_.homeRunning
        || currentStatus_.state != 0) {
        const QString message = QStringLiteral(
            "维修命令、回零或自动流程正在执行，不能进行力传感器去皮。");
        qCWarning(logMotion).noquote() << message;
        emit forceTareFailed(message);
        return;
    }

    MachineStateManager& machineStateManager = MachineStateManager::instance();
    QString stateError;
    if (!machineStateManager.canSubmitCommand(MachineState::Idle, &stateError)) {
        qCWarning(logMotion).noquote() << stateError;
        emit forceTareFailed(stateError);
        return;
    }
    const quint64 commandEpoch = machineStateManager.commandEpoch();

    qCInfo(logMotion) << "请求启动 ACS Buffer 8 执行力传感器去皮";
    forceTarePending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Idle)) {
                QMetaObject::invokeMethod(
                    this,
                    &MotionControlService::rejectQueuedForceTare,
                    Qt::QueuedConnection);
                return;
            }
            client->tareForceSensor();
        },
        Qt::QueuedConnection);
}

MachineMode MotionControlService::machineMode() const
{
    return machineMode_;
}

bool MotionControlService::setMachineMode(MachineMode mode,
                                          QString* errorMessage)
{
    if (mode == machineMode_) {
        return true;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接，不能切换机器模式。"));
        return false;
    }
    if (maintenanceCommandPending_
        || currentStatus_.state != 0) {
        setError(
            errorMessage,
            QStringLiteral("维修命令正在处理或自动流程未处于空闲状态，不能切换机器模式。"));
        return false;
    }

    machineMode_ = mode;
    qCInfo(logMotion)
        << "机器模式切换为"
        << (mode == MachineMode::Automatic ? "自动" : "维修");
    emit machineModeChanged(mode);
    return true;
}

bool MotionControlService::enableAxis(QString* errorMessage)
{
    if (!validateMaintenanceCommand(false, errorMessage)) {
        return false;
    }
    if (currentStatus_.axisEnabled) {
        setError(errorMessage, QStringLiteral("轴当前已经使能。"));
        return false;
    }
    if (!MachineStateManager::instance().canSubmitCommand(
            MachineState::Idle, errorMessage)) {
        return false;
    }

    const quint64 commandEpoch =
        MachineStateManager::instance().commandEpoch();

    qCInfo(logMotion) << "接受维修上使能请求";
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Idle)) {
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        rejectQueuedMaintenanceCommand(
                            MaintenanceCommand::EnableAxis);
                    },
                    Qt::QueuedConnection);
                return;
            }
            client->enableAxis();
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::disableAxis(QString* errorMessage)
{
    if (!validateMaintenanceCommand(false, errorMessage)) {
        return false;
    }
    if (!currentStatus_.axisEnabled) {
        setError(errorMessage, QStringLiteral("轴当前已经下使能。"));
        return false;
    }

    qCInfo(logMotion) << "接受维修下使能请求";
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(client_, &AcsClient::disableAxis, Qt::QueuedConnection);
    return true;
}

bool MotionControlService::homeAxis(QString* errorMessage)
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage, QStringLiteral("ACS 电机服务尚未初始化。"));
        return false;
    }
    if (!connected_ || !hasStatusSnapshot_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接或状态尚未就绪。"));
        return false;
    }
    if (maintenanceCommandPending_) {
        setError(errorMessage, QStringLiteral("上一条控制命令正在处理中。"));
        return false;
    }
    if (currentStatus_.homeRunning) {
        setError(errorMessage, QStringLiteral("机器正在回零。"));
        return false;
    }
    if (currentStatus_.state != 0) {
        setError(
            errorMessage,
            QStringLiteral("机器当前处于运行状态 %1，不能回零。")
                .arg(currentStatus_.state));
        return false;
    }

    if (!MachineStateManager::instance().canSubmitCommand(
            MachineState::Idle, errorMessage)) {
        return false;
    }
    const quint64 commandEpoch =
        MachineStateManager::instance().commandEpoch();

    qCInfo(logMotion) << "接受维修回零请求";
    maintenanceCommandPending_ = true;
    currentStatus_.homeRunning = true;
    emit homeRunningChanged(true);
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Idle)) {
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        rejectQueuedMaintenanceCommand(
                            MaintenanceCommand::HomeAxis);
                    },
                    Qt::QueuedConnection);
                return;
            }
            client->homeAxis();
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::moveRelative(double distanceMillimeters,
                                        QString* errorMessage)
{
    if (!validateMaintenanceCommand(true, errorMessage)) {
        return false;
    }
    if (!std::isfinite(distanceMillimeters)
        || distanceMillimeters == 0.0) {
        setError(errorMessage, QStringLiteral("相对运动距离必须为非零有限数值。"));
        return false;
    }
    if (!MachineStateManager::instance().canSubmitCommand(
            MachineState::Idle, errorMessage)) {
        return false;
    }

    const quint64 commandEpoch =
        MachineStateManager::instance().commandEpoch();

    qCInfo(logMotion)
        << "接受维修相对运动请求，distanceMm=" << distanceMillimeters;
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, distanceMillimeters, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Idle)) {
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        rejectQueuedMaintenanceCommand(
                            MaintenanceCommand::RelativeMove);
                    },
                    Qt::QueuedConnection);
                return;
            }
            client->moveRelative(
                distanceMillimeters,
                kMaintenancePtpVelocityMillimetersPerSecond);
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::moveAbsolute(double positionMillimeters,
                                        QString* errorMessage)
{
    if (!validateMaintenanceCommand(true, errorMessage)) {
        return false;
    }
    if (!std::isfinite(positionMillimeters)) {
        setError(errorMessage, QStringLiteral("绝对运动位置必须为有限数值。"));
        return false;
    }
    if (!MachineStateManager::instance().canSubmitCommand(
            MachineState::Idle, errorMessage)) {
        return false;
    }

    const quint64 commandEpoch =
        MachineStateManager::instance().commandEpoch();

    qCInfo(logMotion)
        << "接受维修绝对运动请求，positionMm=" << positionMillimeters;
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, positionMillimeters, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Idle)) {
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        rejectQueuedMaintenanceCommand(
                            MaintenanceCommand::AbsoluteMove);
                    },
                    Qt::QueuedConnection);
                return;
            }
            client->moveAbsolute(
                positionMillimeters,
                kMaintenancePtpVelocityMillimetersPerSecond);
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::startJog(int direction, QString* errorMessage)
{
    if (!validateMaintenanceCommand(true, errorMessage)) {
        return false;
    }
    if (direction != -1 && direction != 1) {
        setError(errorMessage, QStringLiteral("JOG 方向必须为 -1 或 1。"));
        return false;
    }
    if (!MachineStateManager::instance().canSubmitCommand(
            MachineState::Idle, errorMessage)) {
        return false;
    }

    const quint64 commandEpoch =
        MachineStateManager::instance().commandEpoch();

    const double velocityMillimetersPerSecond =
        direction * kMaintenanceJogVelocityMillimetersPerSecond;
    qCInfo(logMotion)
        << "接受维修 JOG 请求，velocityMmPerSecond="
        << velocityMillimetersPerSecond;
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, velocityMillimetersPerSecond, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Idle)) {
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        rejectQueuedMaintenanceCommand(
                            MaintenanceCommand::StartJog);
                    },
                    Qt::QueuedConnection);
                return;
            }
            client->startJog(velocityMillimetersPerSecond);
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::haltMaintenanceMotion(QString* errorMessage)
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage, QStringLiteral("ACS 电机服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接。"));
        return false;
    }

    qCInfo(logMotion) << "接受维修 HALT 请求";
    QMetaObject::invokeMethod(client_, &AcsClient::haltAxis, Qt::QueuedConnection);
    return true;
}

AcsClient* MotionControlService::acsClient() const
{
    return client_;
}

bool MotionControlService::resetController(QString* errorMessage)
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage, QStringLiteral("ACS 电机服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接。"));
        return false;
    }
    if (maintenanceCommandPending_) {
        setError(errorMessage, QStringLiteral("上一条控制命令正在处理中。"));
        return false;
    }
    MachineStateManager& machineStateManager = MachineStateManager::instance();
    if (!machineStateManager.canSubmitCommand(
            MachineState::Resetting, errorMessage)) {
        return false;
    }
    const quint64 commandEpoch =
        machineStateManager.commandEpoch();

    qCWarning(logMotion) << "接受 ACS 控制器复位请求";
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Resetting)) {
                QMetaObject::invokeMethod(
                    this,
                    &MotionControlService::rejectQueuedControllerReset,
                    Qt::QueuedConnection);
                return;
            }
            client->resetController();
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::homeDone() const
{
    return connected_ && hasStatusSnapshot_ && currentStatus_.homeDone;
}

bool MotionControlService::homeRunning() const
{
    return connected_ && hasStatusSnapshot_ && currentStatus_.homeRunning;
}

bool MotionControlService::hasActiveMotion() const
{
    // return zeroMovePending_ || currentStatus_.axisMoving || std::abs(currentStatus_.feedbackVelocityMillimetersPerSecond) > 0.001
    //     || (!protectionStopped_ && (maintenanceCommandPending_ || currentStatus_.homeRunning || isActiveState(currentStatus_.state)));

    return false;
}

void MotionControlService::requestProtectionStop(const ProtectionStopRequest& request)
{
    if (!workerThread_.isRunning()) {
        qCWarning(logMotion) << "故障停止请求失败：ACS 工作线程未启动" << request.occurrenceId;
        emit protectionStopResult({request.occurrenceId, request.buffer, ProtectionStopState::Failed,
            false, false, QStringLiteral("ACS 工作线程未启动")}); return;
    }
    QMetaObject::invokeMethod(client_, [client = client_, request] { client->stopConfiguredBufferAndKillAll(request); }, Qt::QueuedConnection);
}

bool MotionControlService::moveToZero(QString* errorMessage)
{
    if (!workerThread_.isRunning() || !connected_ || !hasStatusSnapshot_
        || machineMode_ != MachineMode::Automatic || !homeDone()
        || currentStatus_.state != 0
        || hasActiveMotion()) {
        setError(errorMessage, QStringLiteral("当前状态不允许测试前绝对移动到 0。"));
        return false;
    }
    if (!MachineStateManager::instance().canSubmitCommand(
            MachineState::Running, errorMessage)) {
        return false;
    }
    const quint64 commandEpoch = MachineStateManager::instance().commandEpoch();
    zeroMovePending_ = true;
    qCInfo(logMotion) << "测试前绝对移动到 0 mm";
    QMetaObject::invokeMethod(client_, [this, client = client_, commandEpoch] {
        client->moveToZero();
    }, Qt::QueuedConnection);
    return true;
}

bool MotionControlService::start(const TestParameters& parameters,
                                 QString* errorMessage)
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage, QStringLiteral("ACS 电机服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接。"));
        return false;
    }
    if (machineMode_ != MachineMode::Automatic) {
        setError(errorMessage, QStringLiteral("机器当前处于维修模式，不能启动自动测试。"));
        return false;
    }
    if (maintenanceCommandPending_) {
        setError(errorMessage, QStringLiteral("控制命令正在处理中，不能启动自动测试。"));
        return false;
    }
    if (homeRunning()) {
        setError(errorMessage,
                 QStringLiteral("机器正在回零，不能启动自动测试。"));
        return false;
    }
    if (!homeDone()) {
        setError(errorMessage,
                 QStringLiteral("机器尚未完成回零，不能启动自动测试。"));
        return false;
    }
    if (currentStatus_.state != 0) {
        setError(
            errorMessage,
            QStringLiteral("ACS 当前状态为 %1，只有空闲状态 0 可以启动。")
                .arg(currentStatus_.state));
        return false;
    }
    if (!validateTestParameters(parameters, errorMessage)) {
        return false;
    }

    MachineStateManager& machineStateManager = MachineStateManager::instance();
    if (!machineStateManager.canSubmitCommand(
            MachineState::Running, errorMessage)) {
        return false;
    }
    const quint64 commandEpoch = machineStateManager.commandEpoch();

    const TestMotionParameters& selectedTest =
        parameters.selectedTestType == EddyCurrentTestType::RatedSpeed
            ? parameters.ratedSpeedTest
            : parameters.variableSpeedTest;
    PtpMotionParameters ptpMotionParameters;
    if (!calculatePtpMotionParameters(
            selectedTest, &ptpMotionParameters, errorMessage)) {
        return false;
    }

    MotionStartRequest request;
    request.startPositionMeters = ptpMotionParameters.startPositionMeters;
    request.endPositionMeters = ptpMotionParameters.endPositionMeters;
    request.velocityMetersPerSecond = ptpMotionParameters.velocityMetersPerSecond;
    request.accelerationMetersPerSecondSquared =
        ptpMotionParameters.accelerationMetersPerSecondSquared;
    request.decelerationMetersPerSecondSquared =
        ptpMotionParameters.decelerationMetersPerSecondSquared;
    request.repeatCount = parameters.repeatCount;

    const bool requestValid = std::isfinite(request.startPositionMeters)
                              && std::isfinite(request.endPositionMeters)
                              && std::isfinite(request.velocityMetersPerSecond)
                              && std::isfinite(
                                  request.accelerationMetersPerSecondSquared)
                              && std::isfinite(
                                  request.decelerationMetersPerSecondSquared);
    if (!requestValid) {
        setError(errorMessage, QStringLiteral("运动参数换算结果无效。"));
        return false;
    }

    qCInfo(logMotion)
        << "提交启动请求：start(m)=" << request.startPositionMeters
        << "end(m)=" << request.endPositionMeters
        << "velocity(m/s)=" << request.velocityMetersPerSecond
        << "acceleration(m/s2)=" << request.accelerationMetersPerSecondSquared
        << "deceleration(m/s2)=" << request.decelerationMetersPerSecondSquared
        << "repeat=" << request.repeatCount;
    QMetaObject::invokeMethod(
        client_,
        [this, client = client_, request, commandEpoch] {
            if (!MachineStateManager::instance().commandAllowed(
                    commandEpoch, MachineState::Running)) {
                QMetaObject::invokeMethod(
                    this,
                    &MotionControlService::rejectQueuedAutomaticStart,
                    Qt::QueuedConnection);
                return;
            }
            client->start(request);
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::stop(QString* errorMessage)
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage, QStringLiteral("ACS 电机服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接。"));
        return false;
    }
    if (zeroMovePending_) {
        zeroMovePending_ = false;
        qCInfo(logMotion) << "停止测试前定位运动";
        QMetaObject::invokeMethod(client_, [client = client_] {
            client->stop();
        }, Qt::QueuedConnection);
        return true;
    }
    if (!isActiveState(currentStatus_.state)) {
        setError(
            errorMessage,
            QStringLiteral("ACS 当前状态为 %1，没有可停止的运动。")
                .arg(currentStatus_.state));
        return false;
    }

    qCInfo(logMotion)
        << "提交停止请求，当前 ACS state=" << currentStatus_.state;
    QMetaObject::invokeMethod(
        client_,
        [client = client_] {
            client->stop();
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::validateMaintenanceCommand(
    bool requireEnabled,
    QString* errorMessage) const
{
    if (!workerThread_.isRunning()) {
        setError(errorMessage, QStringLiteral("ACS 电机服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接。"));
        return false;
    }
    if (machineMode_ != MachineMode::Maintenance) {
        setError(errorMessage, QStringLiteral("机器不在维修模式。"));
        return false;
    }
    if (maintenanceCommandPending_) {
        setError(errorMessage, QStringLiteral("上一条维修命令正在处理中。"));
        return false;
    }
    if (currentStatus_.state != 0) {
        setError(
            errorMessage,
            QStringLiteral("ACS 自动流程状态为 %1，维修运动被拒绝。")
                .arg(currentStatus_.state));
        return false;
    }
    if (requireEnabled && !currentStatus_.axisEnabled) {
        setError(errorMessage, QStringLiteral("轴尚未使能。"));
        return false;
    }

    return true;
}

void MotionControlService::rejectQueuedForceTare()
{
    forceTarePending_ = false;
    const QString message =
        QStringLiteral("机器状态已变化，队列中的旧去皮命令已作废。");
    qCWarning(logMotion).noquote() << message;
    emit forceTareFailed(message);
}

void MotionControlService::rejectQueuedMaintenanceCommand(
    MaintenanceCommand command)
{
    maintenanceCommandPending_ = false;
    if (command == MaintenanceCommand::HomeAxis
        && currentStatus_.homeRunning) {
        currentStatus_.homeRunning = false;
        emit homeRunningChanged(false);
    }
    const QString message =
        QStringLiteral("机器状态已变化，队列中的旧维修命令已作废。");
    qCWarning(logMotion).noquote() << message;
    emit maintenanceCommandFailed(command, message);
}

void MotionControlService::rejectQueuedControllerReset()
{
    maintenanceCommandPending_ = false;
    const QString message =
        QStringLiteral("机器状态已变化，队列中的旧复位命令已作废。");
    qCWarning(logMotion).noquote() << message;
    emit controllerResetFailed(message);
}

void MotionControlService::rejectQueuedAutomaticStart()
{
    const QString message =
        QStringLiteral("机器状态已变化，队列中的旧启动命令已作废。");
    qCWarning(logMotion).noquote() << message;
    emit commandFailed(message);
}
