#include "ExperimentResultCalculator.h"

#include <cmath>

namespace {

bool hasFiniteValue(const std::optional<double>& value)
{
    return value.has_value() && std::isfinite(*value);
}

} // namespace

ExperimentStatistics ExperimentResultCalculator::calculateSingle(
    const ExperimentForceAggregate& aggregate,
    double testSpeedMetersPerSecond)
{
    ExperimentStatistics result;
    if (aggregate.effectiveSampleCount <= 0
        || !hasFiniteValue(aggregate.averageForceNewtons)
        || !hasFiniteValue(aggregate.minimumForceNewtons)
        || !hasFiniteValue(aggregate.maximumForceNewtons)
        || *aggregate.maximumForceNewtons < *aggregate.minimumForceNewtons) {
        return result;
    }

    result.effectiveSampleCount = aggregate.effectiveSampleCount;
    result.averageForceNewtons = std::abs(*aggregate.averageForceNewtons);

    const double forceRangeNewtons = *aggregate.maximumForceNewtons
                                     - *aggregate.minimumForceNewtons;
    if (std::isfinite(forceRangeNewtons)) {
        result.forceRangeNewtons = forceRangeNewtons;
    }

    if (std::isfinite(testSpeedMetersPerSecond)
        && testSpeedMetersPerSecond > 0.0) {
        const double coefficient = *aggregate.averageForceNewtons
                                   / testSpeedMetersPerSecond;
        if (std::isfinite(coefficient)) {
            result.forceCoefficientNewtonSecondsPerMeter = coefficient;
        }
    }

    if (result.forceRangeNewtons.has_value()
        && *result.averageForceNewtons != 0.0) {
        const double fluctuationRate = *result.forceRangeNewtons
                                       / *result.averageForceNewtons
                                       * 100.0;
        if (std::isfinite(fluctuationRate)) {
            result.fluctuationRatePercent = fluctuationRate;
        }
    }

    return result;
}

ExperimentSummaryStatistics ExperimentResultCalculator::calculateSummary(
    const QVector<ExperimentStatistics>& records)
{
    double forceSumNewtons = 0.0;
    double coefficientSumNewtonSecondsPerMeter = 0.0;
    int effectiveRecordCount = 0;

    for (const ExperimentStatistics& record : records) {
        if (!hasFiniteValue(record.averageForceNewtons)
            || !hasFiniteValue(
                record.forceCoefficientNewtonSecondsPerMeter)) {
            continue;
        }

        forceSumNewtons += *record.averageForceNewtons;
        coefficientSumNewtonSecondsPerMeter +=
            *record.forceCoefficientNewtonSecondsPerMeter;
        ++effectiveRecordCount;
    }

    ExperimentSummaryStatistics result;
    if (effectiveRecordCount == 0) {
        return result;
    }

    const double averageForceNewtons =
        forceSumNewtons / static_cast<double>(effectiveRecordCount);
    const double averageCoefficient =
        coefficientSumNewtonSecondsPerMeter
        / static_cast<double>(effectiveRecordCount);
    if (!std::isfinite(averageForceNewtons)
        || !std::isfinite(averageCoefficient)) {
        return result;
    }

    result.multipleAverageForceNewtons = averageForceNewtons;
    result.multipleAverageForceCoefficientNewtonSecondsPerMeter =
        averageCoefficient;
    return result;
}
