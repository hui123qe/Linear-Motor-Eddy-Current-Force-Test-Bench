#include "AcquisitionDatabaseService.h"

#include "../logging/AppLogger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kDatabaseConfigurationSchemaVersion = 2;
constexpr int kInsertRowsPerStatement = 500;
constexpr int kMaximumReadPageSize = 100000;
constexpr int kMaximumExperimentRecordPageSize = 1000;
constexpr int kPostgreSqlIdentifierByteLimit = 63;

struct DatabaseConfiguration
{
    QString host;
    int port = 5432;
    QString databaseName;
    QString userName;
    QString password;
    QString schemaName = QStringLiteral("public");
    int connectTimeoutSeconds = 5;
};

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QString configurationFilePath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("database.json"));
}

bool loadConfiguration(DatabaseConfiguration* configuration,
                       QString* errorMessage)
{
    QFile file(configurationFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(
            errorMessage,
            QStringLiteral("无法读取 PostgreSQL 配置文件：%1\n%2")
                .arg(file.fileName(), file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(
            errorMessage,
            QStringLiteral("PostgreSQL 配置文件格式错误：%1")
                .arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1)
        != kDatabaseConfigurationSchemaVersion) {
        setError(errorMessage, QStringLiteral("database.json schemaVersion 必须为 2。"));
        return false;
    }

    configuration->host = root.value(QStringLiteral("host")).toString().trimmed();
    configuration->port = root.value(QStringLiteral("port")).toInt(5432);
    configuration->databaseName =
        root.value(QStringLiteral("databaseName")).toString().trimmed();
    configuration->userName =
        root.value(QStringLiteral("userName")).toString().trimmed();
    configuration->password =
        root.value(QStringLiteral("password")).toString();
    configuration->schemaName =
        root.value(QStringLiteral("schemaName")).toString().trimmed();
    configuration->connectTimeoutSeconds =
        root.value(QStringLiteral("connectTimeoutSeconds")).toInt(5);

    static const QRegularExpression identifierPattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (configuration->host.isEmpty()
        || configuration->port <= 0
        || configuration->port > 65535
        || configuration->databaseName.isEmpty()
        || configuration->userName.isEmpty()
        || configuration->password.isEmpty()
        || !identifierPattern.match(configuration->schemaName).hasMatch()
        || configuration->connectTimeoutSeconds <= 0
        || configuration->connectTimeoutSeconds > 60) {
        setError(errorMessage, QStringLiteral("database.json 中的连接或 schema 配置无效。"));
        return false;
    }
    return true;
}

QString normalizedTableComponent(const QString& value)
{
    QString result = value.trimmed().toLower();
    result.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                   QStringLiteral("_"));
    result.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (!result.isEmpty()) {
        return result;
    }

    return QStringLiteral("u_%1")
        .arg(QString::fromLatin1(
            QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256)
                .toHex()
                .left(8)));
}

QString createTableName(const QString& motorModel,
                        const QString& specimenId,
                        int repetitionIndex)
{
    const QString batch = normalizedTableComponent(motorModel)
                          + QStringLiteral("_")
                          + normalizedTableComponent(specimenId);
    const QString suffix = QStringLiteral("_%1_r%2")
                               .arg(QDateTime::currentDateTime().toString(
                                        QStringLiteral("yyyyMMdd_HHmmss_zzz")))
                               .arg(repetitionIndex, 3, 10, QLatin1Char('0'));
    QString tableName = batch + suffix;
    if (tableName.toUtf8().size() <= kPostgreSqlIdentifierByteLimit) {
        return tableName;
    }

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(batch.toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .left(8));
    const int prefixLength =
        kPostgreSqlIdentifierByteLimit - suffix.toUtf8().size() - hash.size() - 1;
    return batch.left(std::max(prefixLength, 1))
           + QStringLiteral("_") + hash + suffix;
}

QString sqlErrorMessage(const QString& action,
                        const QSqlError& error)
{
    return QStringLiteral("%1：%2").arg(action, error.text().trimmed());
}

bool blockChannelsValid(const AcquisitionBlock& block)
{
    const qsizetype sampleCount = block.sampleCount;
    return sampleCount > 0
           && block.accelerationMetersPerSecondSquared.size() == sampleCount
           && block.velocityMetersPerSecond.size() == sampleCount
           && block.motorCurrent.size() == sampleCount
           && block.motorTemperature.size() == sampleCount
           && block.forceNewtons.size() == sampleCount
           && block.positionMeters.size() == sampleCount
           && std::isfinite(block.samplePeriodSeconds)
           && block.samplePeriodSeconds > 0.0;
}

QVariant nullableDouble(const std::optional<double>& value)
{
    if (!value.has_value()) {
        return QVariant(QMetaType::fromType<double>());
    }
    return QVariant(*value);
}

QString experimentRecordSelectColumns(const QString& prefix = {})
{
    const QString qualifiedPrefix =
        prefix.isEmpty() ? QString() : prefix + QLatin1Char('.');
    const QStringList columns = {
        QStringLiteral("id"),
        QStringLiteral("execution_id"),
        QStringLiteral("experiment_name"),
        QStringLiteral("base_experiment_name"),
        QStringLiteral("repetition_index"),
        QStringLiteral("planned_repeat_count"),
        QStringLiteral("finished_at"),
        QStringLiteral("operator_name"),
        QStringLiteral("terminal_state"),
        QStringLiteral("terminal_reason"),
        QStringLiteral("test_speed_m_s"),
        QStringLiteral("statistics_start_m"),
        QStringLiteral("statistics_end_m"),
        QStringLiteral("raw_sample_count"),
        QStringLiteral("effective_sample_count"),
        QStringLiteral("average_force_n"),
        QStringLiteral("force_coefficient_n_s_m"),
        QStringLiteral("force_range_n"),
        QStringLiteral("fluctuation_rate_percent"),
        QStringLiteral("raw_data_table_name"),
        QStringLiteral("parameters_snapshot")
    };

    QStringList qualifiedColumns;
    qualifiedColumns.reserve(columns.size());
    for (const QString& column : columns) {
        qualifiedColumns.append(qualifiedPrefix + column);
    }
    return qualifiedColumns.join(QLatin1Char(','));
}

bool readExperimentRecord(const QSqlQuery& query,
                          int firstColumn,
                          ExperimentRecord* record,
                          QString* errorMessage)
{
    ExperimentRecord parsed;
    parsed.id = query.value(firstColumn).toLongLong();
    parsed.executionId = query.value(firstColumn + 1).toLongLong();
    parsed.experimentName = query.value(firstColumn + 2).toString();
    parsed.baseExperimentName = query.value(firstColumn + 3).toString();
    parsed.repetitionIndex = query.value(firstColumn + 4).toInt();
    parsed.plannedRepeatCount = query.value(firstColumn + 5).toInt();
    parsed.finishedAtUtc = query.value(firstColumn + 6).toDateTime().toUTC();
    parsed.operatorName = query.value(firstColumn + 7).toString();
    if (!experimentTerminalStateFromDatabaseValue(
            query.value(firstColumn + 8).toString(), &parsed.state)) {
        setError(errorMessage, QStringLiteral("数据库包含无法识别的实验最终状态。"));
        return false;
    }
    parsed.terminalReason = query.value(firstColumn + 9).toString();
    parsed.testSpeedMetersPerSecond = query.value(firstColumn + 10).toDouble();
    parsed.statisticsStartMeters = query.value(firstColumn + 11).toDouble();
    parsed.statisticsEndMeters = query.value(firstColumn + 12).toDouble();
    parsed.rawSampleCount = query.value(firstColumn + 13).toLongLong();
    parsed.statistics.effectiveSampleCount =
        query.value(firstColumn + 14).toLongLong();
    if (!query.isNull(firstColumn + 15)) {
        parsed.statistics.averageForceNewtons =
            query.value(firstColumn + 15).toDouble();
    }
    if (!query.isNull(firstColumn + 16)) {
        parsed.statistics.forceCoefficientNewtonSecondsPerMeter =
            query.value(firstColumn + 16).toDouble();
    }
    if (!query.isNull(firstColumn + 17)) {
        parsed.statistics.forceRangeNewtons =
            query.value(firstColumn + 17).toDouble();
    }
    if (!query.isNull(firstColumn + 18)) {
        parsed.statistics.fluctuationRatePercent =
            query.value(firstColumn + 18).toDouble();
    }
    parsed.rawDataTableName = query.value(firstColumn + 19).toString();

    QJsonParseError parseError;
    const QJsonDocument parametersDocument = QJsonDocument::fromJson(
        query.value(firstColumn + 20).toString().toUtf8(), &parseError);
    QString parametersError;
    if (parseError.error != QJsonParseError::NoError
        || !parametersDocument.isObject()
        || !deserializeTestParameters(parametersDocument.object(),
                                      &parsed.parametersSnapshot,
                                      &parametersError)) {
        setError(errorMessage,
                 parametersError.isEmpty()
                     ? QStringLiteral("实验参数快照不是有效 JSON 对象。")
                     : QStringLiteral("实验参数快照无效：%1")
                           .arg(parametersError));
        return false;
    }

    *record = parsed;
    return true;
}

} // namespace

