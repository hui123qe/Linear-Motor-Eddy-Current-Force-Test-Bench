#pragma once

#include <QObject>

#include <optional>

struct TestResultTargets
{
    double averageForceNewtons = 220.0;
    double fluctuationRatePercent = 10.0;
};

struct ActualTestResult
{
    double averageForceNewtons = 0.0;
    double forceRangeNewtons = 0.0;
};

struct TestResultComparison
{
    TestResultTargets targets;
    ActualTestResult actual;
    double actualFluctuationRatePercent = 0.0;
    double averageDeviationNewtons = 0.0;
    double fluctuationRateDeviationPercentagePoints = 0.0;
};

class TestResultService final : public QObject
{
    Q_OBJECT

public:
    explicit TestResultService(QObject* parent = nullptr);

    [[nodiscard]] bool beginTest(const TestResultTargets& targets,
                                 QString* errorMessage = nullptr);
    [[nodiscard]] bool submitActualResult(const ActualTestResult& result,
                                         QString* errorMessage = nullptr);

    [[nodiscard]] std::optional<TestResultTargets> targets() const;
    [[nodiscard]] std::optional<TestResultComparison> comparison() const;

signals:
    void resultCleared();
    void resultUpdated(const TestResultComparison& comparison);

private:
    std::optional<TestResultTargets> targets_;
    std::optional<TestResultComparison> comparison_;
};
