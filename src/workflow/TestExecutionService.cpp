#include "TestExecutionService.h"

#include "../acquisition/DataAcquisitionService.h"
#include "../database/AcquisitionDatabaseService.h"
#include "../logging/AppLogger.h"
#include "../motion/MotionControlService.h"

#include <QDateTime>

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool isActiveMotionState(int state)
{
    return state == 10 || state == 20 || state == 30
           || state == 40 || state == 50 || state == 55
           || state == 60 || state == 70 || state == 75;
}

bool isFaultMotionState(int state)
{
    return state == -1 || state == -4;
}

QString machineStateText(MachineState state)
{
    switch (state) {
    case MachineState::Error:
        return QStringLiteral("错误");
    case MachineState::Idle:
        return QStringLiteral("空闲");
    case MachineState::Running:
        return QStringLiteral("运行中");
    }

    return QStringLiteral("未知");
}

constexpr int kRecordsPerCycle = 2;

int plannedRecordCount(const TestParameters& parameters)
{
    return parameters.repeatCount * kRecordsPerCycle;
}

int cycleIndexForRecord(int recordIndex)
{
    return (recordIndex + kRecordsPerCycle - 1) / kRecordsPerCycle;
}

ExperimentMotionDirection directionForRecord(int recordIndex)
{
    return recordIndex % kRecordsPerCycle == 1
               ? ExperimentMotionDirection::Forward
               : ExperimentMotionDirection::Reverse;
}

qint64 nextExecutionId()
{
    static qint64 previousExecutionId = 0;
    qint64 executionId = QDateTime::currentMSecsSinceEpoch();
    if (executionId <= previousExecutionId) {
        executionId = previousExecutionId + 1;
    }
    previousExecutionId = executionId;
    return executionId;
}

} // namespace

TestExecutionService& TestExecutionService::instance()
{
    static TestExecutionService service;
    return service;
}

TestExecutionService::TestExecutionService()
{
    qRegisterMetaType<ExperimentFinalContext>("ExperimentFinalContext");
    qRegisterMetaType<ExperimentGroupFinalContext>(
        "ExperimentGroupFinalContext");
    qRegisterMetaType<MachineState>("MachineState");

    MotionControlService& motionService = MotionControlService::instance();
    DataAcquisitionService& acquisitionService =
        DataAcquisitionService::instance();
    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();

    connect(&motionService,
            &MotionControlService::connectionChanged,
            this,
            [this](bool connected, const QString& message) {
                controllerConnected_ = connected;
                controllerStatusMessage_ = message;
                if (!connected && hasActiveExecution()) {
                    failExecution(
                        message.isEmpty()
                            ? QStringLiteral("测试运行期间 ACS 控制器连接断开。")
                            : message,
                        true);
                }
            });
    connect(&motionService,
            &MotionControlService::machineModeChanged,
            this,
            [this](MachineMode mode) {
                if (mode != MachineMode::Automatic && hasActiveExecution()) {
                    failExecution(
                        QStringLiteral("测试运行期间机器退出自动模式。"),
                        true);
                }
            });
    connect(&acquisitionService,
            &DataAcquisitionService::readinessChanged,
            this,
            [this](bool ready, const QString& message) {
                acquisitionReady_ = ready;
                acquisitionStatusMessage_ = message;
            });
    connect(&databaseService,
            &AcquisitionDatabaseService::readinessChanged,
            this,
            [this](bool ready, const QString& message) {
                databaseReady_ = ready;
                databaseStatusMessage_ = message;
            });

    connect(&databaseService,
            &AcquisitionDatabaseService::experimentTableCreated,
            this,
            &TestExecutionService::handleExperimentTableCreated);
    connect(&acquisitionService,
            &DataAcquisitionService::collectionStarted,
            this,
            &TestExecutionService::handleCollectionStarted);
    connect(&motionService,
            &MotionControlService::motionStateChanged,
            this,
            &TestExecutionService::handleMotionStateChanged);
    connect(&motionService,
            &MotionControlService::recordProcessingEntered,
            this,
            &TestExecutionService::handleRecordProcessingEntered);
    connect(&motionService,
            &MotionControlService::completedRecordCountChanged,
            this,
            &TestExecutionService::handleCompletedRecordCountChanged);
    connect(&motionService,
            &MotionControlService::startRequestWritten,
            this,
            &TestExecutionService::handleMotionStartRequestWritten);
    connect(&acquisitionService,
            &DataAcquisitionService::blockReady,
            this,
            &TestExecutionService::handleAcquisitionBlock);
    connect(&acquisitionService,
            &DataAcquisitionService::collectionStopped,
            this,
            &TestExecutionService::handleCollectionStopped);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentTableFinished,
            this,
            &TestExecutionService::handleExperimentTableFinished);
    connect(&databaseService,
            &AcquisitionDatabaseService::databaseFailed,
            this,
            [this](const QString& message) {
                failExecution(message, false);
            });
    connect(&acquisitionService,
            &DataAcquisitionService::collectionFailed,
            this,
            [this](const QString& message) {
                failExecution(message, true);
            });
    connect(&motionService,
            &MotionControlService::commandFailed,
            this,
            [this](const QString& message) {
                failExecution(message, true);
            });
    connect(&motionService,
            &MotionControlService::emergencyStopCompleted,
            this,
            [this] {
                failExecution(QStringLiteral("软件急停已触发。"), true);
            });
    connect(&motionService,
            &MotionControlService::emergencyStopFailed,
            this,
            [this](const QString& message) {
                failExecution(message, true);
            });
}

