#include "ExperimentLogService.h"

#include "ExperimentResultCalculator.h"
#include "../database/AcquisitionDatabaseService.h"
#include "../logging/AppLogger.h"
#include "../workflow/TestExecutionService.h"

#include <algorithm>

ExperimentLogService& ExperimentLogService::instance()
{
    static ExperimentLogService service;
    return service;
}

ExperimentLogService::ExperimentLogService()
{
    TestExecutionService& executionService = TestExecutionService::instance();
    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();

    connect(&executionService,
            &TestExecutionService::experimentFinalized,
            this,
            &ExperimentLogService::handleExperimentFinalized);
    connect(&executionService,
            &TestExecutionService::experimentGroupFinalized,
            this,
            &ExperimentLogService::handleExperimentGroupFinalized);
    connect(&databaseService,
            &AcquisitionDatabaseService::readinessChanged,
            this,
            &ExperimentLogService::availabilityChanged);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentForceAggregateRead,
            this,
            &ExperimentLogService::handleForceAggregateRead);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentForceAggregateFailed,
            this,
            &ExperimentLogService::handleForceAggregateFailed);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentRecordStored,
            this,
            &ExperimentLogService::handleRecordStored);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentRecordStoreFailed,
            this,
            &ExperimentLogService::handleRecordStoreFailed);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentRecordsRead,
            this,
            &ExperimentLogService::experimentRecordsLoaded);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentRecordsReadFailed,
            this,
            &ExperimentLogService::experimentRecordsQueryFailed);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentSummaryStored,
            this,
            &ExperimentLogService::handleSummaryStored);
    connect(&databaseService,
            &AcquisitionDatabaseService::experimentSummaryStoreFailed,
            this,
            &ExperimentLogService::handleSummaryStoreFailed);
}

bool ExperimentLogService::isAvailable() const
{
    return AcquisitionDatabaseService::instance().isReady();
}

qint64 ExperimentLogService::requestExperimentRecords(
    const ExperimentRecordFilter& filter,
    QString* errorMessage)
{
    const qint64 requestId = nextHistoryRequestId_++;
    if (nextHistoryRequestId_ <= 0) {
        nextHistoryRequestId_ = 1;
    }
    if (!AcquisitionDatabaseService::instance().requestExperimentRecords(
            requestId, filter, errorMessage)) {
        return 0;
    }
    return requestId;
}

void ExperimentLogService::handleExperimentFinalized(
    const ExperimentFinalContext& context)
{
    qCInfo(logCompletionReceipt)
        << "[实验日志层][单次完成信号] 接收 experimentFinalized"
        << "executionId=" << context.executionId
        << "repetition=" << context.repetitionIndex
        << "plannedRepeatCount=" << context.plannedRepeatCount
        << "state=" << static_cast<int>(context.state)
        << "rawSampleCount=" << context.rawSampleCount
        << "table=" << context.rawDataTableName;
    const PendingKey key(context.executionId, context.repetitionIndex);
    if (pendingRecords_.contains(key)) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][单次完成信号] 忽略重复上下文"
            << "executionId=" << context.executionId
            << "repetition=" << context.repetitionIndex;
        return;
    }
    const TestMotionParameters motionParameters =
        selectedTestMotionParameters(context.parametersSnapshot);
    ExperimentRecord record;
    record.executionId = context.executionId;
    record.experimentName = context.experimentName;
    record.baseExperimentName = context.baseExperimentName;
    record.repetitionIndex = context.repetitionIndex;
    record.plannedRepeatCount = context.plannedRepeatCount;
    record.cycleIndex = context.cycleIndex;
    record.direction = context.direction;
    record.finishedAtUtc = context.finishedAtUtc;
    record.operatorName = context.operatorName;
    record.state = context.state;
    record.terminalReason = context.terminalReason;
    record.testSpeedMetersPerSecond =
        motionParameters.speedMetersPerSecond;
    record.statisticsStartMeters = std::min(
        motionParameters.acquisitionStartMeters,
        motionParameters.acquisitionEndMeters);
    record.statisticsEndMeters = std::max(
        motionParameters.acquisitionStartMeters,
        motionParameters.acquisitionEndMeters);
    record.rawSampleCount = context.rawSampleCount;
    record.rawDataTableName = context.rawDataTableName;
    record.parametersSnapshot = context.parametersSnapshot;
    pendingRecords_.insert(key, record);

    if (context.state != ExperimentTerminalState::Completed) {
        qCInfo(logCompletionReceipt)
            << "[实验日志层][单次终态] 非完成记录不计算统计值，直接请求入库"
            << "executionId=" << context.executionId
            << "repetition=" << context.repetitionIndex
            << "state=" << static_cast<int>(context.state);
        requestRecordStore(key);
        return;
    }

    qCInfo(logCompletionReceipt)
        << "[实验日志层][单次统计] 请求数据库计算有效区间统计"
        << "executionId=" << context.executionId
        << "repetition=" << context.repetitionIndex
        << "statisticsStartMeters=" << record.statisticsStartMeters
        << "statisticsEndMeters=" << record.statisticsEndMeters
        << "table=" << context.rawDataTableName;
    QString errorMessage;
    if (!AcquisitionDatabaseService::instance()
             .requestExperimentForceAggregate(
                 context.executionId,
                 context.repetitionIndex,
                 context.rawDataTableName,
                 record.statisticsStartMeters,
                 record.statisticsEndMeters,
                 &errorMessage)) {
        qCWarning(logCompletionReceipt).noquote()
            << "[实验日志层][单次统计] 统计请求未入队"
            << "executionId=" << context.executionId
            << "repetition=" << context.repetitionIndex
            << "reason=" << errorMessage;
        pendingRecords_.remove(key);
        pendingGroups_[context.executionId].failedRepetitions.insert(
            context.repetitionIndex);
        emit experimentRecordProcessingFailed(
            context.executionId, context.repetitionIndex, errorMessage);
        tryStoreSummary(context.executionId);
    }
}

