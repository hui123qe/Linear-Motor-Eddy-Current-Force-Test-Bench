#include "TestExecutionService.h"

#include "../acquisition/DataAcquisitionService.h"
#include "../config/TestConfigurationService.h"
#include "../database/AcquisitionDatabaseService.h"
#include "../logging/AppLogger.h"
#include "../machine/MachineStateManager.h"
#include "../motion/MotionControlService.h"
#include "../result/TestResultService.h"
#include "../alarm/AlarmService.h"
#include "../temperature/TemperatureDeviceService.h"

#include <QDateTime>
#include <QMetaObject>
#include <QScopedValueRollback>

#include <algorithm>

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
        return QStringLiteral("故障");
    case MachineState::Idle:
        return QStringLiteral("空闲");
    case MachineState::Running:
        return QStringLiteral("运行");
    case MachineState::Resetting:
        return QStringLiteral("复位中");
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
    MotionControlService& motionService = MotionControlService::instance();
    DataAcquisitionService& acquisitionService =
        DataAcquisitionService::instance();
    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();
    MachineStateManager& machineStateManager = MachineStateManager::instance();

    connect(&motionService,
            &MotionControlService::connectionChanged,
            this,
            [this](bool connected, const QString& message) {
                const bool wasConnected = controllerConnected_;
                controllerConnected_ = connected;
                controllerStatusMessage_ = message;
                if (!connected && (wasConnected || hasActiveExecution())) {
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
            &MotionControlService::zeroPositionReached,
            this,
            [this] {
                if (hasActiveExecution() && !userStopRequested_
                    && !pendingTerminalState_.has_value()) {
                    prepareRepetition(1);
                }
            });
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
            &MotionControlService::controllerResetCompleted,
            this,
            &TestExecutionService::handleControllerResetCompleted);
    connect(&motionService,
            &MotionControlService::controllerResetFailed,
            this,
            &TestExecutionService::handleControllerResetFailed);
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
    connect(&machineStateManager,
            &MachineStateManager::machineStateChanged,
            this,
            &TestExecutionService::handleMachineStateChanged);
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
    if (MotionControlService::instance().hasActiveMotion()) {
        ignoredError.clear();
        if (!MotionControlService::instance().stop(&ignoredError)) {
            qCWarning(logMotion).noquote()
                << "退出时停止运动失败：" << ignoredError;
        }
    }
    currentRepetitionIndex_ = 0;
    tableState_ = ExperimentTableState::None;
    resetExecutionContext();
}

bool TestExecutionService::canStart(QString* errorMessage) const
{
    return validateStartPrerequisites(errorMessage);
}

