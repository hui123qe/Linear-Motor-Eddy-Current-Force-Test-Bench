#include "AcsClient.h"

#include "../logging/AppLogger.h"

#include <ACSC.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

#include <cmath>

namespace {

constexpr int kMotionConfigurationSchemaVersion = 1;
constexpr double kMetersToMillimeters = 1000.0;
constexpr int kMinimumPollIntervalMilliseconds = 10;
constexpr int kMaximumPollIntervalMilliseconds = 5000;
constexpr int kErrorBufferSize = 512;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool isInvalidHandle(HANDLE handle)
{
    return handle == ACSC_INVALID;
}

QString configurationFilePath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("motion.json"));
}

} // namespace

AcsClient::AcsClient(QObject* parent)
    : QObject(parent)
    , controllerHandle_(ACSC_INVALID)
    , pollTimer_(new QTimer(this))
{
    pollTimer_->setSingleShot(false);
    connect(pollTimer_, &QTimer::timeout, this, &AcsClient::pollStatus);
}

AcsClient::~AcsClient()
{
    closeConnection();
}

void AcsClient::connectController()
{
    if (controllerHandle_ != ACSC_INVALID) {
        return;
    }

    QString errorMessage;
    if (!loadConfiguration(&errorMessage)
        || !openConnection(&errorMessage)) {
        qCCritical(logMotion).noquote() << errorMessage;
        closeConnection();
        emit connectionChanged(false, errorMessage);
        return;
    }

    pollTimer_->setInterval(pollIntervalMilliseconds_);
    pollTimer_->start();
    qCInfo(logMotion)
        << "ACS SDK 连接成功，mode=" << connectionMode_
        << "axis=" << axis_;
    emit connectionChanged(
        true,
        connectionMode_ == QStringLiteral("simulator")
            ? QStringLiteral("ACS 模拟控制器已连接")
            : QStringLiteral("ACS 控制器已连接：%1:%2").arg(address_).arg(port_));
    pollStatus();
}

void AcsClient::disconnectController()
{
    const bool wasConnected = controllerHandle_ != ACSC_INVALID;
    closeConnection();
    if (wasConnected) {
        qCInfo(logMotion) << "ACS SDK 连接已关闭";
        emit connectionChanged(false, QStringLiteral("ACS 控制器已断开"));
    }
}

void AcsClient::start(const MotionStartRequest& request)
{
    if (controllerHandle_ == ACSC_INVALID) {
        const QString message = QStringLiteral("ACS 控制器未连接，不能启动测试。");
        qCWarning(logMotion).noquote() << message;
        emit commandFailed(message);
        return;
    }

    const bool baseValuesValid = std::isfinite(request.startPositionMeters)
                                 && std::isfinite(request.endPositionMeters)
                                 && std::isfinite(request.velocityMetersPerSecond)
                                 && request.startPositionMeters != request.endPositionMeters
                                 && request.velocityMetersPerSecond > 0.0
                                 && request.repeatCount > 0;
    const bool accelerationValuesValid =
        std::isfinite(request.accelerationMetersPerSecondSquared)
        && std::isfinite(request.decelerationMetersPerSecondSquared)
        && request.accelerationMetersPerSecondSquared > 0.0
        && request.decelerationMetersPerSecondSquared > 0.0;
    if (!baseValuesValid || !accelerationValuesValid
        || !std::isfinite(countsPerMillimeter_)
        || countsPerMillimeter_ <= 0.0) {
        const QString message =
            QStringLiteral("ACS 启动参数或 count/mm 换算系数无效。");
        qCWarning(logMotion).noquote() << message;
        emit commandFailed(message);
        return;
    }

    const double startPositionCounts = request.startPositionMeters
                                       * kMetersToMillimeters
                                       * countsPerMillimeter_;
    const double endPositionCounts = request.endPositionMeters
                                     * kMetersToMillimeters
                                     * countsPerMillimeter_;
    const double velocityCountsPerSecond = request.velocityMetersPerSecond
                                           * kMetersToMillimeters
                                           * countsPerMillimeter_;
    const double accelerationCountsPerSecondSquared =
        request.accelerationMetersPerSecondSquared
        * kMetersToMillimeters
        * countsPerMillimeter_;
    const double decelerationCountsPerSecondSquared =
        request.decelerationMetersPerSecondSquared
        * kMetersToMillimeters
        * countsPerMillimeter_;

    QString errorMessage;
    bool written = writeReal("G_START_POS", startPositionCounts, &errorMessage)
                   && writeReal("G_END_POS", endPositionCounts, &errorMessage)
                   && writeReal("G_TEST_VEL", velocityCountsPerSecond, &errorMessage)
                   && writeReal(
                       "G_TEST_ACC", accelerationCountsPerSecondSquared, &errorMessage)
                   && writeReal(
                       "G_TEST_DEC", decelerationCountsPerSecondSquared, &errorMessage)
                   && writeInteger("G_REPEAT_COUNT", request.repeatCount, &errorMessage);
    if (written) {
        written = writeInteger("G_START_REQ", 1, &errorMessage);
    }

    if (!written) {
        qCCritical(logMotion).noquote() << errorMessage;
        emit commandFailed(errorMessage);
        return;
    }

    qCInfo(logMotion) << "ACS G_START_REQ 已写入";
    emit startRequestWritten();
}

