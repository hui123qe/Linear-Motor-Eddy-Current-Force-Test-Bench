#include "MotionControlService.h"

#include "../config/TestParameters.h"
#include "../logging/AppLogger.h"

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

    client_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, client_, &QObject::deleteLater);
    connect(client_,
            &AcsClient::connectionChanged,
            this,
            [this](bool connected, const QString& message) {
                connected_ = connected;
                if (!connected) {
                    maintenanceCommandPending_ = false;
                    currentStatus_ = AcsMotionStatus{};
                    emit motionStatusChanged(currentStatus_);
                }
                if (connected) {
                    qCInfo(logMotion).noquote() << message;
                } else {
                    qCWarning(logMotion).noquote() << message;
                }
                emit connectionChanged(connected, message);
            });
    connect(client_,
            &AcsClient::statusChanged,
            this,
            [this](const AcsMotionStatus& status) {
                const bool discreteStatusChanged =
                    status.state != currentStatus_.state
                    || status.errorCode != currentStatus_.errorCode
                    || status.currentCount != currentStatus_.currentCount
                    || status.axisEnabled != currentStatus_.axisEnabled
                    || status.axisMoving != currentStatus_.axisMoving;
                if (discreteStatusChanged) {
                    qCInfo(logMotion)
                        << "ACS 状态变化：state=" << status.state
                        << "errorCode=" << status.errorCode
                        << "currentCount=" << status.currentCount
                        << "axisEnabled=" << status.axisEnabled
                        << "axisMoving=" << status.axisMoving;
                }
                currentStatus_ = status;
                emit motionStatusChanged(status);
            });
    connect(client_,
            &AcsClient::sensorReadingsChanged,
            this,
            &MotionControlService::sensorReadingsChanged);
    connect(client_,
            &AcsClient::forceTareWritten,
            this,
            &MotionControlService::forceTareWritten);
    connect(client_,
            &AcsClient::forceTareFailed,
            this,
            &MotionControlService::forceTareFailed);
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
                emit maintenanceCommandFailed(command, message);
            });
    connect(client_,
            &AcsClient::startRequestWritten,
            this,
            &MotionControlService::startRequestWritten);
    connect(client_,
            &AcsClient::stopRequestWritten,
            this,
            &MotionControlService::stopRequestWritten);
    connect(client_,
            &AcsClient::commandFailed,
            this,
            &MotionControlService::commandFailed);

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
        || currentStatus_.axisMoving
        || isActiveState(currentStatus_.state)) {
        setError(
            errorMessage,
            QStringLiteral("轴或自动测试正在运动，请先停止后再断开 ACS 控制器。"));
        return false;
    }

    qCInfo(logMotion) << "请求断开 ACS 控制器";
    QMetaObject::invokeMethod(
        client_, &AcsClient::disconnectController, Qt::QueuedConnection);
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

    qCInfo(logMotion) << "请求执行力传感器去皮";
    QMetaObject::invokeMethod(
        client_, &AcsClient::tareForceSensor, Qt::QueuedConnection);
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
        || currentStatus_.state != 0
        || currentStatus_.axisMoving) {
        setError(
            errorMessage,
            QStringLiteral("轴或自动流程正在运动，不能切换机器模式。"));
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
    if (!validateMaintenanceCommand(false, true, errorMessage)) {
        return false;
    }
    if (currentStatus_.axisEnabled) {
        setError(errorMessage, QStringLiteral("轴当前已经使能。"));
        return false;
    }

    qCInfo(logMotion) << "接受维修上使能请求";
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(client_, &AcsClient::enableAxis, Qt::QueuedConnection);
    return true;
}

bool MotionControlService::disableAxis(QString* errorMessage)
{
    if (!validateMaintenanceCommand(false, true, errorMessage)) {
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

bool MotionControlService::moveToZero(QString* errorMessage)
{
    if (!validateMaintenanceCommand(true, true, errorMessage)) {
        return false;
    }

    qCInfo(logMotion) << "接受维修回零请求";
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [client = client_] {
            client->moveToZero(kMaintenancePtpVelocityMillimetersPerSecond);
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::moveRelative(double distanceMillimeters,
                                        QString* errorMessage)
{
    if (!validateMaintenanceCommand(true, true, errorMessage)) {
        return false;
    }
    if (!std::isfinite(distanceMillimeters)
        || distanceMillimeters == 0.0) {
        setError(errorMessage, QStringLiteral("相对运动距离必须为非零有限数值。"));
        return false;
    }

    qCInfo(logMotion)
        << "接受维修相对运动请求，distanceMm=" << distanceMillimeters;
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [client = client_, distanceMillimeters] {
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
    if (!validateMaintenanceCommand(true, true, errorMessage)) {
        return false;
    }
    if (!std::isfinite(positionMillimeters)) {
        setError(errorMessage, QStringLiteral("绝对运动位置必须为有限数值。"));
        return false;
    }

    qCInfo(logMotion)
        << "接受维修绝对运动请求，positionMm=" << positionMillimeters;
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [client = client_, positionMillimeters] {
            client->moveAbsolute(
                positionMillimeters,
                kMaintenancePtpVelocityMillimetersPerSecond);
        },
        Qt::QueuedConnection);
    return true;
}

bool MotionControlService::startJog(int direction, QString* errorMessage)
{
    if (!validateMaintenanceCommand(true, true, errorMessage)) {
        return false;
    }
    if (direction != -1 && direction != 1) {
        setError(errorMessage, QStringLiteral("JOG 方向必须为 -1 或 1。"));
        return false;
    }

    const double velocityMillimetersPerSecond =
        direction * kMaintenanceJogVelocityMillimetersPerSecond;
    qCInfo(logMotion)
        << "接受维修 JOG 请求，velocityMmPerSecond="
        << velocityMillimetersPerSecond;
    maintenanceCommandPending_ = true;
    QMetaObject::invokeMethod(
        client_,
        [client = client_, velocityMillimetersPerSecond] {
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
    if (currentStatus_.state != 0) {
        setError(
            errorMessage,
            QStringLiteral("ACS 当前状态为 %1，只有空闲状态 0 可以启动。")
                .arg(currentStatus_.state));
        return false;
    }
    if (currentStatus_.axisMoving) {
        setError(errorMessage, QStringLiteral("轴当前正在运动，不能启动自动测试。"));
        return false;
    }
    if (!validateTestParameters(parameters, errorMessage)) {
        return false;
    }

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
        [client = client_, request] {
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
    bool requireStationary,
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
    if (requireStationary && currentStatus_.axisMoving) {
        setError(errorMessage, QStringLiteral("轴正在运动，请先停止。"));
        return false;
    }

    return true;
}
