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
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kDatabaseConfigurationSchemaVersion = 2;
constexpr int kInsertRowsPerStatement = 500;
constexpr int kMaximumReadPageSize = 100000;
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
    void fatalError(const QString& message);

private:
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
