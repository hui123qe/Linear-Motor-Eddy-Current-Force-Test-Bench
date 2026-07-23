#include "TestExecutionService.h"

#include "../acquisition/DataAcquisitionService.h"
#include "../database/AcquisitionDatabaseService.h"
#include "../logging/AppLogger.h"
#include "../motion/MotionControlService.h"

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

} // namespace

TestExecutionService& TestExecutionService::instance()
{
    static TestExecutionService service;
    return service;
}

TestExecutionService::TestExecutionService()
{
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
            &TestExecutionService::failExecution);
    connect(&acquisitionService,
            &DataAcquisitionService::collectionFailed,
            this,
            &TestExecutionService::failExecution);
    connect(&motionService,
            &MotionControlService::commandFailed,
            this,
            &TestExecutionService::failExecution);
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
    completedMotionCount_ = 0;
    tableOpen_ = false;
    userStopRequested_ = false;
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
    int repetitionIndex)
{
    // 建表结果来自数据库线程，返回时流程可能已停止或故障。
    if (currentRepetitionIndex_ != 0
        && repetitionIndex != currentRepetitionIndex_) {
        failExecution(QStringLiteral("数据库返回了非当前实验的数据表。"));
        return;
    }

    tableOpen_ = true;
    // 过期建表结果不得再启动采集，但已创建的表仍需正常收尾。
    if (currentRepetitionIndex_ == 0 || userStopRequested_) {
        handleCollectionStopped();
        return;
    }

    QString errorMessage;
    if (!DataAcquisitionService::instance().startCollection(&errorMessage)) {
        failExecution(errorMessage);
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

    // 运动程序包含全部重复次数，后续轮次只需重新启动采集。
    if (currentRepetitionIndex_ > 1) {
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().start(parameters_, &errorMessage)) {
        failExecution(errorMessage);
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
                .arg(status.errorCode));
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
        failExecution(errorMessage);
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
                failExecution(errorMessage);
            } else {
                qCWarning(logDatabase).noquote()
                    << "流程结束后收尾实验表失败：" << errorMessage;
            }
        }
        return;
    }

    if (userStopRequested_ && currentRepetitionIndex_ != 0) {
        currentRepetitionIndex_ = 0;
        emit executionStopped();
    }
}

void TestExecutionService::handleExperimentTableFinished(
    int repetitionIndex)
{
    // 数据库已完成本轮全部排队写入，仅解除写入屏障，等待运动状态推进流程。
    tableOpen_ = false;

    // 流程已完成、停止或故障时，忽略迟到的收尾通知。
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    // 异步收尾结果必须属于当前轮次，防止数据表串入其他实验。
    if (repetitionIndex != currentRepetitionIndex_) {
        failExecution(QStringLiteral("数据库结束了非当前实验的数据表。"));
        return;
    }

    // 数据库完成只解除本轮写入屏障，不负责启动下一轮。
    // 正常轮次推进由持续到达的运动状态通知统一驱动。
    if (userStopRequested_) {
        currentRepetitionIndex_ = 0;
        emit executionStopped();
    }
}

void TestExecutionService::prepareRepetition(int repetitionIndex)
{
    currentRepetitionIndex_ = repetitionIndex;

    QString errorMessage;
    if (!AcquisitionDatabaseService::instance().beginExperimentTable(
            parameters_.motorModel,
            parameters_.specimenId,
            repetitionIndex,
            kAcquisitionSamplePeriodSeconds,
            &errorMessage)) {
        failExecution(errorMessage);
    }
}

void TestExecutionService::requestCurrentCollectionStop()
{
    QString errorMessage;
    if (!DataAcquisitionService::instance().stopCollection(&errorMessage)) {
        failExecution(errorMessage);
    }
}

void TestExecutionService::tryAdvanceAfterRepetition()
{
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    if (userStopRequested_) {
        if (!tableOpen_) {
            currentRepetitionIndex_ = 0;
            emit executionStopped();
        }
        return;
    }
    // 运动回零和数据库收尾是两个独立完成条件，缺一不可进入下一轮。
    if (completedMotionCount_ < currentRepetitionIndex_ || tableOpen_) {
        return;
    }

    if (currentRepetitionIndex_ >= parameters_.repeatCount) {
        currentRepetitionIndex_ = 0;
        emit executionFinished();
        return;
    }

    prepareRepetition(currentRepetitionIndex_ + 1);
}

void TestExecutionService::failExecution(const QString& message)
{
    if (currentRepetitionIndex_ == 0) {
        return;
    }

    // 先标记流程结束，避免清理期间的迟到通知重复发布故障。
    currentRepetitionIndex_ = 0;
    qCCritical(logApplication).noquote() << "测试流程失败：" << message;

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
    emit executionFailed(message);
}
