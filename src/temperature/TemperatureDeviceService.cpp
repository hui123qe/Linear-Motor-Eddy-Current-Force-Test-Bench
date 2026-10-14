#include "TemperatureDeviceService.h"

#include "../globh.h"

#include "../machine/MachineStateManager.h"
#include "../motion/MotionControlService.h"

#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace {
bool reject(QString* error, const QString& message)
{
    if (error != nullptr) { *error = message; }
    return false;
}

TemperatureConfiguration globalConfiguration()
{
    TemperatureConfiguration config;
    config.backend = g_temperatureUseSimulator ? QStringLiteral("simulator") : QStringLiteral("real");
    config.deviceChannelCount = g_temperatureChannelCount;
    config.channels = {1, 2, 3, 4};
    config.host = g_temperatureHost;
    config.port = g_temperaturePort;
    config.unitId = g_temperatureUnitId;
    config.pollIntervalMs = g_temperaturePollIntervalMs;
    config.requestTimeoutMs = g_temperatureRequestTimeoutMs;
    config.staleAfterMs = g_temperatureStaleAfterMs;
    config.limitsConfigured = true;
    config.warning = g_temperatureWarning;
    config.fault = g_temperatureFault;
    config.simulatedCelsius = g_temperatureSimulatedCelsius;
    return config;
}

class TemperatureDeviceBase : public ITemperatureDevice
{
public:
    explicit TemperatureDeviceBase(QObject* parent) : ITemperatureDevice(parent) {}
protected:
    void begin(const TemperatureConfiguration& config)
    {
        config_ = config;
        session_ = QUuid::createUuid();
        sequence_ = 0;
        peak_.reset();
        emit sessionStarted(config.deviceId, session_);
    }
    void publish(QVector<TemperatureChannelSample> channels)
    {
        TemperatureSnapshot snapshot;
        snapshot.deviceId = config_.deviceId;
        snapshot.sessionId = session_;
        snapshot.sequence = ++sequence_;
        snapshot.sampledAtUtc = QDateTime::currentDateTimeUtc();
        snapshot.observedMonoMs = deviceObservationMonoMs();
        snapshot.channels = std::move(channels);
        for (const TemperatureChannelSample& sample : snapshot.channels) {
            if (sample.quality == DataQuality::Good && (!peak_ || sample.celsius > *peak_)) {
                peak_ = sample.celsius;
                peakChannel_ = sample.channel;
            }
        }
        snapshot.sessionPeakCelsius = peak_;
        snapshot.sessionPeakChannel = peakChannel_;
        emit snapshotReady(snapshot);
    }
    void invalidate(const QString& reason)
    {
        QVector<TemperatureChannelSample> channels;
        for (const int channel : config_.channels) {
            channels.append({channel, 0.0, DataQuality::Invalid, reason});
        }
        publish(channels);
    }
    TemperatureConfiguration config_;
    QUuid session_;
    quint64 sequence_ = 0;
    std::optional<double> peak_;
    int peakChannel_ = 0;
};

class SimulatedTemperatureDevice final : public TemperatureDeviceBase
{
public:
    explicit SimulatedTemperatureDevice(QObject* parent)
        : TemperatureDeviceBase(parent), timer_(new QTimer(this))
    {
        connect(timer_, &QTimer::timeout, this, [this] { sample(); });
    }
    void connectDevice(const TemperatureConfiguration& config) override
    {
        begin(config);
        startedMs_ = deviceObservationMonoMs();
        emit connectionChanged(TemperatureConnectionState::WaitingForData, QStringLiteral("虚拟设备等待数据"));
        timer_->start(config.pollIntervalMs);
        sample();
    }
    void disconnectDevice() override
    {
        timer_->stop();
        invalidate(QStringLiteral("设备已断开"));
        emit connectionChanged(TemperatureConnectionState::Disconnected, QStringLiteral("虚拟设备已断开"));
    }
private:
    void sample()
    {
        QVector<TemperatureChannelSample> channels;
        bool complete = true;
        for (const int channel : config_.channels) {
            const bool probeValid = !config_.simulatedInvalidChannels.contains(channel);
            const double celsius = config_.simulatedCelsius.at(channel - 1)
                + config_.simulatedRiseCelsiusPerSecond * static_cast<double>(deviceObservationMonoMs() - startedMs_) / 1000.0;
            const bool valid = probeValid && std::isfinite(celsius);
            complete = complete && valid;
            channels.append({channel, celsius, valid ? DataQuality::Good : DataQuality::Invalid,
                valid ? QString() : QStringLiteral("模拟探头故障")});
        }
        publish(channels);
        emit connectionChanged(complete ? TemperatureConnectionState::Ready : TemperatureConnectionState::Error,
            complete ? QStringLiteral("虚拟设备采集就绪") : QStringLiteral("虚拟设备探头故障"));
    }
    QTimer* timer_;
    qint64 startedMs_ = 0;
};

