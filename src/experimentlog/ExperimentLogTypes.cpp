#include "ExperimentLogTypes.h"

#include <cmath>

namespace {

QString formatValue(const std::optional<double>& value,
                    int decimals,
                    const QString& suffix)
{
    if (!value.has_value() || !std::isfinite(*value)) {
        return QStringLiteral("----");
    }

    return QStringLiteral("%1 %2")
        .arg(*value, 0, 'f', decimals)
        .arg(suffix);
}

} // namespace

QString experimentTerminalStateDatabaseValue(ExperimentTerminalState state)
{
    switch (state) {
    case ExperimentTerminalState::Completed:
        return QStringLiteral("completed");
    case ExperimentTerminalState::Terminated:
        return QStringLiteral("stopped");
    case ExperimentTerminalState::Fault:
        return QStringLiteral("failed");
    }

    return {};
}

bool experimentTerminalStateFromDatabaseValue(
    const QString& value,
    ExperimentTerminalState* state)
{
    if (state == nullptr) {
        return false;
    }
    if (value == QStringLiteral("completed")) {
        *state = ExperimentTerminalState::Completed;
        return true;
    }
    if (value == QStringLiteral("failed")) {
        *state = ExperimentTerminalState::Fault;
        return true;
    }
    if (value == QStringLiteral("stopped")) {
        *state = ExperimentTerminalState::Terminated;
        return true;
    }
    return false;
}

QString experimentTerminalStateDisplayText(ExperimentTerminalState state)
{
    switch (state) {
    case ExperimentTerminalState::Completed:
        return QStringLiteral("完成");
    case ExperimentTerminalState::Terminated:
        return QStringLiteral("终止");
    case ExperimentTerminalState::Fault:
        return QStringLiteral("故障");
    }

    return QStringLiteral("状态未知");
}

QString experimentMotionDirectionDatabaseValue(
    ExperimentMotionDirection direction)
{
    switch (direction) {
    case ExperimentMotionDirection::Forward:
        return QStringLiteral("forward");
    case ExperimentMotionDirection::Reverse:
        return QStringLiteral("reverse");
    }

    return {};
}

bool experimentMotionDirectionFromDatabaseValue(
    const QString& value,
    ExperimentMotionDirection* direction)
{
    if (direction == nullptr) {
        return false;
    }
    if (value == QStringLiteral("forward")) {
        *direction = ExperimentMotionDirection::Forward;
        return true;
    }
    if (value == QStringLiteral("reverse")) {
        *direction = ExperimentMotionDirection::Reverse;
        return true;
    }
    return false;
}

QString experimentMotionDirectionDisplayText(
    ExperimentMotionDirection direction)
{
    switch (direction) {
    case ExperimentMotionDirection::Forward:
        return QStringLiteral("正向");
    case ExperimentMotionDirection::Reverse:
        return QStringLiteral("反向");
    }

    return QStringLiteral("方向未知");
}

QString formatAverageForce(const std::optional<double>& value)
{
    return formatValue(
        value, kExperimentForceDisplayDecimals, QStringLiteral("N"));
}

QString formatForceCoefficient(const std::optional<double>& value)
{
    return formatValue(
        value,
        kExperimentCoefficientDisplayDecimals,
        QStringLiteral("N·s/m"));
}

QString formatForceRange(const std::optional<double>& value)
{
    return formatValue(
        value, kExperimentForceDisplayDecimals, QStringLiteral("N"));
}

QString formatFluctuationRate(const std::optional<double>& value)
{
    return formatValue(
        value, kExperimentRateDisplayDecimals, QStringLiteral("%"));
}
