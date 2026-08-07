#pragma once

#include "../config/TestParameters.h"

#include <QDateTime>
#include <QDate>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

inline constexpr int kExperimentForceDisplayDecimals = 2;
inline constexpr int kExperimentCoefficientDisplayDecimals = 2;
inline constexpr int kExperimentRateDisplayDecimals = 2;

enum class ExperimentTerminalState
{
    Completed,
    Terminated,
    Fault
};

enum class ExperimentMotionDirection
{
    Forward,
    Reverse
};

enum class ExperimentLogEntryKind
{
    SingleRecord,
    Summary
};

struct ExperimentForceAggregate
{
    qint64 effectiveSampleCount = 0;
    std::optional<double> averageForceNewtons;
    std::optional<double> minimumForceNewtons;
    std::optional<double> maximumForceNewtons;
};

struct ExperimentStatistics
{
    qint64 effectiveSampleCount = 0;
    std::optional<double> averageForceNewtons;
    std::optional<double> forceCoefficientNewtonSecondsPerMeter;
    std::optional<double> forceRangeNewtons;
    std::optional<double> fluctuationRatePercent;
};

struct ExperimentSummaryStatistics
{
    std::optional<double> multipleAverageForceNewtons;
    std::optional<double> multipleAverageForceCoefficientNewtonSecondsPerMeter;
};

struct ExperimentSummaryRecord
{
    qint64 id = 0;
    qint64 executionId = 0;
    QString baseExperimentName;
    QDateTime finishedAtUtc;
    QString operatorName;
    ExperimentTerminalState state = ExperimentTerminalState::Fault;
    ExperimentSummaryStatistics statistics;
};

struct ExperimentFinalContext
{
    qint64 executionId = 0;
    int repetitionIndex = 0;
    int plannedRepeatCount = 0;
    int cycleIndex = 0;
    ExperimentMotionDirection direction = ExperimentMotionDirection::Forward;
    QString baseExperimentName;
    QString experimentName;
    QDateTime finishedAtUtc;
    QString operatorName;
    ExperimentTerminalState state = ExperimentTerminalState::Fault;
    QString terminalReason;
    TestParameters parametersSnapshot;
    QString rawDataTableName;
    qint64 rawSampleCount = 0;
};

struct ExperimentGroupFinalContext
{
    qint64 executionId = 0;
    QString baseExperimentName;
    QDateTime finishedAtUtc;
    QString operatorName;
    ExperimentTerminalState state = ExperimentTerminalState::Fault;
    QString terminalReason;
    int plannedRepeatCount = 0;
    int finalizedRecordCount = 0;
};

struct ExperimentRecord
{
    qint64 id = 0;
    qint64 executionId = 0;
    QString experimentName;
    QString baseExperimentName;
    int repetitionIndex = 0;
    int plannedRepeatCount = 0;
    int cycleIndex = 0;
    ExperimentMotionDirection direction = ExperimentMotionDirection::Forward;
    QDateTime finishedAtUtc;
    QString operatorName;
    ExperimentTerminalState state = ExperimentTerminalState::Fault;
    QString terminalReason;
    double testSpeedMetersPerSecond = 0.0;
    double statisticsStartMeters = 0.0;
    double statisticsEndMeters = 0.0;
    qint64 rawSampleCount = 0;
    ExperimentStatistics statistics;
    QString rawDataTableName;
    TestParameters parametersSnapshot;
};

struct ExperimentRecordListItem
{
    ExperimentLogEntryKind kind = ExperimentLogEntryKind::SingleRecord;
    qint64 id = 0;
    QDateTime finishedAtUtc;
    QString operatorName;
    ExperimentTerminalState state = ExperimentTerminalState::Fault;
    QString terminalReason;
    QString experimentName;
};

struct ExperimentRecordFilter
{
    QDate fromDate;
    QDate toDate;
    QString keyword;
    int offset = 0;
    int limit = 100;
};

struct ExperimentSummaryBundle
{
    ExperimentSummaryRecord summary;
    QVector<ExperimentRecord> records;
};

[[nodiscard]] QString experimentTerminalStateDatabaseValue(
    ExperimentTerminalState state);
[[nodiscard]] bool experimentTerminalStateFromDatabaseValue(
    const QString& value,
    ExperimentTerminalState* state);
[[nodiscard]] QString experimentTerminalStateDisplayText(
    ExperimentTerminalState state);
[[nodiscard]] QString experimentMotionDirectionDatabaseValue(
    ExperimentMotionDirection direction);
[[nodiscard]] bool experimentMotionDirectionFromDatabaseValue(
    const QString& value,
    ExperimentMotionDirection* direction);
[[nodiscard]] QString experimentMotionDirectionDisplayText(
    ExperimentMotionDirection direction);
[[nodiscard]] QString formatAverageForce(
    const std::optional<double>& value);
[[nodiscard]] QString formatForceCoefficient(
    const std::optional<double>& value);
[[nodiscard]] QString formatForceRange(
    const std::optional<double>& value);
[[nodiscard]] QString formatFluctuationRate(
    const std::optional<double>& value);

Q_DECLARE_METATYPE(ExperimentTerminalState)
Q_DECLARE_METATYPE(ExperimentMotionDirection)
Q_DECLARE_METATYPE(ExperimentLogEntryKind)
Q_DECLARE_METATYPE(ExperimentForceAggregate)
Q_DECLARE_METATYPE(ExperimentStatistics)
Q_DECLARE_METATYPE(ExperimentSummaryStatistics)
Q_DECLARE_METATYPE(ExperimentSummaryRecord)
Q_DECLARE_METATYPE(ExperimentFinalContext)
Q_DECLARE_METATYPE(ExperimentGroupFinalContext)
Q_DECLARE_METATYPE(ExperimentRecord)
Q_DECLARE_METATYPE(ExperimentRecordListItem)
Q_DECLARE_METATYPE(ExperimentRecordFilter)
Q_DECLARE_METATYPE(ExperimentSummaryBundle)
Q_DECLARE_METATYPE(QVector<ExperimentStatistics>)
Q_DECLARE_METATYPE(QVector<ExperimentRecordListItem>)
