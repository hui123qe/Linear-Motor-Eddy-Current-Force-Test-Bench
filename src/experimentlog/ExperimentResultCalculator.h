#pragma once

#include "ExperimentLogTypes.h"

class ExperimentResultCalculator final
{
public:
    [[nodiscard]] static ExperimentStatistics calculateSingle(
        const ExperimentForceAggregate& aggregate,
        double testSpeedMetersPerSecond);
    [[nodiscard]] static ExperimentSummaryStatistics calculateSummary(
        const QVector<ExperimentStatistics>& records);
};