class ModbusTcpTemperatureDevice final : public TemperatureDeviceBase
{
public:
    explicit ModbusTcpTemperatureDevice(QObject* parent)
        : TemperatureDeviceBase(parent), socket_(new QTcpSocket(this)), poll_(new QTimer(this)), deadline_(new QTimer(this))
    {
        deadline_->setSingleShot(true);
        connect(socket_, &QTcpSocket::connected, this, [this] {
            deadline_->stop();
            emit connectionChanged(TemperatureConnectionState::WaitingForData, QStringLiteral("已连接，等待可信温度"));
            poll_->start(config_.pollIntervalMs);
            request();
        });
        connect(socket_, &QTcpSocket::readyRead, this, [this] { receive(); });
        connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            fail(QStringLiteral("温度网络错误：%1").arg(socket_->errorString()));
        });
        connect(socket_, &QTcpSocket::disconnected, this, [this] {
            if (!closing_) { fail(QStringLiteral("温度设备意外断开")); }
        });
        connect(poll_, &QTimer::timeout, this, [this] { request(); });
        connect(deadline_, &QTimer::timeout, this, [this] { fail(QStringLiteral("温度连接或请求超时")); });
    }
    void connectDevice(const TemperatureConfiguration& config) override
    {
        begin(config);
        closing_ = false;
        buffer_.clear();
        pending_ = false;
        emit connectionChanged(TemperatureConnectionState::Connecting, QStringLiteral("连接温度设备中"));
        deadline_->start(config.requestTimeoutMs);
        socket_->connectToHost(config.host, static_cast<quint16>(config.port));
    }
    void disconnectDevice() override
    {
        close();
        invalidate(QStringLiteral("设备已断开"));
        emit connectionChanged(TemperatureConnectionState::Disconnected, QStringLiteral("温度设备已断开"));
    }
