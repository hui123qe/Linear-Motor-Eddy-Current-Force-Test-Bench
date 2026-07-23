#include "DataAcquisitionService.h"

#include "../logging/AppLogger.h"
#include "../motion/AcsClient.h"

#include <QDateTime>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace {

constexpr int kMetadataPollIntervalMilliseconds = 50;
constexpr qint64 kStoppingTimeoutMilliseconds = 5000;

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
}

DataAcquisitionService::~DataAcquisitionService()
{
    shutdown();
}

void DataAcquisitionService::initialize(AcsClient* client)
{
    if (initialized_) {
        return;
    }
    if (client == nullptr) {
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
    qCInfo(logAcquisition) << "数据采集服务已绑定共享 AcsClient";
}

void DataAcquisitionService::shutdown()
{
    if (!initialized_) {
        return;
    }

    pollTimer_->stop();
    if (connected_) {
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
    setState(AcquisitionState::Idle);
    qCInfo(logAcquisition) << "数据采集服务已关闭";
}

bool DataAcquisitionService::startCollection(QString* errorMessage)
{
    if (!initialized_) {
        setError(errorMessage, QStringLiteral("数据采集服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接，不能启动采集。"));
        return false;
    }
    if (state_ != AcquisitionState::Idle) {
        setError(errorMessage, QStringLiteral("数据采集服务当前不是空闲状态。"));
        return false;
    }

    lastConsumedSequence_ = 0;
    sessionSampleOffset_ = 0;
    sessionStartSeconds_ = static_cast<double>(
        QDateTime::currentMSecsSinceEpoch()) / 1000.0;
    setState(AcquisitionState::Starting);
    requestMetadata();
    return true;
}

bool DataAcquisitionService::stopCollection(QString* errorMessage)
{
    if (!initialized_) {
        setError(errorMessage, QStringLiteral("数据采集服务尚未初始化。"));
        return false;
    }
    if (!connected_) {
        setError(errorMessage, QStringLiteral("ACS 控制器未连接，不能停止采集。"));
        return false;
    }
    if (state_ == AcquisitionState::Idle) {
        return true;
    }
    if (state_ == AcquisitionState::Stopping) {
        return true;
    }

    setState(AcquisitionState::Stopping);
    stoppingTimer_.start();
    pollTimer_->start();
    requestCollectionControl(false);
    return true;
}

AcquisitionState DataAcquisitionService::state() const
{
    return state_;
}

bool DataAcquisitionService::isReady() const
{
    return initialized_ && connected_ && state_ != AcquisitionState::Fault;
}

void DataAcquisitionService::requestMetadata()
{
    if (client_ == nullptr || state_ == AcquisitionState::Idle
        || state_ == AcquisitionState::Fault) {
        return;
    }

    // 单次定时器只用于无新块时退避；发出请求前先取消未触发的超时，避免重复查询。
    pollTimer_->stop();
    QMetaObject::invokeMethod(
        client_, &AcsClient::readCollectionMetadata, Qt::QueuedConnection);
}

void DataAcquisitionService::onConnectionChanged(bool connected,
                                                 const QString& message)
{
    connected_ = connected;
    if (!connected) {
        emit readinessChanged(false, message);
        if (state_ != AcquisitionState::Idle) {
            failCollection(
                QStringLiteral("采集期间 ACS 连接断开：%1").arg(message));
        }
        return;
    }

    if (state_ == AcquisitionState::Fault) {
        setState(AcquisitionState::Idle);
    }
    emit readinessChanged(isReady(), message);
}

void DataAcquisitionService::onCollectionControlWritten(bool enabled)
{
    if (enabled && state_ == AcquisitionState::Starting) {
        requestMetadata();
        return;
    }

    if (!enabled && state_ == AcquisitionState::Stopping) {
        requestMetadata();
    }
}

void DataAcquisitionService::onMetadataRead(
    const AcsCollectionMetadata& metadata)
{
    if (state_ == AcquisitionState::Starting) {
        if (metadata.controlEnabled == 1) {
            if (metadata.armed != 1) {
                pollTimer_->start();
                return;
            }

            setState(AcquisitionState::Collecting);
            emit collectionStarted();
            pollTimer_->start();
            return;
        }

        if (metadata.controlEnabled != 0 || metadata.activeBlock != 0
            || metadata.armed != 0) {
            failCollection(
                QStringLiteral("采集 Buffer 未处于空闲状态：DCSTART_CON=%1，"
                               "DCSTART=%2，DC_ACTIVE_BLOCK=%3。")
                    .arg(metadata.controlEnabled)
                    .arg(metadata.armed)
                    .arg(metadata.activeBlock));
            return;
        }

        // 启动新会话前以当前发布序号为起点，不消费上一会话留下的块。
        lastConsumedSequence_ = metadata.publishedSequence;
        requestCollectionControl(true);
        return;
    }

    if (state_ != AcquisitionState::Collecting
        && state_ != AcquisitionState::Stopping) {
        return;
    }

    // 停止后仍继续读取尾块；只有控制器已空闲且所有发布序号已消费才结束。
    if (state_ == AcquisitionState::Stopping
        && stoppingTimer_.isValid()
        && stoppingTimer_.elapsed() > kStoppingTimeoutMilliseconds) {
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
        requestBlock(blockIndex, sequence);
        return;
    }
    if (!selectionError.isEmpty()) {
        failCollection(selectionError);
        return;
    }

    if (state_ == AcquisitionState::Stopping
        && metadata.controlEnabled == 0
        && metadata.activeBlock == 0
        && metadata.publishedSequence <= lastConsumedSequence_) {
        pollTimer_->stop();
        setState(AcquisitionState::Idle);
        emit collectionStopped();
        return;
    }

    pollTimer_->start();
}

void DataAcquisitionService::onBlockRead(const AcquisitionBlock& block)
{
    if (state_ != AcquisitionState::Collecting
        && state_ != AcquisitionState::Stopping) {
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
    emit blockReady(block);
    publishForceSamples(block);
    requestMetadata();
}

void DataAcquisitionService::onBlockRejected(int blockIndex,
                                             int expectedSequence,
                                             const QString& message)
{
    Q_UNUSED(blockIndex)
    Q_UNUSED(expectedSequence)

    if (state_ == AcquisitionState::Collecting
        || state_ == AcquisitionState::Stopping) {
        // 拒收表示块在传输期间可能被重用，不推进消费序号，改为重新查询元数据。
        qCWarning(logAcquisition).noquote()
            << "采集块读取结果已丢弃，将重新读取元数据：" << message;
        requestMetadata();
    }
}

void DataAcquisitionService::onCollectionCommandFailed(const QString& message)
{
    if (state_ != AcquisitionState::Fault) {
        failCollection(message);
    }
}

void DataAcquisitionService::setState(AcquisitionState state)
{
    if (state_ == state) {
        return;
    }

    state_ = state;
    emit collectionStateChanged(state_);
}

void DataAcquisitionService::failCollection(const QString& message)
{
    const bool wasActive = state_ == AcquisitionState::Starting
                           || state_ == AcquisitionState::Collecting
                           || state_ == AcquisitionState::Stopping;
    pollTimer_->stop();
    setState(AcquisitionState::Fault);
    qCCritical(logAcquisition).noquote() << message;
    emit readinessChanged(false, message);

    emit collectionFailed(message);

    if (wasActive && connected_ && client_ != nullptr) {
        requestCollectionControl(false);
    }
}

void DataAcquisitionService::requestCollectionControl(bool enabled)
{
    QMetaObject::invokeMethod(
        client_,
        [client = client_, enabled] {
            client->setCollectionEnabled(enabled);
        },
        Qt::QueuedConnection);
}

void DataAcquisitionService::requestBlock(int blockIndex, int expectedSequence)
{
    QMetaObject::invokeMethod(
        client_,
        [client = client_, blockIndex, expectedSequence] {
            client->readCollectionBlock(blockIndex, expectedSequence);
        },
        Qt::QueuedConnection);
}

bool DataAcquisitionService::selectNextBlock(
    const AcsCollectionMetadata& metadata,
    int* blockIndex,
    int* sequence,
    QString* errorMessage) const
{
    if (metadata.publishedSequence < lastConsumedSequence_) {
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
        if (candidateSequence == expectedSequence) {
            *blockIndex = index + 1;
            *sequence = candidateSequence;
            return true;
        }
        if (candidateSequence > lastConsumedSequence_
            && candidateSequence <= metadata.publishedSequence) {
            smallestPublishedSequence =
                std::min(smallestPublishedSequence, candidateSequence);
        }
    }

    // 看得到更新的块却找不到期望序号，说明读取速度落后于控制器覆盖速度。
    if (smallestPublishedSequence != std::numeric_limits<int>::max()) {
        setError(
            errorMessage,
            QStringLiteral("采集块序号 %1 已不可用，当前最早可见序号为 %2，"
                           "环形缓冲区发生覆盖。")
                .arg(expectedSequence)
                .arg(smallestPublishedSequence));
    }
    return false;
}

void DataAcquisitionService::publishForceSamples(const AcquisitionBlock& block)
{
    QVector<QPointF> samples;
    samples.reserve(block.forceNewtons.size());
    for (qsizetype index = 0; index < block.forceNewtons.size(); ++index) {
        const double timestampSeconds =
            sessionStartSeconds_
            + static_cast<double>(sessionSampleOffset_ + index)
                  * block.samplePeriodSeconds;
        samples.append(QPointF(timestampSeconds, block.forceNewtons.at(index)));
    }

    sessionSampleOffset_ += block.forceNewtons.size();
    emit forceSamplesReady(samples);
}
