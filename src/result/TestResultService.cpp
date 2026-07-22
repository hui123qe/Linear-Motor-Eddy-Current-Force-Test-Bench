#include "TestResultService.h"

#include <QString>

#include <cmath>

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validateTargets(const TestResultTargets& targets, QString* errorMessage)
{
    if (!std::isfinite(targets.averageForceNewtons)
        || targets.averageForceNewtons <= 0.0) {
        setError(errorMessage, QStringLiteral("目标平均值必须大于 0 N。"));
        return false;
    }
    if (!std::isfinite(targets.fluctuationRatePercent)
        || targets.fluctuationRatePercent < 0.0
        || targets.fluctuationRatePercent > 100.0) {
        setError(errorMessage, QStringLiteral("目标波动率必须在 0% 到 100% 之间。"));
        return false;
    }

    return true;
}

bool validateActualResult(const ActualTestResult& result, QString* errorMessage)
{
    if (!std::isfinite(result.averageForceNewtons)
        || result.averageForceNewtons <= 0.0) {
        setError(errorMessage, QStringLiteral("实际平均值必须大于 0 N。"));
        return false;
    }
    if (!std::isfinite(result.forceRangeNewtons)
        || result.forceRangeNewtons < 0.0) {
        setError(errorMessage, QStringLiteral("实际波动值不能小于 0 N。"));
        return false;
    }
    return true;
}

} // namespace

TestResultService::TestResultService(QObject* parent)
    : QObject(parent)
{
}

bool TestResultService::beginTest(const TestResultTargets& targets,
                                  QString* errorMessage)
{
    if (!validateTargets(targets, errorMessage)) {
        return false;
    }

    targets_ = targets;
    comparison_.reset();
    emit resultCleared();
    return true;
}

bool TestResultService::submitActualResult(const ActualTestResult& result,
                                          QString* errorMessage)
{
    if (!targets_.has_value()) {
        setError(errorMessage, QStringLiteral("测试尚未开始，不能提交实际结果。"));
        return false;
    }
    if (!validateActualResult(result, errorMessage)) {
        return false;
    }

    TestResultComparison comparison;
    comparison.targets = *targets_;
    comparison.actual = result;
    comparison.actualFluctuationRatePercent =
        result.forceRangeNewtons / result.averageForceNewtons * 100.0;
    comparison.averageDeviationNewtons =
        result.averageForceNewtons - targets_->averageForceNewtons;
    comparison.fluctuationRateDeviationPercentagePoints =
        comparison.actualFluctuationRatePercent - targets_->fluctuationRatePercent;
    comparison_ = comparison;
    emit resultUpdated(comparison);
    return true;
}

std::optional<TestResultTargets> TestResultService::targets() const
{
    return targets_;
}

std::optional<TestResultComparison> TestResultService::comparison() const
{
    return comparison_;
}