class AcquisitionDatabaseWorker final : public QObject
{
    Q_OBJECT

public:
    AcquisitionDatabaseWorker() = default;

    void initialize(const DatabaseConfiguration& configuration)
    {
        configuration_ = configuration;
        connectionName_ = QStringLiteral("acquisition-postgresql-%1")
                              .arg(reinterpret_cast<quintptr>(
                                  QThread::currentThreadId()));
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"), connectionName_);
        database_.setHostName(configuration_.host);
        database_.setPort(configuration_.port);
        database_.setDatabaseName(configuration_.databaseName);
        database_.setUserName(configuration_.userName);
        database_.setPassword(configuration_.password);
        database_.setConnectOptions(
            QStringLiteral("connect_timeout=%1").arg(configuration_.connectTimeoutSeconds));

        if (!database_.open()) {
            emit ready(
                false,
                sqlErrorMessage(
                    QStringLiteral("连接 PostgreSQL 失败"),
                    database_.lastError()));
            return;
        }

        QString schemaError;
        if (!initializeExperimentLogSchema(&schemaError)) {
            database_.close();
            emit ready(false, schemaError);
            return;
        }

        emit ready(
            true,
            QStringLiteral("PostgreSQL 已连接：%1:%2/%3")
                .arg(configuration_.host)
                .arg(configuration_.port)
                .arg(configuration_.databaseName));
    }

    void shutdown()
    {
        currentTableName_.clear();
        if (database_.isValid()) {
            database_.close();
        }
        database_ = QSqlDatabase();
        if (!connectionName_.isEmpty()) {
            QSqlDatabase::removeDatabase(connectionName_);
            connectionName_.clear();
        }
    }

    void beginExperimentTable(const QString& motorModel,
                              const QString& specimenId,
                              int repetitionIndex,
                              double samplePeriodSeconds)
    {
        if (fatalError_) {
            return;
        }
        if (!database_.isOpen()) {
            reportFatal(QStringLiteral("PostgreSQL 未连接，不能创建实验数据表。"));
            return;
        }
        if (!currentTableName_.isEmpty()) {
            reportFatal(QStringLiteral("上一张实验数据表尚未结束。"));
            return;
        }

        const QString tableName =
            createTableName(motorModel, specimenId, repetitionIndex);
        const QString qualifiedTableName = qualifyTable(tableName);
        QSqlQuery query(database_);
        const QString createSql =
            QStringLiteral(
                "CREATE TABLE %1 ("
                "relative_time_ms DOUBLE PRECISION PRIMARY KEY,"
                "acceleration_m_s2 DOUBLE PRECISION NOT NULL,"
                "velocity_m_s DOUBLE PRECISION NOT NULL,"
                "motor_current DOUBLE PRECISION NOT NULL,"
                "motor_temperature DOUBLE PRECISION NOT NULL,"
                "force_n DOUBLE PRECISION NOT NULL,"
                "position_m DOUBLE PRECISION NOT NULL)")
                .arg(qualifiedTableName);
        if (!query.exec(createSql)) {
            reportFatal(sqlErrorMessage(
                QStringLiteral("创建实验数据表 %1 失败").arg(tableName),
                query.lastError()));
            return;
        }

        currentTableName_ = tableName;
        currentRepetitionIndex_ = repetitionIndex;
        samplePeriodMilliseconds_ = samplePeriodSeconds * 1000.0;
        sampleOffset_ = 0;
        emit tableCreated(repetitionIndex, tableName);
    }

    void appendBlock(const AcquisitionBlock& block)
    {
        if (fatalError_) {
            return;
        }
        if (!database_.isOpen() || currentTableName_.isEmpty()) {
            reportFatal(QStringLiteral("没有活动的 PostgreSQL 实验数据表。"));
            return;
        }
        if (!blockChannelsValid(block)) {
            reportFatal(QStringLiteral("采集块通道长度或采样周期无效，拒绝写入数据库。"));
            return;
        }
        const double blockPeriodMilliseconds = block.samplePeriodSeconds * 1000.0;
        if (std::abs(blockPeriodMilliseconds - samplePeriodMilliseconds_) > 1.0e-9) {
            reportFatal(QStringLiteral("采集块采样周期与当前实验表不一致。"));
            return;
        }
        // 一个采集块是最小一致性单元：要么全部提交，要么全部回滚。
        if (!database_.transaction()) {
            reportFatal(sqlErrorMessage(
                QStringLiteral("启动 PostgreSQL 写入事务失败"),
                database_.lastError()));
            return;
        }

        const QString tableName = qualifyTable(currentTableName_);
        int firstRow = 0;
        // 分批生成参数化 INSERT，避免单条 SQL 包含过多参数。
        while (firstRow < block.sampleCount) {
            const int rowCount =
                std::min(kInsertRowsPerStatement, block.sampleCount - firstRow);
            QStringList rows;
            rows.reserve(rowCount);
            for (int row = 0; row < rowCount; ++row) {
                rows.append(QStringLiteral("(?,?,?,?,?,?,?)"));
            }

            QSqlQuery query(database_);
            const QString insertSql =
                QStringLiteral(
                    "INSERT INTO %1 (relative_time_ms,acceleration_m_s2,"
                    "velocity_m_s,motor_current,motor_temperature,force_n,position_m) "
                    "VALUES %2")
                    .arg(tableName, rows.join(QLatin1Char(',')));
            if (!query.prepare(insertSql)) {
                database_.rollback();
                reportFatal(sqlErrorMessage(
                    QStringLiteral("准备 PostgreSQL 批量写入语句失败"),
                    query.lastError()));
                return;
            }

            for (int row = 0; row < rowCount; ++row) {
                const int sampleIndex = firstRow + row;
                const double relativeTimeMilliseconds =
                    static_cast<double>(sampleOffset_ + sampleIndex)
                    * samplePeriodMilliseconds_;
                query.addBindValue(relativeTimeMilliseconds);
                query.addBindValue(
                    block.accelerationMetersPerSecondSquared.at(sampleIndex));
                query.addBindValue(block.velocityMetersPerSecond.at(sampleIndex));
                query.addBindValue(block.motorCurrent.at(sampleIndex));
                query.addBindValue(block.motorTemperature.at(sampleIndex));
                query.addBindValue(block.forceNewtons.at(sampleIndex));
                query.addBindValue(block.positionMeters.at(sampleIndex));
            }
            if (!query.exec()) {
                database_.rollback();
                reportFatal(sqlErrorMessage(
                    QStringLiteral("写入 PostgreSQL 实验数据失败"),
                    query.lastError()));
                return;
            }
            firstRow += rowCount;
        }

        if (!database_.commit()) {
            database_.rollback();
            reportFatal(sqlErrorMessage(
                QStringLiteral("提交 PostgreSQL 实验数据失败"),
                database_.lastError()));
            return;
        }

        sampleOffset_ += block.sampleCount;
    }

    void finishExperimentTable()
    {
        if (fatalError_) {
            return;
        }
        if (currentTableName_.isEmpty()) {
            reportFatal(QStringLiteral("没有可结束的 PostgreSQL 实验数据表。"));
            return;
        }

        const QString tableName = currentTableName_;
        const int repetitionIndex = currentRepetitionIndex_;
        const qint64 sampleCount = sampleOffset_;
        currentTableName_.clear();
        currentRepetitionIndex_ = 0;
        sampleOffset_ = 0;
        samplePeriodMilliseconds_ = 0.0;
        emit tableFinished(repetitionIndex, tableName, sampleCount);
    }