TestExecutionService::~TestExecutionService()
{
    shutdown();
}

MachineState TestExecutionService::machineState() const
{
    return machineState_;
}

QString TestExecutionService::machineStateReason() const
{
    return machineStateReason_;
}

void TestExecutionService::shutdown()
{
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    userStopRequested_ = true;
    QString ignoredError;
    if (!DataAcquisitionService::instance().stopCollection(&ignoredError)) {
        qCWarning(logAcquisition).noquote()
            << "退出时停止采集失败：" << ignoredError;
    }
    if (isActiveMotionState(motionState_)) {
        ignoredError.clear();
        if (!MotionControlService::instance().stop(&ignoredError)) {
            qCWarning(logMotion).noquote()
                << "退出时停止运动失败：" << ignoredError;
        }
    }
    currentRepetitionIndex_ = 0;
    tableOpen_ = false;
    resetExecutionContext();
}

bool TestExecutionService::start(const TestParameters& parameters,
                                 QString* errorMessage)
{
    if (!validateStartPrerequisites(errorMessage)) {
        return false;
    }
    if (!validateTestParameters(parameters, errorMessage)) {
        return false;
    }

    parameters_ = parameters;
    executionId_ = nextExecutionId();
    baseExperimentName_ = experimentBaseName(parameters_)
                          + QLatin1Char('-')
                          + QDateTime::currentDateTime().toString(
                              QStringLiteral("yyyyMMdd_HHmmss"));
    completedRecordCount_ = 0;
    motionStartRequested_ = false;
    motionStarted_ = false;
    finalizedRecordCount_ = 0;
    tableOpen_ = false;
    userStopRequested_ = false;
    currentRecordDataFinished_ = false;
    pendingTerminalState_.reset();
    terminalReason_.clear();
    setMachineState(MachineState::Running, QStringLiteral("测试正在运行"));
    emit executionStarted(executionId_, baseExperimentName_);
    prepareRepetition(1);
    return currentRepetitionIndex_ != 0;
}