void ExperimentLogService::handleExperimentGroupFinalized(
    const ExperimentGroupFinalContext& context)
{
    qCInfo(logCompletionReceipt)
        << "[实验日志层][整组完成信号] 接收 experimentGroupFinalized"
        << "executionId=" << context.executionId
        << "plannedRepeatCount=" << context.plannedRepeatCount
        << "finalizedRecordCount=" << context.finalizedRecordCount
        << "state=" << static_cast<int>(context.state);
    if (context.executionId <= 0) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][整组完成信号] 忽略无效 executionId"
            << "executionId=" << context.executionId;
        return;
    }
    if (context.finalizedRecordCount <= 0) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][整组完成信号] 没有已完成单次记录，清理整组上下文"
            << "executionId=" << context.executionId;
        pendingGroups_.remove(context.executionId);
        return;
    }

    PendingGroup& group = pendingGroups_[context.executionId];
    if (group.finalContextReceived) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][整组完成信号] 忽略重复上下文"
            << "executionId=" << context.executionId;
        return;
    }
    group.finalContext = context;
    group.finalContextReceived = true;
    tryStoreSummary(context.executionId);
}

void ExperimentLogService::handleForceAggregateRead(
    qint64 executionId,
    int repetitionIndex,
    const ExperimentForceAggregate& aggregate)
{
    qCInfo(logCompletionReceipt)
        << "[实验日志层][单次统计回执] 收到数据库统计结果"
        << "executionId=" << executionId
        << "repetition=" << repetitionIndex
        << "effectiveSampleCount=" << aggregate.effectiveSampleCount
        << "hasAverageForce=" << aggregate.averageForceNewtons.has_value()
        << "hasMinimumForce=" << aggregate.minimumForceNewtons.has_value()
        << "hasMaximumForce=" << aggregate.maximumForceNewtons.has_value();
    const PendingKey key(executionId, repetitionIndex);
    auto record = pendingRecords_.find(key);
    if (record == pendingRecords_.end()) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][单次统计回执] 忽略：找不到待处理单次记录"
            << "executionId=" << executionId
            << "repetition=" << repetitionIndex;
        return;
    }

    record->statistics = ExperimentResultCalculator::calculateSingle(
        aggregate, record->testSpeedMetersPerSecond);
    if (!record->statistics.averageForceNewtons.has_value()
        || !record->statistics
                .forceCoefficientNewtonSecondsPerMeter.has_value()) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][单次统计回执] 统计结果无效，不请求单次记录入库"
            << "executionId=" << executionId
            << "repetition=" << repetitionIndex
            << "effectiveSampleCount=" << aggregate.effectiveSampleCount;
        pendingRecords_.erase(record);
        pendingGroups_[executionId].failedRepetitions.insert(
            repetitionIndex);
        const QString message = QStringLiteral(
            "实验统计结果无效，不生成单次实验记录。");
        emit experimentRecordProcessingFailed(
            executionId, repetitionIndex, message);
        tryStoreSummary(executionId);
        return;
    }
    requestRecordStore(key);
}