bool TestExecutionService::start(const TestResultTargets& resultTargets,
                                 QString* errorMessage)
{
    if (!validateStartPrerequisites(errorMessage)) {
        return false;
    }
    if (!validateTestResultTargets(resultTargets, errorMessage)) {
        return false;
    }

    const std::optional<TestParameters> lockedConfiguration =
        TestConfigurationService::instance().lockedConfiguration();
    if (!lockedConfiguration.has_value()) {
        setError(errorMessage,
                 QStringLiteral("请先在参数设置页确定并锁定配置。"));
        return false;
    }
    if (!MachineStateManager::instance().beginExecution(
            QStringLiteral("测试正在运行"), errorMessage)) {
        return false;
    }

    parameters_ = *lockedConfiguration;
    pendingResultTargets_ = resultTargets;
    executionId_ = nextExecutionId();
    baseExperimentName_ = experimentBaseName(parameters_)
                          + QLatin1Char('-')
                          + QDateTime::currentDateTime().toString(
                              QStringLiteral("yyyyMMdd_HHmmss"));
    completedRecordCount_ = 0;
    motionStartRequested_ = false;
    motionStarted_ = false;
    finalizedRecordCount_ = 0;
    tableState_ = ExperimentTableState::None;
    userStopRequested_ = false;
    currentRecordDataFinished_ = false;
    pendingTerminalState_.reset();
    terminalReason_.clear();
    emit executionStarted(executionId_, baseExperimentName_);
    currentRepetitionIndex_ = 1;
    QString motionError;
    if (!MotionControlService::instance().moveToZero(&motionError)) {
        setError(errorMessage, motionError);
        failExecution(motionError, true);
        return false;
    }
    return true;
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

    bool accepted = false;
    {
        const QScopedValueRollback<bool> stopGuard(terminalStopRequestsInProgress_, true);
        userStopRequested_ = true;
        pendingTerminalState_ = ExperimentTerminalState::Terminated;
        terminalReason_ = QStringLiteral("用户停止");
        MachineStateManager::instance().updateRunningReason(
            QStringLiteral("正在停止测试"));
        accepted = requestExecutionStop(true, errorMessage);
    }
    tryFinishPendingTerminalState();
    return accepted;
}
bool TestExecutionService::reset(QString* errorMessage)
{
    if (hasActiveExecution() || pendingTerminalState_.has_value() || MotionControlService::instance().hasActiveMotion()) {
        setError(errorMessage, QStringLiteral("实验收尾或运动尚未结束，不能复位。")); return false;
    }
    MachineStateManager& machineStateManager = MachineStateManager::instance();
    if (!machineStateManager.beginReset(errorMessage)) {
        return false;
    }

    QString resetError;
    if (!MotionControlService::instance().resetController(&resetError)) {
        const QString message =
            resetError.isEmpty() ? QStringLiteral("控制器复位请求失败。")
                                 : resetError;
        machineStateManager.failReset(message);
        setError(errorMessage, message);
        return false;
    }

    return true;
}
void TestExecutionService::handleMachineStateChanged(
    MachineState state,
    const QString& reason)
{
    if (state != MachineState::Error || !hasActiveExecution()
        || pendingTerminalState_.has_value()) {
        return;
    }

    qCCritical(logSafety).noquote()
        << "机器进入故障状态，测试流程开始收尾：" << reason;
    // Hardware safety or the alarm response owns motion termination; this path closes the experiment.
    failExecution(reason, true, false);
}

void TestExecutionService::handleControllerResetCompleted()
{
    MachineStateManager::instance().completeReset();
}

void TestExecutionService::handleControllerResetFailed(
    const QString& message)
{
    MachineStateManager::instance().failReset(message);
}

