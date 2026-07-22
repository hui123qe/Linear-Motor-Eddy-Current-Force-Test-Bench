#include "TestParameters.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <cmath>
#include <utility>

namespace {

constexpr int kSchemaVersion = 3;
constexpr int kMinimumRepeatCount = 1;
constexpr int kMaximumRepeatCount = 999;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool readObject(const QJsonObject& source,
                const QString& key,
                QJsonObject* destination,
                QString* errorMessage)
{
    const QJsonValue value = source.value(key);
    if (!value.isObject()) {
        setError(errorMessage, QStringLiteral("配置项 %1 必须是对象。").arg(key));
        return false;
    }

    *destination = value.toObject();
    return true;
}

bool readString(const QJsonObject& source,
                const QString& key,
                QString* destination,
                QString* errorMessage)
{
    const QJsonValue value = source.value(key);
    if (!value.isString()) {
        setError(errorMessage, QStringLiteral("配置项 %1 必须是字符串。").arg(key));
        return false;
    }

    *destination = value.toString();
    return true;
}

bool readDouble(const QJsonObject& source,
                const QString& key,
                double* destination,
                QString* errorMessage)
{
    const QJsonValue value = source.value(key);
    if (!value.isDouble()) {
        setError(errorMessage, QStringLiteral("配置项 %1 必须是数值。").arg(key));
        return false;
    }

    *destination = value.toDouble();
    return true;
}

bool readInteger(const QJsonObject& source,
                 const QString& key,
                 int* destination,
                 QString* errorMessage)
{
    double value = 0.0;
    if (!readDouble(source, key, &value, errorMessage)) {
        return false;
    }
    if (std::floor(value) != value) {
        setError(errorMessage, QStringLiteral("配置项 %1 必须是整数。").arg(key));
        return false;
    }

    *destination = static_cast<int>(value);
    return true;
}

bool validateValues(const TestParameters& parameters, QString* errorMessage)
{
    switch (parameters.selectedTestType) {
    case EddyCurrentTestType::RatedSpeed:
    case EddyCurrentTestType::VariableSpeed:
        break;
    default:
        setError(errorMessage, QStringLiteral("测试项目类型无效。"));
        return false;
    }

    if (parameters.motorModel.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("电机型号不能为空。"));
        return false;
    }
    if (parameters.specimenId.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("试验件编号不能为空。"));
        return false;
    }
    if (parameters.repeatCount < kMinimumRepeatCount
        || parameters.repeatCount > kMaximumRepeatCount) {
        setError(errorMessage, QStringLiteral("重复次数必须在 1 到 999 之间。"));
        return false;
    }
    PtpMotionParameters motionParameters;
    QString motionError;
    if (!calculatePtpMotionParameters(
            parameters.ratedSpeedTest, &motionParameters, &motionError)) {
        setError(
            errorMessage,
            QStringLiteral("额定速度测试：%1").arg(motionError));
        return false;
    }
    if (!calculatePtpMotionParameters(
            parameters.variableSpeedTest, &motionParameters, &motionError)) {
        setError(
            errorMessage,
            QStringLiteral("不同速度测试：%1").arg(motionError));
        return false;
    }

    return true;
}

QString testTypeToString(EddyCurrentTestType testType)
{
    switch (testType) {
    case EddyCurrentTestType::RatedSpeed:
        return QStringLiteral("ratedSpeed");
    case EddyCurrentTestType::VariableSpeed:
        return QStringLiteral("variableSpeed");
    }

    return {};
}

bool testTypeFromString(const QString& value,
                        EddyCurrentTestType* testType,
                        QString* errorMessage)
{
    if (value == QStringLiteral("ratedSpeed")) {
        *testType = EddyCurrentTestType::RatedSpeed;
        return true;
    }
    if (value == QStringLiteral("variableSpeed")) {
        *testType = EddyCurrentTestType::VariableSpeed;
        return true;
    }

    setError(errorMessage,
             QStringLiteral("配置项 selectedTestType 只能是 ratedSpeed 或 variableSpeed。"));
    return false;
}