private:
    void close()
    {
        closing_ = true;
        poll_->stop(); deadline_->stop(); pending_ = false;
        socket_->abort(); buffer_.clear();
    }
    void fail(const QString& reason)
    {
        if (closing_) { return; }
        close();
        invalidate(reason);
        emit connectionChanged(TemperatureConnectionState::Error, reason);
    }
    void request()
    {
        if (pending_ || socket_->state() != QAbstractSocket::ConnectedState) { return; }
        firstChannel_ = *std::min_element(config_.channels.cbegin(), config_.channels.cend());
        const int lastChannel = *std::max_element(config_.channels.cbegin(), config_.channels.cend());
        count_ = lastChannel - firstChannel_ + 1;
        ++transaction_;
        QByteArray request(12, '\0');
        qToBigEndian<quint16>(transaction_, request.data());
        qToBigEndian<quint16>(6, request.data() + 4);
        request[6] = static_cast<char>(config_.unitId); request[7] = 4;
        qToBigEndian<quint16>(static_cast<quint16>(firstChannel_ - 1), request.data() + 8);
        qToBigEndian<quint16>(static_cast<quint16>(count_), request.data() + 10);
        pending_ = true;
        deadline_->start(config_.requestTimeoutMs);
        if (socket_->write(request) != request.size()) { fail(QStringLiteral("温度读取请求发送失败")); }
    }
    void receive()
    {
        buffer_.append(socket_->readAll());
        if (buffer_.size() > 4096) { fail(QStringLiteral("温度响应缓冲区超过限制")); return; }
        while (buffer_.size() >= 7 && !closing_) {
            const quint16 length = qFromBigEndian<quint16>(buffer_.constData() + 4);
            if (length < 3 || length > 254 || qFromBigEndian<quint16>(buffer_.constData() + 2) != 0) {
                fail(QStringLiteral("Modbus TCP 首部无效")); return;
            }
            if (buffer_.size() < 6 + length) { return; }
            const QByteArray frame = buffer_.left(6 + length);
            buffer_.remove(0, 6 + length);
            if (!pending_ || qFromBigEndian<quint16>(frame.constData()) != transaction_) { continue; }
            if (static_cast<quint8>(frame[6]) != config_.unitId) { fail(QStringLiteral("温度响应站号不匹配")); return; }
            if (static_cast<quint8>(frame[7]) == 0x84) {
                fail(QStringLiteral("Modbus 温度读取异常，代码 %1").arg(static_cast<quint8>(frame[8]))); return;
            }
            if (frame[7] != 4 || length != 3 + count_ * 2 || static_cast<quint8>(frame[8]) != count_ * 2) {
                fail(QStringLiteral("温度响应功能码或长度不匹配")); return;
            }
            deadline_->stop(); pending_ = false;
            QVector<TemperatureChannelSample> channels;
            bool complete = true;
            for (const int channel : config_.channels) {
                const qint16 raw = qFromBigEndian<qint16>(frame.constData() + 9 + (channel - firstChannel_) * 2);
                const bool valid = raw != -5000;
                complete = complete && valid;
                channels.append({channel, valid ? static_cast<double>(raw) / 10.0 : 0.0,
                    valid ? DataQuality::Good : DataQuality::Invalid,
                    valid ? QString() : QStringLiteral("探头未接入或线路故障")});
            }
            publish(channels);
            emit connectionChanged(complete ? TemperatureConnectionState::Ready : TemperatureConnectionState::Error,
                complete ? QStringLiteral("温度采集就绪") : QStringLiteral("温度探头故障"));
        }
    }
    QTcpSocket* socket_;
    QTimer* poll_;
    QTimer* deadline_;
    QByteArray buffer_;
    quint16 transaction_ = 0;
    int firstChannel_ = 1;
    int count_ = 0;
    bool pending_ = false;
    bool closing_ = true;
};
} // namespace

class TemperatureWorker final : public QObject
{
    Q_OBJECT
public:
    void connectDevice(const TemperatureConfiguration& config)
    {
        if (device_ != nullptr) { device_->disconnectDevice(); delete device_; }
        device_ = config.backend == QStringLiteral("simulator")
            ? static_cast<ITemperatureDevice*>(new SimulatedTemperatureDevice(this))
            : static_cast<ITemperatureDevice*>(new ModbusTcpTemperatureDevice(this));
        connect(device_, &ITemperatureDevice::connectionChanged, this, &TemperatureWorker::connectionChanged);
        connect(device_, &ITemperatureDevice::sessionStarted, this, &TemperatureWorker::sessionStarted);
        connect(device_, &ITemperatureDevice::snapshotReady, this, &TemperatureWorker::snapshotReady);
        device_->connectDevice(config);
    }
    void disconnectDevice() { if (device_ != nullptr) { device_->disconnectDevice(); delete device_; device_ = nullptr; } }
signals:
    void connectionChanged(TemperatureConnectionState state, const QString& detail);
    void sessionStarted(const QString& sourceId, const QUuid& sessionId);
    void snapshotReady(const TemperatureSnapshot& snapshot);
private:
    ITemperatureDevice* device_ = nullptr;
};

TemperatureDeviceService& TemperatureDeviceService::instance()
{
    static TemperatureDeviceService service;
    return service;
}
TemperatureDeviceService::TemperatureDeviceService()
{
    qRegisterMetaType<TemperatureConfiguration>(); qRegisterMetaType<TemperatureSnapshot>();
    qRegisterMetaType<TemperatureConnectionState>();
    qRegisterMetaType<TemperatureExperimentSummary>();
}
TemperatureDeviceService::~TemperatureDeviceService() { shutdown(); }
TemperatureConfiguration TemperatureDeviceService::configuration() const { const QMutexLocker lock(&mutex_); return configuration_; }
TemperatureSnapshot TemperatureDeviceService::snapshot() const { const QMutexLocker lock(&mutex_); return snapshot_; }
TemperatureConnectionState TemperatureDeviceService::connectionState() const { const QMutexLocker lock(&mutex_); return state_; }
QString TemperatureDeviceService::connectionDetail() const { const QMutexLocker lock(&mutex_); return detail_; }