    void requestExperimentTables()
    {
        if (fatalError_) {
            return;
        }
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema = ? AND table_type = 'BASE TABLE' "
            "ORDER BY table_name DESC"));
        query.addBindValue(configuration_.schemaName);
        if (!query.exec()) {
            reportFatal(sqlErrorMessage(
                QStringLiteral("读取 PostgreSQL 实验表列表失败"),
                query.lastError()));
            return;
        }

        QStringList tableNames;
        while (query.next()) {
            tableNames.append(query.value(0).toString());
        }
        emit tablesRead(tableNames);
    }

    void requestExperimentData(const QString& tableName,
                               qint64 offset,
                               int limit)
    {
        if (fatalError_) {
            return;
        }
        static const QRegularExpression identifierPattern(
            QStringLiteral("^[a-z0-9_]+$"));
        if (!identifierPattern.match(tableName).hasMatch()) {
            reportFatal(QStringLiteral("实验表名无效。"));
            return;
        }

        QSqlQuery query(database_);
        const QString selectSql = QStringLiteral(
            "SELECT relative_time_ms,acceleration_m_s2,velocity_m_s,"
            "motor_current,motor_temperature,force_n,position_m "
            "FROM %1 ORDER BY relative_time_ms LIMIT ? OFFSET ?")
                                      .arg(qualifyTable(tableName));
        query.prepare(selectSql);
        query.addBindValue(limit);
        query.addBindValue(offset);
        if (!query.exec()) {
            reportFatal(sqlErrorMessage(
                QStringLiteral("读取 PostgreSQL 实验数据失败"),
                query.lastError()));
            return;
        }

        QVector<ExperimentSample> samples;
        samples.reserve(limit);
        while (query.next()) {
            ExperimentSample sample;
            sample.relativeTimeMilliseconds = query.value(0).toDouble();
            sample.accelerationMetersPerSecondSquared = query.value(1).toDouble();
            sample.velocityMetersPerSecond = query.value(2).toDouble();
            sample.motorCurrent = query.value(3).toDouble();
            sample.motorTemperature = query.value(4).toDouble();
            sample.forceNewtons = query.value(5).toDouble();
            sample.positionMeters = query.value(6).toDouble();
            samples.append(sample);
        }
        emit dataRead(tableName, offset, samples);
    }

    void requestExperimentForceAggregate(qint64 executionId,
                                         int repetitionIndex,
                                         const QString& tableName,
                                         double statisticsStartMeters,
                                         double statisticsEndMeters)
    {
        qCInfo(logCompletionReceipt)
            << "[数据库线程][单次统计] 开始查询"
            << "executionId=" << executionId
            << "repetition=" << repetitionIndex
            << "table=" << tableName
            << "statisticsStartMeters=" << statisticsStartMeters
            << "statisticsEndMeters=" << statisticsEndMeters;
        if (!database_.isOpen()) {
            qCWarning(logCompletionReceipt)
                << "[数据库线程][单次统计] 查询失败：数据库未连接"
                << "executionId=" << executionId
                << "repetition=" << repetitionIndex;
            emit forceAggregateFailed(
                executionId,
                repetitionIndex,
                QStringLiteral("PostgreSQL 未连接，不能统计实验结果。"));
            return;
        }
        static const QRegularExpression identifierPattern(
            QStringLiteral("^[a-z0-9_]+$"));
        if (!identifierPattern.match(tableName).hasMatch()) {
            qCWarning(logCompletionReceipt)
                << "[数据库线程][单次统计] 查询失败：原始数据表名无效"
                << "executionId=" << executionId
                << "repetition=" << repetitionIndex
                << "table=" << tableName;
            emit forceAggregateFailed(
                executionId,
                repetitionIndex,
                QStringLiteral("实验原始数据表名无效。"));
            return;
        }

        const double minimumPosition =
            std::min(statisticsStartMeters, statisticsEndMeters);
        const double maximumPosition =
            std::max(statisticsStartMeters, statisticsEndMeters);
        QSqlQuery query(database_);
        query.prepare(
            QStringLiteral(
                "SELECT COUNT(force_n), AVG(force_n), MIN(force_n), MAX(force_n) "
                "FROM %1 WHERE position_m BETWEEN ? AND ?")
                .arg(qualifyTable(tableName)));
        query.addBindValue(minimumPosition);
        query.addBindValue(maximumPosition);
        if (!query.exec() || !query.next()) {
            qCWarning(logCompletionReceipt).noquote()
                << "[数据库线程][单次统计] SQL 查询失败"
                << "executionId=" << executionId
                << "repetition=" << repetitionIndex
                << "reason=" << query.lastError().text().trimmed();
            emit forceAggregateFailed(
                executionId,
                repetitionIndex,
                sqlErrorMessage(
                    QStringLiteral("统计实验有效位置区间失败"),
                    query.lastError()));
            return;
        }

        ExperimentForceAggregate aggregate;
        aggregate.effectiveSampleCount = query.value(0).toLongLong();
        if (aggregate.effectiveSampleCount > 0) {
            aggregate.averageForceNewtons = query.value(1).toDouble();
            aggregate.minimumForceNewtons = query.value(2).toDouble();
            aggregate.maximumForceNewtons = query.value(3).toDouble();
        }
        qCInfo(logCompletionReceipt)
            << "[数据库线程][单次统计] 查询完成，发送 forceAggregateRead"
            << "executionId=" << executionId
            << "repetition=" << repetitionIndex
            << "effectiveSampleCount=" << aggregate.effectiveSampleCount;
        emit forceAggregateRead(executionId, repetitionIndex, aggregate);
    }

    void storeExperimentRecord(const ExperimentRecord& record)
    {
        qCInfo(logCompletionReceipt)
            << "[数据库线程][单次入库] 开始保存"
            << "executionId=" << record.executionId
            << "repetition=" << record.repetitionIndex
            << "rawSampleCount=" << record.rawSampleCount
            << "effectiveSampleCount="
            << record.statistics.effectiveSampleCount;
        if (!database_.isOpen()) {
            qCWarning(logCompletionReceipt)
                << "[数据库线程][单次入库] 保存失败：数据库未连接"
                << "executionId=" << record.executionId
                << "repetition=" << record.repetitionIndex;
            emit recordStoreFailed(
                record.executionId,
                record.repetitionIndex,
                QStringLiteral("PostgreSQL 未连接，不能保存实验记录。"));
            return;
        }

        const QString stateValue =
            experimentTerminalStateDatabaseValue(record.state);
        if (stateValue.isEmpty()) {
            qCWarning(logCompletionReceipt)
                << "[数据库线程][单次入库] 保存失败：实验最终状态无效"
                << "executionId=" << record.executionId
                << "repetition=" << record.repetitionIndex;
            emit recordStoreFailed(
                record.executionId,
                record.repetitionIndex,
                QStringLiteral("实验最终状态无效。"));
            return;
        }

        const QString recordsTable =
            qualifyTable(QStringLiteral("experiment_records"));
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "INSERT INTO %1 (execution_id,experiment_name,base_experiment_name,"
            "repetition_index,planned_repeat_count,finished_at,operator_name,"
            "terminal_state,terminal_reason,test_speed_m_s,statistics_start_m,"
            "statistics_end_m,raw_sample_count,effective_sample_count,"
            "average_force_n,force_coefficient_n_s_m,force_range_n,"
            "fluctuation_rate_percent,raw_data_table_name,parameters_snapshot) "
            "VALUES (?,?,?,?,?,?,NULLIF(?,''),?,NULLIF(?,''),?,?,?,?,?,?,?,?,?,"
            "NULLIF(?,''),CAST(? AS JSONB)) "
            "ON CONFLICT (execution_id,repetition_index) DO NOTHING RETURNING id")
                          .arg(recordsTable));
        query.addBindValue(record.executionId);
        query.addBindValue(record.experimentName);
        query.addBindValue(record.baseExperimentName);
        query.addBindValue(record.repetitionIndex);
        query.addBindValue(record.plannedRepeatCount);
        query.addBindValue(record.finishedAtUtc);
        query.addBindValue(record.operatorName.trimmed());
        query.addBindValue(stateValue);
        query.addBindValue(record.terminalReason.trimmed());
        query.addBindValue(record.testSpeedMetersPerSecond);
        query.addBindValue(record.statisticsStartMeters);
        query.addBindValue(record.statisticsEndMeters);
        query.addBindValue(record.rawSampleCount);
        query.addBindValue(record.statistics.effectiveSampleCount);
        query.addBindValue(nullableDouble(record.statistics.averageForceNewtons));
        query.addBindValue(nullableDouble(
            record.statistics.forceCoefficientNewtonSecondsPerMeter));
        query.addBindValue(nullableDouble(record.statistics.forceRangeNewtons));
        query.addBindValue(nullableDouble(
            record.statistics.fluctuationRatePercent));
        query.addBindValue(record.rawDataTableName);
        query.addBindValue(QString::fromUtf8(
            QJsonDocument(serializeTestParameters(record.parametersSnapshot))
                .toJson(QJsonDocument::Compact)));
        if (!query.exec()) {
            qCWarning(logCompletionReceipt).noquote()
                << "[数据库线程][单次入库] SQL 插入失败"
                << "executionId=" << record.executionId
                << "repetition=" << record.repetitionIndex
                << "reason=" << query.lastError().text().trimmed();
            emit recordStoreFailed(
                record.executionId,
                record.repetitionIndex,
                sqlErrorMessage(
                    QStringLiteral("保存实验流程记录失败"),
                    query.lastError()));
            return;
        }

        ExperimentRecord storedRecord = record;
        if (query.next()) {
            storedRecord.id = query.value(0).toLongLong();
            qCInfo(logCompletionReceipt)
                << "[数据库线程][单次入库] 插入完成，发送 recordStored"
                << "executionId=" << storedRecord.executionId
                << "repetition=" << storedRecord.repetitionIndex
                << "recordId=" << storedRecord.id;
            emit recordStored(storedRecord);
            return;
        }

        QSqlQuery existingQuery(database_);
        existingQuery.prepare(QStringLiteral(
            "SELECT id,experiment_name,base_experiment_name,terminal_state,"
            "COALESCE(raw_data_table_name,'') FROM %1 "
            "WHERE execution_id = ? AND repetition_index = ?")
                                  .arg(recordsTable));
        existingQuery.addBindValue(record.executionId);
        existingQuery.addBindValue(record.repetitionIndex);
        if (!existingQuery.exec() || !existingQuery.next()) {
            qCWarning(logCompletionReceipt).noquote()
                << "[数据库线程][单次入库] 读取幂等记录失败"
                << "executionId=" << record.executionId
                << "repetition=" << record.repetitionIndex
                << "reason=" << existingQuery.lastError().text().trimmed();
            emit recordStoreFailed(
                record.executionId,
                record.repetitionIndex,
                sqlErrorMessage(
                    QStringLiteral("读取幂等实验记录失败"),
                    existingQuery.lastError()));
            return;
        }
        if (existingQuery.value(1).toString() != record.experimentName
            || existingQuery.value(2).toString() != record.baseExperimentName
            || existingQuery.value(3).toString() != stateValue
            || existingQuery.value(4).toString() != record.rawDataTableName) {
            qCWarning(logCompletionReceipt)
                << "[数据库线程][单次入库] 幂等记录内容冲突"
                << "executionId=" << record.executionId
                << "repetition=" << record.repetitionIndex;
            emit recordStoreFailed(
                record.executionId,
                record.repetitionIndex,
                QStringLiteral("同一执行与重复序号已存在内容冲突的实验记录。"));
            return;
        }

        storedRecord.id = existingQuery.value(0).toLongLong();
        qCInfo(logCompletionReceipt)
            << "[数据库线程][单次入库] 幂等记录确认完成，发送 recordStored"
            << "executionId=" << storedRecord.executionId
            << "repetition=" << storedRecord.repetitionIndex
            << "recordId=" << storedRecord.id;
        emit recordStored(storedRecord);
    }

    void requestExperimentRecords(qint64 requestId,
                                  const ExperimentRecordFilter& filter)
    {
        if (!database_.isOpen()) {
            emit recordsReadFailed(
                requestId,
                QStringLiteral("PostgreSQL 未连接，不能查询实验记录。"));
            return;
        }

        const QDateTime fromUtc =
            QDateTime(filter.fromDate, QTime(0, 0)).toUTC();
        const QDateTime toExclusiveUtc =
            QDateTime(filter.toDate.addDays(1), QTime(0, 0)).toUTC();
        const QString keyword = filter.keyword.trimmed();
        const QString recordsTable =
            qualifyTable(QStringLiteral("experiment_records"));
        const QString summariesTable =
            qualifyTable(QStringLiteral("experiment_summaries"));
        QString selectSql = QStringLiteral(
            "SELECT entry_kind,id,finished_at,operator_name,terminal_state,"
            "terminal_reason,experiment_name FROM ("
            "SELECT 0 AS entry_kind,id,finished_at,operator_name,terminal_state,"
            "terminal_reason,experiment_name FROM %1 UNION ALL "
            "SELECT 1 AS entry_kind,id,finished_at,operator_name,summary_state "
            "AS terminal_state,'' AS terminal_reason,base_experiment_name || "
            "'-汇总' AS experiment_name FROM %2) entries "
            "WHERE finished_at >= ? AND finished_at < ?")
                                .arg(recordsTable, summariesTable);
        if (!keyword.isEmpty()) {
            selectSql += QStringLiteral(
                " AND (POSITION(LOWER(?) IN LOWER(experiment_name)) > 0"
                " OR POSITION(LOWER(?) IN LOWER(COALESCE(operator_name,''))) > 0"
                " OR POSITION(LOWER(?) IN LOWER(CASE terminal_state"
                " WHEN 'completed' THEN '完成' WHEN 'failed' THEN '故障'"
                " WHEN 'stopped' THEN '终止' ELSE terminal_state END)) > 0"
                " OR POSITION(LOWER(?) IN LOWER(COALESCE(terminal_reason,''))) > 0)");
        }
        selectSql += QStringLiteral(
            " ORDER BY finished_at DESC, id DESC LIMIT ? OFFSET ?");

        QSqlQuery query(database_);
        query.prepare(selectSql);
        query.addBindValue(fromUtc);
        query.addBindValue(toExclusiveUtc);
        if (!keyword.isEmpty()) {
            query.addBindValue(keyword);
            query.addBindValue(keyword);
            query.addBindValue(keyword);
            query.addBindValue(keyword);
        }
        query.addBindValue(filter.limit);
        query.addBindValue(filter.offset);
        if (!query.exec()) {
            emit recordsReadFailed(
                requestId,
                sqlErrorMessage(
                    QStringLiteral("查询实验流程记录失败"),
                    query.lastError()));
            return;
        }

        QVector<ExperimentRecordListItem> records;
        records.reserve(filter.limit);
        while (query.next()) {
            ExperimentRecordListItem item;
            item.kind = query.value(0).toInt() == 0
                            ? ExperimentLogEntryKind::SingleRecord
                            : ExperimentLogEntryKind::Summary;
            item.id = query.value(1).toLongLong();
            item.finishedAtUtc = query.value(2).toDateTime().toUTC();
            item.operatorName = query.value(3).toString();
            if (!experimentTerminalStateFromDatabaseValue(
                    query.value(4).toString(), &item.state)) {
                emit recordsReadFailed(
                    requestId,
                    QStringLiteral("数据库包含无法识别的实验最终状态。"));
                return;
            }
            item.terminalReason = query.value(5).toString();
            item.experimentName = query.value(6).toString();
            records.append(item);
        }
        emit recordsRead(requestId, records);
    }

    void storeExperimentSummary(const ExperimentSummaryBundle& bundle)
    {
        const ExperimentSummaryRecord& summary = bundle.summary;
        qCInfo(logCompletionReceipt)
            << "[数据库线程][整组入库] 开始保存"
            << "executionId=" << summary.executionId
            << "memberCount=" << bundle.records.size();
        if (!database_.isOpen()) {
            qCWarning(logCompletionReceipt)
                << "[数据库线程][整组入库] 保存失败：数据库未连接"
                << "executionId=" << summary.executionId;
            emit summaryStoreFailed(
                summary.executionId,
                QStringLiteral("PostgreSQL 未连接，不能保存实验汇总。"));
            return;
        }

        const QString stateValue =
            experimentTerminalStateDatabaseValue(summary.state);
        if (stateValue.isEmpty()) {
            emit summaryStoreFailed(
                summary.executionId,
                QStringLiteral("实验汇总状态无效。"));
            return;
        }

        if (!database_.transaction()) {
            qCWarning(logCompletionReceipt).noquote()
                << "[数据库线程][整组入库] 启动事务失败"
                << "executionId=" << summary.executionId
                << "reason=" << database_.lastError().text().trimmed();
            emit summaryStoreFailed(
                summary.executionId,
                sqlErrorMessage(QStringLiteral("启动实验汇总保存事务失败"),
                                database_.lastError()));
            return;
        }

        const QString summariesTable =
            qualifyTable(QStringLiteral("experiment_summaries"));
        const QString membersTable =
            qualifyTable(QStringLiteral("experiment_summary_members"));
        const auto fail = [this, &summary](const QString& message) {
            database_.rollback();
            qCWarning(logCompletionReceipt).noquote()
                << "[数据库线程][整组入库] 保存失败并回滚"
                << "executionId=" << summary.executionId
                << "reason=" << message;
            emit summaryStoreFailed(summary.executionId, message);
        };

        QSqlQuery summaryQuery(database_);
        summaryQuery.prepare(QStringLiteral(
            "INSERT INTO %1 (execution_id,base_experiment_name,finished_at,"
            "operator_name,summary_state,multiple_average_force_n,"
            "multiple_average_force_coefficient_n_s_m) "
            "VALUES (?,?,?,NULLIF(?,''),?,?,?) "
            "ON CONFLICT (execution_id) DO NOTHING RETURNING id")
                                 .arg(summariesTable));
        summaryQuery.addBindValue(summary.executionId);
        summaryQuery.addBindValue(summary.baseExperimentName);
        summaryQuery.addBindValue(summary.finishedAtUtc);
        summaryQuery.addBindValue(summary.operatorName.trimmed());
        summaryQuery.addBindValue(stateValue);
        summaryQuery.addBindValue(nullableDouble(
            summary.statistics.multipleAverageForceNewtons));
        summaryQuery.addBindValue(nullableDouble(
            summary.statistics
                .multipleAverageForceCoefficientNewtonSecondsPerMeter));
        if (!summaryQuery.exec()) {
            fail(sqlErrorMessage(QStringLiteral("保存实验汇总记录失败"),
                                 summaryQuery.lastError()));
            return;
        }

        ExperimentSummaryBundle storedBundle = bundle;
        bool insertedSummary = false;
        if (summaryQuery.next()) {
            storedBundle.summary.id = summaryQuery.value(0).toLongLong();
            insertedSummary = true;
        } else {
            QSqlQuery existingSummary(database_);
            existingSummary.prepare(QStringLiteral(
                "SELECT id,base_experiment_name,summary_state FROM %1 "
                "WHERE execution_id = ?")
                                        .arg(summariesTable));
            existingSummary.addBindValue(summary.executionId);
            if (!existingSummary.exec() || !existingSummary.next()) {
                fail(sqlErrorMessage(
                    QStringLiteral("读取幂等实验汇总失败"),
                    existingSummary.lastError()));
                return;
            }
            if (existingSummary.value(1).toString()
                    != summary.baseExperimentName
                || existingSummary.value(2).toString() != stateValue) {
                fail(QStringLiteral("同一执行已存在内容冲突的实验汇总。"));
                return;
            }
            storedBundle.summary.id =
                existingSummary.value(0).toLongLong();
        }

        if (insertedSummary) {
            for (const ExperimentRecord& record : bundle.records) {
                QSqlQuery memberQuery(database_);
                memberQuery.prepare(QStringLiteral(
                    "INSERT INTO %1 (summary_id,experiment_record_id,"
                    "repetition_index) VALUES (?,?,?)")
                                        .arg(membersTable));
                memberQuery.addBindValue(storedBundle.summary.id);
                memberQuery.addBindValue(record.id);
                memberQuery.addBindValue(record.repetitionIndex);
                if (!memberQuery.exec()) {
                    fail(sqlErrorMessage(
                        QStringLiteral("保存实验汇总明细关系失败"),
                        memberQuery.lastError()));
                    return;
                }
            }
        } else {
            QSqlQuery existingMembers(database_);
            existingMembers.prepare(QStringLiteral(
                "SELECT experiment_record_id,repetition_index FROM %1 "
                "WHERE summary_id = ? ORDER BY repetition_index")
                                        .arg(membersTable));
            existingMembers.addBindValue(storedBundle.summary.id);
            if (!existingMembers.exec()) {
                fail(sqlErrorMessage(
                    QStringLiteral("读取幂等实验汇总明细失败"),
                    existingMembers.lastError()));
                return;
            }
            qsizetype memberIndex = 0;
            while (existingMembers.next()) {
                if (memberIndex >= bundle.records.size()) {
                    fail(QStringLiteral(
                        "既有实验汇总的明细数量与本次请求不一致。"));
                    return;
                }
                const ExperimentRecord& requestedRecord =
                    bundle.records.at(memberIndex);
                if (existingMembers.value(0).toLongLong()
                        != requestedRecord.id
                    || existingMembers.value(1).toInt()
                           != requestedRecord.repetitionIndex) {
                    fail(QStringLiteral(
                        "既有实验汇总的明细关系与本次请求不一致。"));
                    return;
                }
                ++memberIndex;
            }
            if (memberIndex != bundle.records.size()) {
                fail(QStringLiteral(
                    "既有实验汇总的明细数量与本次请求不一致。"));
                return;
            }
        }

        if (!database_.commit()) {
            fail(sqlErrorMessage(QStringLiteral("提交实验汇总保存事务失败"),
                                 database_.lastError()));
            return;
        }
        qCInfo(logCompletionReceipt)
            << "[数据库线程][整组入库] 提交完成，发送 summaryStored"
            << "executionId=" << storedBundle.summary.executionId
            << "summaryId=" << storedBundle.summary.id
            << "memberCount=" << storedBundle.records.size();
        emit summaryStored(storedBundle);
    }

    bool readExperimentRecordForExport(qint64 recordId,
                                       ExperimentRecord* record,
                                       QString* errorMessage)
    {
        if (!database_.isOpen()) {
            setError(errorMessage,
                     QStringLiteral("PostgreSQL 未连接，不能读取实验记录。"));
            return false;
        }

        QSqlQuery query(database_);
        query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE id = ?")
                          .arg(experimentRecordSelectColumns(),
                               qualifyTable(
                                   QStringLiteral("experiment_records"))));
        query.addBindValue(recordId);
        if (!query.exec() || !query.next()) {
            setError(errorMessage,
                     query.lastError().isValid()
                         ? sqlErrorMessage(QStringLiteral("读取实验记录失败"),
                                           query.lastError())
                         : QStringLiteral("没有找到选中的实验记录。"));
            return false;
        }

        QString parseError;
        if (!readExperimentRecord(query, 0, record, &parseError)) {
            setError(errorMessage, parseError);
            return false;
        }
        return true;
    }

    bool readExperimentSummaryForExport(qint64 summaryId,
                                        ExperimentSummaryBundle* bundle,
                                        QString* errorMessage)
    {
        if (!database_.isOpen()) {
            setError(errorMessage,
                     QStringLiteral("PostgreSQL 未连接，不能读取实验汇总。"));
            return false;
        }
        *bundle = ExperimentSummaryBundle{};

        const QString summariesTable =
            qualifyTable(QStringLiteral("experiment_summaries"));
        const QString membersTable =
            qualifyTable(QStringLiteral("experiment_summary_members"));
        const QString recordsTable =
            qualifyTable(QStringLiteral("experiment_records"));
        QSqlQuery summaryQuery(database_);
        summaryQuery.prepare(QStringLiteral(
            "SELECT s.id,s.execution_id,s.base_experiment_name,s.finished_at,"
            "s.operator_name,s.summary_state,s.multiple_average_force_n,"
            "s.multiple_average_force_coefficient_n_s_m "
            "FROM %1 s WHERE s.id = ?")
                                 .arg(summariesTable));
        summaryQuery.addBindValue(summaryId);
        if (!summaryQuery.exec() || !summaryQuery.next()) {
            setError(errorMessage,
                     summaryQuery.lastError().isValid()
                         ? sqlErrorMessage(QStringLiteral("读取实验汇总失败"),
                                           summaryQuery.lastError())
                         : QStringLiteral("没有找到选中的实验汇总。"));
            return false;
        }

        ExperimentSummaryRecord& summary = bundle->summary;
        summary.id = summaryQuery.value(0).toLongLong();
        summary.executionId = summaryQuery.value(1).toLongLong();
        summary.baseExperimentName = summaryQuery.value(2).toString();
        summary.finishedAtUtc = summaryQuery.value(3).toDateTime().toUTC();
        summary.operatorName = summaryQuery.value(4).toString();
        if (!experimentTerminalStateFromDatabaseValue(
                summaryQuery.value(5).toString(), &summary.state)) {
            setError(errorMessage,
                     QStringLiteral("数据库包含无法识别的实验汇总状态。"));
            return false;
        }
        if (!summaryQuery.isNull(6)) {
            summary.statistics.multipleAverageForceNewtons =
                summaryQuery.value(6).toDouble();
        }
        if (!summaryQuery.isNull(7)) {
            summary.statistics
                .multipleAverageForceCoefficientNewtonSecondsPerMeter =
                summaryQuery.value(7).toDouble();
        }

        QSqlQuery membersQuery(database_);
        membersQuery.prepare(QStringLiteral(
            "SELECT %1 FROM %2 m JOIN %3 r ON r.id = m.experiment_record_id "
            "WHERE m.summary_id = ? ORDER BY m.repetition_index")
                                 .arg(experimentRecordSelectColumns(
                                          QStringLiteral("r")),
                                      membersTable,
                                      recordsTable));
        membersQuery.addBindValue(summary.id);
        if (!membersQuery.exec()) {
            setError(errorMessage,
                     sqlErrorMessage(QStringLiteral("读取实验汇总明细失败"),
                                     membersQuery.lastError()));
            return false;
        }
        while (membersQuery.next()) {
            ExperimentRecord record;
            QString parseError;
            if (!readExperimentRecord(
                    membersQuery, 0, &record, &parseError)) {
                setError(errorMessage, parseError);
                return false;
            }
            bundle->records.append(record);
        }
        return true;
    }

    bool readExperimentRawDataForExport(
        const QString& tableName,
        double statisticsStartMeters,
        double statisticsEndMeters,
        qint64 offset,
        int limit,
        QVector<ExperimentSample>* samples,
        QString* errorMessage)
    {
        if (!database_.isOpen()) {
            setError(errorMessage,
                     QStringLiteral("PostgreSQL 未连接，不能读取实验原始数据。"));
            return false;
        }
        static const QRegularExpression identifierPattern(
            QStringLiteral("^[a-z0-9_]+$"));
        if (!identifierPattern.match(tableName).hasMatch()) {
            setError(errorMessage, QStringLiteral("实验原始数据表名无效。"));
            return false;
        }

        const double minimumPosition =
            std::min(statisticsStartMeters, statisticsEndMeters);
        const double maximumPosition =
            std::max(statisticsStartMeters, statisticsEndMeters);
        QSqlQuery query(database_);
        query.prepare(QStringLiteral(
            "SELECT relative_time_ms,acceleration_m_s2,velocity_m_s,"
            "motor_current,motor_temperature,force_n,position_m FROM %1 "
            "WHERE position_m BETWEEN ? AND ? "
            "ORDER BY relative_time_ms LIMIT ? OFFSET ?")
                          .arg(qualifyTable(tableName)));
        query.addBindValue(minimumPosition);
        query.addBindValue(maximumPosition);
        query.addBindValue(limit);
        query.addBindValue(offset);
        if (!query.exec()) {
            setError(errorMessage,
                     sqlErrorMessage(QStringLiteral("读取实验导出数据失败"),
                                     query.lastError()));
            return false;
        }

        samples->clear();
        samples->reserve(limit);
        while (query.next()) {
            ExperimentSample sample;
            sample.relativeTimeMilliseconds = query.value(0).toDouble();
            sample.accelerationMetersPerSecondSquared =
                query.value(1).toDouble();
            sample.velocityMetersPerSecond = query.value(2).toDouble();
            sample.motorCurrent = query.value(3).toDouble();
            sample.motorTemperature = query.value(4).toDouble();
            sample.forceNewtons = query.value(5).toDouble();
            sample.positionMeters = query.value(6).toDouble();
            samples->append(sample);
        }
        return true;
    }