void AcsClient::stop()
{
    if (controllerHandle_ == ACSC_INVALID) {
        const QString message =
            QStringLiteral("ACS 控制器未连接，不能发送停止请求。");
        qCWarning(logMotion).noquote() << message;
        emit commandFailed(message);
        return;
    }

    QString errorMessage;
    if (!writeInteger("G_ABORT_LATCH", 1, &errorMessage)) {
        qCCritical(logMotion).noquote() << errorMessage;
        emit commandFailed(errorMessage);
        return;
    }

    qCInfo(logMotion) << "ACS G_ABORT_LATCH 已写入";
    emit stopRequestWritten();
}

void AcsClient::pollStatus()
{
    if (controllerHandle_ == ACSC_INVALID) {
        return;
    }

    AcsMotionStatus status;
    QString errorMessage;
    if (!readInteger("G_STATE", &status.state, &errorMessage)
        || !readInteger("G_ERROR_CODE", &status.errorCode, &errorMessage)
        || !readInteger("G_CURRENT_COUNT", &status.currentCount, &errorMessage)) {
        handleCommunicationFailure(errorMessage);
        return;
    }

    emit statusChanged(status);
}

bool AcsClient::loadConfiguration(QString* errorMessage)
{
    QFile file(configurationFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(
            errorMessage,
            QStringLiteral("无法读取 ACS 配置文件：%1\n%2")
                .arg(file.fileName(), file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(
            errorMessage,
            QStringLiteral("ACS 配置文件格式错误：%1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1)
        != kMotionConfigurationSchemaVersion) {
        setError(errorMessage, QStringLiteral("ACS 配置 schemaVersion 必须为 1。"));
        return false;
    }

    const QJsonObject connection = root.value(QStringLiteral("connection")).toObject();
    const QJsonObject controller = root.value(QStringLiteral("controller")).toObject();
    connectionMode_ = connection.value(QStringLiteral("mode")).toString();
    address_ = connection.value(QStringLiteral("address")).toString();
    port_ = connection.value(QStringLiteral("port")).toInt();
    axis_ = controller.value(QStringLiteral("axis")).toInt(-1);
    countsPerMillimeter_ =
        controller.value(QStringLiteral("countsPerMillimeter")).toDouble();
    pollIntervalMilliseconds_ = root.value(QStringLiteral("pollIntervalMs")).toInt();

    if ((connectionMode_ != QStringLiteral("simulator")
         && connectionMode_ != QStringLiteral("ethernetTcp"))
        || axis_ < 0
        || !std::isfinite(countsPerMillimeter_)
        || countsPerMillimeter_ <= 0.0
        || pollIntervalMilliseconds_ < kMinimumPollIntervalMilliseconds
        || pollIntervalMilliseconds_ > kMaximumPollIntervalMilliseconds) {
        setError(errorMessage, QStringLiteral("ACS 配置中的连接方式、轴号、count/mm 或轮询周期无效。"));
        return false;
    }
    if (connectionMode_ == QStringLiteral("ethernetTcp")
        && (address_.trimmed().isEmpty() || port_ <= 0 || port_ > 65535)) {
        setError(errorMessage, QStringLiteral("ACS Ethernet TCP 地址或端口无效。"));
        return false;
    }

    qCInfo(logMotion)
        << "ACS 配置加载成功：mode=" << connectionMode_
        << "axis=" << axis_
        << "countsPerMillimeter=" << countsPerMillimeter_
        << "pollIntervalMs=" << pollIntervalMilliseconds_;
    return true;
}

bool AcsClient::openConnection(QString* errorMessage)
{
    HANDLE handle = ACSC_INVALID;
    if (connectionMode_ == QStringLiteral("simulator")) {
       QByteArray address = address_.toLatin1();
        handle = acsc_OpenCommEthernetTCP(address.data(), port_);

        handle = acsc_OpenCommEthernetTCP(address.data(), port_);
    } else {
        QByteArray address = address_.toLatin1();
        handle = acsc_OpenCommEthernetTCP(address.data(), port_);
    }

    if (isInvalidHandle(handle)) {
        setError(errorMessage, sdkError(QStringLiteral("连接 ACS 控制器失败")));
        return false;
    }

    controllerHandle_ = handle;
    return true;
}

void AcsClient::closeConnection()
{
    pollTimer_->stop();
    if (controllerHandle_ == ACSC_INVALID) {
        return;
    }

    acsc_CloseComm(static_cast<HANDLE>(controllerHandle_));
    controllerHandle_ = ACSC_INVALID;
}

bool AcsClient::readInteger(const char* variable,
                            int* value,
                            QString* errorMessage) const
{
    char* variableName = const_cast<char*>(variable);
    if (acsc_ReadInteger(static_cast<HANDLE>(controllerHandle_),
                         ACSC_NONE,
                         variableName,
                         0,
                         0,
                         0,
                         0,
                         value,
                         ACSC_SYNCHRONOUS)
        != 0) {
        return true;
    }

    setError(
        errorMessage,
        sdkError(QStringLiteral("读取 ACS 变量 %1 失败")
                     .arg(QString::fromLatin1(variable))));
    return false;
}

bool AcsClient::writeInteger(const char* variable,
                             int value,
                             QString* errorMessage) const
{
    char* variableName = const_cast<char*>(variable);
    int writtenValue = value;
    if (acsc_WriteInteger(static_cast<HANDLE>(controllerHandle_),
                          ACSC_NONE,
                          variableName,
                          0,
                          0,
                          0,
                          0,
                          &writtenValue,
                          ACSC_SYNCHRONOUS)
        != 0) {
        return true;
    }

    setError(
        errorMessage,
        sdkError(QStringLiteral("写入 ACS 变量 %1 失败")
                     .arg(QString::fromLatin1(variable))));
    return false;
}

bool AcsClient::writeReal(const char* variable,
                          double value,
                          QString* errorMessage) const
{
    char* variableName = const_cast<char*>(variable);
    double writtenValue = value;
    if (acsc_WriteReal(static_cast<HANDLE>(controllerHandle_),
                       ACSC_NONE,
                       variableName,
                       0,
                       0,
                       0,
                       0,
                       &writtenValue,
                       ACSC_SYNCHRONOUS)
        != 0) {
        return true;
    }

    setError(
        errorMessage,
        sdkError(QStringLiteral("写入 ACS 变量 %1 失败")
                     .arg(QString::fromLatin1(variable))));
    return false;
}

QString AcsClient::sdkError(const QString& action) const
{
    const int errorCode = acsc_GetLastError();
    char errorBuffer[kErrorBufferSize]{};
    int received = 0;
    const int result = acsc_GetErrorString(
        static_cast<HANDLE>(controllerHandle_),
        errorCode,
        errorBuffer,
        kErrorBufferSize,
        &received);
    int errorTextLength = 0;
    if (result != 0 && received > 0) {
        errorTextLength = received < kErrorBufferSize
                              ? received
                              : kErrorBufferSize - 1;
    }
    const QString errorText = QString::fromLocal8Bit(errorBuffer, errorTextLength).trimmed();
    return errorText.isEmpty()
               ? QStringLiteral("%1（ACS 错误码 %2）").arg(action).arg(errorCode)
               : QStringLiteral("%1（ACS 错误码 %2：%3）")
                     .arg(action)
                     .arg(errorCode)
                     .arg(errorText);
}

void AcsClient::handleCommunicationFailure(const QString& message)
{
    qCCritical(logMotion).noquote() << "ACS 通信失败：" << message;
    closeConnection();
    emit connectionChanged(false, message);
}
