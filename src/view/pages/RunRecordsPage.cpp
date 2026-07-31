#include "RunRecordsPage.h"

#include "../../experimentlog/ExperimentLogService.h"
#include "../../experimentlog/ExperimentExportService.h"
#include "../widgets/ViewHelpers.h"

#include <QDate>
#include <QDateTime>
#include <QDateEdit>
#include <QDir>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStandardPaths>
#include <QVBoxLayout>

RunRecordsPage::RunRecordsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto* heading = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("实验记录"), "pageTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("按日期或实验名称查询每次实验的关键结果"),
        "pageDescription"));
    heading->addLayout(titleBox);
    heading->addStretch();
    exportButton_ = ViewHelpers::makeButton(
        QStringLiteral("导出选中记录"), QStringLiteral("primary"));
    exportButton_->setEnabled(false);
    heading->addWidget(exportButton_);
    layout->addLayout(heading);

    auto* filter = new QFrame;
    filter->setObjectName(QStringLiteral("filterBar"));
    auto* filterLayout = new QHBoxLayout(filter);
    filterLayout->setContentsMargins(14, 10, 14, 10);
    filterLayout->setSpacing(10);

    startDateInput_ = new QDateEdit;
    startDateInput_->setCalendarPopup(true);
    startDateInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    startDateInput_->setDate(QDate::currentDate());
    endDateInput_ = new QDateEdit;
    endDateInput_->setCalendarPopup(true);
    endDateInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    endDateInput_->setDate(QDate::currentDate());
    startDateInput_->setMaximumDate(endDateInput_->date());
    endDateInput_->setMinimumDate(startDateInput_->date());

    filterLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("日期"), "fieldLabel"),
        0,
        Qt::AlignVCenter);
    filterLayout->addWidget(startDateInput_);
    filterLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("至"), "fieldLabel"),
        0,
        Qt::AlignVCenter);
    filterLayout->addWidget(endDateInput_);

    keywordInput_ = new QLineEdit;
    keywordInput_->setPlaceholderText(
        QStringLiteral("搜索实验名称、操作员、状态或原因"));
    filterLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("关键词"), "fieldLabel"),
        0,
        Qt::AlignVCenter);
    filterLayout->addWidget(keywordInput_, 1);
    layout->addWidget(filter);

    recordsTable_ = ViewHelpers::makeTable(
        {QStringLiteral("时间"),
         QStringLiteral("操作员"),
         QStringLiteral("实验结果"),
         QStringLiteral("实验名称")},
        {});
    recordsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recordsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    recordsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recordsTable_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    recordsTable_->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::Stretch);
    layout->addWidget(recordsTable_, 1);

    connect(startDateInput_,
            &QDateEdit::dateChanged,
            endDateInput_,
            &QDateEdit::setMinimumDate);
    connect(endDateInput_,
            &QDateEdit::dateChanged,
            startDateInput_,
            &QDateEdit::setMaximumDate);
    connect(startDateInput_,
            &QDateEdit::dateChanged,
            this,
            &RunRecordsPage::refreshRecords);
    connect(endDateInput_,
            &QDateEdit::dateChanged,
            this,
            &RunRecordsPage::refreshRecords);
    connect(keywordInput_,
            &QLineEdit::returnPressed,
            this,
            &RunRecordsPage::refreshRecords);
    connect(recordsTable_,
            &QTableWidget::itemSelectionChanged,
            this,
            &RunRecordsPage::updateExportAvailability);
    connect(exportButton_,
            &QPushButton::clicked,
            this,
            &RunRecordsPage::exportSelectedRecord);

    ExperimentLogService& logService = ExperimentLogService::instance();
    connect(&logService,
            &ExperimentLogService::availabilityChanged,
            this,
            &RunRecordsPage::handleLogAvailabilityChanged);
    connect(&logService,
            &ExperimentLogService::experimentRecordsLoaded,
            this,
            &RunRecordsPage::handleRecordsLoaded);
    connect(&logService,
            &ExperimentLogService::experimentRecordsQueryFailed,
            this,
            &RunRecordsPage::handleRecordsQueryFailed);
    connect(&logService,
            &ExperimentLogService::experimentRecordSaved,
            this,
            &RunRecordsPage::refreshRecords);
    connect(&logService,
            &ExperimentLogService::experimentSummarySaved,
            this,
            &RunRecordsPage::refreshRecords);

    ExperimentExportService& exportService =
        ExperimentExportService::instance();
    connect(&exportService,
            &ExperimentExportService::exportProgress,
            this,
            &RunRecordsPage::handleExportProgress);
    connect(&exportService,
            &ExperimentExportService::exportCompleted,
            this,
            &RunRecordsPage::handleExportCompleted);
    connect(&exportService,
            &ExperimentExportService::exportFailed,
            this,
            &RunRecordsPage::handleExportFailed);

    if (logService.isAvailable()) {
        refreshRecords();
    }
}

