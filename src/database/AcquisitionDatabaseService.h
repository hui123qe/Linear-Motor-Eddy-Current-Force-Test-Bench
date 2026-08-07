#pragma once

#include "../acquisition/AcquisitionTypes.h"
#include "../experimentlog/ExperimentLogTypes.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVector>

struct ExperimentSample
{
    double relativeTimeMilliseconds = 0.0;
    double accelerationMetersPerSecondSquared = 0.0;
    double velocityMetersPerSecond = 0.0;
    double motorCurrent = 0.0;
    double motorTemperature = 0.0;
    double forceNewtons = 0.0;
    double positionMeters = 0.0;
};

Q_DECLARE_METATYPE(ExperimentSample)
Q_DECLARE_METATYPE(QVector<ExperimentSample>)

class AcquisitionDatabaseWorker;

class AcquisitionDatabaseService final : public QObject
{
    Q_OBJECT

public:
    static AcquisitionDatabaseService& instance();

    void initialize();
    void shutdown();

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool beginExperimentTable(const QString& motorModel,
                                            const QString& specimenId,
                                            int repetitionIndex,
                                            int cycleIndex,
                                            ExperimentMotionDirection direction,
                                            double samplePeriodSeconds,
                                            QString* errorMessage = nullptr);
    [[nodiscard]] bool appendBlock(const AcquisitionBlock& block,
                                   QString* errorMessage = nullptr);
    [[nodiscard]] bool finishExperimentTable(QString* errorMessage = nullptr);
    [[nodiscard]] bool requestExperimentTables(QString* errorMessage = nullptr);
    [[nodiscard]] bool requestExperimentData(const QString& tableName,
                                             qint64 offset,
                                             int limit,
                                             QString* errorMessage = nullptr);
    [[nodiscard]] bool requestExperimentForceAggregate(
        qint64 executionId,
        int repetitionIndex,
        const QString& tableName,
        double statisticsStartMeters,
        double statisticsEndMeters,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool storeExperimentRecord(
        const ExperimentRecord& record,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool requestExperimentRecords(
        qint64 requestId,
        const ExperimentRecordFilter& filter,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool storeExperimentSummary(
        const ExperimentSummaryBundle& bundle,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool readExperimentRecordForExport(
        qint64 recordId,
        ExperimentRecord* record,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool readExperimentSummaryForExport(
        qint64 summaryId,
        ExperimentSummaryBundle* bundle,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool readExperimentRawDataForExport(
        const QString& tableName,
        double statisticsStartMeters,
        double statisticsEndMeters,
        qint64 offset,
        int limit,
        QVector<ExperimentSample>* samples,
        QString* errorMessage = nullptr);

signals:
    void readinessChanged(bool ready, const QString& message);
    void experimentTableCreated(int repetitionIndex, const QString& tableName);
    void experimentTableFinished(int repetitionIndex,
                                 const QString& tableName,
                                 qint64 sampleCount);
    void experimentTablesRead(const QStringList& tableNames);
    void experimentDataRead(const QString& tableName,
                            qint64 offset,
                            const QVector<ExperimentSample>& samples);
    void experimentForceAggregateRead(
        qint64 executionId,
        int repetitionIndex,
        const ExperimentForceAggregate& aggregate);
    void experimentForceAggregateFailed(qint64 executionId,
                                        int repetitionIndex,
                                        const QString& message);
    void experimentRecordStored(const ExperimentRecord& record);
    void experimentRecordStoreFailed(qint64 executionId,
                                     int repetitionIndex,
                                     const QString& message);
    void experimentRecordsRead(
        qint64 requestId,
        const QVector<ExperimentRecordListItem>& records);
    void experimentRecordsReadFailed(qint64 requestId,
                                     const QString& message);
    void experimentSummaryStored(const ExperimentSummaryBundle& bundle);
    void experimentSummaryStoreFailed(qint64 executionId,
                                      const QString& message);
    void databaseFailed(const QString& message);

private:
    AcquisitionDatabaseService();
    ~AcquisitionDatabaseService() override;

    AcquisitionDatabaseService(const AcquisitionDatabaseService&) = delete;
    AcquisitionDatabaseService& operator=(const AcquisitionDatabaseService&) = delete;

    void handleReady(bool ready, const QString& message);
    void handleFailure(const QString& message);

    AcquisitionDatabaseWorker* worker_ = nullptr;
    QThread workerThread_;
    bool initialized_ = false;
    bool ready_ = false;
};
