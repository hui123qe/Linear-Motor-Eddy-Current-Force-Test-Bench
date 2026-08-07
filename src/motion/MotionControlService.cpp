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

    client_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, client_, &QObject::deleteLater);
    connect(client_,
            &AcsClient::connectionChanged,
            this,
            [this](bool connected, const QString& message) {
                connected_ = connected;
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
                if (status.state != currentStatus_.state
                    || status.errorCode != currentStatus_.errorCode
                    || status.currentCount != currentStatus_.currentCount) {
                    qCInfo(logMotion)
                        << "ACS 状态变化：state=" << status.state
                        << "errorCode=" << status.errorCode
                        << "currentCount=" << status.currentCount;
                    currentStatus_ = status;
                    emit motionStatusChanged(status);
                }
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

void MotionControlService::disconnectController()
{
    if (!workerThread_.isRunning()) {
        return;
    }

    qCInfo(logMotion) << "请求断开 ACS 控制器";
    QMetaObject::invokeMethod(
        client_, &AcsClient::disconnectController, Qt::QueuedConnection);
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