bool TestExecutionService::stop(QString* errorMessage)
{
    if (!hasActiveExecution()) {
        setError(errorMessage, QStringLiteral("当前没有正在执行的测试。"));
        return false;
    }
    if (pendingTerminalState_.has_value()) {
        setError(errorMessage, QStringLiteral("测试正在执行终止收尾。"));
        return false;
    }

    userStopRequested_ = true;
    pendingTerminalState_ = ExperimentTerminalState::Terminated;
    terminalReason_ = QStringLiteral("用户停止");
    QString acquisitionError;
    const bool acquisitionAccepted =
        DataAcquisitionService::instance().stopCollection(&acquisitionError);
    QString motionError;
    const bool motionAccepted =
        !isActiveMotionState(motionState_)
        || MotionControlService::instance().stop(&motionError);
    if (!acquisitionAccepted || !motionAccepted) {
        const QString failureMessage =
            !motionAccepted ? motionError : acquisitionError;
        setError(errorMessage, failureMessage);
        failExecution(failureMessage, true);
        return false;
    }
    setMachineState(MachineState::Running, QStringLiteral("正在停止测试"));
    return true;
}

void TestExecutionService::handleExperimentTableCreated(
    int repetitionIndex,
    const QString& tableName)
{
    // 建表结果来自数据库线程，返回时流程可能已停止或故障。
    if (currentRepetitionIndex_ != 0
        && repetitionIndex != currentRepetitionIndex_) {
        failExecution(QStringLiteral("数据库返回了非当前实验的数据表。"),
                      true);
        return;
    }

    tableOpen_ = true;
    currentRawDataTableName_ = tableName;
    // 过期建表结果不得再启动采集，但已创建的表仍需正常收尾。
    if (currentRepetitionIndex_ == 0 || userStopRequested_
        || pendingTerminalState_.has_value()) {
        handleCollectionStopped();
        return;
    }

    QString errorMessage;
    if (!DataAcquisitionService::instance().startCollection(&errorMessage)) {
        failExecution(errorMessage, true);
        return;
    }
}

void TestExecutionService::handleCollectionStarted()
{
    if (currentRepetitionIndex_ == 0 || userStopRequested_
        || pendingTerminalState_.has_value()) {
        QString ignoredError;
        if (!DataAcquisitionService::instance().stopCollection(&ignoredError)) {
            qCWarning(logAcquisition).noquote()
                << "过期采集启动结果的停止请求失败：" << ignoredError;
        }
        return;
    }

    collectionStarted_ = true;

    // 运动程序包含全部正反向记录，后续记录只需重新启动采集。
    if (currentRepetitionIndex_ > 1 || motionStartRequested_ || motionStarted_) {
        return;
    }

    QString errorMessage;
    motionStartRequested_ = true;
    if (!MotionControlService::instance().start(parameters_, &errorMessage)) {
        motionStartRequested_ = false;
        failExecution(errorMessage, true);
    }
}

void TestExecutionService::handleMotionStartRequestWritten()
{
    if (!motionStartRequested_ || !hasActiveExecution()
        || userStopRequested_ || pendingTerminalState_.has_value()) {
        return;
    }

    motionStartRequested_ = false;
    motionStarted_ = true;
    completedRecordCount_ = 0;
    qCInfo(logMotion)
        << "[流程层][记录推进] 本次启动请求已写入，启用完成计数处理"
        << "executionId=" << executionId_;
}

void TestExecutionService::handleMotionStateChanged(int state, int errorCode)
{
    const int previousState = motionState_;
    motionState_ = state;
    qCInfo(logMotion)
        << "[流程层][运动状态] 状态变化"
        << "executionId=" << executionId_
        << "previousState=" << previousState
        << "state=" << state
        << "errorCode=" << errorCode;

    if (!hasActiveExecution()) {
        return;
    }
    if (state < 0 && !userStopRequested_) {
        failExecution(
            QStringLiteral("运动程序进入故障状态 %1，错误码 %2。")
                .arg(state)
                .arg(errorCode),
            true);
    }
}

