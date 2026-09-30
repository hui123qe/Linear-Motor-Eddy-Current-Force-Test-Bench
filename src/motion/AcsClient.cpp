#include "AcsClient.h"

#include "AcsVariableNames.h"

#include "../logging/AppLogger.h"

#include <ACSC.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <QTimer>

#include <array>
#include <cmath>

namespace {

constexpr int kMotionConfigurationSchemaVersion = 1;
constexpr double kMetersToMillimeters = 1000.0;
constexpr int kMinimumPollIntervalMilliseconds = 10;
constexpr int kMaximumPollIntervalMilliseconds = 5000;
constexpr int kSensorPollIntervalMilliseconds = 200;
constexpr int kErrorBufferSize = 512;
constexpr int kHomingBuffer = 7;
constexpr int kForceTareBuffer = 8;
constexpr std::array<int, 3> kResetBuffers = {1, 2, 3};

const std::array<const char*, kAcquisitionBlockCount> kCollectionBlockVariables = {
    "DC_Data_1",
    "DC_Data_2",
    "DC_Data_3",
    "DC_Data_4",
    "DC_Data_5"
};

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

QString integerArrayText(
    const std::array<int, kAcquisitionBlockCount>& values)
{
    QStringList items;
    items.reserve(kAcquisitionBlockCount);
    for (const int value : values) {
        items.append(QString::number(value));
    }
    return QStringLiteral("[%1]").arg(items.join(QLatin1Char(',')));
}

QString metadataText(const AcsCollectionMetadata& metadata)
{
    return QStringLiteral(
               "control=%1 active=%2 finished=%3 published=%4 "
               "finishedCount=%5 finishedPartial=%6 validCounts=%7 "
               "partialFlags=%8 blockSequences=%9")
        .arg(metadata.controlEnabled)
        .arg(metadata.activeBlock)
        .arg(metadata.finishedBlock)
        .arg(metadata.publishedSequence)
        .arg(metadata.finishedCount)
        .arg(metadata.finishedPartial ? 1 : 0)
        .arg(integerArrayText(metadata.validCounts))
        .arg(integerArrayText(metadata.partialFlags))
        .arg(integerArrayText(metadata.blockSequences));
}

} // namespace