signals:
    void ready(bool ready, const QString& message);
    void tableCreated(int repetitionIndex, const QString& tableName);
    void tableFinished(int repetitionIndex,
                       const QString& tableName,
                       qint64 sampleCount);
    void tablesRead(const QStringList& tableNames);
    void dataRead(const QString& tableName,
                  qint64 offset,
                  const QVector<ExperimentSample>& samples);
    void forceAggregateRead(qint64 executionId,
                            int repetitionIndex,
                            const ExperimentForceAggregate& aggregate);
    void forceAggregateFailed(qint64 executionId,
                              int repetitionIndex,
                              const QString& message);
    void recordStored(const ExperimentRecord& record);
    void recordStoreFailed(qint64 executionId,
                           int repetitionIndex,
                           const QString& message);
    void recordsRead(qint64 requestId,
                     const QVector<ExperimentRecordListItem>& records);
    void recordsReadFailed(qint64 requestId, const QString& message);
    void summaryStored(const ExperimentSummaryBundle& bundle);
    void summaryStoreFailed(qint64 executionId, const QString& message);
    void fatalError(const QString& message);

private:
    bool initializeExperimentLogSchema(QString* errorMessage)
    {
        if (!database_.transaction()) {
            setError(errorMessage,
                     sqlErrorMessage(
                         QStringLiteral("启动实验日志 schema 初始化事务失败"),
                         database_.lastError()));
            return false;
        }

        const auto fail = [this, errorMessage](const QString& message) {
            database_.rollback();
            setError(errorMessage, message);
            return false;
        };
        const auto execute = [this, &fail](const QString& sql,
                                           const QString& action) {
            QSqlQuery query(database_);
            if (!query.exec(sql)) {
                return fail(sqlErrorMessage(action, query.lastError()));
            }
            return true;
        };

        const QString recordsTable =
            qualifyTable(QStringLiteral("experiment_records"));
        const QString summariesTable =
            qualifyTable(QStringLiteral("experiment_summaries"));
        const QString membersTable =
            qualifyTable(QStringLiteral("experiment_summary_members"));
        const QStringList schemaStatements = {
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS %1 ("
                "id BIGSERIAL PRIMARY KEY,"
                "execution_id BIGINT NOT NULL,"
                "experiment_name TEXT NOT NULL,"
                "base_experiment_name TEXT NOT NULL,"
                "repetition_index INTEGER NOT NULL CHECK (repetition_index > 0),"
                "planned_repeat_count INTEGER NOT NULL CHECK (planned_repeat_count > 0),"
                "finished_at TIMESTAMPTZ NOT NULL,"
                "operator_name TEXT NULL,"
                "terminal_state TEXT NOT NULL CHECK (terminal_state IN "
                "('completed','failed','stopped')),"
                "terminal_reason TEXT NULL,"
                "test_speed_m_s DOUBLE PRECISION NOT NULL,"
                "statistics_start_m DOUBLE PRECISION NOT NULL,"
                "statistics_end_m DOUBLE PRECISION NOT NULL,"
                "raw_sample_count BIGINT NOT NULL CHECK (raw_sample_count >= 0),"
                "effective_sample_count BIGINT NOT NULL DEFAULT 0 "
                "CHECK (effective_sample_count >= 0),"
                "average_force_n DOUBLE PRECISION NULL,"
                "force_coefficient_n_s_m DOUBLE PRECISION NULL,"
                "force_range_n DOUBLE PRECISION NULL,"
                "fluctuation_rate_percent DOUBLE PRECISION NULL,"
                "raw_data_table_name TEXT NULL,"
                "parameters_snapshot JSONB NOT NULL,"
                "UNIQUE (execution_id, repetition_index))")
                .arg(recordsTable),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS %1 ("
                "id BIGSERIAL PRIMARY KEY,"
                "execution_id BIGINT NOT NULL UNIQUE,"
                "base_experiment_name TEXT NOT NULL,"
                "finished_at TIMESTAMPTZ NOT NULL,"
                "operator_name TEXT NULL,"
                "summary_state TEXT NOT NULL CHECK (summary_state IN "
                "('completed','failed','stopped')),"
                "multiple_average_force_n DOUBLE PRECISION NULL,"
                "multiple_average_force_coefficient_n_s_m DOUBLE PRECISION NULL)")
                .arg(summariesTable),
            QStringLiteral(
                "ALTER TABLE %1 ADD COLUMN IF NOT EXISTS summary_state "
                "TEXT NOT NULL DEFAULT 'completed'")
                .arg(summariesTable),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS %1 ("
                "summary_id BIGINT NOT NULL REFERENCES %2(id) ON DELETE CASCADE,"
                "experiment_record_id BIGINT NOT NULL REFERENCES %3(id) "
                "ON DELETE RESTRICT,"
                "repetition_index INTEGER NOT NULL CHECK (repetition_index > 0),"
                "PRIMARY KEY (summary_id, experiment_record_id),"
                "UNIQUE (summary_id, repetition_index))")
                .arg(membersTable, summariesTable, recordsTable),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS experiment_records_finished_at_idx "
                "ON %1 (finished_at DESC)")
                .arg(recordsTable),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS experiment_records_name_idx "
                "ON %1 (experiment_name)")
                .arg(recordsTable),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS experiment_summary_members_record_idx "
                "ON %1 (experiment_record_id)")
                .arg(membersTable)
        };
        for (const QString& statement : schemaStatements) {
            if (!execute(statement,
                         QStringLiteral("初始化实验日志固定表或索引失败"))) {
                return false;
            }
        }

        if (!database_.commit()) {
            return fail(sqlErrorMessage(
                QStringLiteral("提交实验日志 schema 初始化事务失败"),
                database_.lastError()));
        }
        return true;
    }

    QString qualifyTable(const QString& tableName) const
    {
        QSqlDriver* driver = database_.driver();
        return driver->escapeIdentifier(configuration_.schemaName, QSqlDriver::TableName)
               + QLatin1Char('.')
               + driver->escapeIdentifier(tableName, QSqlDriver::TableName);
    }

    void reportFatal(const QString& message)
    {
        // 首个致命错误会停止后续队列任务，并且只向上层发布一次。
        if (fatalError_) {
            return;
        }
        fatalError_ = true;
        emit fatalError(message);
    }

    DatabaseConfiguration configuration_;
    QSqlDatabase database_;
    QString connectionName_;
    QString currentTableName_;
    int currentRepetitionIndex_ = 0;
    qint64 sampleOffset_ = 0;
    double samplePeriodMilliseconds_ = 0.0;
    bool fatalError_ = false;
};

