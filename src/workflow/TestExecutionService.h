#pragma once

#include "../config/TestParameters.h"
#include "../experimentlog/ExperimentLogTypes.h"

#include <QObject>

#include <optional>

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
    void executionStarted(qint64 executionId,
                          const QString& baseExperimentName);
    void executionFinished();
    void executionStopped();
    void executionFailed(const QString& message);
    void experimentFinalized(const ExperimentFinalContext& context);
    void experimentGroupFinalized(const ExperimentGroupFinalContext& context);

private:
    TestExecutionService();
    ~TestExecutionService() override;

    TestExecutionService(const TestExecutionService&) = delete;
    TestExecutionService& operator=(const TestExecutionService&) = delete;

    void handleExperimentTableCreated(int repetitionIndex,
                                      const QString& tableName);
    void handleCollectionStarted();
    void handleMotionStatusChanged(const AcsMotionStatus& status);
    void handleAcquisitionBlock(const AcquisitionBlock& block);
    void handleCollectionStopped();
    void handleExperimentTableFinished(int repetitionIndex,
                                       const QString& tableName,
                                       qint64 sampleCount);
    void prepareRepetition(int repetitionIndex);
    void requestCurrentCollectionStop();
    void tryAdvanceAfterRepetition();
    void finalizeCurrentRepetition(ExperimentTerminalState state,
                                   const QString& reason);
    void finalizeExecutionGroup(ExperimentTerminalState state,
                                const QString& reason);
    void finishPendingTerminalState();
    void resetExecutionContext();
    void failExecution(const QString& message, bool databaseUsable);

    TestParameters parameters_;
    qint64 executionId_ = 0;
    QString baseExperimentName_;
    QString currentRawDataTableName_;
    QString terminalReason_;
    int currentRepetitionIndex_ = 0;
    int motionState_ = 0;
    int completedMotionCount_ = 0;
    int finalizedRecordCount_ = 0;
    qint64 currentRawSampleCount_ = 0;
    bool tableOpen_ = false;
    bool collectionStarted_ = false;
    bool userStopRequested_ = false;
    std::optional<ExperimentTerminalState> pendingTerminalState_;
};
