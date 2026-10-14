#pragma once

#include "../config/TestParameters.h"
#include "../experimentlog/ExperimentLogTypes.h"
#include "../machine/MachineStateManager.h"
#include "../result/TestResultService.h"

#include <QMetaType>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

struct AcquisitionBlock;

class TestExecutionService final : public QObject
{
    Q_OBJECT

public:
    static TestExecutionService& instance();

    void shutdown();
    [[nodiscard]] bool canStart(QString* errorMessage = nullptr) const;
    [[nodiscard]] bool start(const TestResultTargets& resultTargets,
                             QString* errorMessage = nullptr);
    [[nodiscard]] bool stop(QString* errorMessage = nullptr);
    [[nodiscard]] bool reset(QString* errorMessage = nullptr);

signals:
    void executionStarted(qint64 executionId,
                          const QString& baseExperimentName);
    void executionFinished();
    void executionStopped();
    void executionFailed(const QString& message);
    void chartSamplesReady(const QVector<QPointF>& samples);
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
    void handleMotionStateChanged(int state, int errorCode);
    void handleRecordProcessingEntered(int state);
    void handleCompletedRecordCountChanged(int completedCount);
    void handleMotionStartRequestWritten();
    void handleAcquisitionBlock(const AcquisitionBlock& block);
    void handleCollectionStopped();
    void handleMachineStateChanged(MachineState state,
                                   const QString& reason);
    void handleControllerResetCompleted();
    void handleControllerResetFailed(const QString& message);
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
    void tryFinishPendingTerminalState(bool databaseUsable);
    void finishPendingTerminalState();
    void resetExecutionContext();
    void failExecution(const QString& message,
                       bool databaseUsable,
                       bool requestMotionStop = true);
    [[nodiscard]] bool hasActiveExecution() const;
    [[nodiscard]] bool validateStartPrerequisites(
        QString* errorMessage) const;

    TestParameters parameters_;
    qint64 executionId_ = 0;
    QString baseExperimentName_;
    QString currentRawDataTableName_;
    QString terminalReason_;
    int currentRepetitionIndex_ = 0;
    int motionState_ = 0;
    int completedRecordCount_ = 0;
    int finalizedRecordCount_ = 0;
    qint64 currentRawSampleCount_ = 0;
    bool tableOpen_ = false;
    bool currentRecordDataFinished_ = false;
    bool collectionStarted_ = false;
    bool motionStartRequested_ = false;
    bool motionStarted_ = false;
    bool userStopRequested_ = false;
    bool controllerConnected_ = false;
    bool acquisitionReady_ = false;
    bool databaseReady_ = false;
    QString controllerStatusMessage_;
    QString acquisitionStatusMessage_;
    QString databaseStatusMessage_;
    std::optional<TestResultTargets> pendingResultTargets_;
    std::optional<ExperimentTerminalState> pendingTerminalState_;
};