void TestExecutionService::handleRecordProcessingEntered(int state)
{
    if (!hasActiveExecution() || !motionStarted_
        || currentRepetitionIndex_ == 0 || userStopRequested_
        || pendingTerminalState_.has_value()) {
        qCDebug(logMotion)
            << "[流程层][采集边界] 本次测试未启动或正在收尾，忽略处理边界"
            << "state=" << state;
        return;
    }

    const AcquisitionState acquisitionState =
        DataAcquisitionService::instance().state();
    qCInfo(logMotion)
        << "[流程层][采集边界] 进入记录处理状态"
        << "executionId=" << executionId_
        << "repetition=" << currentRepetitionIndex_
        << "state=" << state
        << "acquisitionState=" << static_cast<int>(acquisitionState)
        << "table=" << currentRawDataTableName_;
    if (acquisitionState == AcquisitionState::Starting
        || acquisitionState == AcquisitionState::Collecting) {
        requestCurrentCollectionStop();
    }
}

void TestExecutionService::handleCompletedRecordCountChanged(int completedCount)
{
    // 建表、首次采集启动期间可能仍读到上次测试的计数，不能写入本次上下文。
    if (!hasActiveExecution() || !motionStarted_
        || userStopRequested_ || pendingTerminalState_.has_value()) {
        qCDebug(logMotion)
            << "[流程层][记录推进] 本次测试未启动或正在收尾，忽略完成计数"
            << "executionId=" << executionId_
            << "completedCount=" << completedCount;
        return;
    }
    if (completedCount <= completedRecordCount_) {
        qCDebug(logMotion)
            << "[流程层][记录推进] 忽略清零、重复或回退的计数"
            << "completedCount=" << completedCount
            << "acceptedCount=" << completedRecordCount_;
        return;
    }

    completedRecordCount_ = completedCount;
    qCInfo(logMotion)
        << "[流程层][记录推进] 接受本次测试完成计数"
        << "executionId=" << executionId_
        << "repetition=" << currentRepetitionIndex_
        << "completedCount=" << completedRecordCount_
        << "tableOpen=" << tableOpen_;
    tryAdvanceAfterRepetition();
}

void TestExecutionService::handleAcquisitionBlock(
    const AcquisitionBlock& block)
{
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    QString errorMessage;
    if (!AcquisitionDatabaseService::instance().appendBlock(
            block, &errorMessage)) {
        failExecution(errorMessage, true);
    }
}

void TestExecutionService::handleCollectionStopped()
{
    if (tableOpen_) {
        // 采集块和收尾命令进入同一数据库线程队列，
        // 因此收尾一定排在已发送的数据块之后。
        QString errorMessage;
        if (!AcquisitionDatabaseService::instance().finishExperimentTable(
                &errorMessage)) {
            if (currentRepetitionIndex_ != 0) {
                failExecution(errorMessage, true);
            } else {
                qCWarning(logDatabase).noquote()
                    << "流程结束后收尾实验表失败：" << errorMessage;
            }
        }
        return;
    }

    if (pendingTerminalState_.has_value() && hasActiveExecution()) {
        finishPendingTerminalState();
    }
}

void TestExecutionService::handleExperimentTableFinished(
    int repetitionIndex,
    const QString& tableName,
    qint64 sampleCount)
{
    // 流程已完成、停止或故障时，忽略迟到的收尾通知。
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    // 异步收尾结果必须属于当前轮次，防止数据表串入其他实验。
    if (repetitionIndex != currentRepetitionIndex_) {
        failExecution(QStringLiteral("数据库结束了非当前实验的数据表。"),
                      true);
        return;
    }
    if (!currentRawDataTableName_.isEmpty()
        && tableName != currentRawDataTableName_) {
        failExecution(QStringLiteral("数据库结束的数据表名称与当前实验不一致。"),
                      true);
        return;
    }
    currentRawDataTableName_ = tableName;
    currentRawSampleCount_ = sampleCount;
    // 数据库收尾与完成计数分别到达，后到的条件负责尝试推进。
    tableOpen_ = false;
    currentRecordDataFinished_ = true;

    if (pendingTerminalState_.has_value()) {
        finishPendingTerminalState();
        return;
    }

    tryAdvanceAfterRepetition();
}