AcquisitionDatabaseService& AcquisitionDatabaseService::instance()
{
    static AcquisitionDatabaseService service;
    return service;
}

AcquisitionDatabaseService::AcquisitionDatabaseService()
{
    qRegisterMetaType<ExperimentSample>("ExperimentSample");
    qRegisterMetaType<QVector<ExperimentSample>>("QVector<ExperimentSample>");
    qRegisterMetaType<ExperimentForceAggregate>(
        "ExperimentForceAggregate");
    qRegisterMetaType<ExperimentRecord>("ExperimentRecord");
    qRegisterMetaType<QVector<ExperimentRecordListItem>>(
        "QVector<ExperimentRecordListItem>");
    qRegisterMetaType<ExperimentSummaryBundle>("ExperimentSummaryBundle");
    workerThread_.setObjectName(QStringLiteral("PostgreSqlAcquisitionThread"));
}

AcquisitionDatabaseService::~AcquisitionDatabaseService()
{
    shutdown();
}

void AcquisitionDatabaseService::initialize()
{
    if (initialized_) {
        return;
    }

    DatabaseConfiguration configuration;
    QString errorMessage;
    if (!loadConfiguration(&configuration, &errorMessage)) {
        handleReady(false, errorMessage);
        return;
    }

    worker_ = new AcquisitionDatabaseWorker;
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    // Worker 的全部结果通过显式队列连接返回 service 线程；
    // QSqlDatabase 本身始终只在 worker 线程中使用。
    connect(worker_,
            &AcquisitionDatabaseWorker::ready,
            this,
            &AcquisitionDatabaseService::handleReady,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::tableCreated,
            this,
            &AcquisitionDatabaseService::experimentTableCreated,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::tableFinished,
            this,
            &AcquisitionDatabaseService::experimentTableFinished,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::tablesRead,
            this,
            &AcquisitionDatabaseService::experimentTablesRead,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::dataRead,
            this,
            &AcquisitionDatabaseService::experimentDataRead,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::forceAggregateRead,
            this,
            &AcquisitionDatabaseService::experimentForceAggregateRead,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::forceAggregateFailed,
            this,
            &AcquisitionDatabaseService::experimentForceAggregateFailed,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::recordStored,
            this,
            &AcquisitionDatabaseService::experimentRecordStored,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::recordStoreFailed,
            this,
            &AcquisitionDatabaseService::experimentRecordStoreFailed,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::recordsRead,
            this,
            &AcquisitionDatabaseService::experimentRecordsRead,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::recordsReadFailed,
            this,
            &AcquisitionDatabaseService::experimentRecordsReadFailed,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::summaryStored,
            this,
            &AcquisitionDatabaseService::experimentSummaryStored,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::summaryStoreFailed,
            this,
            &AcquisitionDatabaseService::experimentSummaryStoreFailed,
            Qt::QueuedConnection);
    connect(worker_,
            &AcquisitionDatabaseWorker::fatalError,
            this,
            &AcquisitionDatabaseService::handleFailure,
            Qt::QueuedConnection);
    initialized_ = true;
    workerThread_.start();
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, configuration] {
            worker->initialize(configuration);
        },
        Qt::QueuedConnection);
}