void ExperimentLogService::handleForceAggregateFailed(
    qint64 executionId,
    int repetitionIndex,
    const QString& message)
{
    qCWarning(logCompletionReceipt).noquote()
        << "[实验日志层][单次统计回执] 数据库统计失败"
        << "executionId=" << executionId
        << "repetition=" << repetitionIndex
        << "reason=" << message;
    const PendingKey key(executionId, repetitionIndex);
    if (!pendingRecords_.contains(key)) {
        return;
    }

    pendingRecords_.remove(key);
    pendingGroups_[executionId].failedRepetitions.insert(repetitionIndex);
    qCWarning(logDatabase).noquote()
        << "实验统计查询失败，不生成单次实验记录：" << message;
    emit experimentRecordProcessingFailed(
        executionId, repetitionIndex, message);
    tryStoreSummary(executionId);
}

void ExperimentLogService::handleRecordStored(
    const ExperimentRecord& record)
{
    qCInfo(logCompletionReceipt)
        << "[实验日志层][单次入库回执] 收到 experimentRecordStored"
        << "executionId=" << record.executionId
        << "repetition=" << record.repetitionIndex
        << "recordId=" << record.id;
    const PendingKey key(record.executionId, record.repetitionIndex);
    if (!pendingRecords_.remove(key)) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][单次入库回执] 忽略：找不到待处理单次记录"
            << "executionId=" << record.executionId
            << "repetition=" << record.repetitionIndex
            << "recordId=" << record.id;
        return;
    }
    pendingGroups_[record.executionId].records.insert(
        record.repetitionIndex, record);
    qCInfo(logCompletionReceipt)
        << "[实验日志层][单次 UI 信号] 发送 experimentRecordSaved"
        << "executionId=" << record.executionId
        << "repetition=" << record.repetitionIndex
        << "recordId=" << record.id;
    emit experimentRecordSaved(record);
    tryStoreSummary(record.executionId);
}

void ExperimentLogService::handleRecordStoreFailed(
    qint64 executionId,
    int repetitionIndex,
    const QString& message)
{
    qCWarning(logCompletionReceipt).noquote()
        << "[实验日志层][单次入库回执] 数据库保存失败"
        << "executionId=" << executionId
        << "repetition=" << repetitionIndex
        << "reason=" << message;
    const PendingKey key(executionId, repetitionIndex);
    if (!pendingRecords_.remove(key)) {
        return;
    }
    pendingGroups_[executionId].failedRepetitions.insert(repetitionIndex);
    emit experimentRecordProcessingFailed(
        executionId, repetitionIndex, message);
    tryStoreSummary(executionId);
}

void ExperimentLogService::handleSummaryStored(
    const ExperimentSummaryBundle& bundle)
{
    qCInfo(logCompletionReceipt)
        << "[实验日志层][整组入库回执] 收到 experimentSummaryStored"
        << "executionId=" << bundle.summary.executionId
        << "summaryId=" << bundle.summary.id
        << "memberCount=" << bundle.records.size();
    if (!pendingGroups_.remove(bundle.summary.executionId)) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][整组入库回执] 忽略：找不到待处理整组上下文"
            << "executionId=" << bundle.summary.executionId
            << "summaryId=" << bundle.summary.id;
        return;
    }
    qCInfo(logCompletionReceipt)
        << "[实验日志层][整组 UI 信号] 发送 experimentSummarySaved"
        << "executionId=" << bundle.summary.executionId
        << "summaryId=" << bundle.summary.id;
    emit experimentSummarySaved(bundle.summary);
}

void ExperimentLogService::handleSummaryStoreFailed(
    qint64 executionId,
    const QString& message)
{
    qCWarning(logCompletionReceipt).noquote()
        << "[实验日志层][整组入库回执] 数据库保存失败"
        << "executionId=" << executionId
        << "reason=" << message;
    if (!pendingGroups_.remove(executionId)) {
        return;
    }
    emit experimentSummaryProcessingFailed(executionId, message);
}

