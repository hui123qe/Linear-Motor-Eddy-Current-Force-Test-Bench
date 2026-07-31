#include "ExperimentExportService.h"

#include "../database/AcquisitionDatabaseService.h"
#include "../logging/AppLogger.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>

#include <xlsxdocument.h>
#include <xlsxformat.h>

#include <algorithm>
#include <memory>

namespace {

constexpr int kExportPageSize = 5000;

QString exportKindText(ExperimentExportKind kind)
{
    return kind == ExperimentExportKind::SingleRecord
               ? QStringLiteral("单次实验")
               : QStringLiteral("实验汇总");
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QString testTypeText(EddyCurrentTestType testType)
{
    switch (testType) {
    case EddyCurrentTestType::RatedSpeed:
        return QStringLiteral("标准件涡流力测试");
    case EddyCurrentTestType::VariableSpeed:
        return QStringLiteral("非标准件涡流力测试");
    }
    return QStringLiteral("未知");
}

QString safeSheetName(const QString& source, int fallbackIndex)
{
    QString name = source.trimmed();
    static const QString invalidCharacters = QStringLiteral("[]:*?/\\");
    for (const QChar character : invalidCharacters) {
        name.replace(character, QLatin1Char('_'));
    }
    if (name.isEmpty()) {
        name = QStringLiteral("实验-%1").arg(fallbackIndex);
    }
    return name.left(31);
}

bool initializeFirstSheet(QXlsx::Document* document,
                          const QString& sheetName)
{
    const QStringList existingSheetNames = document->sheetNames();
    if (existingSheetNames.isEmpty()) {
        return document->addSheet(sheetName)
               && document->selectSheet(sheetName);
    }

    const QString& firstSheetName = existingSheetNames.first();
    if (firstSheetName != sheetName
        && !document->renameSheet(firstSheetName, sheetName)) {
        return false;
    }
    return document->selectSheet(sheetName);
}

QXlsx::Format headingFormat()
{
    QXlsx::Format format;
    format.setFontBold(true);
    format.setFillPattern(QXlsx::Format::PatternSolid);
    format.setPatternForegroundColor(QColor(QStringLiteral("#DCEAF7")));
    return format;
}

void writeOptional(QXlsx::Document* document,
                   int row,
                   int column,
                   const std::optional<double>& value,
                   int decimals = kExperimentForceDisplayDecimals)
{
    if (value.has_value()) {
        QXlsx::Format numberFormat;
        numberFormat.setNumberFormat(
            decimals <= 0
                ? QStringLiteral("0")
                : QStringLiteral("0.")
                      + QString(decimals, QLatin1Char('0')));
        document->write(row, column, *value, numberFormat);
    }
}

int writeRecordInformation(QXlsx::Document* document,
                           const ExperimentRecord& record)
{
    const QXlsx::Format heading = headingFormat();
    document->write(1, 1, QStringLiteral("实验信息"), heading);
    const QVector<QPair<QString, QVariant>> values = {
        {QStringLiteral("实验名称"), record.experimentName},
        {QStringLiteral("基础实验名称"), record.baseExperimentName},
        {QStringLiteral("重复序号"), record.repetitionIndex},
        {QStringLiteral("计划重复次数"), record.plannedRepeatCount},
        {QStringLiteral("结束时间"),
         record.finishedAtUtc.toLocalTime().toString(
             QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
        {QStringLiteral("操作员"), record.operatorName},
        {QStringLiteral("实验状态"),
         experimentTerminalStateDisplayText(record.state)},
        {QStringLiteral("终止/故障原因"), record.terminalReason},
        {QStringLiteral("测试速度 (m/s)"), record.testSpeedMetersPerSecond},
        {QStringLiteral("统计位置起点 (m)"), record.statisticsStartMeters},
        {QStringLiteral("统计位置终点 (m)"), record.statisticsEndMeters},
        {QStringLiteral("原始采样数"), record.rawSampleCount},
        {QStringLiteral("有效采样数"),
         record.statistics.effectiveSampleCount},
        {QStringLiteral("测试类型"),
         testTypeText(record.parametersSnapshot.selectedTestType)},
        {QStringLiteral("电机型号"), record.parametersSnapshot.motorModel},
        {QStringLiteral("试验件编号"), record.parametersSnapshot.specimenId}
    };
    int row = 2;
    for (const QPair<QString, QVariant>& value : values) {
        document->write(row, 1, value.first);
        document->write(row, 2, value.second);
        ++row;
    }

    document->write(row, 1, QStringLiteral("统计结果"), heading);
    ++row;
    document->write(row, 1, QStringLiteral("平均涡流力 (N)"));
    writeOptional(document,
                  row++,
                  2,
                  record.statistics.averageForceNewtons);
    document->write(row, 1, QStringLiteral("涡流力系数 (N·s/m)"));
    writeOptional(
        document,
        row++,
        2,
        record.statistics.forceCoefficientNewtonSecondsPerMeter,
        kExperimentCoefficientDisplayDecimals);
    document->write(row, 1, QStringLiteral("涡流力波动值 (N)"));
    writeOptional(document,
                  row++,
                  2,
                  record.statistics.forceRangeNewtons);
    document->write(row, 1, QStringLiteral("涡流力波动率 (%)"));
    writeOptional(document,
                  row++,
                  2,
                  record.statistics.fluctuationRatePercent,
                  kExperimentRateDisplayDecimals);
    document->setColumnWidth(1, 24.0);
    document->setColumnWidth(2, 32.0);
    return row + 1;
}

void writeSampleHeader(QXlsx::Document* document, int row)
{
    const QXlsx::Format heading = headingFormat();
    const QStringList headers = {
        QStringLiteral("相对时间 (ms)"),
        QStringLiteral("加速度 (m/s²)"),
        QStringLiteral("速度 (m/s)"),
        QStringLiteral("电机电流"),
        QStringLiteral("电机温度"),
        QStringLiteral("涡流力 (N)"),
        QStringLiteral("位置 (m)")
    };
    for (qsizetype column = 0; column < headers.size(); ++column) {
        document->write(row,
                        static_cast<int>(column) + 1,
                        headers.at(column),
                        heading);
    }
    document->setColumnWidth(1, 7, 18.0);
}

} // namespace

class ExperimentExportWorker final : public QObject
{
    Q_OBJECT

public:
    void execute(qint64 recordId,
                 const QString& targetPath,
                 ExperimentExportKind kind)
    {
        recordId_ = recordId;
        targetPath_ = targetPath;
        kind_ = kind;
        writtenSampleCount_ = 0;
        elapsedTimer_.start();

        qCInfo(logExport).noquote()
            << "[导出][任务开始]"
            << "类型" << exportKindText(kind_)
            << "记录ID" << recordId_
            << "目标路径" << targetPath_;

        QString errorMessage;
        QVector<ExperimentRecord> records;
        if (kind_ == ExperimentExportKind::SingleRecord) {
            ExperimentRecord record;
            if (!AcquisitionDatabaseService::instance()
                     .readExperimentRecordForExport(
                         recordId_, &record, &errorMessage)) {
                reportFailure(errorMessage);
                return;
            }
            records.append(record);
            if (!initializeSingleWorkbook(record, &errorMessage)) {
                reportFailure(errorMessage);
                return;
            }
        } else {
            ExperimentSummaryBundle bundle;
            if (!AcquisitionDatabaseService::instance()
                     .readExperimentSummaryForExport(
                         recordId_, &bundle, &errorMessage)) {
                reportFailure(errorMessage);
                return;
            }
            records = bundle.records;
            if (!initializeSummaryWorkbook(bundle, &errorMessage)) {
                reportFailure(errorMessage);
                return;
            }
        }

        qCInfo(logExport)
            << "[导出][数据库读取完成]"
            << "记录ID" << recordId_
            << "待导出记录数" << records.size();
        emit progressChanged(10);

        for (qsizetype index = 0; index < records.size(); ++index) {
            const ExperimentRecord& record = records.at(index);
            if (kind_ == ExperimentExportKind::Summary
                && !createSummaryRecordSheet(record, &errorMessage)) {
                reportFailure(errorMessage);
                return;
            }
            if (!writeRecordSamples(
                    record, index, records.size(), &errorMessage)) {
                reportFailure(errorMessage);
                return;
            }
        }

        emit progressChanged(90);
        if (!saveWorkbook(&errorMessage)) {
            reportFailure(errorMessage);
            return;
        }

        const QString completedPath = targetPath_;
        const qint64 fileSize = QFileInfo(completedPath).size();
        const qint64 elapsedMilliseconds = elapsedTimer_.elapsed();
        qCInfo(logExport).noquote()
            << "[导出][任务完成]"
            << "类型" << exportKindText(kind_)
            << "记录ID" << recordId_
            << "已写样本数" << writtenSampleCount_
            << "文件大小(字节)" << fileSize
            << "耗时(ms)" << elapsedMilliseconds
            << "目标路径" << completedPath;
        clear();
        emit completed(completedPath);
    }

signals:
    void progressChanged(int percent);
    void completed(const QString& targetPath);
    void failed(const QString& message);

private:
    bool initializeSingleWorkbook(const ExperimentRecord& record,
                                  QString* errorMessage)
    {
        document_ = std::make_unique<QXlsx::Document>();
        if (!initializeFirstSheet(document_.get(),
                                  QStringLiteral("实验数据"))) {
            setError(errorMessage,
                     QStringLiteral("创建实验数据工作表失败。"));
            return false;
        }

        nextDataRow_ = writeRecordInformation(document_.get(), record);
        writeSampleHeader(document_.get(), nextDataRow_++);
        qCInfo(logExport).noquote()
            << "[导出][工作簿初始化]"
            << "类型" << exportKindText(kind_)
            << "记录ID" << record.id
            << "实验名称" << record.experimentName
            << "工作表" << QStringLiteral("实验数据");
        return true;
    }

    bool initializeSummaryWorkbook(
        const ExperimentSummaryBundle& bundle,
        QString* errorMessage)
    {
        document_ = std::make_unique<QXlsx::Document>();
        if (!initializeFirstSheet(document_.get(),
                                  QStringLiteral("汇总结果"))) {
            setError(errorMessage,
                     QStringLiteral("创建汇总结果工作表失败。"));
            return false;
        }

        const ExperimentSummaryRecord& summary = bundle.summary;
        const QXlsx::Format heading = headingFormat();
        document_->write(1, 1, QStringLiteral("汇总结果"), heading);
        const QVector<QPair<QString, QVariant>> values = {
            {QStringLiteral("基础实验名称"), summary.baseExperimentName},
            {QStringLiteral("结束时间"),
             summary.finishedAtUtc.toLocalTime().toString(
                 QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
            {QStringLiteral("操作员"), summary.operatorName},
            {QStringLiteral("汇总状态"),
             experimentTerminalStateDisplayText(summary.state)}
        };
        int row = 2;
        for (const QPair<QString, QVariant>& value : values) {
            document_->write(row, 1, value.first);
            document_->write(row, 2, value.second);
            ++row;
        }
        document_->write(row, 1,
                         QStringLiteral("多次平均涡流力 (N)"));
        writeOptional(document_.get(), row++, 2,
                      summary.statistics.multipleAverageForceNewtons);
        document_->write(
            row, 1,
            QStringLiteral("多次平均涡流力系数 (N·s/m)"));
        writeOptional(
            document_.get(), row, 2,
            summary.statistics
                .multipleAverageForceCoefficientNewtonSecondsPerMeter,
            kExperimentCoefficientDisplayDecimals);
        document_->setColumnWidth(1, 30.0);
        document_->setColumnWidth(2, 48.0);

        qCInfo(logExport).noquote()
            << "[导出][工作簿初始化]"
            << "类型" << exportKindText(kind_)
            << "记录ID" << recordId_
            << "汇总ID" << summary.id
            << "基础实验名称" << summary.baseExperimentName
            << "成员数" << bundle.records.size();
        return true;
    }

    bool createSummaryRecordSheet(const ExperimentRecord& record,
                                  QString* errorMessage)
    {
        QString sheetName =
            safeSheetName(record.experimentName, record.repetitionIndex);
        const QString baseName = sheetName;
        int duplicateIndex = 2;
        while (document_->sheetNames().contains(sheetName)) {
            const QString suffix =
                QStringLiteral("-%1").arg(duplicateIndex++);
            sheetName = baseName.left(31 - suffix.size()) + suffix;
        }

        if (!document_->addSheet(sheetName)
            || !document_->selectSheet(sheetName)) {
            setError(errorMessage,
                     QStringLiteral("创建实验明细工作表失败。"));
            return false;
        }

        nextDataRow_ = writeRecordInformation(document_.get(), record);
        writeSampleHeader(document_.get(), nextDataRow_++);
        qCInfo(logExport).noquote()
            << "[导出][创建明细工作表]"
            << "记录ID" << record.id
            << "实验名称" << record.experimentName
            << "工作表" << sheetName;
        return true;
    }

    bool writeRecordSamples(const ExperimentRecord& record,
                            qsizetype recordIndex,
                            qsizetype recordCount,
                            QString* errorMessage)
    {
        if (record.rawDataTableName.trimmed().isEmpty()) {
            qCInfo(logExport)
                << "[导出][无原始数据表]"
                << "记录ID" << record.id;
            emit progressChanged(
                progressForCompletedRecord(recordIndex, recordCount));
            return true;
        }

        qint64 offset = 0;
        qint64 recordSampleCount = 0;
        while (true) {
            QVector<ExperimentSample> samples;
            qCInfo(logExport).noquote()
                << "[导出][读取分页数据]"
                << "记录ID" << record.id
                << "数据表" << record.rawDataTableName
                << "偏移" << offset
                << "页大小" << kExportPageSize;
            if (!AcquisitionDatabaseService::instance()
                     .readExperimentRawDataForExport(
                         record.rawDataTableName,
                         record.statisticsStartMeters,
                         record.statisticsEndMeters,
                         offset,
                         kExportPageSize,
                         &samples,
                         errorMessage)) {
                return false;
            }

            for (const ExperimentSample& sample : samples) {
                document_->write(nextDataRow_, 1,
                                 sample.relativeTimeMilliseconds);
                document_->write(
                    nextDataRow_, 2,
                    sample.accelerationMetersPerSecondSquared);
                document_->write(nextDataRow_, 3,
                                 sample.velocityMetersPerSecond);
                document_->write(nextDataRow_, 4, sample.motorCurrent);
                document_->write(nextDataRow_, 5, sample.motorTemperature);
                document_->write(nextDataRow_, 6, sample.forceNewtons);
                document_->write(nextDataRow_, 7, sample.positionMeters);
                ++nextDataRow_;
            }

            const int pageSampleCount = samples.size();
            offset += pageSampleCount;
            recordSampleCount += pageSampleCount;
            writtenSampleCount_ += pageSampleCount;
            const bool recordCompleted =
                pageSampleCount < kExportPageSize;
            emit progressChanged(progressForRecord(
                record, recordIndex, recordCount,
                recordSampleCount, recordCompleted));
            qCInfo(logExport)
                << "[导出][分页写入完成]"
                << "记录ID" << record.id
                << "偏移" << offset
                << "本页样本数" << pageSampleCount
                << "累计样本数" << writtenSampleCount_;

            if (recordCompleted) {
                return true;
            }
        }
    }

    int progressForRecord(const ExperimentRecord& record,
                          qsizetype recordIndex,
                          qsizetype recordCount,
                          qint64 recordSampleCount,
                          bool recordCompleted) const
    {
        if (recordCount <= 0) {
            return 90;
        }

        const qint64 completedUnits =
            static_cast<qint64>(recordIndex) * 80;
        if (recordCompleted) {
            return 10 + static_cast<int>(
                            (completedUnits + 80) / recordCount);
        }

        const qint64 expectedSampleCount =
            std::max<qint64>(record.rawSampleCount, 1);
        const qint64 partialUnits = std::min<qint64>(
            recordSampleCount * 80 / expectedSampleCount, 79);
        return 10 + static_cast<int>(
                        (completedUnits + partialUnits) / recordCount);
    }

    int progressForCompletedRecord(qsizetype recordIndex,
                                   qsizetype recordCount) const
    {
        if (recordCount <= 0) {
            return 90;
        }
        return 10 + static_cast<int>(
                        (static_cast<qint64>(recordIndex) + 1)
                        * 80 / recordCount);
    }

    bool saveWorkbook(QString* errorMessage)
    {
        qCInfo(logExport).noquote()
            << "[导出][保存开始]"
            << "记录ID" << recordId_
            << "工作表数量" << document_->sheetNames().size()
            << "目标路径" << targetPath_;
        emit progressChanged(95);

        if (!document_->saveAs(targetPath_)) {
            setError(errorMessage,
                     QStringLiteral("QXlsx 写入工作簿失败。"));
            return false;
        }
        return true;
    }

    void reportFailure(const QString& message)
    {
        const QString effectiveMessage =
            message.trimmed().isEmpty()
                ? QStringLiteral("Excel 导出失败，未返回具体原因。")
                : message;
        qCWarning(logExport).noquote()
            << "[导出][任务失败]"
            << "类型" << exportKindText(kind_)
            << "记录ID" << recordId_
            << "已写样本数" << writtenSampleCount_
            << "耗时(ms)" << elapsedTimer_.elapsed()
            << "目标路径" << targetPath_
            << "原因" << effectiveMessage;
        clear();
        emit failed(effectiveMessage);
    }

    void clear()
    {
        document_.reset();
        recordId_ = 0;
        targetPath_.clear();
        writtenSampleCount_ = 0;
        nextDataRow_ = 1;
    }

    std::unique_ptr<QXlsx::Document> document_;
    qint64 recordId_ = 0;
    QString targetPath_;
    ExperimentExportKind kind_ = ExperimentExportKind::SingleRecord;
    qint64 writtenSampleCount_ = 0;
    int nextDataRow_ = 1;
    QElapsedTimer elapsedTimer_;
};

ExperimentExportService& ExperimentExportService::instance()
{
    static ExperimentExportService service;
    return service;
}

ExperimentExportService::ExperimentExportService()
{
    qRegisterMetaType<ExperimentExportKind>("ExperimentExportKind");
    workerThread_.setObjectName(QStringLiteral("ExperimentExcelExportThread"));
    worker_ = new ExperimentExportWorker;
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_,
            &QThread::finished,
            worker_,
            &QObject::deleteLater);
    connect(worker_,
            &ExperimentExportWorker::progressChanged,
            this,
            &ExperimentExportService::exportProgress,
            Qt::QueuedConnection);
    connect(worker_,
            &ExperimentExportWorker::completed,
            this,
            &ExperimentExportService::handleWriterCompleted,
            Qt::QueuedConnection);
    connect(worker_,
            &ExperimentExportWorker::failed,
            this,
            &ExperimentExportService::handleWriterFailed,
            Qt::QueuedConnection);
    workerThread_.start();
    qCInfo(logExport) << "[导出][服务启动] Excel 导出线程已启动";
}

ExperimentExportService::~ExperimentExportService()
{
    shutdown();
}

bool ExperimentExportService::isBusy() const
{
    return busy_;
}

bool ExperimentExportService::exportSingleRecord(
    qint64 recordId,
    const QString& targetPath,
    QString* errorMessage)
{
    return beginExport(recordId, targetPath,
                       ExperimentExportKind::SingleRecord,
                       errorMessage);
}

bool ExperimentExportService::exportSummary(
    qint64 summaryId,
    const QString& targetPath,
    QString* errorMessage)
{
    return beginExport(summaryId, targetPath,
                       ExperimentExportKind::Summary,
                       errorMessage);
}

bool ExperimentExportService::beginExport(
    qint64 recordId,
    const QString& targetPath,
    ExperimentExportKind kind,
    QString* errorMessage)
{
    if (busy_) {
        setError(errorMessage,
                 QStringLiteral("已有 Excel 导出任务正在执行。"));
        qCWarning(logExport)
            << "[导出][任务拒绝] 已有导出任务正在执行";
        return false;
    }
    if (recordId <= 0 || targetPath.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("Excel 导出参数无效。"));
        qCWarning(logExport).noquote()
            << "[导出][任务拒绝] 参数无效"
            << "记录ID" << recordId
            << "目标路径" << targetPath;
        return false;
    }
    if (!AcquisitionDatabaseService::instance().isReady()) {
        setError(errorMessage,
                 QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }

    const QString absoluteTargetPath =
        QFileInfo(targetPath).absoluteFilePath();
    busy_ = true;
    const bool invoked = QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, recordId, absoluteTargetPath, kind] {
            worker->execute(recordId, absoluteTargetPath, kind);
        },
        Qt::QueuedConnection);
    if (!invoked) {
        busy_ = false;
        setError(errorMessage,
                 QStringLiteral("无法启动 Excel 导出线程任务。"));
        return false;
    }

    emit exportStarted(kind);
    emit exportProgress(0);
    return true;
}

void ExperimentExportService::shutdown()
{
    if (!workerThread_.isRunning()) {
        return;
    }

    qCInfo(logExport)
        << "[导出][服务关闭] 等待 Excel 导出线程结束";
    workerThread_.quit();
    workerThread_.wait();
    worker_ = nullptr;
    busy_ = false;
    qCInfo(logExport)
        << "[导出][服务关闭] Excel 导出线程已停止";
}

void ExperimentExportService::handleWriterCompleted(
    const QString& targetPath)
{
    busy_ = false;
    emit exportCompleted(targetPath);
}

void ExperimentExportService::handleWriterFailed(
    const QString& message)
{
    busy_ = false;
    emit exportFailed(message);
}

#include "ExperimentExportService.moc"