void TestExecutionService::prepareRepetition(int repetitionIndex)
{
    currentRepetitionIndex_ = repetitionIndex;
    currentRawDataTableName_.clear();
    currentRawSampleCount_ = 0;
    collectionStarted_ = false;
    currentRecordDataFinished_ = false;

    QString errorMessage;
    if (!AcquisitionDatabaseService::instance().beginExperimentTable(
            parameters_.motorModel,
            parameters_.specimenId,
            repetitionIndex,
            cycleIndexForRecord(repetitionIndex),
            directionForRecord(repetitionIndex),
            kAcquisitionSamplePeriodSeconds,
            &errorMessage)) {
        failExecution(errorMessage, true);
    }
}

void TestExecutionService::requestCurrentCollectionStop()
{
    QString errorMessage;
    if (!DataAcquisitionService::instance().stopCollection(&errorMessage)) {
        failExecution(errorMessage, true);
    }
}

void TestExecutionService::tryAdvanceAfterRepetition()
{
    if (!hasActiveExecution() || !motionStarted_
        || currentRepetitionIndex_ == 0) {
        return;
    }

    if (userStopRequested_ || pendingTerminalState_.has_value()) {
        if (!tableOpen_) {
            finishPendingTerminalState();
        }
        return;
    }
    // 下位记录计数和数据库收尾是两个独立完成条件，缺一不可进入下一条记录。
    if (completedRecordCount_ < currentRepetitionIndex_
        || !currentRecordDataFinished_ || tableOpen_) {
        return;
    }

    if (currentRepetitionIndex_ >= plannedRecordCount(parameters_)) {
        finalizeCurrentRepetition(ExperimentTerminalState::Completed, {});
        finalizeExecutionGroup(ExperimentTerminalState::Completed, {});
        resetExecutionContext();
        setMachineState(MachineState::Idle, QStringLiteral("测试已完成"));
        emit executionFinished();
        return;
    }

    finalizeCurrentRepetition(ExperimentTerminalState::Completed, {});
    prepareRepetition(currentRepetitionIndex_ + 1);
}

void TestExecutionService::finalizeCurrentRepetition(
    ExperimentTerminalState state,
    const QString& reason)
{
    if (executionId_ == 0 || currentRepetitionIndex_ <= 0) {
        qCWarning(logCompletionReceipt)
            << "[流程层][单次完成信号] 跳过：完成上下文无效"
            << "executionId=" << executionId_
            << "repetition=" << currentRepetitionIndex_;
        return;
    }
    if (!collectionStarted_ && currentRawSampleCount_ <= 0) {
        qCWarning(logCompletionReceipt)
            << "[流程层][单次完成信号] 跳过：采集未启动且没有原始样本"
            << "executionId=" << executionId_
            << "repetition=" << currentRepetitionIndex_
            << "plannedRepeatCount=" << parameters_.repeatCount
            << "table=" << currentRawDataTableName_;
        return;
    }

    ExperimentFinalContext context;
    context.executionId = executionId_;
    context.repetitionIndex = currentRepetitionIndex_;
    context.plannedRepeatCount = parameters_.repeatCount;
    context.cycleIndex = cycleIndexForRecord(currentRepetitionIndex_);
    context.direction = directionForRecord(currentRepetitionIndex_);
    context.baseExperimentName = baseExperimentName_;
    context.experimentName = baseExperimentName_
                             + QLatin1Char('-')
                             + QString::number(context.cycleIndex)
                             + QLatin1Char('-')
                             + experimentMotionDirectionDisplayText(
                                 context.direction);
    context.finishedAtUtc = QDateTime::currentDateTimeUtc();
    context.state = state;
    context.terminalReason = reason;
    context.parametersSnapshot = parameters_;
    context.rawDataTableName = currentRawDataTableName_;
    context.rawSampleCount = currentRawSampleCount_;
    ++finalizedRecordCount_;
    qCInfo(logCompletionReceipt)
        << "[流程层][单次完成信号] 发送 experimentFinalized"
        << "executionId=" << context.executionId
        << "repetition=" << context.repetitionIndex
        << "plannedRepeatCount=" << context.plannedRepeatCount
        << "state=" << static_cast<int>(context.state)
        << "rawSampleCount=" << context.rawSampleCount
        << "table=" << context.rawDataTableName
        << "finalizedRecordCount=" << finalizedRecordCount_;
    emit experimentFinalized(context);
}

