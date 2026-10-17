#pragma once

#include <QObject>
#include <QJsonObject>

#include <optional>

struct TestResultTargets
{
    double averageForceNewtons = 220.0;
    double averageForceToleranceNewtons = 0.0;
    double forceCoefficientNewtonSecondsPerMeter = 0.0;
    double forceCoefficientToleranceNewtonSecondsPerMeter = 0.0;
};

struct ActualTestResult
{
    double averageForceNewtons = 0.0;
    double forceCoefficientNewtonSecondsPerMeter = 0.0;
};

struct TestResultComparison
{
    TestResultTargets targets;
    ActualTestResult actual;
    bool averageForcePassed = false;
    bool forceCoefficientPassed = false;
};

[[nodiscard]] bool validateTestResultTargets(
    const TestResultTargets& targets,
    QString* errorMessage = nullptr);

[[nodiscard]] QJsonObject testResultTargetsToJson(const TestResultTargets& targets);
[[nodiscard]] bool testResultTargetsFromJson(const QJsonObject& object,
                                            TestResultTargets* targets,
                                            QString* errorMessage = nullptr);
[[nodiscard]] TestResultComparison compareTestResult(
    const TestResultTargets& targets, const ActualTestResult& actual);

class TestResultService final : public QObject
{
    Q_OBJECT

public:
    static TestResultService& instance();

    [[nodiscard]] bool loadSavedTargets(TestResultTargets* targets,
                                         QString* errorMessage = nullptr) const;
    [[nodiscard]] bool saveTargets(const TestResultTargets& targets,
                                   QString* errorMessage = nullptr) const;
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
    TestResultService();

    std::optional<TestResultTargets> targets_;
    std::optional<TestResultComparison> comparison_;
};