void ExperimentLogService::requestRecordStore(const PendingKey& key)
{
    const auto record = pendingRecords_.constFind(key);
    if (record == pendingRecords_.constEnd()) {
        qCWarning(logCompletionReceipt)
            << "[实验日志层][单次入库请求] 跳过：找不到待处理单次记录"
            << "executionId=" << key.first
            << "repetition=" << key.second;
        return;
    }

    qCInfo(logCompletionReceipt)
        << "[实验日志层][单次入库请求] 请求保存实验记录"
        << "executionId=" << record->executionId
        << "repetition=" << record->repetitionIndex
        << "effectiveSampleCount="
        << record->statistics.effectiveSampleCount;
    QString errorMessage;
    if (!AcquisitionDatabaseService::instance().storeExperimentRecord(
            *record, &errorMessage)) {
        qCWarning(logCompletionReceipt).noquote()
            << "[实验日志层][单次入库请求] 保存请求未入队"
            << "executionId=" << record->executionId
            << "repetition=" << record->repetitionIndex
            << "reason=" << errorMessage;
        const qint64 executionId = record->executionId;
        const int repetitionIndex = record->repetitionIndex;
        pendingRecords_.remove(key);
        pendingGroups_[executionId].failedRepetitions.insert(
            repetitionIndex);
        emit experimentRecordProcessingFailed(
            executionId, repetitionIndex, errorMessage);
        tryStoreSummary(executionId);
    }
}

void ExperimentLogService::tryStoreSummary(qint64 executionId)
{
    QMap<qint64, PendingGroup>::iterator groupIterator =
        pendingGroups_.find(executionId);
    if (groupIterator == pendingGroups_.end()
        || !groupIterator->finalContextReceived
        || groupIterator->storeRequested) {
        qCDebug(logCompletionReceipt)
            << "[实验日志层][整组汇总判定] 尚不具备汇总条件"
            << "executionId=" << executionId
            << "groupExists=" << (groupIterator != pendingGroups_.end())
            << "finalContextReceived="
            << (groupIterator != pendingGroups_.end()
                    && groupIterator->finalContextReceived)
            << "storeRequested="
            << (groupIterator != pendingGroups_.end()
                    && groupIterator->storeRequested);
        return;
    }

    PendingGroup& group = groupIterator.value();
    const int handledRecordCount = group.records.size()
                                   + group.failedRepetitions.size();
    if (handledRecordCount < group.finalContext.finalizedRecordCount) {
        qCInfo(logCompletionReceipt)
            << "[实验日志层][整组汇总判定] 等待单次记录处理完成"
            << "executionId=" << executionId
            << "handledRecordCount=" << handledRecordCount
            << "finalizedRecordCount="
            << group.finalContext.finalizedRecordCount;
        return;
    }
    if (!group.failedRepetitions.isEmpty()) {
        const QString message = QStringLiteral(
            "有 %1 条单次实验记录保存失败，未生成实验汇总。")
                                    .arg(group.failedRepetitions.size());
        qCWarning(logCompletionReceipt).noquote()
            << "[实验日志层][整组汇总判定] 存在单次处理失败，不生成汇总"
            << "executionId=" << executionId
            << "failedCount=" << group.failedRepetitions.size()
            << "reason=" << message;
        pendingGroups_.erase(groupIterator);
        emit experimentSummaryProcessingFailed(executionId, message);
        return;
    }

    ExperimentSummaryBundle bundle;
    ExperimentSummaryRecord& summary = bundle.summary;
    summary.executionId = executionId;
    summary.baseExperimentName =
        group.finalContext.baseExperimentName;
    summary.finishedAtUtc = group.finalContext.finishedAtUtc;
    summary.operatorName = group.finalContext.operatorName;
    summary.state = group.finalContext.state;

    QVector<ExperimentStatistics> includedStatistics;
    for (QMap<int, ExperimentRecord>::const_iterator recordIterator =
             group.records.cbegin();
         recordIterator != group.records.cend();
         ++recordIterator) {
        const ExperimentRecord& record = recordIterator.value();
        includedStatistics.append(record.statistics);
        bundle.records.append(record);
    }
    summary.statistics =
        ExperimentResultCalculator::calculateSummary(includedStatistics);

    qCInfo(logCompletionReceipt)
        << "[实验日志层][整组入库请求] 请求保存实验汇总"
        << "executionId=" << executionId
        << "memberCount=" << bundle.records.size()
        << "plannedRepeatCount=" << group.finalContext.plannedRepeatCount
        << "hasAverageForce="
        << summary.statistics.multipleAverageForceNewtons.has_value()
        << "hasAverageCoefficient="
        << summary.statistics
               .multipleAverageForceCoefficientNewtonSecondsPerMeter
               .has_value();
    QString errorMessage;
    if (!AcquisitionDatabaseService::instance().storeExperimentSummary(
            bundle, &errorMessage)) {
        qCWarning(logCompletionReceipt).noquote()
            << "[实验日志层][整组入库请求] 保存请求未入队"
            << "executionId=" << executionId
            << "reason=" << errorMessage;
        pendingGroups_.erase(groupIterator);
        emit experimentSummaryProcessingFailed(executionId, errorMessage);
        return;
    }
    group.storeRequested = true;
}
