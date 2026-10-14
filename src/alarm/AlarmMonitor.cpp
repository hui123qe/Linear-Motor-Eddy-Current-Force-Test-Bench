#include "AlarmMonitor.h"

#include <QSet>
#include <cmath>
#include <limits>

namespace {
struct ThresholdRule
{
    TemperatureLimitParameters parameters;
    bool active = false;
    qint64 pendingTrigger = -1;
    qint64 pendingRecovery = -1;
    qint64 lastSample = 0;
    ConditionState evaluate(std::optional<double> maximum, bool complete, qint64 now, int maximumGap)
    {
        if (lastSample != 0 && now - lastSample > maximumGap) { pendingTrigger = -1; pendingRecovery = -1; }
        lastSample = now;
        if (!maximum) { pendingTrigger = -1; pendingRecovery = -1; return ConditionState::Unknown; }
        if (!active) {
            if (*maximum >= parameters.triggerCelsius) {
                if (pendingTrigger < 0) { pendingTrigger = now; }
                if (now - pendingTrigger >= parameters.triggerDelayMs) { active = true; pendingTrigger = -1; }
                return active ? ConditionState::Abnormal : (complete ? ConditionState::Normal : ConditionState::Unknown);
            }
            pendingTrigger = -1;
            return complete ? ConditionState::Normal : ConditionState::Unknown;
        }
        if (*maximum >= parameters.triggerCelsius) { pendingRecovery = -1; return ConditionState::Abnormal; }
        if (!complete) { pendingRecovery = -1; return ConditionState::Unknown; }
        if (*maximum <= parameters.recoveryCelsius) {
            if (pendingRecovery < 0) { pendingRecovery = now; }
            if (now - pendingRecovery >= parameters.recoveryDelayMs) { active = false; pendingRecovery = -1; }
        } else { pendingRecovery = -1; }
        return active ? ConditionState::Abnormal : ConditionState::Normal;
    }
    void invalidate() { pendingTrigger = -1; pendingRecovery = -1; lastSample = 0; }
};

struct SourceState
{
    QUuid session;
    QMap<PointId, quint64> sequences;
};

struct TemperatureReading
{
    std::optional<double> maximum;
    int maximumChannel = 0;
    bool complete = false;
    QStringList missingChannels;
};

TemperatureReading inspectTemperature(const DeviceUpdate& update, const PointValue& point,
                                      const TemperatureConfiguration& config, qint64 now)
{
    const bool correctType = point.value.metaType() == QMetaType::fromType<TemperatureSnapshot>();
    const TemperatureSnapshot snapshot = correctType ? point.value.value<TemperatureSnapshot>() : TemperatureSnapshot{};
    const bool trustedSnapshot = correctType && snapshot.sessionId == update.sessionId
        && snapshot.deviceId == update.sourceId && snapshot.sequence == point.sequence
        && snapshot.observedMonoMs == point.observedMonoMs && point.quality == DataQuality::Good
        && now - point.observedMonoMs <= config.staleAfterMs;

    TemperatureReading reading;
    reading.complete = trustedSnapshot;
    QSet<int> received;
    for (const TemperatureChannelSample& channel : snapshot.channels) {
        if (!config.channels.contains(channel.channel) || received.contains(channel.channel)) {
            reading.complete = false;
            continue;
        }
        received.insert(channel.channel);
        if (channel.quality != DataQuality::Good || !std::isfinite(channel.celsius)) {
            reading.complete = false;
            reading.missingChannels.append(QString::number(channel.channel));
            continue;
        }
        if (!reading.maximum || channel.celsius > *reading.maximum) {
            reading.maximum = channel.celsius;
            reading.maximumChannel = channel.channel;
        }
    }
    for (const int channel : config.channels) {
        if (!received.contains(channel)) {
            reading.complete = false;
            reading.missingChannels.append(QString::number(channel));
        }
    }
    if (!trustedSnapshot) { reading.maximum.reset(); }
    return reading;
}
} // namespace

struct AlarmMonitor::State
{
    TemperatureConfiguration configuration;
    ThresholdRule warning;
    ThresholdRule fault;
    QMap<QString, SourceState> sources;
    qint64 temperatureUntil = 0;
    qint64 safetyUntil = 0;
};

AlarmMonitor::AlarmMonitor(QObject* parent) : QObject(parent), state_(std::make_unique<State>()) {}
AlarmMonitor::~AlarmMonitor() = default;

