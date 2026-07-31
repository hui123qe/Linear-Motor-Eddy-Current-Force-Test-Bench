#pragma once

#include "ExperimentLogTypes.h"

#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>

class ExperimentLogService final : public QObject
{
    Q_OBJECT

public:
    static ExperimentLogService& instance();
    [[nodiscard]] bool isAvailable() const;

    [[nodiscard]] qint64 requestExperimentRecords(
        const ExperimentRecordFilter& filter,
        QString* errorMessage = nullptr);

signals:
    void availabilityChanged(bool available, const QString& message);
    void experimentRecordSaved(const ExperimentRecord& record);
    void experimentRecordProcessingFailed(qint64 executionId,
                                          int repetitionIndex,
                                          const QString& message);
    void experimentSummarySaved(const ExperimentSummaryRecord& summary);
    void experimentSummaryProcessingFailed(qint64 executionId,
                                            const QString& message);
    void experimentRecordsLoaded(
        qint64 requestId,
        const QVector<ExperimentRecordListItem>& records);
    void experimentRecordsQueryFailed(qint64 requestId,
                                      const QString& message);

private:
    ExperimentLogService();

    ExperimentLogService(const ExperimentLogService&) = delete;
    ExperimentLogService& operator=(const ExperimentLogService&) = delete;

    using PendingKey = QPair<qint64, int>;

    struct PendingGroup
    {
        ExperimentGroupFinalContext finalContext;
        QMap<int, ExperimentRecord> records;
        QSet<int> failedRepetitions;
        bool finalContextReceived = false;
        bool storeRequested = false;
    };

    void handleExperimentFinalized(const ExperimentFinalContext& context);
    void handleExperimentGroupFinalized(
        const ExperimentGroupFinalContext& context);
    void handleForceAggregateRead(
        qint64 executionId,
        int repetitionIndex,
        const ExperimentForceAggregate& aggregate);
    void handleForceAggregateFailed(qint64 executionId,
                                    int repetitionIndex,
                                    const QString& message);
    void handleRecordStored(const ExperimentRecord& record);
    void handleRecordStoreFailed(qint64 executionId,
                                 int repetitionIndex,
                                 const QString& message);
    void handleSummaryStored(const ExperimentSummaryBundle& bundle);
    void handleSummaryStoreFailed(qint64 executionId,
                                  const QString& message);
    void requestRecordStore(const PendingKey& key);
    void tryStoreSummary(qint64 executionId);

    QMap<PendingKey, ExperimentRecord> pendingRecords_;
    QMap<qint64, PendingGroup> pendingGroups_;
    qint64 nextHistoryRequestId_ = 1;
};