void TestExecutionService::finalizeExecutionGroup(
    ExperimentTerminalState state,
    const QString& reason)
{
    if (executionId_ == 0) {
        return;
    }

    ExperimentGroupFinalContext context;
    context.executionId = executionId_;
    context.baseExperimentName = baseExperimentName_;
    context.finishedAtUtc = QDateTime::currentDateTimeUtc();
    context.state = state;
    context.terminalReason = reason;
    context.plannedRepeatCount = parameters_.repeatCount;
    context.finalizedRecordCount = finalizedRecordCount_;
    qCInfo(logCompletionReceipt)
        << "[流程层][整组完成信号] 发送 experimentGroupFinalized"
        << "executionId=" << context.executionId
        << "plannedRepeatCount=" << context.plannedRepeatCount
        << "finalizedRecordCount=" << context.finalizedRecordCount
        << "state=" << static_cast<int>(context.state);
    emit experimentGroupFinalized(context);
}

void TestExecutionService::tryFinishPendingTerminalState(bool databaseUsable)
{
    if (!pendingTerminalState_.has_value()) {
        return;
    }
    if (!databaseUsable) {
        tableOpen_ = false;
        finishPendingTerminalState();
        return;
    }

    const AcquisitionState acquisitionState =
        DataAcquisitionService::instance().state();
    const bool acquisitionFinished = acquisitionState == AcquisitionState::Idle
                                     || acquisitionState == AcquisitionState::Fault;
    if (!acquisitionFinished) {
        return;
    }
    if (!tableOpen_) {
        finishPendingTerminalState();
        return;
    }

    QString finishError;
    if (!AcquisitionDatabaseService::instance().finishExperimentTable(
            &finishError)) {
        if (!terminalReason_.isEmpty()) {
            terminalReason_ += QStringLiteral("；");
        }
        terminalReason_ += finishError;
        tableOpen_ = false;
        finishPendingTerminalState();
    }
}

void TestExecutionService::finishPendingTerminalState()
{
    if (!pendingTerminalState_.has_value()) {
        return;
    }

    const ExperimentTerminalState state = *pendingTerminalState_;
    const QString reason = terminalReason_;
    finalizeCurrentRepetition(state, reason);
    finalizeExecutionGroup(state, reason);
    resetExecutionContext();

    if (state == ExperimentTerminalState::Terminated) {
        setMachineState(MachineState::Idle, QStringLiteral("测试已停止"));
        emit executionStopped();
        return;
    }

    setMachineState(MachineState::Error, reason);
}

void TestExecutionService::resetExecutionContext()
{
    parameters_ = {};
    executionId_ = 0;
    baseExperimentName_.clear();
    currentRawDataTableName_.clear();
    terminalReason_.clear();
    currentRepetitionIndex_ = 0;
    completedRecordCount_ = 0;
    finalizedRecordCount_ = 0;
    currentRawSampleCount_ = 0;
    tableOpen_ = false;
    collectionStarted_ = false;
    currentRecordDataFinished_ = false;
    userStopRequested_ = false;
    motionStartRequested_ = false;
    motionStarted_ = false;
    pendingTerminalState_.reset();
}

