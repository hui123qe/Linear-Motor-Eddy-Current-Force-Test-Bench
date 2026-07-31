#pragma once

#include <QObject>
#include <QThread>

enum class ExperimentExportKind
{
    SingleRecord,
    Summary
};

class ExperimentExportWorker;

class ExperimentExportService final : public QObject
{
    Q_OBJECT

public:
    static ExperimentExportService& instance();

    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] bool exportSingleRecord(
        qint64 recordId,
        const QString& targetPath,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool exportSummary(
        qint64 summaryId,
        const QString& targetPath,
        QString* errorMessage = nullptr);
    void shutdown();

signals:
    void exportStarted(ExperimentExportKind kind);
    void exportProgress(int percent);
    void exportCompleted(const QString& targetPath);
    void exportFailed(const QString& message);

private:
    ExperimentExportService();
    ~ExperimentExportService() override;

    ExperimentExportService(const ExperimentExportService&) = delete;
    ExperimentExportService& operator=(const ExperimentExportService&) = delete;

    [[nodiscard]] bool beginExport(qint64 recordId,
                                   const QString& targetPath,
                                   ExperimentExportKind kind,
                                   QString* errorMessage);
    void handleWriterCompleted(const QString& targetPath);
    void handleWriterFailed(const QString& message);

    ExperimentExportWorker* worker_ = nullptr;
    QThread workerThread_;
    bool busy_ = false;
};

Q_DECLARE_METATYPE(ExperimentExportKind)