QJsonObject toJson(const TestParameters& parameters)
{
    QJsonObject batch;
    batch.insert(QStringLiteral("motorModel"), parameters.motorModel.trimmed());
    batch.insert(QStringLiteral("specimenId"), parameters.specimenId.trimmed());
    batch.insert(QStringLiteral("repeatCount"), parameters.repeatCount);

    const auto motionToJson = [](const TestMotionParameters& motion) {
        QJsonObject object;
        object.insert(QStringLiteral("accelerationStartMeters"),
                      motion.accelerationStartMeters);
        object.insert(QStringLiteral("accelerationDistanceMeters"),
                      motion.accelerationDistanceMeters);
        object.insert(QStringLiteral("endPositionMeters"),
                      motion.endPositionMeters);
        object.insert(QStringLiteral("speedMetersPerSecond"),
                      motion.speedMetersPerSecond);
        return object;
    };

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("selectedTestType"),
                testTypeToString(parameters.selectedTestType));
    root.insert(QStringLiteral("batch"), batch);
    root.insert(QStringLiteral("ratedSpeedTest"),
                motionToJson(parameters.ratedSpeedTest));
    root.insert(QStringLiteral("variableSpeedTest"),
                motionToJson(parameters.variableSpeedTest));
    return root;
}

bool fromJson(const QJsonObject& root, TestParameters* parameters, QString* errorMessage)
{
    int schemaVersion = 0;
    if (!readInteger(root,
                     QStringLiteral("schemaVersion"),
                     &schemaVersion,
                     errorMessage)) {
        return false;
    }
    if (schemaVersion != kSchemaVersion) {
        setError(errorMessage,
                 QStringLiteral("不支持的配置版本：%1。").arg(schemaVersion));
        return false;
    }

    QJsonObject batch;
    QJsonObject ratedSpeedTest;
    QJsonObject variableSpeedTest;
    if (!readObject(root, QStringLiteral("batch"), &batch, errorMessage)
        || !readObject(root,
                       QStringLiteral("ratedSpeedTest"),
                       &ratedSpeedTest,
                       errorMessage)
        || !readObject(root,
                       QStringLiteral("variableSpeedTest"),
                       &variableSpeedTest,
                       errorMessage)) {
        return false;
    }

    TestParameters parsed;
    QString selectedTestType;
    if (!readString(root,
                    QStringLiteral("selectedTestType"),
                    &selectedTestType,
                    errorMessage)
        || !testTypeFromString(selectedTestType,
                               &parsed.selectedTestType,
                               errorMessage)
        || !readString(batch,
                    QStringLiteral("motorModel"),
                    &parsed.motorModel,
                    errorMessage)
        || !readString(batch,
                       QStringLiteral("specimenId"),
                       &parsed.specimenId,
                       errorMessage)
        || !readInteger(batch,
                        QStringLiteral("repeatCount"),
                        &parsed.repeatCount,
                        errorMessage)
        || !readDouble(ratedSpeedTest,
                       QStringLiteral("accelerationStartMeters"),
                       &parsed.ratedSpeedTest.accelerationStartMeters,
                       errorMessage)
        || !readDouble(ratedSpeedTest,
                       QStringLiteral("accelerationDistanceMeters"),
                       &parsed.ratedSpeedTest.accelerationDistanceMeters,
                       errorMessage)
        || !readDouble(ratedSpeedTest,
                       QStringLiteral("endPositionMeters"),
                       &parsed.ratedSpeedTest.endPositionMeters,
                       errorMessage)
        || !readDouble(ratedSpeedTest,
                       QStringLiteral("speedMetersPerSecond"),
                       &parsed.ratedSpeedTest.speedMetersPerSecond,
                       errorMessage)
        || !readDouble(variableSpeedTest,
                       QStringLiteral("accelerationStartMeters"),
                       &parsed.variableSpeedTest.accelerationStartMeters,
                       errorMessage)
        || !readDouble(variableSpeedTest,
                       QStringLiteral("accelerationDistanceMeters"),
                       &parsed.variableSpeedTest.accelerationDistanceMeters,
                       errorMessage)
        || !readDouble(variableSpeedTest,
                       QStringLiteral("endPositionMeters"),
                       &parsed.variableSpeedTest.endPositionMeters,
                       errorMessage)
        || !readDouble(variableSpeedTest,
                       QStringLiteral("speedMetersPerSecond"),
                       &parsed.variableSpeedTest.speedMetersPerSecond,
                       errorMessage)) {
        return false;
    }
    if (!validateValues(parsed, errorMessage)) {
        return false;
    }

    *parameters = parsed;
    return true;
}

} // namespace