bool TemperatureDeviceService::validateConfiguration(const TemperatureConfiguration& config, QString* error)
{
    if (config.deviceId != QStringLiteral("temperature/main") || (config.backend != "simulator" && config.backend != "real")) {
        return reject(error, QStringLiteral("温度设备 ID 或虚拟/真实类型无效。"));
    }
    if (config.role != "tcp-client" || config.protocol != "modbus-tcp") {
        return reject(error, QStringLiteral("第一版仅支持 TCP Client + Modbus TCP。"));
    }
    if (config.deviceChannelCount != g_temperatureChannelCount || config.channels != QVector<int>{1, 2, 3, 4}) {
        return reject(error, QStringLiteral("温度设备固定使用 CH1～CH4。"));
    }
    if (config.host.trimmed().isEmpty() || config.port < 1 || config.port > 65535 || config.unitId < 1 || config.unitId > 247
        || config.pollIntervalMs < 100 || config.pollIntervalMs > 60000
        || config.requestTimeoutMs < 100 || config.requestTimeoutMs > 60000
        || config.staleAfterMs < config.pollIntervalMs + config.requestTimeoutMs || config.staleAfterMs > 120000) {
        return reject(error, QStringLiteral("温度网络参数无效，数据时效须覆盖轮询间隔与请求超时。"));
    }
    if (config.limitsConfigured) {
        for (const TemperatureLimitParameters& limit : {config.warning, config.fault}) {
            if (!std::isfinite(limit.triggerCelsius) || !std::isfinite(limit.recoveryCelsius)
                || limit.recoveryCelsius >= limit.triggerCelsius || limit.triggerDelayMs < 0
                || limit.triggerDelayMs > 3600000 || limit.recoveryDelayMs < 0 || limit.recoveryDelayMs > 3600000) {
                return reject(error, QStringLiteral("温度恢复线必须低于触发线，持续时间须在 0～3600000 ms。"));
            }
        }
        if (config.fault.triggerCelsius <= config.warning.triggerCelsius) {
            return reject(error, QStringLiteral("温度故障触发线必须高于预警触发线。"));
        }
    }
    if (config.simulatedCelsius.size() != g_temperatureChannelCount || !std::isfinite(config.simulatedRiseCelsiusPerSecond)) {
        return reject(error, QStringLiteral("虚拟温度需要四路有限数值与有限温升速度。"));
    }
    for (const double value : config.simulatedCelsius) {
        if (!std::isfinite(value)) { return reject(error, QStringLiteral("虚拟温度必须为有限数值。")); }
    }
    for (const int channel : config.simulatedInvalidChannels) {
        if (channel < 1 || channel > config.deviceChannelCount) { return reject(error, QStringLiteral("虚拟故障通道超出范围。")); }
    }
    return true;
}