void TestExecutionService::failExecution(const QString& message,
                                         bool databaseUsable)
{
    if (!hasActiveExecution()) {
        return;
    }

    if (pendingTerminalState_.has_value()) {
        qCWarning(logApplication).noquote()
            << "实验终止清理期间发生附加错误：" << message;
        const bool failureAlreadyReported =
            *pendingTerminalState_ == ExperimentTerminalState::Fault;
        pendingTerminalState_ = ExperimentTerminalState::Fault;
        if (!terminalReason_.isEmpty()) {
            terminalReason_ += QStringLiteral("；");
        }
        terminalReason_ += message;
        if (!failureAlreadyReported) {
            emit executionFailed(message);
        }
        setMachineState(MachineState::Error, terminalReason_);
        tryFinishPendingTerminalState(databaseUsable);
        return;
    }

    pendingTerminalState_ = ExperimentTerminalState::Fault;
    terminalReason_ = message;
    qCCritical(logApplication).noquote() << "测试流程失败：" << message;
    setMachineState(MachineState::Error, message);
    emit executionFailed(message);

    QString ignoredError;
    if (DataAcquisitionService::instance().state() != AcquisitionState::Idle) {
        if (!DataAcquisitionService::instance().stopCollection(&ignoredError)) {
            qCWarning(logAcquisition).noquote()
                << "流程故障后停止采集失败：" << ignoredError;
        }
    }
    if (isActiveMotionState(motionState_)) {
        ignoredError.clear();
        if (!MotionControlService::instance().stop(&ignoredError)) {
            qCWarning(logMotion).noquote()
                << "流程故障后停止运动失败：" << ignoredError;
        }
    }

    tryFinishPendingTerminalState(databaseUsable);
}

bool TestExecutionService::hasActiveExecution() const
{
    return executionId_ != 0;
}

bool TestExecutionService::validateStartPrerequisites(
    QString* errorMessage) const
{
    if (hasActiveExecution()) {
        setError(errorMessage, QStringLiteral("已有测试流程正在执行或收尾。"));
        return false;
    }
    if (MotionControlService::instance().machineMode()
        != MachineMode::Automatic) {
        setError(errorMessage,
                 QStringLiteral("机器当前处于维修模式，不能启动自动测试。"));
        return false;
    }
    if (!controllerConnected_) {
        setError(errorMessage,
                 controllerStatusMessage_.isEmpty()
                     ? QStringLiteral("ACS 控制器未连接。")
                     : controllerStatusMessage_);
        return false;
    }
    if (MotionControlService::instance().homeRunning()) {
        setError(errorMessage,
                 QStringLiteral("机器正在回零，不能启动测试。"));
        return false;
    }
    if (!MotionControlService::instance().homeDone()) {
        setError(errorMessage,
                 QStringLiteral("机器尚未完成回零，请先在维修模式下执行回零。"));
        return false;
    }
    if (isFaultMotionState(motionState_)) {
        setError(errorMessage,
                 QStringLiteral("运动控制器处于故障状态 %1。")
                     .arg(motionState_));
        return false;
    }
    if (!acquisitionReady_) {
        setError(errorMessage,
                 acquisitionStatusMessage_.isEmpty()
                     ? QStringLiteral("数据采集服务未就绪。")
                     : acquisitionStatusMessage_);
        return false;
    }
    if (!databaseReady_) {
        setError(errorMessage,
                 databaseStatusMessage_.isEmpty()
                     ? QStringLiteral("PostgreSQL 数据库未就绪。")
                     : databaseStatusMessage_);
        return false;
    }
    if (isActiveMotionState(motionState_)) {
        setError(errorMessage,
                 QStringLiteral("控制器程序正在运行，不能启动测试。"));
        return false;
    }

    return true;
}

void TestExecutionService::setMachineState(MachineState state,
                                           const QString& reason)
{
    if (machineState_ == state && machineStateReason_ == reason) {
        return;
    }

    machineState_ = state;
    machineStateReason_ = reason;
    qCInfo(logApplication).noquote()
        << "机器状态迁移：" << machineStateText(machineState_)
        << "-" << machineStateReason_;
    emit machineStateChanged(machineState_, machineStateReason_);
}