void AlarmMonitor::configure(const TemperatureConfiguration& configuration)
{
    state_->configuration = configuration;
    state_->warning.parameters = configuration.warning;
    state_->fault.parameters = configuration.fault;
    state_->warning.invalidate(); state_->fault.invalidate();
    state_->temperatureUntil = 0;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    emit conditionEvaluated({"TEMP_WARNING", configuration.deviceId, ConditionState::Unknown, now, {}});
    emit conditionEvaluated({"TEMP_FAULT", configuration.deviceId, ConditionState::Unknown, now, {}});
}
void AlarmMonitor::setSourceSession(const QString& sourceId, const QUuid& sessionId)
{
    state_->sources[sourceId] = {sessionId, {}};
    if (sourceId == state_->configuration.deviceId) {
        state_->warning.invalidate(); state_->fault.invalidate();
        state_->temperatureUntil = 0;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        emit conditionEvaluated({"TEMP_WARNING", sourceId, ConditionState::Unknown, now, {}});
        emit conditionEvaluated({"TEMP_FAULT", sourceId, ConditionState::Unknown, now, {}});
    } else if (sourceId == QStringLiteral("safety/main")) {
        state_->safetyUntil = 0;
        emit conditionEvaluated({"SAFETY_CIRCUIT_FAULT", sourceId, ConditionState::Unknown, QDateTime::currentDateTimeUtc(), {}});
    }
}

void AlarmMonitor::update(const DeviceUpdate& update)
{
    if (!state_->sources.contains(update.sourceId) || update.sessionId.isNull()) { return; }
    SourceState& source = state_->sources[update.sourceId];
    if (source.session != update.sessionId) { return; }
    const qint64 now = deviceObservationMonoMs();
    for (auto it = update.points.cbegin(); it != update.points.cend(); ++it) {
        const PointValue& point = it.value();
        if (point.sequence == 0 || point.sequence <= source.sequences.value(it.key())
            || point.observedMonoMs <= 0 || point.observedMonoMs > now) { continue; }
        source.sequences[it.key()] = point.sequence;
        if (it.key() == PointId::TemperatureBatch && update.sourceId == state_->configuration.deviceId) {
            updateTemperature(update, point, now);
        } else if (it.key() == PointId::SafetyCircuit && update.sourceId == QStringLiteral("safety/main")) {
            updateSafetyCircuit(update, point, now);
        }
    }
}

void AlarmMonitor::updateTemperature(const DeviceUpdate& update, const PointValue& point, qint64 now)
{
    const TemperatureConfiguration& config = state_->configuration;
    const TemperatureReading reading = inspectTemperature(update, point, config, now);
    state_->temperatureUntil = point.observedMonoMs + config.staleAfterMs;

    QVariantMap details{{"maximumChannel", reading.maximumChannel}, {"allChannelsGood", reading.complete},
        {"invalidChannels", reading.missingChannels}, {"sampledAtUtc", point.sampledAtUtc}};
    if (reading.maximum) { details.insert("maximumCelsius", *reading.maximum); }
    for (const QString& code : {QStringLiteral("TEMP_WARNING"), QStringLiteral("TEMP_FAULT")}) {
        ThresholdRule& rule = code == "TEMP_WARNING" ? state_->warning : state_->fault;
        const ConditionState condition = config.limitsConfigured
            ? rule.evaluate(reading.maximum, reading.complete, point.observedMonoMs, config.staleAfterMs)
            : ConditionState::Unknown;
        details.insert("triggerCelsius", rule.parameters.triggerCelsius);
        details.insert("recoveryCelsius", rule.parameters.recoveryCelsius);
        emit conditionEvaluated({code, update.sourceId, condition, point.sampledAtUtc, details});
    }
}

void AlarmMonitor::updateSafetyCircuit(const DeviceUpdate& update, const PointValue& point, qint64 now)
{
    const bool good = point.quality == DataQuality::Good
        && point.value.metaType() == QMetaType::fromType<bool>() && now - point.observedMonoMs <= 2000;
    state_->safetyUntil = point.observedMonoMs + 2000;
    emit conditionEvaluated({"SAFETY_CIRCUIT_FAULT", update.sourceId,
        good ? (point.value.toBool() ? ConditionState::Abnormal : ConditionState::Normal) : ConditionState::Unknown,
        point.sampledAtUtc, {{"triggered", good ? point.value : QVariant()}}});
}

void AlarmMonitor::tick()
{
    const qint64 now = deviceObservationMonoMs();
    if (state_->temperatureUntil > 0 && now >= state_->temperatureUntil) {
        state_->temperatureUntil = 0;
        state_->warning.invalidate(); state_->fault.invalidate();
        for (const QString& code : {QStringLiteral("TEMP_WARNING"), QStringLiteral("TEMP_FAULT")}) {
            emit conditionEvaluated({code, state_->configuration.deviceId, ConditionState::Unknown,
                QDateTime::currentDateTimeUtc(), {{"reason", QStringLiteral("数据过期")}}});
        }
    }
    if (state_->safetyUntil > 0 && now >= state_->safetyUntil) {
        state_->safetyUntil = 0;
        emit conditionEvaluated({"SAFETY_CIRCUIT_FAULT", "safety/main", ConditionState::Unknown,
            QDateTime::currentDateTimeUtc(), {{"reason", QStringLiteral("安全反馈过期")}}});
    }
}