void TemperatureDeviceService::initialize()
{
    if (initialized_) { return; }
    QString error;
    const TemperatureConfiguration config = globalConfiguration();
    const bool valid = validateConfiguration(config, &error);
    {
        const QMutexLocker lock(&mutex_);
        configuration_ = config;
        detail_ = valid ? QStringLiteral("未连接") : error;
    }
    worker_ = new TemperatureWorker;
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &TemperatureWorker::sessionStarted, this, [this](const QString& id, const QUuid& session) {
        { const QMutexLocker lock(&mutex_); snapshot_ = TemperatureSnapshot{}; snapshot_.deviceId = id; snapshot_.sessionId = session; }
        emit sessionStarted(id, session);
    }, Qt::DirectConnection);
    connect(worker_, &TemperatureWorker::snapshotReady, this, [this](const TemperatureSnapshot& snapshot) {
        TemperatureSnapshot published = snapshot;
        {
            const QMutexLocker lock(&mutex_);
            if (!experiment_.experimentName.isEmpty() && snapshot.observedMonoMs >= experimentStartedMs_) {
                bool complete = true;
                for (const TemperatureChannelSample& sample : snapshot.channels) {
                    if (sample.quality != DataQuality::Good || !std::isfinite(sample.celsius)) { complete = false; continue; }
                    if (!experiment_.maximumCelsius || sample.celsius > *experiment_.maximumCelsius) {
                        experiment_.maximumCelsius = sample.celsius; experiment_.maximumChannel = sample.channel;
                        experiment_.maximumAtUtc = snapshot.sampledAtUtc;
                    }
                }
                if (complete) { ++experiment_.goodScanCount; } else { ++experiment_.incompleteScanCount; }
                published.experimentPeakCelsius = experiment_.maximumCelsius;
                published.experimentPeakChannel = experiment_.maximumChannel;
            }
            snapshot_ = published;
        }
        emit snapshotChanged(published);
    }, Qt::DirectConnection);
    connect(worker_, &TemperatureWorker::connectionChanged, this, [this](TemperatureConnectionState state, const QString& detail) {
        { const QMutexLocker lock(&mutex_); state_ = state; detail_ = detail; }
        emit connectionChanged(state, detail);
    }, Qt::DirectConnection);
    workerThread_.start(); initialized_ = true;
    emit configurationChanged(configuration()); emit connectionChanged(connectionState(), connectionDetail());
    // Acquisition follows application lifetime, independently of experiment recording.
    if (valid) { static_cast<void>(connectDevice()); }
}

bool TemperatureDeviceService::connectDevice(QString* error)
{
    if (!initialized_) { return reject(error, QStringLiteral("温度服务尚未初始化。")); }
    const TemperatureConfiguration config = configuration();
    if (!validateConfiguration(config, error)) { return false; }
    const TemperatureConnectionState state = connectionState();
    if (state != TemperatureConnectionState::Disconnected && state != TemperatureConnectionState::Error) {
        return reject(error, QStringLiteral("温度设备已连接或正在连接。"));
    }
    { const QMutexLocker lock(&mutex_); state_ = TemperatureConnectionState::Connecting; detail_ = QStringLiteral("连接中"); }
    emit connectionChanged(TemperatureConnectionState::Connecting, QStringLiteral("连接中"));
    QMetaObject::invokeMethod(worker_, [worker = worker_, config] { worker->connectDevice(config); }, Qt::QueuedConnection);
    return true;
}

bool TemperatureDeviceService::disconnectDevice(QString* error)
{
    if (!initialized_) { return reject(error, QStringLiteral("温度服务尚未初始化。")); }
    const MachineState state = MachineStateManager::instance().state();
    if (state == MachineState::Running || state == MachineState::Resetting || MotionControlService::instance().hasActiveMotion()) {
        return reject(error, QStringLiteral("机器运行、复位或运动尚未结束，不能断开温度设备。"));
    }
    QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->disconnectDevice(); }, Qt::QueuedConnection);
    return true;
}

void TemperatureDeviceService::shutdown()
{
    if (!initialized_) { return; }
    finishExperimentRecording();
    QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->disconnectDevice(); }, Qt::BlockingQueuedConnection);
    workerThread_.quit(); workerThread_.wait(); worker_ = nullptr; initialized_ = false;
}

void TemperatureDeviceService::beginExperimentRecording(const QString& name)
{
    const QMutexLocker lock(&mutex_);
    experiment_ = {}; experiment_.experimentName = name; experiment_.backend = configuration_.backend;
    experiment_.channels = configuration_.channels; experiment_.startedAtUtc = QDateTime::currentDateTimeUtc();
    experimentStartedMs_ = deviceObservationMonoMs();
}
void TemperatureDeviceService::finishExperimentRecording()
{
    TemperatureExperimentSummary summary;
    {
        const QMutexLocker lock(&mutex_);
        if (experiment_.experimentName.isEmpty()) { return; }
        summary = experiment_; summary.finishedAtUtc = QDateTime::currentDateTimeUtc(); experiment_ = {};
    }
    emit experimentTemperatureFinalized(summary);
}

#include "TemperatureDeviceService.moc"
