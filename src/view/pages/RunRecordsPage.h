#pragma once

#include "../../experimentlog/ExperimentLogTypes.h"

#include <QWidget>

class QDateEdit;
class QLineEdit;
class QProgressDialog;
class QPushButton;
class QTableWidget;

class RunRecordsPage final : public QWidget
{
    Q_OBJECT

public:
    explicit RunRecordsPage(QWidget* parent = nullptr);

private slots:
    // 实验记录查询
    void refreshRecords();
    void handleLogAvailabilityChanged(bool available,
                                      const QString& message);
    void handleRecordsLoaded(
        qint64 requestId,
        const QVector<ExperimentRecordListItem>& records);
    void handleRecordsQueryFailed(qint64 requestId,
                                  const QString& message);

    // Excel 导出
    void exportSelectedRecord();
    void handleExportProgress(int percent);
    void handleExportCompleted(const QString& targetPath);
    void handleExportFailed(const QString& message);
    void updateExportAvailability();

private:
    void closeExportProgressDialog();

    // 查询条件
    QDateEdit* startDateInput_ = nullptr;
    QDateEdit* endDateInput_ = nullptr;
    QLineEdit* keywordInput_ = nullptr;

    // 记录列表与导出入口
    QPushButton* exportButton_ = nullptr;
    QTableWidget* recordsTable_ = nullptr;
    QProgressDialog* exportProgressDialog_ = nullptr;

    // 异步请求标识，用于忽略迟到结果
    qint64 latestRequestId_ = 0;
};
