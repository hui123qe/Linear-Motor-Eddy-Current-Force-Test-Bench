#include "TestResultService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

#include <cmath>
#include <limits>

namespace {

QString targetsFilePath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("test-result-targets.json"));
}

bool withinTolerance(double actual, double target, double tolerance)
{
    return actual >= target - tolerance && actual <= target + tolerance;
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validateActualResult(const ActualTestResult& result, QString* errorMessage)
{
    if (!std::isfinite(result.averageForceNewtons)
        || result.averageForceNewtons < 0.0) {
        setError(errorMessage, QStringLiteral("实际平均涡流力必须为非负有限数值。"));
        return false;
    }
    if (!std::isfinite(result.forceCoefficientNewtonSecondsPerMeter)
        || result.forceCoefficientNewtonSecondsPerMeter < 0.0) {
        setError(errorMessage, QStringLiteral("实际平均涡流力系数必须为非负有限数值。"));
        return false;
    }
    return true;
}

} // namespace

bool validateTestResultTargets(const TestResultTargets& targets,
                               QString* errorMessage)
{
    if (!std::isfinite(targets.averageForceNewtons)
        || targets.averageForceNewtons <= 0.0) {
        setError(errorMessage, QStringLiteral("目标平均值必须大于 0 N。"));
        return false;
    }
    if (!std::isfinite(targets.forceCoefficientNewtonSecondsPerMeter)
        || targets.forceCoefficientNewtonSecondsPerMeter < 0.0) {
        setError(errorMessage, QStringLiteral("目标平均涡流力系数必须为非负有限数值。"));
        return false;
    }
    if (!std::isfinite(targets.averageForceToleranceNewtons)
        || targets.averageForceToleranceNewtons < 0.0
        || !std::isfinite(targets.forceCoefficientToleranceNewtonSecondsPerMeter)
        || targets.forceCoefficientToleranceNewtonSecondsPerMeter < 0.0) {
        setError(errorMessage, QStringLiteral("目标容差必须为非负有限数值。"));
        return false;
    }

    return true;
}

TestResultService& TestResultService::instance()
{
    static TestResultService service;
    return service;
}

TestResultService::TestResultService()
{
}

bool TestResultService::loadSavedTargets(TestResultTargets* targets,
                                        QString* errorMessage) const
{
    QFile file(targetsFilePath());
    if (!file.exists()) {
        *targets = TestResultTargets{};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, file.errorString());
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        setError(errorMessage, QStringLiteral("测试目标 JSON 格式无效。"));
        return false;
    }
    return testResultTargetsFromJson(document.object(), targets, errorMessage);
}

bool testResultTargetsFromJson(const QJsonObject& object,
                               TestResultTargets* targets, QString* errorMessage)
{
    const double invalid = std::numeric_limits<double>::quiet_NaN();
    TestResultTargets saved;
    saved.averageForceNewtons =
        object.value(QStringLiteral("averageForceNewtons")).toDouble(invalid);
    saved.averageForceToleranceNewtons =
        object.value(QStringLiteral("averageForceToleranceNewtons")).toDouble(invalid);
    saved.forceCoefficientNewtonSecondsPerMeter =
        object.value(QStringLiteral("forceCoefficientNewtonSecondsPerMeter")).toDouble(invalid);
    saved.forceCoefficientToleranceNewtonSecondsPerMeter =
        object.value(QStringLiteral("forceCoefficientToleranceNewtonSecondsPerMeter")).toDouble(invalid);
    if (!validateTestResultTargets(saved, errorMessage)) {
        return false;
    }
    *targets = saved;
    return true;
}

QJsonObject testResultTargetsToJson(const TestResultTargets& targets)
{
    return QJsonObject{
        {QStringLiteral("averageForceNewtons"), targets.averageForceNewtons},
        {QStringLiteral("averageForceToleranceNewtons"),
         targets.averageForceToleranceNewtons},
        {QStringLiteral("forceCoefficientNewtonSecondsPerMeter"),
         targets.forceCoefficientNewtonSecondsPerMeter},
        {QStringLiteral("forceCoefficientToleranceNewtonSecondsPerMeter"),
         targets.forceCoefficientToleranceNewtonSecondsPerMeter}
    };
}

bool TestResultService::saveTargets(const TestResultTargets& targets,
                                    QString* errorMessage) const
{
    if (!validateTestResultTargets(targets, errorMessage)) {
        return false;
    }
    const QByteArray data = QJsonDocument(testResultTargetsToJson(targets))
                                .toJson(QJsonDocument::Indented);
    QSaveFile file(targetsFilePath());
    if (!file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size() || !file.commit()) {
        setError(errorMessage, file.errorString());
        return false;
    }
    return true;
}

bool TestResultService::beginTest(const TestResultTargets& targets,
                                  QString* errorMessage)
{
    if (!validateTestResultTargets(targets, errorMessage)) {
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

    comparison_ = compareTestResult(*targets_, result);
    emit resultUpdated(*comparison_);
    return true;
}

TestResultComparison compareTestResult(const TestResultTargets& targets,
                                       const ActualTestResult& result)
{
    TestResultComparison comparison;
    comparison.targets = targets;
    comparison.actual = result;
    comparison.averageForcePassed = withinTolerance(
        result.averageForceNewtons,
        targets.averageForceNewtons,
        targets.averageForceToleranceNewtons);
    comparison.forceCoefficientPassed = withinTolerance(
        result.forceCoefficientNewtonSecondsPerMeter,
        targets.forceCoefficientNewtonSecondsPerMeter,
        targets.forceCoefficientToleranceNewtonSecondsPerMeter);
    return comparison;
}

std::optional<TestResultTargets> TestResultService::targets() const
{
    return targets_;
}

std::optional<TestResultComparison> TestResultService::comparison() const
{
    return comparison_;
}