bool calculatePtpMotionParameters(const TestMotionParameters& parameters,
                                  PtpMotionParameters* motionParameters,
                                  QString* errorMessage)
{
    if (motionParameters == nullptr) {
        setError(errorMessage, QStringLiteral("PTP 运动参数接收对象不能为空。"));
        return false;
    }
    if (!std::isfinite(parameters.accelerationStartMeters)
        || !std::isfinite(parameters.accelerationDistanceMeters)
        || !std::isfinite(parameters.endPositionMeters)
        || !std::isfinite(parameters.speedMetersPerSecond)) {
        setError(errorMessage, QStringLiteral("PTP 位置、距离和速度必须为有限数值。"));
        return false;
    }
    if (parameters.accelerationDistanceMeters <= 0.0) {
        setError(errorMessage, QStringLiteral("加速距离必须大于 0。"));
        return false;
    }
    if (parameters.speedMetersPerSecond <= 0.0
        || parameters.speedMetersPerSecond > kMaximumTestSpeedMetersPerSecond) {
        setError(errorMessage, QStringLiteral("速度必须大于 0 且不大于 5 m/s。"));
        return false;
    }

    const double travelDistanceMeters = parameters.endPositionMeters
                                        - parameters.accelerationStartMeters;
    if (travelDistanceMeters < 2.0 * parameters.accelerationDistanceMeters) {
        setError(
            errorMessage,
            QStringLiteral("结束位置与开始加速位置的距离必须不小于两倍加速距离。"));
        return false;
    }

    const double accelerationMetersPerSecondSquared =
        parameters.speedMetersPerSecond * parameters.speedMetersPerSecond
        / (2.0 * parameters.accelerationDistanceMeters);
    if (!std::isfinite(accelerationMetersPerSecondSquared)
        || accelerationMetersPerSecondSquared
               > kMaximumTestAccelerationMetersPerSecondSquared) {
        setError(errorMessage, QStringLiteral("计算加速度和减速度不能超过 25 m/s2。"));
        return false;
    }

    motionParameters->startPositionMeters = parameters.accelerationStartMeters;
    motionParameters->endPositionMeters = parameters.endPositionMeters;
    motionParameters->velocityMetersPerSecond = parameters.speedMetersPerSecond;
    motionParameters->accelerationMetersPerSecondSquared =
        accelerationMetersPerSecondSquared;
    motionParameters->decelerationMetersPerSecondSquared =
        accelerationMetersPerSecondSquared;
    return true;
}

bool validateTestParameters(const TestParameters& parameters, QString* errorMessage)
{
    return validateValues(parameters, errorMessage);
}

QString testBatchName(const TestParameters& parameters)
{
    const QString motorModel = parameters.motorModel.trimmed();
    const QString specimenId = parameters.specimenId.trimmed();
    if (motorModel.isEmpty() || specimenId.isEmpty()) {
        return {};
    }

    return motorModel + QStringLiteral(" --") + specimenId;
}

QString TestParametersStore::defaultFilePath()
{
    const QString directory = QCoreApplication::applicationDirPath();
    if (directory.isEmpty()) {
        return {};
    }
    return QDir(directory).filePath(QStringLiteral("parameters.json"));
}

TestParametersStore::TestParametersStore(QString filePath)
    : filePath_(std::move(filePath))
{
}

QString TestParametersStore::filePath() const
{
    return filePath_;
}

bool TestParametersStore::load(TestParameters* parameters, QString* errorMessage) const
{
    if (parameters == nullptr) {
        setError(errorMessage, QStringLiteral("配置接收对象不能为空。"));
        return false;
    }
    if (filePath_.isEmpty()) {
        setError(errorMessage, QStringLiteral("无法确定用户配置目录。"));
        return false;
    }

    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(errorMessage,
                 QStringLiteral("无法读取配置文件：%1").arg(file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(errorMessage,
                 QStringLiteral("配置文件 JSON 格式错误：%1").arg(parseError.errorString()));
        return false;
    }
    if (!document.isObject()) {
        setError(errorMessage, QStringLiteral("配置文件根节点必须是对象。"));
        return false;
    }

    return fromJson(document.object(), parameters, errorMessage);
}

bool TestParametersStore::save(const TestParameters& parameters, QString* errorMessage) const
{
    if (!validateTestParameters(parameters, errorMessage)) {
        return false;
    }
    if (filePath_.isEmpty()) {
        setError(errorMessage, QStringLiteral("无法确定用户配置目录。"));
        return false;
    }

    const QFileInfo fileInfo(filePath_);
    QDir parentDirectory = fileInfo.dir();
    if (!parentDirectory.exists() && !parentDirectory.mkpath(QStringLiteral("."))) {
        setError(errorMessage,
                 QStringLiteral("无法创建配置目录：%1").arg(parentDirectory.path()));
        return false;
    }

    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(errorMessage,
                 QStringLiteral("无法写入配置文件：%1").arg(file.errorString()));
        return false;
    }

    const QByteArray content = QJsonDocument(toJson(parameters)).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size()) {
        setError(errorMessage,
                 QStringLiteral("配置文件写入不完整：%1").arg(file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(errorMessage,
                 QStringLiteral("配置文件提交失败：%1").arg(file.errorString()));
        return false;
    }

    return true;
}