AcsClient::AcsClient(QObject* parent)
    : QObject(parent)
    , controllerHandle_(ACSC_INVALID)
    , pollTimer_(new QTimer(this))
    , sensorPollTimer_(new QTimer(this))
{
    pollTimer_->setSingleShot(false);
    connect(pollTimer_, &QTimer::timeout, this, &AcsClient::pollStatus);
    sensorPollTimer_->setSingleShot(false);
    sensorPollTimer_->setInterval(kSensorPollIntervalMilliseconds);
    connect(sensorPollTimer_, &QTimer::timeout, this, &AcsClient::pollSensors);
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
    sensorPollTimer_->start();
    qCInfo(logMotion)
        << "ACS SDK 连接成功，mode=" << connectionMode_
        << "axis=" << axis_;
    emit connectionChanged(
        true,
        connectionMode_ == QStringLiteral("simulator")
            ? QStringLiteral("ACS 模拟控制器已连接")
            : QStringLiteral("ACS 控制器已连接：%1:%2").arg(address_).arg(port_));

    pollStatus();
    pollSensors();
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

void AcsClient::emergencyStop()
{
    if (controllerHandle_ == ACSC_INVALID) {
        const QString message =
            QStringLiteral("ACS 控制器未连接，不能执行软件急停。");
        qCCritical(logMotion).noquote() << message;
        emit emergencyStopFailed(message);
        return;
    }

    qCCritical(logMotion) << "执行软件急停：ACS Kill All";
    if (acsc_KillAll(static_cast<HANDLE>(controllerHandle_),
                     ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(QStringLiteral("ACS Kill All 软件急停失败"));
        qCCritical(logMotion).noquote() << message;
        emit emergencyStopFailed(message);
        return;
    }

    emit emergencyStopCompleted();
}

void AcsClient::tareForceSensor()
{
    if (controllerHandle_ == ACSC_INVALID) {
        const QString message =
            QStringLiteral("ACS 控制器未连接，不能执行力传感器去皮。");
        qCWarning(logMotion).noquote() << message;
        emit forceTareFailed(message);
        return;
    }

    qCInfo(logMotion)
        << "启动力传感器去皮程序，buffer=" << kForceTareBuffer;
    if (acsc_RunBuffer(static_cast<HANDLE>(controllerHandle_),
                       kForceTareBuffer,
                       nullptr,
                       ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(
            QStringLiteral("启动 ACS Buffer %1 力传感器去皮程序失败")
                .arg(kForceTareBuffer));
        qCWarning(logMotion).noquote() << message;
        emit forceTareFailed(message);
        return;
    }

    emit forceTareStarted();
}

void AcsClient::enableAxis()
{
    if (controllerHandle_ == ACSC_INVALID) {
        emit maintenanceCommandFailed(
            MaintenanceCommand::EnableAxis,
            QStringLiteral("ACS 控制器未连接，不能执行上使能。"));
        return;
    }

    qCInfo(logMotion) << "提交维修命令：上使能，axis=" << axis_;
    if (acsc_Enable(static_cast<HANDLE>(controllerHandle_),
                    axis_,
                    ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(QStringLiteral("ACS 轴上使能失败"));
        qCWarning(logMotion).noquote() << message;
        emit maintenanceCommandFailed(MaintenanceCommand::EnableAxis, message);
        return;
    }

    emit maintenanceCommandCompleted(MaintenanceCommand::EnableAxis);
}

void AcsClient::disableAxis()
{
    if (controllerHandle_ == ACSC_INVALID) {
        emit maintenanceCommandFailed(
            MaintenanceCommand::DisableAxis,
            QStringLiteral("ACS 控制器未连接，不能执行下使能。"));
        return;
    }

    qCInfo(logMotion) << "提交维修命令：下使能，axis=" << axis_;
    if (acsc_Disable(static_cast<HANDLE>(controllerHandle_),
                     axis_,
                     ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(QStringLiteral("ACS 轴下使能失败"));
        qCWarning(logMotion).noquote() << message;
        emit maintenanceCommandFailed(MaintenanceCommand::DisableAxis, message);
        return;
    }

    emit maintenanceCommandCompleted(MaintenanceCommand::DisableAxis);
}

void AcsClient::homeAxis()
{
    if (controllerHandle_ == ACSC_INVALID) {
        emit maintenanceCommandFailed(
            MaintenanceCommand::HomeAxis,
            QStringLiteral("ACS 控制器未连接，不能执行回零。"));
        return;
    }

    qCInfo(logMotion)
        << "提交维修命令：回零，buffer=" << kHomingBuffer;
    if (acsc_RunBuffer(static_cast<HANDLE>(controllerHandle_),
                       kHomingBuffer,
                       nullptr,
                       ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(
            QStringLiteral("启动 ACS Buffer %1 回零程序失败")
                .arg(kHomingBuffer));
        qCWarning(logMotion).noquote() << message;
        emit maintenanceCommandFailed(
            MaintenanceCommand::HomeAxis, message);
        return;
    }

    emit maintenanceCommandCompleted(MaintenanceCommand::HomeAxis);
}

void AcsClient::restartControlBuffers()
{
    if (controllerHandle_ == ACSC_INVALID) {
        emit controlBuffersRestartFailed(
            QStringLiteral("ACS 控制器未连接，不能执行复位。"));
        return;
    }

    // 在同一工作线程中同步完成停止、启动，期间轮询不会插入。
    const HANDLE handle = static_cast<HANDLE>(controllerHandle_);
    for (const int buffer : kResetBuffers) {
        qCInfo(logMotion) << "复位：停止 ACS Buffer" << buffer;
        if (acsc_StopBuffer(handle, buffer, ACSC_SYNCHRONOUS) == 0) {
            const QString message = sdkError(
                QStringLiteral("复位失败：停止 ACS Buffer %1 失败，部分程序可能已停止。")
                    .arg(buffer));
            qCCritical(logMotion).noquote() << message;
            emit controlBuffersRestartFailed(message);
            return;
        }
    }
    for (const int buffer : kResetBuffers) {
        qCInfo(logMotion) << "复位：启动 ACS Buffer" << buffer;
        if (acsc_RunBuffer(handle, buffer, nullptr, ACSC_SYNCHRONOUS) == 0) {
            const QString message = sdkError(
                QStringLiteral("复位失败：启动 ACS Buffer %1 失败，程序尚未全部恢复运行。")
                    .arg(buffer));
            qCCritical(logMotion).noquote() << message;
            emit controlBuffersRestartFailed(message);
            return;
        }
    }

    qCInfo(logMotion) << "复位完成：ACS Buffer 1、2、3 已重新启动";
    emit controlBuffersRestartCompleted();
}

void AcsClient::moveRelative(double distanceMillimeters,
                             double velocityMillimetersPerSecond)
{
    const double distanceControllerUnits =
        distanceMillimeters * countsPerMillimeter_;
    qCInfo(logMotion)
        << "提交维修命令：相对运动，axis=" << axis_
        << "distanceMm=" << distanceMillimeters
        << "velocityMmPerSecond=" << velocityMillimetersPerSecond;
    executePointMotion(MaintenanceCommand::RelativeMove,
                       ACSC_AMF_RELATIVE,
                       distanceControllerUnits,
                       velocityMillimetersPerSecond);
}

void AcsClient::moveAbsolute(double positionMillimeters,
                             double velocityMillimetersPerSecond)
{
    const double positionControllerUnits =
        positionMillimeters * countsPerMillimeter_;
    qCInfo(logMotion)
        << "提交维修命令：绝对运动，axis=" << axis_
        << "positionMm=" << positionMillimeters
        << "velocityMmPerSecond=" << velocityMillimetersPerSecond;
    executePointMotion(MaintenanceCommand::AbsoluteMove,
                       0,
                       positionControllerUnits,
                       velocityMillimetersPerSecond);
}

void AcsClient::startJog(double velocityMillimetersPerSecond)
{
    if (controllerHandle_ == ACSC_INVALID) {
        emit maintenanceCommandFailed(
            MaintenanceCommand::StartJog,
            QStringLiteral("ACS 控制器未连接，不能执行 JOG。"));
        return;
    }

    QString errorMessage;
    if (!configureMaintenanceMotion(
            std::abs(velocityMillimetersPerSecond), &errorMessage)) {
        qCWarning(logMotion).noquote() << errorMessage;
        emit maintenanceCommandFailed(
            MaintenanceCommand::StartJog, errorMessage);
        return;
    }

    const double velocityControllerUnits =
        velocityMillimetersPerSecond * countsPerMillimeter_;
    qCInfo(logMotion)
        << "提交维修命令：JOG，axis=" << axis_
        << "velocityMmPerSecond=" << velocityMillimetersPerSecond;
    if (acsc_Jog(static_cast<HANDLE>(controllerHandle_),
                 0,
                 axis_,
                 velocityControllerUnits,
                 ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(QStringLiteral("ACS JOG 启动失败"));
        qCWarning(logMotion).noquote() << message;
        emit maintenanceCommandFailed(MaintenanceCommand::StartJog, message);
        return;
    }

    emit maintenanceCommandCompleted(MaintenanceCommand::StartJog);
}

void AcsClient::haltAxis()
{
    if (controllerHandle_ == ACSC_INVALID) {
        emit maintenanceCommandFailed(
            MaintenanceCommand::Halt,
            QStringLiteral("ACS 控制器未连接，不能停止维修运动。"));
        return;
    }

    qCInfo(logMotion) << "提交维修命令：HALT，axis=" << axis_;
    if (acsc_Halt(static_cast<HANDLE>(controllerHandle_),
                  axis_,
                  ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(QStringLiteral("ACS HALT 失败"));
        qCWarning(logMotion).noquote() << message;
        emit maintenanceCommandFailed(MaintenanceCommand::Halt, message);
        return;
    }

    emit maintenanceCommandCompleted(MaintenanceCommand::Halt);
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
                   && writeInteger("G_REPEAT_COUNT", request.repeatCount, &errorMessage)
                   // 下位机计数会跨实验保留，启动前必须清零，避免旧值误判本轮已完成。
                   && writeInteger("G_CURRENT_COUNT", 0, &errorMessage);
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

void AcsClient::setCollectionEnabled(bool enabled)
{
    qCInfo(logAcquisition)
        << "[采集流程][ACS 控制写入] 开始，DCSTART_CON="
        << (enabled ? 1 : 0);
    if (controllerHandle_ == ACSC_INVALID) {
        qCCritical(logAcquisition)
            << "[采集流程][ACS 控制写入] 失败：控制器未连接";
        emit collectionCommandFailed(
            QStringLiteral("ACS 控制器未连接，不能更改采集状态。"));
        return;
    }

    QString errorMessage;
    if (!writeInteger("DCSTART_CON", enabled ? 1 : 0, &errorMessage)) {
        qCCritical(logAcquisition).noquote()
            << "[采集流程][ACS 控制写入] SDK 写入失败，message="
            << errorMessage;
        emit collectionCommandFailed(errorMessage);
        return;
    }

    qCInfo(logAcquisition)
        << "[采集流程][ACS 控制写入] 成功并发送回执，DCSTART_CON="
        << (enabled ? 1 : 0);
    emit collectionControlWritten(enabled);
}

void AcsClient::readCollectionMetadata()
{
    QElapsedTimer readTimer;
    readTimer.start();
    qCDebug(logAcquisition)
        << "[采集流程][ACS 元数据读取] 开始：依次读取 6 个标量和 3 个块数组";
    if (controllerHandle_ == ACSC_INVALID) {
        qCCritical(logAcquisition)
            << "[采集流程][ACS 元数据读取] 失败：控制器未连接";
        emit collectionCommandFailed(
            QStringLiteral("ACS 控制器未连接，不能读取采集元数据。"));
        return;
    }

    AcsCollectionMetadata metadata;
    int finishedPartial = 0;
    QString errorMessage;
    const bool readSucceeded =
        readInteger("DCSTART_CON", &metadata.controlEnabled, &errorMessage)
        && readInteger("DC_ACTIVE_BLOCK", &metadata.activeBlock, &errorMessage)
        && readInteger("DC_FINISHED_BLOCK", &metadata.finishedBlock, &errorMessage)
        && readInteger(
            "DC_BLOCK_SEQUENCE", &metadata.publishedSequence, &errorMessage)
        && readInteger("DC_FINISHED_COUNT", &metadata.finishedCount, &errorMessage)
        && readInteger("DC_FINISHED_PARTIAL", &finishedPartial, &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_VALID_COUNT",
            0,
            kAcquisitionBlockCount - 1,
            metadata.validCounts.data(),
            &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_PARTIAL_MAP",
            0,
            kAcquisitionBlockCount - 1,
            metadata.partialFlags.data(),
            &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_SEQ_MAP",
            0,
            kAcquisitionBlockCount - 1,
            metadata.blockSequences.data(),
            &errorMessage);
    if (!readSucceeded) {
        qCCritical(logAcquisition).noquote()
            << "[采集流程][ACS 元数据读取] SDK 读取失败，elapsedMs="
            << readTimer.elapsed()
            << "message=" << errorMessage;
        emit collectionCommandFailed(errorMessage);
        return;
    }

    metadata.finishedPartial = finishedPartial != 0;
    qCDebug(logAcquisition).noquote()
        << "[采集流程][ACS 元数据读取] 完成并发送回调，elapsedMs="
        << readTimer.elapsed()
        << metadataText(metadata);
    emit collectionMetadataRead(metadata);
}

void AcsClient::readCollectionBlock(int blockIndex, int expectedSequence)
{
    QElapsedTimer readTimer;
    readTimer.start();
    qCInfo(logAcquisition)
        << "[采集流程][ACS 块读取] 开始，block=" << blockIndex
        << "expected=" << expectedSequence;
    if (controllerHandle_ == ACSC_INVALID) {
        qCCritical(logAcquisition)
            << "[采集流程][ACS 块读取] 失败：控制器未连接，block="
            << blockIndex
            << "expected=" << expectedSequence;
        emit collectionCommandFailed(
            QStringLiteral("ACS 控制器未连接，不能读取采集块。"));
        return;
    }
    if (blockIndex < 1 || blockIndex > kAcquisitionBlockCount
        || expectedSequence <= 0) {
        qCCritical(logAcquisition)
            << "[采集流程][ACS 块读取] 参数无效，block=" << blockIndex
            << "expected=" << expectedSequence
            << "blockCount=" << kAcquisitionBlockCount;
        emit collectionCommandFailed(
            QStringLiteral("采集块读取参数无效：block=%1，sequence=%2。")
                .arg(blockIndex)
                .arg(expectedSequence));
        return;
    }

    const int metadataIndex = blockIndex - 1;
    // 先读取块所属序号和有效点数，确认控制器此刻没有写该块。
    qCDebug(logAcquisition)
        << "[采集流程][ACS 块读取] 开始读取前快照，block=" << blockIndex
        << "metadataIndex=" << metadataIndex;
    int activeBlockBefore = 0;
    int sequenceBefore = 0;
    int validCountBefore = 0;
    int partialBefore = 0;
    QString errorMessage;
    const bool metadataRead =
        readInteger("DC_ACTIVE_BLOCK", &activeBlockBefore, &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_SEQ_MAP",
            metadataIndex,
            metadataIndex,
            &sequenceBefore,
            &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_VALID_COUNT",
            metadataIndex,
            metadataIndex,
            &validCountBefore,
            &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_PARTIAL_MAP",
            metadataIndex,
            metadataIndex,
            &partialBefore,
            &errorMessage);
    if (!metadataRead) {
        qCCritical(logAcquisition).noquote()
            << "[采集流程][ACS 块读取] 读取前快照失败，block="
            << blockIndex
            << "expected=" << expectedSequence
            << "elapsedMs=" << readTimer.elapsed()
            << "message=" << errorMessage;
        emit collectionCommandFailed(errorMessage);
        return;
    }

    qCDebug(logAcquisition)
        << "[采集流程][ACS 块读取] 读取前快照完成，block=" << blockIndex
        << "expected=" << expectedSequence
        << "activeBefore=" << activeBlockBefore
        << "sequenceBefore=" << sequenceBefore
        << "validCountBefore=" << validCountBefore
        << "partialBefore=" << partialBefore
        << "elapsedMs=" << readTimer.elapsed();
    if (activeBlockBefore == blockIndex || sequenceBefore != expectedSequence
        || validCountBefore <= 0
        || validCountBefore > kAcquisitionBlockCapacity) {
        qCWarning(logAcquisition)
            << "[采集流程][ACS 块读取] 读取前校验拒收，block="
            << blockIndex
            << "expected=" << expectedSequence
            << "activeConflict=" << (activeBlockBefore == blockIndex)
            << "sequenceMismatch=" << (sequenceBefore != expectedSequence)
            << "countInvalid="
            << (validCountBefore <= 0
                || validCountBefore > kAcquisitionBlockCapacity)
            << "activeBefore=" << activeBlockBefore
            << "sequenceBefore=" << sequenceBefore
            << "validCountBefore=" << validCountBefore;
        emit collectionBlockRejected(
            blockIndex,
            expectedSequence,
            QStringLiteral("读取前校验失败：active=%1，sequence=%2，count=%3。")
                .arg(activeBlockBefore)
                .arg(sequenceBefore)
                .arg(validCountBefore));
        return;
    }

    // ACS 一次返回 6 个通道的连续矩阵，避免分通道通信导致时间窗不一致。
    QVector<double> matrixValues(
        kAcquisitionChannelCount * validCountBefore);
    qCInfo(logAcquisition)
        << "[采集流程][ACS 块读取] 开始矩阵传输，block=" << blockIndex
        << "sequence=" << sequenceBefore
        << "variable=" << kCollectionBlockVariables.at(metadataIndex)
        << "channels=" << kAcquisitionChannelCount
        << "samplesPerChannel=" << validCountBefore
        << "totalValues=" << matrixValues.size();
    if (!readRealMatrix(
            kCollectionBlockVariables.at(metadataIndex),
            0,
            kAcquisitionChannelCount - 1,
            0,
            validCountBefore - 1,
            matrixValues.data(),
            &errorMessage)) {
        qCCritical(logAcquisition).noquote()
            << "[采集流程][ACS 块读取] 矩阵传输失败，block="
            << blockIndex
            << "sequence=" << sequenceBefore
            << "elapsedMs=" << readTimer.elapsed()
            << "message=" << errorMessage;
        emit collectionCommandFailed(errorMessage);
        return;
    }
    qCInfo(logAcquisition)
        << "[采集流程][ACS 块读取] 矩阵传输完成，block=" << blockIndex
        << "sequence=" << sequenceBefore
        << "totalValues=" << matrixValues.size()
        << "elapsedMs=" << readTimer.elapsed();

    // 数据传输完成后重读元数据，确认读取期间该环形块没有被控制器复用。
    qCDebug(logAcquisition)
        << "[采集流程][ACS 块读取] 开始读取后快照，block=" << blockIndex
        << "sequenceBefore=" << sequenceBefore;
    int activeBlockAfter = 0;
    int sequenceAfter = 0;
    int validCountAfter = 0;
    const bool validationRead =
        readInteger("DC_ACTIVE_BLOCK", &activeBlockAfter, &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_SEQ_MAP",
            metadataIndex,
            metadataIndex,
            &sequenceAfter,
            &errorMessage)
        && readIntegerArray(
            "DC_BLOCK_VALID_COUNT",
            metadataIndex,
            metadataIndex,
            &validCountAfter,
            &errorMessage);
    if (!validationRead) {
        qCCritical(logAcquisition).noquote()
            << "[采集流程][ACS 块读取] 读取后快照失败，block="
            << blockIndex
            << "expected=" << expectedSequence
            << "elapsedMs=" << readTimer.elapsed()
            << "message=" << errorMessage;
        emit collectionCommandFailed(errorMessage);
        return;
    }
    qCDebug(logAcquisition)
        << "[采集流程][ACS 块读取] 读取后快照完成，block=" << blockIndex
        << "expected=" << expectedSequence
        << "activeAfter=" << activeBlockAfter
        << "sequenceAfter=" << sequenceAfter
        << "validCountAfter=" << validCountAfter
        << "elapsedMs=" << readTimer.elapsed();
    if (activeBlockAfter == blockIndex || sequenceAfter != sequenceBefore
        || validCountAfter != validCountBefore) {
        qCWarning(logAcquisition)
            << "[采集流程][ACS 块读取] 读取后校验拒收，block="
            << blockIndex
            << "expected=" << expectedSequence
            << "activeConflict=" << (activeBlockAfter == blockIndex)
            << "sequenceChanged=" << (sequenceAfter != sequenceBefore)
            << "countChanged=" << (validCountAfter != validCountBefore)
            << "activeAfter=" << activeBlockAfter
            << "sequenceBefore=" << sequenceBefore
            << "sequenceAfter=" << sequenceAfter
            << "validCountBefore=" << validCountBefore
            << "validCountAfter=" << validCountAfter;
        emit collectionBlockRejected(
            blockIndex,
            expectedSequence,
            QStringLiteral("读取后校验失败：active=%1，sequence=%2，count=%3。")
                .arg(activeBlockAfter)
                .arg(sequenceAfter)
                .arg(validCountAfter));
        return;
    }

    AcquisitionBlock block;
    block.sequence = sequenceBefore;
    block.blockIndex = blockIndex;
    block.sampleCount = validCountBefore;
    block.partial = partialBefore != 0;
    block.accelerationMetersPerSecondSquared.resize(validCountBefore);
    block.velocityMetersPerSecond.resize(validCountBefore);
    block.motorCurrent.resize(validCountBefore);
    block.motorTemperature.resize(validCountBefore);
    block.forceNewtons.resize(validCountBefore);
    block.positionMeters.resize(validCountBefore);

    // ACS 矩阵按通道连续存放：[channel][sample]。
    qCDebug(logAcquisition)
        << "[采集流程][ACS 块读取] 开始矩阵通道拆分和单位换算，block="
        << blockIndex
        << "sequence=" << sequenceBefore
        << "sampleCount=" << validCountBefore;
    const double countsToMeters = 1.0 / (countsPerMillimeter_ * kMetersToMillimeters);
    for (int sample = 0; sample < validCountBefore; ++sample) {
        block.accelerationMetersPerSecondSquared[sample] =
            matrixValues.at(sample) * countsToMeters;
        block.velocityMetersPerSecond[sample] =
            matrixValues.at(validCountBefore + sample) * countsToMeters;
        block.motorCurrent[sample] =
            matrixValues.at(validCountBefore * 2 + sample);
        block.motorTemperature[sample] =
            matrixValues.at(validCountBefore * 3 + sample);
        block.forceNewtons[sample] =
            matrixValues.at(validCountBefore * 4 + sample);
        block.positionMeters[sample] =
            matrixValues.at(validCountBefore * 5 + sample) * countsToMeters;
    }

    qCInfo(logAcquisition)
        << "[采集流程][ACS 块读取] 校验与转换完成，发送数据块回调，block="
        << block.blockIndex
        << "sequence=" << block.sequence
        << "sampleCount=" << block.sampleCount
        << "partial=" << block.partial
        << "elapsedMs=" << readTimer.elapsed();
    emit collectionBlockRead(block);
}

void AcsClient::pollStatus()
{
    if (controllerHandle_ == ACSC_INVALID) {
        return;
    }

    AcsMotionStatus status;
    int homeDone = 0;
    int homeRunning = 0;
    QString errorMessage;
    if (!readInteger("G_STATE", &status.state, &errorMessage)
        || !readInteger("G_ERROR_CODE", &status.errorCode, &errorMessage)
        || !readInteger("G_CURRENT_COUNT", &status.currentCount, &errorMessage)
        || !readInteger(AcsVariableNames::homeDone,
                        &homeDone,
                        &errorMessage)
        || !readInteger(AcsVariableNames::homeRunning,
                        &homeRunning,
                        &errorMessage)) {
        handleCommunicationFailure(errorMessage);
        return;
    }
    status.homeDone = homeDone == 1;
    status.homeRunning = homeRunning == 1;

    int motorState = 0;
    double feedbackPosition = 0.0;
    double feedbackVelocity = 0.0;
    if (acsc_GetMotorState(static_cast<HANDLE>(controllerHandle_),
                           axis_,
                           &motorState,
                           ACSC_SYNCHRONOUS)
            == 0
        || acsc_GetFPosition(static_cast<HANDLE>(controllerHandle_),
                             axis_,
                             &feedbackPosition,
                             ACSC_SYNCHRONOUS)
               == 0
        || acsc_GetFVelocity(static_cast<HANDLE>(controllerHandle_),
                             axis_,
                             &feedbackVelocity,
                             ACSC_SYNCHRONOUS)
               == 0) {
        handleCommunicationFailure(
            sdkError(QStringLiteral("读取 ACS 轴状态失败")));
        return;
    }

    status.axisEnabled = (motorState & ACSC_MST_ENABLE) != 0;
    status.feedbackPositionMillimeters =
        feedbackPosition / countsPerMillimeter_;
    status.feedbackVelocityMillimetersPerSecond =
        feedbackVelocity / countsPerMillimeter_;

    emit statusChanged(status);
}

void AcsClient::pollSensors()
{
    if (controllerHandle_ == ACSC_INVALID) {
        return;
    }

    AcsSensorReadings readings;
    QString errorMessage;
    for (std::size_t index = 0;
         index < AcsVariableNames::pressureValues.size();
         ++index) {
        if (!readReal(AcsVariableNames::pressureValues.at(index),
                      &readings.pressureValues.at(index),
                      &errorMessage)) {
            qCWarning(logMotion).noquote() << errorMessage;
            return;
        }
    }
    if (!readReal(AcsVariableNames::forceValue,
                  &readings.forceValue,
                  &errorMessage)) {
        qCWarning(logMotion).noquote() << errorMessage;
        return;
    }

    emit sensorReadingsChanged(readings);
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
    sensorPollTimer_->stop();
    if (controllerHandle_ == ACSC_INVALID) {
        return;
    }

    acsc_CloseComm(static_cast<HANDLE>(controllerHandle_));
    controllerHandle_ = ACSC_INVALID;
}

bool AcsClient::configureMaintenanceMotion(
    double velocityMillimetersPerSecond,
    QString* errorMessage)
{
    if (controllerHandle_ == ACSC_INVALID) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接。"));
        return false;
    }
    if (!std::isfinite(velocityMillimetersPerSecond)
        || velocityMillimetersPerSecond <= 0.0
        || !std::isfinite(countsPerMillimeter_)
        || countsPerMillimeter_ <= 0.0) {
        setError(errorMessage, QStringLiteral("维修运动速度或单位换算参数无效。"));
        return false;
    }

    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;
    if (!readReal(AcsVariableNames::positioningAcceleration,
                  &acceleration,
                  errorMessage)
        || !readReal(AcsVariableNames::positioningDeceleration,
                     &deceleration,
                     errorMessage)
        || !readReal(AcsVariableNames::positioningJerk,
                     &jerk,
                     errorMessage)) {
        return false;
    }
    if (!std::isfinite(acceleration) || acceleration <= 0.0
        || !std::isfinite(deceleration) || deceleration <= 0.0
        || !std::isfinite(jerk) || jerk <= 0.0) {
        setError(
            errorMessage,
            QStringLiteral("ACS 维修运动加速度、减速度或 Jerk 参数无效。"));
        return false;
    }

    const double velocityControllerUnits =
        velocityMillimetersPerSecond * countsPerMillimeter_;
    if (acsc_SetVelocity(static_cast<HANDLE>(controllerHandle_),
                         axis_,
                         velocityControllerUnits,
                         ACSC_SYNCHRONOUS)
            == 0
        || acsc_SetAcceleration(static_cast<HANDLE>(controllerHandle_),
                                axis_,
                                acceleration,
                                ACSC_SYNCHRONOUS)
               == 0
        || acsc_SetDeceleration(static_cast<HANDLE>(controllerHandle_),
                                axis_,
                                deceleration,
                                ACSC_SYNCHRONOUS)
               == 0
        || acsc_SetJerk(static_cast<HANDLE>(controllerHandle_),
                        axis_,
                        jerk,
                        ACSC_SYNCHRONOUS)
               == 0) {
        setError(
            errorMessage,
            sdkError(QStringLiteral("设置 ACS 维修运动参数失败")));
        return false;
    }

    return true;
}

void AcsClient::executePointMotion(
    MaintenanceCommand command,
    int flags,
    double pointControllerUnits,
    double velocityMillimetersPerSecond)
{
    if (controllerHandle_ == ACSC_INVALID) {
        emit maintenanceCommandFailed(
            command, QStringLiteral("ACS 控制器未连接，不能执行 PTP 运动。"));
        return;
    }
    if (!std::isfinite(pointControllerUnits)) {
        emit maintenanceCommandFailed(
            command, QStringLiteral("维修运动目标位置无效。"));
        return;
    }

    QString errorMessage;
    if (!configureMaintenanceMotion(
            velocityMillimetersPerSecond, &errorMessage)) {
        qCWarning(logMotion).noquote() << errorMessage;
        emit maintenanceCommandFailed(command, errorMessage);
        return;
    }
    if (acsc_ToPoint(static_cast<HANDLE>(controllerHandle_),
                     flags,
                     axis_,
                     pointControllerUnits,
                     ACSC_SYNCHRONOUS)
        == 0) {
        const QString message = sdkError(QStringLiteral("ACS PTP 运动启动失败"));
        qCWarning(logMotion).noquote() << message;
        emit maintenanceCommandFailed(command, message);
        return;
    }

    emit maintenanceCommandCompleted(command);
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

bool AcsClient::readReal(const char* variable,
                         double* value,
                         QString* errorMessage) const
{
    char* variableName = const_cast<char*>(variable);
    if (acsc_ReadReal(static_cast<HANDLE>(controllerHandle_),
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

bool AcsClient::readIntegerArray(const char* variable,
                                 int firstIndex,
                                 int lastIndex,
                                 int* values,
                                 QString* errorMessage) const
{
    char* variableName = const_cast<char*>(variable);
    if (acsc_ReadInteger(static_cast<HANDLE>(controllerHandle_),
                         ACSC_NONE,
                         variableName,
                         firstIndex,
                         lastIndex,
                         ACSC_NONE,
                         ACSC_NONE,
                         values,
                         ACSC_SYNCHRONOUS)
        != 0) {
        return true;
    }

    setError(
        errorMessage,
        sdkError(QStringLiteral("读取 ACS 数组 %1[%2..%3] 失败")
                     .arg(QString::fromLatin1(variable))
                     .arg(firstIndex)
                     .arg(lastIndex)));
    return false;
}

bool AcsClient::readRealMatrix(const char* variable,
                               int firstRow,
                               int lastRow,
                               int firstColumn,
                               int lastColumn,
                               double* values,
                               QString* errorMessage) const
{
    char* variableName = const_cast<char*>(variable);
    if (acsc_ReadReal(static_cast<HANDLE>(controllerHandle_),
                      ACSC_NONE,
                      variableName,
                      firstRow,
                      lastRow,
                      firstColumn,
                      lastColumn,
                      values,
                      ACSC_SYNCHRONOUS)
        != 0) {
        return true;
    }

    setError(
        errorMessage,
        sdkError(QStringLiteral("读取 ACS 矩阵 %1[%2..%3][%4..%5] 失败")
                     .arg(QString::fromLatin1(variable))
                     .arg(firstRow)
                     .arg(lastRow)
                     .arg(firstColumn)
                     .arg(lastColumn)));
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
