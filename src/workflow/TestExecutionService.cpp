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
           || state == 40 || state == 50 || state == 60;
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

    MotionControlService& motionService = MotionControlService::instance();
    DataAcquisitionService& acquisitionService =
        DataAcquisitionService::instance();
    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();

    connect(&databaseService,
            &AcquisitionDatabaseService::experimentTableCreated,
            this,
            &TestExecutionService::handleExperimentTableCreated);
    connect(&acquisitionService,
            &DataAcquisitionService::collectionStarted,
            this,
            &TestExecutionService::handleCollectionStarted);
    connect(&motionService,
            &MotionControlService::motionStatusChanged,
            this,
            &TestExecutionService::handleMotionStatusChanged);
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
}

TestExecutionService::~TestExecutionService()
{
    shutdown();
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
    if (currentRepetitionIndex_ != 0) {
        setError(errorMessage, QStringLiteral("已有测试流程正在执行。"));
        return false;
    }
    if (!validateTestParameters(parameters, errorMessage)) {
        return false;
    }
    if (!AcquisitionDatabaseService::instance().isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (!DataAcquisitionService::instance().isReady()) {
        setError(errorMessage, QStringLiteral("ACS 数据采集服务尚未就绪。"));
        return false;
    }

    parameters_ = parameters;
    executionId_ = nextExecutionId();
    baseExperimentName_ = experimentBaseName(parameters_)
                          + QLatin1Char('-')
                          + QDateTime::currentDateTime().toString(
                              QStringLiteral("yyyyMMdd_HHmmss"));
    completedMotionCount_ = 0;
    finalizedRecordCount_ = 0;
    tableOpen_ = false;
    userStopRequested_ = false;
    pendingTerminalState_.reset();
    terminalReason_.clear();
    emit executionStarted(executionId_, baseExperimentName_);
    prepareRepetition(1);
    return currentRepetitionIndex_ != 0;
}

bool TestExecutionService::stop(QString* errorMessage)
{
    if (currentRepetitionIndex_ == 0) {
        setError(errorMessage, QStringLiteral("当前没有正在执行的测试。"));
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
        setError(errorMessage,
                 !motionAccepted ? motionError : acquisitionError);
        return false;
    }
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
    if (currentRepetitionIndex_ == 0 || userStopRequested_) {
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
    if (currentRepetitionIndex_ == 0 || userStopRequested_) {
        QString ignoredError;
        if (!DataAcquisitionService::instance().stopCollection(&ignoredError)) {
            qCWarning(logAcquisition).noquote()
                << "过期采集启动结果的停止请求失败：" << ignoredError;
        }
        return;
    }

    collectionStarted_ = true;

    // 运动程序包含全部重复次数，后续轮次只需重新启动采集。
    if (currentRepetitionIndex_ > 1) {
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().start(parameters_, &errorMessage)) {
        failExecution(errorMessage, true);
    }
}

void TestExecutionService::handleMotionStatusChanged(
    const AcsMotionStatus& status)
{
    const int previousMotionState = motionState_;
    motionState_ = status.state;
    completedMotionCount_ = status.currentCount;

    if (currentRepetitionIndex_ == 0) {
        return;
    }
    if (status.state < 0 && !userStopRequested_) {
        failExecution(
            QStringLiteral("运动程序进入故障状态 %1，错误码 %2。")
                .arg(status.state)
                .arg(status.errorCode),
            true);
        return;
    }

    if (pendingTerminalState_.has_value()) {
        return;
    }

    // 只在首次进入“返回零点”时停止本轮采集，避免状态轮询重复发送停止命令。
    if (status.state == 60 && previousMotionState != 60) {
        const AcquisitionState acquisitionState =
            DataAcquisitionService::instance().state();
        if (acquisitionState == AcquisitionState::Starting
            || acquisitionState == AcquisitionState::Collecting) {
            requestCurrentCollectionStop();
        }
    }
    if (completedMotionCount_ >= currentRepetitionIndex_) {
        tryAdvanceAfterRepetition();
    }
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

    if (userStopRequested_ && currentRepetitionIndex_ != 0) {
        finishPendingTerminalState();
    }
}

void TestExecutionService::handleExperimentTableFinished(
    int repetitionIndex,
    const QString& tableName,
    qint64 sampleCount)
{
    // 数据库已完成本轮全部排队写入，仅解除写入屏障，等待运动状态推进流程。
    tableOpen_ = false;

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

    QString errorMessage;
    if (!AcquisitionDatabaseService::instance().beginExperimentTable(
            parameters_.motorModel,
            parameters_.specimenId,
            repetitionIndex,
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
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    if (userStopRequested_) {
        if (!tableOpen_) {
            finishPendingTerminalState();
        }
        return;
    }
    // 运动回零和数据库收尾是两个独立完成条件，缺一不可进入下一轮。
    if (completedMotionCount_ < currentRepetitionIndex_ || tableOpen_) {
        return;
    }

    if (currentRepetitionIndex_ >= parameters_.repeatCount) {
        finalizeCurrentRepetition(ExperimentTerminalState::Completed, {});
        finalizeExecutionGroup(ExperimentTerminalState::Completed, {});
        resetExecutionContext();
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
    context.baseExperimentName = baseExperimentName_;
    context.experimentName = baseExperimentName_
                             + QLatin1Char('-')
                             + QString::number(currentRepetitionIndex_);
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
        emit executionStopped();
    }
}

void TestExecutionService::resetExecutionContext()
{
    parameters_ = {};
    executionId_ = 0;
    baseExperimentName_.clear();
    currentRawDataTableName_.clear();
    terminalReason_.clear();
    currentRepetitionIndex_ = 0;
    completedMotionCount_ = 0;
    finalizedRecordCount_ = 0;
    currentRawSampleCount_ = 0;
    tableOpen_ = false;
    collectionStarted_ = false;
    userStopRequested_ = false;
    pendingTerminalState_.reset();
}

void TestExecutionService::failExecution(const QString& message,
                                         bool databaseUsable)
{
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    if (pendingTerminalState_.has_value()) {
        qCWarning(logApplication).noquote()
            << "实验终止清理期间发生附加错误：" << message;
        if (!databaseUsable) {
            const bool failureAlreadyReported =
                *pendingTerminalState_ == ExperimentTerminalState::Fault;
            pendingTerminalState_ = ExperimentTerminalState::Fault;
            if (!terminalReason_.isEmpty()) {
                terminalReason_ += QStringLiteral("；");
            }
            terminalReason_ += message;
            tableOpen_ = false;
            if (!failureAlreadyReported) {
                emit executionFailed(message);
            }
            finishPendingTerminalState();
        }
        return;
    }

    pendingTerminalState_ = ExperimentTerminalState::Fault;
    terminalReason_ = message;
    qCCritical(logApplication).noquote() << "测试流程失败：" << message;
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

    if (!databaseUsable) {
        tableOpen_ = false;
        finishPendingTerminalState();
        return;
    }
    const AcquisitionState acquisitionState =
        DataAcquisitionService::instance().state();
    if (tableOpen_ && acquisitionState == AcquisitionState::Idle) {
        QString finishError;
        if (!AcquisitionDatabaseService::instance().finishExperimentTable(
                &finishError)) {
            terminalReason_ += QStringLiteral("；%1").arg(finishError);
            tableOpen_ = false;
            finishPendingTerminalState();
        }
        return;
    }
    if (!tableOpen_ && acquisitionState == AcquisitionState::Idle) {
        finishPendingTerminalState();
    }
}
