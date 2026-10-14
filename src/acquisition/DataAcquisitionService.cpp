#include "DataAcquisitionService.h"

#include "../logging/AppLogger.h"
#include "../motion/AcsClient.h"

#include <QMetaObject>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace {

constexpr int kMetadataPollIntervalMilliseconds = 50;
constexpr qint64 kStoppingTimeoutMilliseconds = 5000;
constexpr qint64 kStartingTimeoutMilliseconds = 5000;

QString acquisitionStateText(AcquisitionState state)
{
    switch (state) {
    case AcquisitionState::Idle:
        return QStringLiteral("Idle");
    case AcquisitionState::Starting:
        return QStringLiteral("Starting");
    case AcquisitionState::Collecting:
        return QStringLiteral("Collecting");
    case AcquisitionState::Stopping:
        return QStringLiteral("Stopping");
    case AcquisitionState::Fault:
        return QStringLiteral("Fault");
    }

    return QStringLiteral("Unknown");
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
               "sessionBefore=%10 sessionAfter=%11 control=%1 active=%2 finished=%3 published=%4 "
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
        .arg(integerArrayText(metadata.blockSequences))
        .arg(metadata.sessionBefore)
        .arg(metadata.sessionAfter);
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

} // namespace

DataAcquisitionService& DataAcquisitionService::instance()
{
    static DataAcquisitionService service;
    return service;
}

DataAcquisitionService::DataAcquisitionService()
    : pollTimer_(new QTimer(this))
{
    pollTimer_->setInterval(kMetadataPollIntervalMilliseconds);
    pollTimer_->setSingleShot(true);
    connect(pollTimer_,
            &QTimer::timeout,
            this,
            &DataAcquisitionService::requestMetadata);
    qCDebug(logAcquisition)
        << "[采集流程][服务初始化] 元数据单次轮询定时器已配置，intervalMs="
        << kMetadataPollIntervalMilliseconds;
}

DataAcquisitionService::~DataAcquisitionService()
{
    shutdown();
}

void DataAcquisitionService::initialize(AcsClient* client)
{
    if (initialized_) {
        qCDebug(logAcquisition)
            << "[采集流程][服务初始化] 忽略重复初始化请求";
        return;
    }
    if (client == nullptr) {
        qCCritical(logAcquisition)
            << "[采集流程][服务初始化] 失败：共享 AcsClient 为空";
        failCollection(QStringLiteral("采集服务初始化失败：共享 AcsClient 为空。"));
        return;
    }

    client_ = client;
    qRegisterMetaType<AcquisitionState>("AcquisitionState");
    qRegisterMetaType<AcsCollectionMetadata>("AcsCollectionMetadata");
    qRegisterMetaType<AcquisitionBlock>("AcquisitionBlock");

    connect(client_,
            &AcsClient::connectionChanged,
            this,
            &DataAcquisitionService::onConnectionChanged);
    connect(client_,
            &AcsClient::collectionControlWritten,
            this,
            &DataAcquisitionService::onCollectionControlWritten);
    connect(client_,
            &AcsClient::collectionMetadataRead,
            this,
            &DataAcquisitionService::onMetadataRead);
    connect(client_,
            &AcsClient::collectionBlockRead,
            this,
            &DataAcquisitionService::onBlockRead);
    connect(client_,
            &AcsClient::collectionBlockRejected,
            this,
            &DataAcquisitionService::onBlockRejected);
    connect(client_,
            &AcsClient::collectionCommandFailed,
            this,
            &DataAcquisitionService::onCollectionCommandFailed);

    initialized_ = true;
    qCInfo(logAcquisition)
        << "[采集流程][服务初始化] 已绑定共享 AcsClient，初始状态="
        << acquisitionStateText(state());
}

