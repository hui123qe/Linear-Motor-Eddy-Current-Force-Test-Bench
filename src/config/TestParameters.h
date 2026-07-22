#pragma once

#include <QString>

enum class EddyCurrentTestType
{
    RatedSpeed,
    VariableSpeed
};

inline constexpr double kMaximumTestSpeedMetersPerSecond = 5.0;
inline constexpr double kMaximumTestAccelerationMetersPerSecondSquared = 25.0;

struct TestMotionParameters
{
    double accelerationStartMeters = 0.0;
    double accelerationDistanceMeters = 0.5;
    double endPositionMeters = 1.7;
    double speedMetersPerSecond = 3.0;
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