void AcquisitionDatabaseService::shutdown()
{
    if (!initialized_) {
        return;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_] {
            worker->shutdown();
        },
        Qt::BlockingQueuedConnection);
    workerThread_.quit();
    workerThread_.wait();
    worker_ = nullptr;
    initialized_ = false;
    ready_ = false;
}

bool AcquisitionDatabaseService::isReady() const
{
    return initialized_ && ready_;
}

bool AcquisitionDatabaseService::beginExperimentTable(
    const QString& motorModel,
    const QString& specimenId,
    int repetitionIndex,
    double samplePeriodSeconds,
    QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (motorModel.trimmed().isEmpty() || specimenId.trimmed().isEmpty()
        || repetitionIndex <= 0
        || !std::isfinite(samplePeriodSeconds)
        || samplePeriodSeconds <= 0.0) {
        setError(errorMessage, QStringLiteral("实验表创建参数无效。"));
        return false;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_,
         motorModel,
         specimenId,
         repetitionIndex,
         samplePeriodSeconds] {
            worker->beginExperimentTable(
                motorModel, specimenId, repetitionIndex, samplePeriodSeconds);
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::appendBlock(const AcquisitionBlock& block,
                                              QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, block] {
            worker->appendBlock(block);
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::finishExperimentTable(QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_] {
            worker->finishExperimentTable();
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::requestExperimentTables(QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_] {
            worker->requestExperimentTables();
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::requestExperimentData(
    const QString& tableName,
    qint64 offset,
    int limit,
    QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (offset < 0 || limit <= 0 || limit > kMaximumReadPageSize) {
        setError(errorMessage, QStringLiteral("数据库分页读取范围无效。"));
        return false;
    }
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, tableName, offset, limit] {
            worker->requestExperimentData(tableName, offset, limit);
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::requestExperimentForceAggregate(
    qint64 executionId,
    int repetitionIndex,
    const QString& tableName,
    double statisticsStartMeters,
    double statisticsEndMeters,
    QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (executionId <= 0 || repetitionIndex <= 0
        || tableName.trimmed().isEmpty()
        || !std::isfinite(statisticsStartMeters)
        || !std::isfinite(statisticsEndMeters)) {
        setError(errorMessage, QStringLiteral("实验统计查询参数无效。"));
        return false;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_,
         executionId,
         repetitionIndex,
         tableName,
         statisticsStartMeters,
         statisticsEndMeters] {
            worker->requestExperimentForceAggregate(
                executionId,
                repetitionIndex,
                tableName,
                statisticsStartMeters,
                statisticsEndMeters);
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::storeExperimentRecord(
    const ExperimentRecord& record,
    QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    QString parameterError;
    if (record.executionId <= 0 || record.repetitionIndex <= 0
        || record.plannedRepeatCount <= 0
        || experimentTerminalStateDatabaseValue(record.state).isEmpty()
        || record.experimentName.trimmed().isEmpty()
        || record.baseExperimentName.trimmed().isEmpty()
        || !record.finishedAtUtc.isValid()
        || !std::isfinite(record.testSpeedMetersPerSecond)
        || record.testSpeedMetersPerSecond <= 0.0
        || !std::isfinite(record.statisticsStartMeters)
        || !std::isfinite(record.statisticsEndMeters)
        || record.rawSampleCount < 0
        || !validateTestParameters(
            record.parametersSnapshot, &parameterError)) {
        setError(errorMessage,
                 parameterError.isEmpty()
                     ? QStringLiteral("实验流程记录字段无效。")
                     : QStringLiteral("实验参数快照无效：%1")
                           .arg(parameterError));
        return false;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, record] {
            worker->storeExperimentRecord(record);
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::requestExperimentRecords(
    qint64 requestId,
    const ExperimentRecordFilter& filter,
    QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (requestId <= 0 || !filter.fromDate.isValid()
        || !filter.toDate.isValid() || filter.fromDate > filter.toDate
        || filter.offset < 0 || filter.limit <= 0
        || filter.limit > kMaximumExperimentRecordPageSize) {
        setError(errorMessage, QStringLiteral("实验记录查询条件无效。"));
        return false;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, requestId, filter] {
            worker->requestExperimentRecords(requestId, filter);
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::storeExperimentSummary(
    const ExperimentSummaryBundle& bundle,
    QString* errorMessage)
{
    if (!isReady()) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }

    const ExperimentSummaryRecord& summary = bundle.summary;
    const bool hasAverageForce =
        summary.statistics.multipleAverageForceNewtons.has_value();
    const bool hasAverageCoefficient =
        summary.statistics
            .multipleAverageForceCoefficientNewtonSecondsPerMeter
            .has_value();
    if (summary.executionId <= 0
        || summary.baseExperimentName.trimmed().isEmpty()
        || !summary.finishedAtUtc.isValid()
        || bundle.records.isEmpty()
        || experimentTerminalStateDatabaseValue(summary.state).isEmpty()
        || hasAverageForce != hasAverageCoefficient
        || (hasAverageForce
            && (!std::isfinite(
                    *summary.statistics.multipleAverageForceNewtons)
                || !std::isfinite(
                    *summary.statistics
                         .multipleAverageForceCoefficientNewtonSecondsPerMeter)))
        || (summary.state == ExperimentTerminalState::Completed
            && !hasAverageForce)) {
        setError(errorMessage, QStringLiteral("实验汇总记录字段无效。"));
        return false;
    }
    const int plannedRepeatCount =
        bundle.records.first().plannedRepeatCount;
    QSet<int> repetitionIndices;
    for (qsizetype index = 0; index < bundle.records.size(); ++index) {
        const ExperimentRecord& record = bundle.records.at(index);
        const bool isLastRecord = index == bundle.records.size() - 1;
        if (record.id <= 0
            || record.executionId != summary.executionId
            || record.repetitionIndex <= 0
            || record.repetitionIndex != index + 1
            || record.plannedRepeatCount != plannedRepeatCount
            || experimentTerminalStateDatabaseValue(record.state).isEmpty()
            || (!isLastRecord
                && record.state != ExperimentTerminalState::Completed)
            || (record.state == ExperimentTerminalState::Completed
                && (!record.statistics.averageForceNewtons.has_value()
                    || !record.statistics
                            .forceCoefficientNewtonSecondsPerMeter
                            .has_value()))) {
            setError(errorMessage, QStringLiteral("实验汇总明细字段无效。"));
            return false;
        }
        repetitionIndices.insert(record.repetitionIndex);
    }
    if (plannedRepeatCount <= 0
        || bundle.records.size() > plannedRepeatCount
        || repetitionIndices.size() != bundle.records.size()
        || bundle.records.last().state != summary.state
        || (summary.state == ExperimentTerminalState::Completed
            && bundle.records.size() != plannedRepeatCount)) {
        setError(errorMessage,
                 QStringLiteral("实验汇总状态或明细范围无效。"));
        return false;
    }

    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, bundle] {
            worker->storeExperimentSummary(bundle);
        },
        Qt::QueuedConnection);
    return true;
}

bool AcquisitionDatabaseService::readExperimentRecordForExport(
    qint64 recordId,
    ExperimentRecord* record,
    QString* errorMessage)
{
    if (worker_ == nullptr) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (recordId <= 0 || record == nullptr) {
        setError(errorMessage, QStringLiteral("实验记录读取参数无效。"));
        return false;
    }

    bool readSucceeded = false;
    const Qt::ConnectionType connectionType =
        worker_->thread() == QThread::currentThread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection;
    const bool invoked = QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, recordId, record, errorMessage, &readSucceeded] {
            readSucceeded = worker->readExperimentRecordForExport(
                recordId, record, errorMessage);
        },
        connectionType);
    if (!invoked) {
        setError(errorMessage, QStringLiteral("无法调度实验记录读取任务。"));
    }
    return invoked && readSucceeded;
}

bool AcquisitionDatabaseService::readExperimentSummaryForExport(
    qint64 summaryId,
    ExperimentSummaryBundle* bundle,
    QString* errorMessage)
{
    if (worker_ == nullptr) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (summaryId <= 0 || bundle == nullptr) {
        setError(errorMessage, QStringLiteral("实验汇总读取参数无效。"));
        return false;
    }

    bool readSucceeded = false;
    const Qt::ConnectionType connectionType =
        worker_->thread() == QThread::currentThread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection;
    const bool invoked = QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, summaryId, bundle, errorMessage, &readSucceeded] {
            readSucceeded = worker->readExperimentSummaryForExport(
                summaryId, bundle, errorMessage);
        },
        connectionType);
    if (!invoked) {
        setError(errorMessage, QStringLiteral("无法调度实验汇总读取任务。"));
    }
    return invoked && readSucceeded;
}

bool AcquisitionDatabaseService::readExperimentRawDataForExport(
    const QString& tableName,
    double statisticsStartMeters,
    double statisticsEndMeters,
    qint64 offset,
    int limit,
    QVector<ExperimentSample>* samples,
    QString* errorMessage)
{
    if (worker_ == nullptr) {
        setError(errorMessage, QStringLiteral("PostgreSQL 数据库尚未就绪。"));
        return false;
    }
    if (tableName.trimmed().isEmpty()
        || !std::isfinite(statisticsStartMeters)
        || !std::isfinite(statisticsEndMeters)
        || offset < 0 || limit <= 0 || limit > kMaximumReadPageSize
        || samples == nullptr) {
        setError(errorMessage, QStringLiteral("实验导出数据读取参数无效。"));
        return false;
    }

    bool readSucceeded = false;
    const Qt::ConnectionType connectionType =
        worker_->thread() == QThread::currentThread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection;
    const bool invoked = QMetaObject::invokeMethod(
        worker_,
        [worker = worker_,
         tableName,
         statisticsStartMeters,
         statisticsEndMeters,
         offset,
         limit,
         samples,
         errorMessage,
         &readSucceeded] {
            readSucceeded = worker->readExperimentRawDataForExport(
                tableName,
                statisticsStartMeters,
                statisticsEndMeters,
                offset,
                limit,
                samples,
                errorMessage);
        },
        connectionType);
    if (!invoked) {
        setError(errorMessage, QStringLiteral("无法调度实验导出数据读取任务。"));
    }
    return invoked && readSucceeded;
}

void AcquisitionDatabaseService::handleReady(bool ready,
                                             const QString& message)
{
    ready_ = ready;
    if (ready) {
        qCInfo(logDatabase).noquote() << message;
    } else {
        qCCritical(logDatabase).noquote() << message;
    }
    emit readinessChanged(ready, message);
}

void AcquisitionDatabaseService::handleFailure(const QString& message)
{
    ready_ = false;
    qCCritical(logDatabase).noquote() << message;
    emit readinessChanged(false, message);
    emit databaseFailed(message);
}

#include "AcquisitionDatabaseService.moc"