void DataAcquisitionService::shutdown()
{
    if (!initialized_) {
        qCDebug(logAcquisition)
            << "[采集流程][服务关闭] 服务未初始化，无需关闭";
        return;
    }

    qCInfo(logAcquisition)
        << "[采集流程][服务关闭] 开始，state="
        << acquisitionStateText(state())
        << "connected=" << connected_
        << "lastConsumed=" << lastConsumedSequence_;
    invalidateRequests();
    if (connected_) {
        qCInfo(logAcquisition)
            << "[采集流程][服务关闭] 同步请求控制器关闭 DCSTART_CON";
        QMetaObject::invokeMethod(
            client_,
            [client = client_] {
                client->setCollectionEnabled(false);
            },
            Qt::BlockingQueuedConnection);
    }

    disconnect(client_, nullptr, this, nullptr);
    client_ = nullptr;
    initialized_ = false;
    connected_ = false;
    setPhase(Phase::Idle);
    qCInfo(logAcquisition) << "[采集流程][服务关闭] 完成";
}

bool DataAcquisitionService::startCollection(QString* errorMessage)
{
    qCInfo(logAcquisition)
        << "[采集流程][启动请求] 收到，state="
        << acquisitionStateText(state())
        << "initialized=" << initialized_
        << "connected=" << connected_;
    if (!initialized_) {
        qCWarning(logAcquisition)
            << "[采集流程][启动请求] 拒绝：服务尚未初始化";
        setError(errorMessage, QStringLiteral("数据采集服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        qCWarning(logAcquisition)
            << "[采集流程][启动请求] 拒绝：ACS 控制器未连接";
        setError(errorMessage, QStringLiteral("ACS 控制器未连接，不能启动采集。"));
        return false;
    }
    if (state() != AcquisitionState::Idle) {
        qCWarning(logAcquisition)
            << "[采集流程][启动请求] 拒绝：当前状态不是 Idle，state="
            << acquisitionStateText(state());
        setError(errorMessage, QStringLiteral("数据采集服务当前不是空闲状态。"));
        return false;
    }

    invalidateRequests();
    lastConsumedSequence_ = 0;
    startingTimer_.start();
    qCInfo(logAcquisition)
        << "[采集流程][启动请求] 会话本地状态已重置，lastConsumed="
        << lastConsumedSequence_;
    setPhase(Phase::CheckingIdle);
    requestMetadata();
    return true;
}

bool DataAcquisitionService::stopCollection(QString* errorMessage)
{
    qCInfo(logAcquisition)
        << "[采集流程][停止请求] 收到，state="
        << acquisitionStateText(state())
        << "initialized=" << initialized_
        << "connected=" << connected_
        << "lastConsumed=" << lastConsumedSequence_;
    if (!initialized_) {
        qCWarning(logAcquisition)
            << "[采集流程][停止请求] 拒绝：服务尚未初始化";
        setError(errorMessage, QStringLiteral("数据采集服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        qCWarning(logAcquisition)
            << "[采集流程][停止请求] 拒绝：ACS 控制器未连接";
        setError(errorMessage, QStringLiteral("ACS 控制器未连接，不能停止采集。"));
        return false;
    }
    if (state() == AcquisitionState::Idle) {
        qCInfo(logAcquisition)
            << "[采集流程][停止请求] 当前已是 Idle，直接视为成功";
        return true;
    }
    if (state() == AcquisitionState::Fault) {
        return true; // Fault cleanup already owns the stop command.
    }
    if (state() == AcquisitionState::Stopping) {
        qCInfo(logAcquisition)
            << "[采集流程][停止请求] 已在 Stopping，忽略重复请求";
        return true;
    }

    stoppingTimer_.start();
    if (phase_ == Phase::CheckingIdle) {
        finishCollection();
    } else if (phase_ == Phase::Collecting) {
        const quint64 generation = generation_;
        setPhase(Phase::Disabling);
        if (generation == generation_ && phase_ == Phase::Disabling) {
            requestCollectionControl(false);
        }
    } else {
        const quint64 generation = generation_;
        setPhase(Phase::StoppingAwaitingSession);
        if (generation == generation_ && phase_ == Phase::StoppingAwaitingSession) {
            requestMetadata();
        }
    }
    return true;
}

AcquisitionState DataAcquisitionService::state() const
{
    switch (phase_) {
    case Phase::Idle:
        return AcquisitionState::Idle;
    case Phase::CheckingIdle:
    case Phase::Enabling:
    case Phase::AwaitingSession:
        return AcquisitionState::Starting;
    case Phase::Collecting:
        return AcquisitionState::Collecting;
    case Phase::StoppingAwaitingSession:
    case Phase::Disabling:
    case Phase::Draining:
        return AcquisitionState::Stopping;
    case Phase::Fault:
        return AcquisitionState::Fault;
    }
    return AcquisitionState::Fault;
}

bool DataAcquisitionService::isReady() const
{
    return initialized_ && connected_ && state() != AcquisitionState::Fault;
}

void DataAcquisitionService::requestMetadata()
{
    if (client_ == nullptr || state() == AcquisitionState::Idle
        || state() == AcquisitionState::Fault) {
        qCDebug(logAcquisition)
            << "[采集流程][元数据请求] 已跳过，clientNull="
            << (client_ == nullptr)
            << "state=" << acquisitionStateText(state());
        return;
    }

    if (pendingRead_ != PendingRead::None || pendingControl_ != PendingControl::None) {
        qCDebug(logAcquisition) << "[采集流程][请求合并] generation=" << generation_
                               << "phase=" << phaseName(phase_)
                               << "pendingRead=" << static_cast<int>(pendingRead_)
                               << "pendingControl=" << static_cast<int>(pendingControl_);
        return;
    }
    // Requests remain in flight until the main thread handles their callbacks.
    qCDebug(logAcquisition)
        << "[采集流程][元数据请求] 入队，state="
        << acquisitionStateText(state())
        << "lastConsumed=" << lastConsumedSequence_
        << "timerWasActive=" << pollTimer_->isActive();
    pollTimer_->stop();
    pendingRead_ = PendingRead::Metadata;
    const quint64 generation = generation_;
    QMetaObject::invokeMethod(
        client_, [client = client_, generation] {
            client->readCollectionMetadata(generation);
        }, Qt::QueuedConnection);
}

void DataAcquisitionService::onConnectionChanged(bool connected,
                                                 const QString& message)
{
    qCInfo(logAcquisition).noquote()
        << "[采集流程][连接状态] connected=" << connected
        << "state=" << acquisitionStateText(state())
        << "message=" << message;
    connected_ = connected;
    if (!connected) {
        emit readinessChanged(false, message);
        if (state() != AcquisitionState::Idle) {
            failCollection(
                QStringLiteral("采集期间 ACS 连接断开：%1").arg(message));
        }
        return;
    }

    if (state() == AcquisitionState::Fault) {
        setPhase(Phase::Idle);
    }
    emit readinessChanged(isReady(), message);
}

void DataAcquisitionService::onCollectionControlWritten(quint64 generation, bool enabled)
{
    const PendingControl expected = enabled ? PendingControl::Enable
                                            : PendingControl::Disable;
    if (generation != generation_ || pendingControl_ != expected) {
        return;
    }
    pendingControl_ = PendingControl::None;
    qCInfo(logAcquisition) << "[采集流程][控制写入回执] generation=" << generation
                          << "enabled=" << enabled << "state=" << acquisitionStateText(state());
    if (phase_ == Phase::Enabling) {
        setPhase(Phase::AwaitingSession);
    } else if (phase_ == Phase::Disabling) {
        setPhase(Phase::Draining);
    }
    requestMetadata();
}

void DataAcquisitionService::onMetadataRead(
    quint64 generation, const AcsCollectionMetadata& metadata)
{
    if (generation != generation_ || pendingRead_ != PendingRead::Metadata) {
        return;
    }
    pendingRead_ = PendingRead::None;
    if (pendingControl_ != PendingControl::None) {
        return; // The control acknowledgement will request a fresh snapshot.
    }
    qCDebug(logAcquisition).noquote()
        << "[采集流程][元数据回调] state=" << acquisitionStateText(state())
        << "lastConsumed=" << lastConsumedSequence_
        << metadataText(metadata);

    if (phase_ == Phase::Idle || phase_ == Phase::Fault) {
        return;
    }
    if ((phase_ == Phase::CheckingIdle || phase_ == Phase::Enabling
         || phase_ == Phase::AwaitingSession || phase_ == Phase::StoppingAwaitingSession)
        && startingTimer_.elapsed() > kStartingTimeoutMilliseconds) {
        failCollection(QStringLiteral("采集会话初始化超时，expectedSession=%1，ack=%2/%3。")
                           .arg(controllerSession_).arg(metadata.sessionBefore).arg(metadata.sessionAfter));
        return;
    }
    switch (phase_) {
    case Phase::CheckingIdle:
        checkIdleBeforeStart(metadata);
        return;
    case Phase::AwaitingSession:
    case Phase::StoppingAwaitingSession:
        confirmSession(metadata);
        return;
    case Phase::Collecting:
    case Phase::Draining:
        consumePublishedBlocks(metadata);
        return;
    case Phase::Idle:
    case Phase::Enabling:
    case Phase::Disabling:
    case Phase::Fault:
        return;
    }
}

void DataAcquisitionService::checkIdleBeforeStart(const AcsCollectionMetadata& metadata)
{
    if (metadata.controlEnabled != 0 || metadata.activeBlock != 0
        || metadata.sessionBefore != metadata.sessionAfter) {
        failCollection(QStringLiteral("采集启动前控制器未处于稳定空闲状态。"));
        return;
    }
    // A new controller session starts from zero, not the old publication count.
    controllerSession_ = metadata.sessionAfter == std::numeric_limits<int>::max()
                             ? 1 : metadata.sessionAfter + 1;
    if (controllerSession_ <= 0) {
        failCollection(QStringLiteral("控制器采集会话回执无效。"));
        return;
    }
    setPhase(Phase::Enabling);
    requestCollectionControl(true);
}

void DataAcquisitionService::confirmSession(const AcsCollectionMetadata& metadata)
{
    if (metadata.sessionBefore != controllerSession_
        || metadata.sessionAfter != controllerSession_) {
        pollTimer_->start();
        return;
    }
    lastConsumedSequence_ = 0;
    qCInfo(logAcquisition) << "[采集流程][会话确认] generation=" << generation_
                          << "session=" << controllerSession_
                          << "published=" << metadata.publishedSequence;
    if (phase_ == Phase::StoppingAwaitingSession) {
        setPhase(Phase::Disabling);
        requestCollectionControl(false);
        return;
    }
    if (metadata.controlEnabled != 1) {
        failCollection(QStringLiteral("采集会话初始化后控制开关意外关闭。"));
        return;
    }
    const quint64 generation = generation_;
    setPhase(Phase::Collecting);
    if (generation != generation_ || phase_ != Phase::Collecting) {
        return;
    }
    emit collectionStarted();
    if (generation != generation_ || phase_ != Phase::Collecting) {
        return;
    }
    // Accept a first snapshot that already contains block 1.
    consumePublishedBlocks(metadata);
}

void DataAcquisitionService::consumePublishedBlocks(const AcsCollectionMetadata& metadata)
{
    if (metadata.sessionBefore != controllerSession_
        || metadata.sessionAfter != controllerSession_) {
        failCollection(QStringLiteral("采集中控制器会话改变，expected=%1，ack=%2/%3。")
                           .arg(controllerSession_).arg(metadata.sessionBefore).arg(metadata.sessionAfter));
        return;
    }
    // 停止后仍继续读取尾块；只有控制器已空闲且所有发布序号已消费才结束。
    if (phase_ == Phase::Draining
        && stoppingTimer_.isValid()
        && stoppingTimer_.elapsed() > kStoppingTimeoutMilliseconds) {
        qCCritical(logAcquisition)
            << "[采集流程][停止排空] 超时，elapsedMs="
            << stoppingTimer_.elapsed()
            << "lastConsumed=" << lastConsumedSequence_;
        failCollection(
            QStringLiteral("停止采集超时：DCSTART_CON=%1，DC_ACTIVE_BLOCK=%2，"
                           "最后发布序号=%3。")
                .arg(metadata.controlEnabled)
                .arg(metadata.activeBlock)
                .arg(metadata.publishedSequence));
        return;
    }

    int blockIndex = 0;
    int sequence = 0;
    QString selectionError;
    if (selectNextBlock(metadata, &blockIndex, &sequence, &selectionError)) {
        qCInfo(logAcquisition)
            << "[采集流程][块选择] 找到下一个逻辑块，block="
            << blockIndex
            << "sequence=" << sequence
            << "active=" << metadata.activeBlock
            << "published=" << metadata.publishedSequence;
        requestBlock(blockIndex, sequence);
        return;
    }
    if (!selectionError.isEmpty()) {
        qCCritical(logAcquisition).noquote()
            << "[采集流程][块选择] 失败：" << selectionError;
        failCollection(selectionError);
        return;
    }

    if (phase_ == Phase::Draining
        && metadata.controlEnabled == 0
        && metadata.activeBlock == 0
        && metadata.publishedSequence <= lastConsumedSequence_) {
        qCInfo(logAcquisition)
            << "[采集流程][停止排空] 已完成，control="
            << metadata.controlEnabled
            << "active=" << metadata.activeBlock
            << "published=" << metadata.publishedSequence
            << "lastConsumed=" << lastConsumedSequence_
            << "elapsedMs=" << stoppingTimer_.elapsed();
        finishCollection();
        return;
    }

    qCDebug(logAcquisition)
        << "[采集流程][等待新块] 当前无可读块，state="
        << acquisitionStateText(state())
        << "expected=" << (lastConsumedSequence_ + 1)
        << "published=" << metadata.publishedSequence
        << "active=" << metadata.activeBlock
        << "retryMs=" << kMetadataPollIntervalMilliseconds;
    pollTimer_->start();
}

void DataAcquisitionService::onBlockRead(quint64 generation, const AcquisitionBlock& block)
{
    if (generation != generation_ || pendingRead_ != PendingRead::Block) {
        return;
    }
    qCInfo(logAcquisition)
        << "[采集流程][块读取回调] 收到稳定数据块，state="
        << acquisitionStateText(state())
        << "block=" << block.blockIndex
        << "sequence=" << block.sequence
        << "sampleCount=" << block.sampleCount
        << "partial=" << block.partial
        << "lastConsumed=" << lastConsumedSequence_;
    if (state() != AcquisitionState::Collecting
        && state() != AcquisitionState::Stopping) {
        qCWarning(logAcquisition)
            << "[采集流程][块读取回调] 当前状态不接受数据块，已忽略，state="
            << acquisitionStateText(state());
        return;
    }
    // AcsClient 已校验单块稳定性，此处再保证跨块序号严格连续。
    if (block.sequence != lastConsumedSequence_ + 1) {
        failCollection(
            QStringLiteral("采集块序号不连续：期望 %1，收到 %2。")
                .arg(lastConsumedSequence_ + 1)
                .arg(block.sequence));
        return;
    }

    lastConsumedSequence_ = block.sequence;
    qCInfo(logAcquisition)
        << "[采集流程][块消费] 序号已推进，lastConsumed="
        << lastConsumedSequence_
        << "nextExpected=" << (lastConsumedSequence_ + 1);
    emit blockReady(block);
    if (generation != generation_) {
        return;
    }
    publishForcePositionSamples(block);
    if (generation != generation_) {
        return;
    }
    pendingRead_ = PendingRead::None;
    qCDebug(logAcquisition)
        << "[采集流程][块消费] 数据已发布，立即查询下一块";
    requestMetadata();
}

void DataAcquisitionService::onBlockRejected(quint64 generation, int blockIndex,
                                             int expectedSequence,
                                             const QString& message)
{
    if (generation != generation_ || pendingRead_ != PendingRead::Block) {
        return;
    }
    pendingRead_ = PendingRead::None;
    if (state() == AcquisitionState::Collecting
        || state() == AcquisitionState::Stopping) {
        // 拒收表示块在传输期间可能被重用，不推进消费序号，改为重新查询元数据。
        qCWarning(logAcquisition).noquote()
            << "[采集流程][块拒收] 数据已丢弃并重新查询元数据，state="
            << acquisitionStateText(state())
            << "block=" << blockIndex
            << "expected=" << expectedSequence
            << "lastConsumed=" << lastConsumedSequence_
            << "reason=" << message;
        requestMetadata();
        return;
    }

    qCDebug(logAcquisition)
        << "[采集流程][块拒收] 当前状态不再重试，state="
        << acquisitionStateText(state())
        << "block=" << blockIndex
        << "expected=" << expectedSequence;
}

void DataAcquisitionService::onCollectionCommandFailed(quint64 generation, const QString& message)
{
    if (generation != generation_) {
        return;
    }
    qCCritical(logAcquisition).noquote()
        << "[采集流程][ACS 命令失败] state=" << acquisitionStateText(state())
        << "message=" << message;
    if (state() != AcquisitionState::Fault) {
        failCollection(message);
        return;
    }

    qCDebug(logAcquisition)
        << "[采集流程][ACS 命令失败] 已处于 Fault，不重复触发故障流程";
}

void DataAcquisitionService::invalidateRequests()
{
    ++generation_;
    pollTimer_->stop();
    pendingRead_ = PendingRead::None;
    pendingControl_ = PendingControl::None;
    startingTimer_.invalidate();
}

void DataAcquisitionService::finishCollection()
{
    invalidateRequests();
    stoppingTimer_.invalidate();
    setPhase(Phase::Idle);
    emit collectionStopped();
}

const char* DataAcquisitionService::phaseName(Phase phase)
{
    switch (phase) {
    case Phase::Idle: return "Idle";
    case Phase::CheckingIdle: return "CheckingIdle";
    case Phase::Enabling: return "Enabling";
    case Phase::AwaitingSession: return "AwaitingSession";
    case Phase::Collecting: return "Collecting";
    case Phase::StoppingAwaitingSession: return "StoppingAwaitingSession";
    case Phase::Disabling: return "Disabling";
    case Phase::Draining: return "Draining";
    case Phase::Fault: return "Fault";
    }
    return "Unknown";
}

void DataAcquisitionService::setPhase(Phase phase)
{
    if (phase_ == phase) {
        return;
    }
    const AcquisitionState previousState = state();
    phase_ = phase;
    qCInfo(logAcquisition)
        << "[采集流程][状态迁移]"
        << acquisitionStateText(previousState)
        << "->" << acquisitionStateText(state())
        << "phase=" << phaseName(phase_)
        << "lastConsumed=" << lastConsumedSequence_;
    if (previousState != state()) {
        emit collectionStateChanged(state());
    }
}

void DataAcquisitionService::failCollection(const QString& message)
{
    const AcquisitionState previousState = state();
    const bool wasActive = state() == AcquisitionState::Starting
                           || state() == AcquisitionState::Collecting
                           || state() == AcquisitionState::Stopping;
    invalidateRequests();
    setPhase(Phase::Fault);
    qCCritical(logAcquisition).noquote()
        << "[采集流程][故障] previousState="
        << acquisitionStateText(previousState)
        << "wasActive=" << wasActive
        << "connected=" << connected_
        << "lastConsumed=" << lastConsumedSequence_
        << "message=" << message;
    emit readinessChanged(false, message);

    emit collectionFailed(message);

    if (wasActive && connected_ && client_ != nullptr) {
        qCWarning(logAcquisition)
            << "[采集流程][故障] 请求控制器关闭 DCSTART_CON";
        requestCollectionControl(false);
        return;
    }

    qCDebug(logAcquisition)
        << "[采集流程][故障] 无需发送采集关闭命令";
}

void DataAcquisitionService::requestCollectionControl(bool enabled)
{
    qCInfo(logAcquisition)
        << "[采集流程][控制请求] 入队，enabled=" << enabled
        << "state=" << acquisitionStateText(state());
    pendingControl_ = enabled ? PendingControl::Enable : PendingControl::Disable;
    const quint64 generation = generation_;
    const int session = controllerSession_;
    QMetaObject::invokeMethod(
        client_,
        [client = client_, enabled, generation, session] {
            client->setCollectionEnabled(enabled, generation, session);
        },
        Qt::QueuedConnection);
}

void DataAcquisitionService::requestBlock(int blockIndex, int expectedSequence)
{
    if (pendingRead_ != PendingRead::None || pendingControl_ != PendingControl::None) {
        requestMetadata();
        return;
    }
    pendingRead_ = PendingRead::Block;
    const quint64 generation = generation_;
    const int session = controllerSession_;
    qCInfo(logAcquisition)
        << "[采集流程][块读取请求] 入队，block=" << blockIndex
        << "expected=" << expectedSequence
        << "state=" << acquisitionStateText(state())
        << "lastConsumed=" << lastConsumedSequence_;
    QMetaObject::invokeMethod(
        client_,
        [client = client_, blockIndex, expectedSequence, generation, session] {
            client->readCollectionBlock(blockIndex, expectedSequence, generation, session);
        },
        Qt::QueuedConnection);
}

bool DataAcquisitionService::selectNextBlock(
    const AcsCollectionMetadata& metadata,
    int* blockIndex,
    int* sequence,
    QString* errorMessage) const
{
    qCDebug(logAcquisition)
        << "[采集流程][块选择] 开始，lastConsumed="
        << lastConsumedSequence_
        << "expected=" << (lastConsumedSequence_ + 1)
        << "published=" << metadata.publishedSequence
        << "active=" << metadata.activeBlock;
    if (metadata.publishedSequence < lastConsumedSequence_) {
        qCWarning(logAcquisition)
            << "[采集流程][块选择] 发布序号回退，lastConsumed="
            << lastConsumedSequence_
            << "published=" << metadata.publishedSequence;
        setError(
            errorMessage,
            QStringLiteral("采集发布序号从 %1 回退到 %2，常驻 Buffer 可能已重启。")
                .arg(lastConsumedSequence_)
                .arg(metadata.publishedSequence));
        return false;
    }

    // 物理块会循环覆用，所以使用全局发布序号定位下一个逻辑块。
    const int expectedSequence = lastConsumedSequence_ + 1;
    int smallestPublishedSequence = std::numeric_limits<int>::max();

    for (int index = 0; index < kAcquisitionBlockCount; ++index) {
        const int candidateSequence = metadata.blockSequences.at(index);
        qCDebug(logAcquisition)
            << "[采集流程][块选择] 检查物理块，block=" << (index + 1)
            << "candidate=" << candidateSequence
            << "isActive=" << (metadata.activeBlock == index + 1)
            << "isExpected=" << (candidateSequence == expectedSequence)
            << "isPublishedUnconsumed="
            << (candidateSequence > lastConsumedSequence_
                && candidateSequence <= metadata.publishedSequence);
        if (candidateSequence == expectedSequence) {
            *blockIndex = index + 1;
            *sequence = candidateSequence;
            qCDebug(logAcquisition)
                << "[采集流程][块选择] 精确命中，block=" << *blockIndex
                << "sequence=" << *sequence;
            return true;
        }
        if (candidateSequence > lastConsumedSequence_
            && candidateSequence <= metadata.publishedSequence) {
            smallestPublishedSequence =
                std::min(smallestPublishedSequence, candidateSequence);
            qCDebug(logAcquisition)
                << "[采集流程][块选择] 更新最早可见未消费序号，value="
                << smallestPublishedSequence;
        }
    }

    // 看得到更新的块却找不到期望序号，说明读取速度落后于控制器覆盖速度。
    if (smallestPublishedSequence != std::numeric_limits<int>::max()) {
        qCWarning(logAcquisition)
            << "[采集流程][块选择] 期望序号已不可见，expected="
            << expectedSequence
            << "smallestVisible=" << smallestPublishedSequence;
        setError(
            errorMessage,
            QStringLiteral("采集块序号 %1 已不可用，当前最早可见序号为 %2，"
                           "环形缓冲区发生覆盖。")
                .arg(expectedSequence)
                .arg(smallestPublishedSequence));
    } else {
        qCDebug(logAcquisition)
            << "[采集流程][块选择] 尚无新的已发布块，expected="
            << expectedSequence
            << "published=" << metadata.publishedSequence;
    }
    return false;
}

void DataAcquisitionService::publishForcePositionSamples(
    const AcquisitionBlock& block)
{
    qCDebug(logAcquisition)
        << "[采集流程][位移力数据发布] 开始，block=" << block.blockIndex
        << "sequence=" << block.sequence
        << "points=" << block.forceNewtons.size();
    QVector<QPointF> samples;
    samples.reserve(block.forceNewtons.size());
    for (qsizetype index = 0; index < block.forceNewtons.size(); ++index) {
        samples.append(QPointF(block.positionMeters.at(index),
                               block.forceNewtons.at(index)));
    }

    qCDebug(logAcquisition)
        << "[采集流程][位移力数据发布] 完成，sequence=" << block.sequence
        << "points=" << samples.size();
    emit forcePositionSamplesReady(samples);
}
