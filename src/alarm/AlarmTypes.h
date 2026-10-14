#pragma once

#include "../temperature/TemperatureTypes.h"

#include <QMap>
#include <QStringList>
#include <QVariant>

enum class PointId { TemperatureBatch, SafetyCircuit };
enum class ConditionState { Normal, Abnormal, Unknown };
enum class AlarmSeverity { Warning, Fault };
// Preserve the database's existing raised/acknowledged/recovered/closed codes.
enum class AlarmState
{
    UnacknowledgedUnrecovered = 0,
    AcknowledgedUnrecovered = 2,
    RecoveredUnacknowledged = 1,
    Closed = 5
};

struct AlarmDefinition
{
    QString code;
    QString title;
    AlarmSeverity severity = AlarmSeverity::Fault;
};

struct PointValue
{
    QVariant value;
    DataQuality quality = DataQuality::Invalid;
    QDateTime sampledAtUtc;
    qint64 observedMonoMs = 0;
    quint64 sequence = 0;
};
struct DeviceUpdate
{
    QString sourceId;
    QUuid sessionId;
    QMap<PointId, PointValue> points;
};
struct ConditionReport
{
    QString code;
    QString sourceId;
    ConditionState state = ConditionState::Unknown;
    QDateTime observedAtUtc;
    QVariantMap details;
};
struct AlarmRecord
{
    QUuid occurrenceId;
    QString code;
    QString sourceId;
    QString title;
    AlarmSeverity severity = AlarmSeverity::Warning;
    // Lifecycle is derived from recovery, acknowledgement and closure; input
    // quality (ConditionState::Unknown) must not undo a recorded recovery.
    bool acknowledged = false;
    quint64 revision = 1;
    QDateTime raisedAtUtc;
    QDateTime recoveredAtUtc;
    QDateTime acknowledgedAtUtc;
    QDateTime closedAtUtc;
    QString acknowledgedBy;
    QString experimentName;
    QVariantMap triggerDetails;
    QVariantMap latestDetails;

    [[nodiscard]] AlarmState state() const
    {
        if (closedAtUtc.isValid()) { return AlarmState::Closed; }
        if (recoveredAtUtc.isValid()) { return AlarmState::RecoveredUnacknowledged; }
        return acknowledged ? AlarmState::AcknowledgedUnrecovered
                            : AlarmState::UnacknowledgedUnrecovered;
    }
};
struct AlarmEvent
{
    QUuid eventId;
    AlarmRecord record;
    QDateTime eventAtUtc;
};
Q_DECLARE_METATYPE(DeviceUpdate)
Q_DECLARE_METATYPE(AlarmDefinition)
Q_DECLARE_METATYPE(ConditionReport)
Q_DECLARE_METATYPE(AlarmRecord)
Q_DECLARE_METATYPE(QVector<AlarmRecord>)
Q_DECLARE_METATYPE(AlarmEvent)