void RunRecordsPage::refreshRecords()
{
    ExperimentRecordFilter filter;
    filter.fromDate = startDateInput_->date();
    filter.toDate = endDateInput_->date();
    filter.keyword = keywordInput_->text().trimmed();
    filter.limit = 100;

    QString errorMessage;
    const qint64 requestId =
        ExperimentLogService::instance().requestExperimentRecords(
            filter, &errorMessage);
    if (requestId == 0) {
        latestRequestId_ = 0;
        handleRecordsQueryFailed(0, errorMessage);
        return;
    }
    latestRequestId_ = requestId;
    recordsTable_->setToolTip(QString());
    updateExportAvailability();
}

void RunRecordsPage::handleLogAvailabilityChanged(
    bool available,
    const QString& message)
{
    if (available) {
        refreshRecords();
    } else {
        handleRecordsQueryFailed(0, message);
    }
}

void RunRecordsPage::handleRecordsLoaded(
    qint64 requestId,
    const QVector<ExperimentRecordListItem>& records)
{
    if (requestId != latestRequestId_) {
        return;
    }

    recordsTable_->clearContents();
    recordsTable_->setRowCount(records.size());
    recordsTable_->setToolTip(QString());
    for (qsizetype row = 0; row < records.size(); ++row) {
        const ExperimentRecordListItem& record = records.at(row);
        auto* finishedAtItem = new QTableWidgetItem(
            record.finishedAtUtc.toLocalTime().toString(
                QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        finishedAtItem->setData(Qt::UserRole, record.id);
        finishedAtItem->setData(
            Qt::UserRole + 1, static_cast<int>(record.kind));
        recordsTable_->setItem(row, 0, finishedAtItem);
        recordsTable_->setItem(
            row,
            1,
            new QTableWidgetItem(record.operatorName.isEmpty()
                                     ? QStringLiteral("--")
                                     : record.operatorName));

        QString resultText =
            record.kind == ExperimentLogEntryKind::Summary
                ? QStringLiteral("汇总%1")
                      .arg(experimentTerminalStateDisplayText(record.state))
                : experimentTerminalStateDisplayText(record.state);
        if (!record.terminalReason.trimmed().isEmpty()) {
            resultText += QStringLiteral("：") + record.terminalReason;
        }
        recordsTable_->setItem(row, 2, new QTableWidgetItem(resultText));
        recordsTable_->setItem(
            row, 3, new QTableWidgetItem(record.experimentName));
    }
}

void RunRecordsPage::handleRecordsQueryFailed(
    qint64 requestId,
    const QString& message)
{
    if (requestId != 0 && requestId != latestRequestId_) {
        return;
    }

    recordsTable_->clearContents();
    recordsTable_->setRowCount(1);
    recordsTable_->setItem(
        0, 0, new QTableWidgetItem(QStringLiteral("查询失败")));
    recordsTable_->setItem(0, 2, new QTableWidgetItem(message));
    recordsTable_->setToolTip(message);
    updateExportAvailability();
}

void RunRecordsPage::handleExportProgress(int percent)
{
    if (exportProgressDialog_ == nullptr) {
        return;
    }
    exportProgressDialog_->setLabelText(
        percent >= 90
            ? QStringLiteral("正在保存 Excel 工作簿…")
            : QStringLiteral("正在读取实验数据并生成 Excel…"));
    // 模态 QProgressDialog::setValue() 可能处理事件并触发槽重入。
    // 因此必须将它放在最后，调用后不再访问对话框。
    exportProgressDialog_->setValue(percent);
}

void RunRecordsPage::handleExportCompleted(
    const QString& targetPath)
{
    if (exportProgressDialog_ != nullptr) {
        exportProgressDialog_->setLabelText(
            QStringLiteral("Excel 工作簿保存完成"));
        exportProgressDialog_->setValue(100);
    }
    closeExportProgressDialog();
    exportButton_->setToolTip(QString());
    updateExportAvailability();
    QMessageBox::information(
        this,
        QStringLiteral("导出完成"),
        QStringLiteral("Excel 文件已保存：\n%1").arg(targetPath));
}

void RunRecordsPage::handleExportFailed(
    const QString& message)
{
    closeExportProgressDialog();
    exportButton_->setToolTip(message);
    updateExportAvailability();
    QMessageBox::warning(this, QStringLiteral("导出失败"), message);
}

void RunRecordsPage::exportSelectedRecord()
{
    const int row = recordsTable_->currentRow();
    QTableWidgetItem* idItem =
        row >= 0 ? recordsTable_->item(row, 0) : nullptr;
    QTableWidgetItem* nameItem =
        row >= 0 ? recordsTable_->item(row, 3) : nullptr;
    if (idItem == nullptr || nameItem == nullptr) {
        QMessageBox::information(
            this,
            QStringLiteral("导出实验记录"),
            QStringLiteral("请先选择一条实验记录。"));
        return;
    }

    const qint64 recordId = idItem->data(Qt::UserRole).toLongLong();
    const ExperimentLogEntryKind entryKind =
        static_cast<ExperimentLogEntryKind>(
            idItem->data(Qt::UserRole + 1).toInt());
    const ExperimentExportKind kind =
        entryKind == ExperimentLogEntryKind::Summary
            ? ExperimentExportKind::Summary
            : ExperimentExportKind::SingleRecord;
    const bool summary = kind == ExperimentExportKind::Summary;
    QString fileBaseName = nameItem->text().trimmed();
    static const QString invalidFileCharacters =
        QStringLiteral("<>:\"/\\|?*");
    for (const QChar character : invalidFileCharacters) {
        fileBaseName.replace(character, QLatin1Char('_'));
    }
    fileBaseName += QLatin1Char('_')
                    + QDateTime::currentDateTime().toString(
                        QStringLiteral("yyyyMMdd_HHmmss"))
                    + QStringLiteral(".xlsx");
    const QString documentsDirectory =
        QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
    QSettings settings;
    QString initialDirectory =
        settings.value(QStringLiteral("export/lastDirectory"),
                       documentsDirectory)
            .toString();
    if (!QDir(initialDirectory).exists()) {
        initialDirectory = documentsDirectory;
    }
    const QString targetPath = QFileDialog::getSaveFileName(
        this,
        summary ? QStringLiteral("导出选中实验汇总")
                : QStringLiteral("导出选中实验"),
        QDir(initialDirectory).filePath(fileBaseName),
        QStringLiteral("Excel 工作簿 (*.xlsx)"));
    if (targetPath.isEmpty()) {
        return;
    }

    QString normalizedPath = targetPath;
    if (!normalizedPath.endsWith(QStringLiteral(".xlsx"),
                                 Qt::CaseInsensitive)) {
        normalizedPath += QStringLiteral(".xlsx");
    }
    settings.setValue(QStringLiteral("export/lastDirectory"),
                      QFileInfo(normalizedPath).absolutePath());

    if (exportProgressDialog_ == nullptr) {
        exportProgressDialog_ = new QProgressDialog(
            QString(), QString(), 0, 100, this);
        exportProgressDialog_->setWindowTitle(
            QStringLiteral("导出实验记录"));
        exportProgressDialog_->setWindowModality(Qt::WindowModal);
        exportProgressDialog_->setCancelButton(nullptr);
        exportProgressDialog_->setAutoClose(false);
        exportProgressDialog_->setAutoReset(false);
        exportProgressDialog_->setMinimumDuration(0);
    }
    exportProgressDialog_->setLabelText(
        QStringLiteral("正在准备导出…"));
    exportProgressDialog_->setValue(0);
    exportProgressDialog_->show();

    QString errorMessage;
    bool started = false;
    if (kind == ExperimentExportKind::Summary) {
        started = ExperimentExportService::instance()
                      .exportSummary(
                          recordId, normalizedPath, &errorMessage);
    } else {
        started = ExperimentExportService::instance()
                      .exportSingleRecord(
                          recordId, normalizedPath, &errorMessage);
    }
    if (!started) {
        closeExportProgressDialog();
        QMessageBox::warning(
            this, QStringLiteral("导出失败"), errorMessage);
        updateExportAvailability();
        return;
    }
    updateExportAvailability();
}

void RunRecordsPage::updateExportAvailability()
{
    if (exportButton_ == nullptr || recordsTable_ == nullptr) {
        return;
    }
    QTableWidgetItem* selectedItem =
        recordsTable_->currentRow() >= 0
            ? recordsTable_->item(recordsTable_->currentRow(), 0)
            : nullptr;
    exportButton_->setEnabled(
        !ExperimentExportService::instance().isBusy()
        && selectedItem != nullptr
        && selectedItem->data(Qt::UserRole).toLongLong() > 0);
}

void RunRecordsPage::closeExportProgressDialog()
{
    if (exportProgressDialog_ == nullptr) {
        return;
    }
    // 复用页面持有的对话框，避免 setValue() 处理事件期间销毁对象。
    exportProgressDialog_->hide();
}