void TestExecutionService::handleExperimentTableCreated(
    int repetitionIndex,
    const QString& tableName)
{
    if (!hasActiveExecution() || tableState_ != ExperimentTableState::Creating) {
        qCWarning(logDatabase) << "忽略非等待建表状态的回执：" << tableName;
        return;
    }
    if (repetitionIndex != currentRepetitionIndex_) {
        failExecution(QStringLiteral("数据库返回了非当前实验的数据表。"),
                      false);
        return;
    }

    tableState_ = ExperimentTableState::Open;
    currentRawDataTableName_ = tableName;
    if (pendingTerminalState_.has_value()) {
        tryFinishPendingTerminalState();
        return;
    }
    if (userStopRequested_) {
        requestExperimentTableFinish();
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

    if (pendingResultTargets_.has_value()) {
        QString errorMessage;
        if (!TestResultService::instance().beginTest(
                *pendingResultTargets_, &errorMessage)) {
            pendingResultTargets_.reset();
            failExecution(errorMessage, true);
            return;
        }
        pendingResultTargets_.reset();
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

    if (state == -2 && errorCode == 2001) {
        qCInfo(logMotion)
            << "[流程层][运动状态] ACS 已确认正常停止，不升级为机器故障"
            << "executionId=" << executionId_
            << "userStopRequested=" << userStopRequested_;
        return;
    }
    if (state < 0) {
        const QString message =
            QStringLiteral("运动程序进入故障状态 %1，错误码 %2。")
                .arg(state)
                .arg(errorCode);
        failExecution(message, true);
        return;
    }
    if (!hasActiveExecution()) {
        return;
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
        << "tableState=" << static_cast<int>(tableState_);
    tryAdvanceAfterRepetition();
}

void TestExecutionService::handleAcquisitionBlock(
    const AcquisitionBlock& block)
{
    if (currentRepetitionIndex_ == 0 || tableState_ == ExperimentTableState::Failed) {
        return;
    }

    QString errorMessage;
    if (!AcquisitionDatabaseService::instance().appendBlock(
            block, &errorMessage)) {
        failExecution(errorMessage, true);
        return;
    }

    const TestMotionParameters motionParameters =
        selectedTestMotionParameters(parameters_);
    const double minimumPosition =
        std::min(motionParameters.acquisitionStartMeters,
                 motionParameters.acquisitionEndMeters);
    const double maximumPosition =
        std::max(motionParameters.acquisitionStartMeters,
                 motionParameters.acquisitionEndMeters);
    const qsizetype sampleCount =
        std::min(block.positionMeters.size(), block.forceNewtons.size());
    QVector<QPointF> samples;
    samples.reserve(sampleCount);
    for (qsizetype index = 0; index < sampleCount; ++index) {
        const double positionMeters = block.positionMeters.at(index);
        if (positionMeters >= minimumPosition
            && positionMeters <= maximumPosition) {
            samples.append(
                QPointF(positionMeters, block.forceNewtons.at(index)));
        }
    }
    if (!samples.isEmpty()) {
        emit chartSamplesReady(samples);
    }
}

void TestExecutionService::handleCollectionStopped()
{
    if (pendingTerminalState_.has_value()) {
        tryFinishPendingTerminalState();
        return;
    }

    requestExperimentTableFinish();
    // 提交失败也会登记终止状态，由同一入口完成故障收尾。
    tryFinishPendingTerminalState();
}

void TestExecutionService::requestExperimentTableFinish()
{
    if (tableState_ != ExperimentTableState::Open) {
        return;
    }

    // 先登记在途状态；结束请求与采集块仍进入同一数据库线程队列。
    tableState_ = ExperimentTableState::Finishing;
    qCInfo(logDatabase) << "[流程层][实验表收尾] 提交结束请求"
                       << "executionId=" << executionId_
                       << "repetition=" << currentRepetitionIndex_
                       << "table=" << currentRawDataTableName_;
    QString errorMessage;
    if (AcquisitionDatabaseService::instance().finishExperimentTable(&errorMessage)) {
        return;
    }

    tableState_ = ExperimentTableState::Failed;
    const bool alreadyTerminating = pendingTerminalState_.has_value();
    const QScopedValueRollback<bool> stopGuard(terminalStopRequestsInProgress_, true);
    recordExecutionFailure(QStringLiteral("实验表收尾失败：%1").arg(errorMessage));
    if (!alreadyTerminating) {
        static_cast<void>(requestExecutionStop(true));
    }
}

void TestExecutionService::handleExperimentTableFinished(
    int repetitionIndex,
    const QString& tableName,
    qint64 sampleCount)
{
    // 流程已完成、停止或故障时，忽略迟到的收尾通知。
    if (!hasActiveExecution() || tableState_ != ExperimentTableState::Finishing) {
        return;
    }

    // 异步收尾结果必须属于当前轮次，防止数据表串入其他实验。
    if (repetitionIndex != currentRepetitionIndex_) {
        failExecution(QStringLiteral("数据库结束了非当前实验的数据表。"),
                      false);
        return;
    }
    if (!currentRawDataTableName_.isEmpty()
        && tableName != currentRawDataTableName_) {
        failExecution(QStringLiteral("数据库结束的数据表名称与当前实验不一致。"),
                      false);
        return;
    }
    currentRawDataTableName_ = tableName;
    currentRawSampleCount_ = sampleCount;
    // 数据库收尾与完成计数分别到达，后到的条件负责尝试推进。
    tableState_ = ExperimentTableState::None;
    currentRecordDataFinished_ = true;

    if (pendingTerminalState_.has_value()) {
        tryFinishPendingTerminalState();
        return;
    }

    tryAdvanceAfterRepetition();
}

void TestExecutionService::prepareRepetition(int repetitionIndex)
{
    if (tableState_ != ExperimentTableState::None) {
        return;
    }
    tableState_ = ExperimentTableState::Creating;
    const QString name = baseExperimentName_ + QLatin1Char('-') + QString::number(cycleIndexForRecord(repetitionIndex))
        + QLatin1Char('-') + experimentMotionDirectionDisplayText(directionForRecord(repetitionIndex));
    TemperatureDeviceService::instance().beginExperimentRecording(name);
    AlarmService::instance().setExperimentName(name);
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
        failExecution(errorMessage, false);
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
        tryFinishPendingTerminalState();
        return;
    }
    // 下位记录计数和数据库收尾是两个独立完成条件，缺一不可进入下一条记录。
    if (completedRecordCount_ < currentRepetitionIndex_
        || !currentRecordDataFinished_ || tableState_ != ExperimentTableState::None) {
        return;
    }

    if (currentRepetitionIndex_ >= plannedRecordCount(parameters_)) {
        finalizeCurrentRepetition(ExperimentTerminalState::Completed, {});
        finalizeExecutionGroup(ExperimentTerminalState::Completed, {});
        resetExecutionContext();
        MachineStateManager::instance().completeExecution(
            QStringLiteral("测试已完成"));
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
    TemperatureDeviceService::instance().finishExperimentRecording();
    AlarmService::instance().setExperimentName({});
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

void TestExecutionService::tryFinishPendingTerminalState()
{
    if (!hasActiveExecution() || !pendingTerminalState_.has_value()
        || terminalStopRequestsInProgress_ || finalizingExecution_) {
        return;
    }
    // 数据库失败时保持原有降级终止策略，但必须先发出停止请求。
    if (tableState_ == ExperimentTableState::Failed) {
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
    if (tableState_ == ExperimentTableState::Creating
        || tableState_ == ExperimentTableState::Finishing) {
        return;
    }

    requestExperimentTableFinish();
    if (tableState_ == ExperimentTableState::None
        || tableState_ == ExperimentTableState::Failed) {
        finishPendingTerminalState();
    }
}

void TestExecutionService::finishPendingTerminalState()
{
    if (!hasActiveExecution() || !pendingTerminalState_.has_value()
        || finalizingExecution_) {
        return;
    }

    const QScopedValueRollback<bool> finalizeGuard(finalizingExecution_, true);
    const ExperimentTerminalState state = *pendingTerminalState_;
    const QString reason = terminalReason_;
    finalizeCurrentRepetition(state, reason);
    finalizeExecutionGroup(state, reason);
    resetExecutionContext();

    if (state == ExperimentTerminalState::Terminated) {
        MachineStateManager::instance().completeExecution(
            QStringLiteral("测试已停止"));
        emit executionStopped();
        return;
    }

    MachineStateManager::instance().enterFault(reason);
}

void TestExecutionService::resetExecutionContext()
{
    TemperatureDeviceService::instance().finishExperimentRecording();
    AlarmService::instance().setExperimentName({});
    parameters_ = {};
    executionId_ = 0;
    baseExperimentName_.clear();
    currentRawDataTableName_.clear();
    terminalReason_.clear();
    currentRepetitionIndex_ = 0;
    completedRecordCount_ = 0;
    finalizedRecordCount_ = 0;
    currentRawSampleCount_ = 0;
    tableState_ = ExperimentTableState::None;
    collectionStarted_ = false;
    currentRecordDataFinished_ = false;
    userStopRequested_ = false;
    motionStartRequested_ = false;
    motionStarted_ = false;
    pendingResultTargets_.reset();
    pendingTerminalState_.reset();
}

void TestExecutionService::failExecution(const QString& message,
                                         bool databaseUsable,
                                         bool requestMotionStop)
{
    if (finalizingExecution_) {
        // 已发布的结果快照不再改写，迟到故障仍立即锁存到机器状态。
        qCCritical(logApplication).noquote()
            << "最终结果发布期间发生附加故障：" << message;
        MachineStateManager::instance().enterFault(message);
        return;
    }
    if (!hasActiveExecution()) {
        qCCritical(logApplication).noquote()
            << "机器故障已锁存，等待复位：" << message;
        MachineStateManager::instance().enterFault(message);
        return;
    }

    const bool alreadyTerminating = pendingTerminalState_.has_value();
    {
        const QScopedValueRollback<bool> stopGuard(terminalStopRequestsInProgress_, true);
        if (!databaseUsable) {
            tableState_ = ExperimentTableState::Failed;
        }
        recordExecutionFailure(message);
        if (!alreadyTerminating) {
            static_cast<void>(requestExecutionStop(requestMotionStop));
        }
    }
    tryFinishPendingTerminalState();
}

void TestExecutionService::recordExecutionFailure(const QString& message)
{
    const bool failureAlreadyReported =
        pendingTerminalState_ == ExperimentTerminalState::Fault;
    if (pendingTerminalState_.has_value()) {
        qCWarning(logApplication).noquote()
            << "实验终止清理期间发生附加错误：" << message;
        if (!terminalReason_.isEmpty()) {
            terminalReason_ += QStringLiteral("；");
        }
        terminalReason_ += message;
    } else {
        terminalReason_ = message;
        qCCritical(logApplication).noquote() << "测试流程失败：" << message;
    }
    pendingTerminalState_ = ExperimentTerminalState::Fault;
    MachineStateManager::instance().enterFault(terminalReason_);
    if (!failureAlreadyReported) {
        // UI 会弹模态对话框，延后通知以免中断当前停止请求调用栈。
        QMetaObject::invokeMethod(
            this, [this, message] { emit executionFailed(message); },
            Qt::QueuedConnection);
    }
}

bool TestExecutionService::requestExecutionStop(bool requestMotionStop,
                                                QString* errorMessage)
{
    QString acquisitionError;
    bool acquisitionAccepted = true;
    if (DataAcquisitionService::instance().state() != AcquisitionState::Idle) {
        acquisitionAccepted =
            DataAcquisitionService::instance().stopCollection(&acquisitionError);
        if (!acquisitionAccepted) {
            recordExecutionFailure(
                QStringLiteral("停止采集请求失败：%1").arg(acquisitionError));
        }
    }

    QString motionError;
    bool motionAccepted = true;
    if (requestMotionStop && MotionControlService::instance().hasActiveMotion()) {
        motionAccepted = MotionControlService::instance().stop(&motionError);
        if (!motionAccepted) {
            recordExecutionFailure(
                QStringLiteral("停止运动请求失败：%1").arg(motionError));
        }
    }
    if (!acquisitionAccepted || !motionAccepted) {
        setError(errorMessage, !motionAccepted ? motionError : acquisitionError);
        return false;
    }
    return true;
}

bool TestExecutionService::hasActiveExecution() const
{
    return executionId_ != 0;
}

bool TestExecutionService::validateStartPrerequisites(
    QString* errorMessage) const
{
    const MachineState machineState =
        MachineStateManager::instance().state();
    if (machineState != MachineState::Idle) {
        setError(errorMessage,
                 QStringLiteral("机器当前处于%1状态，不能启动测试。")
                     .arg(machineStateText(machineState)));
        return false;
    }
    if (!MachineStateManager::instance().canSubmitCommand(
            MachineState::Idle, errorMessage)) {
        return false;
    }
    if (hasActiveExecution()) {
        setError(errorMessage, QStringLiteral("已有测试流程正在执行或收尾。"));
        return false;
    }

    const std::optional<TestParameters> lockedConfiguration =
        TestConfigurationService::instance().lockedConfiguration();
    if (!lockedConfiguration.has_value()) {
        setError(errorMessage,
                 QStringLiteral("请先在参数设置页确定并锁定配置。"));
        return false;
    }
    if (!validateTestParameters(*lockedConfiguration, errorMessage)) {
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
