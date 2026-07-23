#pragma once

#include "../config/TestParameters.h"

#include <QObject>

struct AcquisitionBlock;
struct AcsMotionStatus;

class TestExecutionService final : public QObject
{
    Q_OBJECT

public:
    static TestExecutionService& instance();

    void shutdown();
    [[nodiscard]] bool start(const TestParameters& parameters,
                             QString* errorMessage = nullptr);
    [[nodiscard]] bool stop(QString* errorMessage = nullptr);

signals:
    void executionFinished();
    void executionStopped();
    void executionFailed(const QString& message);

private:
    TestExecutionService();
    ~TestExecutionService() override;

    TestExecutionService(const TestExecutionService&) = delete;
    TestExecutionService& operator=(const TestExecutionService&) = delete;

    void handleExperimentTableCreated(int repetitionIndex);
    void handleCollectionStarted();
    void handleMotionStatusChanged(const AcsMotionStatus& status);
    void handleAcquisitionBlock(const AcquisitionBlock& block);
    void handleCollectionStopped();
    void handleExperimentTableFinished(int repetitionIndex);
    void prepareRepetition(int repetitionIndex);
    void requestCurrentCollectionStop();
    void tryAdvanceAfterRepetition();
    void failExecution(const QString& message);

    TestParameters parameters_;
    int currentRepetitionIndex_ = 0;
    int motionState_ = 0;
    int completedMotionCount_ = 0;
    bool tableOpen_ = false;
    bool userStopRequested_ = false;
};
