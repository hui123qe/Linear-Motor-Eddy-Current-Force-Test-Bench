#pragma once

#include <QString>

class QJsonObject;

enum class EddyCurrentTestType
{
    RatedSpeed,
    VariableSpeed
};

inline constexpr double kMaximumTestSpeedMetersPerSecond = 5.0;
inline constexpr double kMaximumTestAccelerationMetersPerSecondSquared = 25.0;
inline constexpr double kStandardAccelerationStartMeters = 0.0;
inline constexpr double kStandardAccelerationDistanceMeters = 0.5;
inline constexpr double kStandardEndPositionMeters = 1.7;
inline constexpr double kDefaultAcquisitionStartMeters = 0.5;
inline constexpr double kDefaultAcquisitionEndMeters = 1.2;

struct TestMotionParameters
{
    double accelerationStartMeters = kStandardAccelerationStartMeters;
    double accelerationDistanceMeters = kStandardAccelerationDistanceMeters;
    double endPositionMeters = kStandardEndPositionMeters;
    double speedMetersPerSecond = 3.0;
    double acquisitionStartMeters = kDefaultAcquisitionStartMeters;
    double acquisitionEndMeters = kDefaultAcquisitionEndMeters;
};

struct PtpMotionParameters
{
    double startPositionMeters = 0.0;
    double endPositionMeters = 0.0;
    double velocityMetersPerSecond = 0.0;
    double accelerationMetersPerSecondSquared = 0.0;
    double decelerationMetersPerSecondSquared = 0.0;
};

struct TestParameters
{
    EddyCurrentTestType selectedTestType = EddyCurrentTestType::RatedSpeed;
    QString motorModel = QStringLiteral("HIT-HSM-02");
    QString specimenId = QStringLiteral("HIT-EC-017");
    int repeatCount = 1;

    TestMotionParameters ratedSpeedTest;
    TestMotionParameters variableSpeedTest;
};

[[nodiscard]] bool calculatePtpMotionParameters(
    const TestMotionParameters& parameters,
    PtpMotionParameters* motionParameters,
    QString* errorMessage = nullptr);
[[nodiscard]] bool validateTestParameters(const TestParameters& parameters,
                                          QString* errorMessage = nullptr);
[[nodiscard]] QString testBatchName(const TestParameters& parameters);
[[nodiscard]] QString experimentBaseName(const TestParameters& parameters);
[[nodiscard]] TestMotionParameters selectedTestMotionParameters(
    const TestParameters& parameters);
[[nodiscard]] QJsonObject serializeTestParameters(
    const TestParameters& parameters);
[[nodiscard]] bool deserializeTestParameters(
    const QJsonObject& object,
    TestParameters* parameters,
    QString* errorMessage = nullptr);

class TestParametersStore final
{
public:
    static QString defaultFilePath();

    explicit TestParametersStore(QString filePath = defaultFilePath());

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool load(TestParameters* parameters, QString* errorMessage = nullptr) const;
    [[nodiscard]] bool save(const TestParameters& parameters, QString* errorMessage = nullptr) const;

private:
    QString filePath_;
};
