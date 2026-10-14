#include "AlarmService.h"

#include "AlarmMonitor.h"
#include "../motion/AcsClient.h"
#include "../safety/SafetyMonitor.h"
#include "../temperature/TemperatureDeviceService.h"
#include "../logging/AppLogger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <cmath>

namespace {
DeviceUpdate temperatureUpdate(const TemperatureSnapshot& snapshot)
{
    DeviceUpdate update;
    update.sourceId = snapshot.deviceId;
    update.sessionId = snapshot.sessionId;
    update.points.insert(PointId::TemperatureBatch, {
        QVariant::fromValue(snapshot), DataQuality::Good,
        snapshot.sampledAtUtc, snapshot.observedMonoMs, snapshot.sequence});
    return update;
}

QString keyFor(const QString& code, const QString& source) { return code + QLatin1Char('|') + source; }
QJsonObject recordJson(const AlarmRecord& record)
{
    return {{"id", record.occurrenceId.toString(QUuid::WithoutBraces)}, {"code", record.code},
        {"source", record.sourceId}, {"title", record.title}, {"severity", static_cast<int>(record.severity)},
        {"state", static_cast<int>(record.state())},
        {"ack", record.acknowledged}, {"revision", QString::number(record.revision)},
        {"raised", record.raisedAtUtc.toString(Qt::ISODateWithMs)}, {"recovered", record.recoveredAtUtc.toString(Qt::ISODateWithMs)},
        {"ackAt", record.acknowledgedAtUtc.toString(Qt::ISODateWithMs)}, {"closed", record.closedAtUtc.toString(Qt::ISODateWithMs)},
        {"ackBy", record.acknowledgedBy}, {"experiment", record.experimentName},
        {"trigger", QJsonObject::fromVariantMap(record.triggerDetails)}, {"latest", QJsonObject::fromVariantMap(record.latestDetails)}};
}
AlarmRecord recordFromJson(const QJsonObject& object)
{
    AlarmRecord record;
    record.occurrenceId = QUuid(object.value("id").toString());
    record.code = object.value("code").toString(); record.sourceId = object.value("source").toString();
    record.title = object.value("title").toString();
    record.severity = static_cast<AlarmSeverity>(object.value("severity").toInt());
    record.acknowledged = object.value("ack").toBool();
    record.revision = object.value("revision").toString().toULongLong();
    record.raisedAtUtc = QDateTime::fromString(object.value("raised").toString(), Qt::ISODateWithMs);
    record.recoveredAtUtc = QDateTime::fromString(object.value("recovered").toString(), Qt::ISODateWithMs);
    record.acknowledgedAtUtc = QDateTime::fromString(object.value("ackAt").toString(), Qt::ISODateWithMs);
    record.closedAtUtc = QDateTime::fromString(object.value("closed").toString(), Qt::ISODateWithMs);
    record.acknowledgedBy = object.value("ackBy").toString(); record.experimentName = object.value("experiment").toString();
    record.triggerDetails = object.value("trigger").toObject().toVariantMap();
    record.latestDetails = object.value("latest").toObject().toVariantMap();
    return record;
}
} // namespace

class AlarmStorageWorker final : public QObject
{
    Q_OBJECT
public:
    void initialize()
    {
        timer_ = new QTimer(this); timer_->setInterval(5000);
        connect(timer_, &QTimer::timeout, this, [this] { flush(); });
        timer_->start(); flush();
    }
    void append(const AlarmEvent& event)
    {
        if (queue_.size() >= 4096) {
            emit statusChanged(QStringLiteral("报警离线队列已满，本事件未持久化；禁止继续运行"), false);
            overflow_ = true; return;
        }
        queue_.append(event);
        if (database_.isOpen() && !flushScheduled_) {
            flushScheduled_ = true;
            QTimer::singleShot(0, this, [this] { flushScheduled_ = false; flush(); });
        }
    }
    void shutdown()
    {
        timer_->stop(); flush(); database_.close(); database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("alarm_storage"));
    }
    void storeTemperatureSummary(const TemperatureExperimentSummary& summary)
    {
        if (summaries_.size() >= 256) {
            overflow_ = true; emit statusChanged(QStringLiteral("温度峰值离线队列已满，部分摘要未保存"), false); return;
        }
        QJsonArray channels; for (const int channel : summary.channels) { channels.append(channel); }
        QJsonObject payload{{"experimentName", summary.experimentName}, {"backend", summary.backend}, {"channels", channels},
            {"startedAtUtc", summary.startedAtUtc.toString(Qt::ISODateWithMs)}, {"finishedAtUtc", summary.finishedAtUtc.toString(Qt::ISODateWithMs)},
            {"maximumChannel", summary.maximumChannel}, {"maximumAtUtc", summary.maximumAtUtc.toString(Qt::ISODateWithMs)},
            {"goodScanCount", QString::number(summary.goodScanCount)}, {"incompleteScanCount", QString::number(summary.incompleteScanCount)}};
        payload.insert("maximumCelsius", summary.maximumCelsius ? QJsonValue(*summary.maximumCelsius) : QJsonValue(QJsonValue::Null));
        summaries_.append(payload);
        if (database_.isOpen() && !flushScheduled_) {
            flushScheduled_ = true; QTimer::singleShot(0, this, [this] { flushScheduled_ = false; flush(); });
        }
    }
signals:
    void restored(const QVector<AlarmRecord>& records);
    void statusChanged(const QString& status, bool trusted);
private:
    bool open()
    {
        if (database_.isOpen()) { return true; }
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath("database.json"));
        if (!file.open(QIODevice::ReadOnly)) { return false; }
        const QJsonObject config = QJsonDocument::fromJson(file.readAll()).object();
        const QString schema = config.value("schemaName").toString();
        static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        if (config.value("schemaVersion").toInt() != 2 || !identifier.match(schema).hasMatch()) { return false; }
        if (!database_.isValid()) { database_ = QSqlDatabase::addDatabase("QPSQL", "alarm_storage"); }
        database_.setHostName(config.value("host").toString()); database_.setPort(config.value("port").toInt(5432));
        database_.setDatabaseName(config.value("databaseName").toString());
        database_.setUserName(config.value("userName").toString()); database_.setPassword(config.value("password").toString());
        const int timeout = qBound(1, config.value("connectTimeoutSeconds").toInt(5), 60);
        database_.setConnectOptions(QStringLiteral("connect_timeout=%1").arg(timeout));
        if (!database_.open()) { return false; }
        recordsTable_ = QStringLiteral("\"%1\".alarm_occurrences").arg(schema);
        eventsTable_ = QStringLiteral("\"%1\".alarm_events").arg(schema);
        summariesTable_ = QStringLiteral("\"%1\".temperature_experiment_peaks").arg(schema);
        QSqlQuery query(database_);
        if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (id uuid PRIMARY KEY, revision bigint NOT NULL, closed boolean NOT NULL, raised_at timestamptz NOT NULL, payload jsonb NOT NULL)").arg(recordsTable_))
            || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (id uuid PRIMARY KEY, occurrence_id uuid NOT NULL, event_at timestamptz NOT NULL, event_type integer NOT NULL, payload jsonb NOT NULL)").arg(eventsTable_))
            || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (experiment_name text PRIMARY KEY, payload jsonb NOT NULL)").arg(summariesTable_))) {
            database_.close(); return false;
        }
        return true;
    }
    void flush()
    {
        if (!open()) { emit statusChanged(QStringLiteral("报警数据库不可用，等待恢复"), false); return; }
        if (!restored_) {
            QSqlQuery query(database_);
            if (!query.exec(QStringLiteral("SELECT payload::text FROM %1 WHERE NOT closed UNION ALL SELECT payload::text FROM (SELECT payload FROM %1 WHERE closed ORDER BY raised_at DESC LIMIT 1000) history").arg(recordsTable_))) {
                emit statusChanged(QStringLiteral("读取报警历史失败"), false); database_.close(); return;
            }
            QVector<AlarmRecord> records;
            while (query.next()) {
                const QJsonDocument document = QJsonDocument::fromJson(query.value(0).toString().toUtf8());
                const AlarmRecord record = recordFromJson(document.object());
                const QJsonObject payload = document.object();
                if (!document.isObject() || record.occurrenceId.isNull() || !record.raisedAtUtc.isValid()
                    || record.code.isEmpty() || record.sourceId.isEmpty() || record.revision == 0
                    || !payload.value("ack").isBool()
                    || !payload.value("severity").isDouble()
                    || static_cast<int>(record.severity) < 0 || static_cast<int>(record.severity) > 1) {
                    emit statusChanged(QStringLiteral("报警历史记录损坏，禁止运行"), false); return;
                }
                records.append(record);
            }
            restored_ = true; emit restored(records);
        }
        while (!queue_.isEmpty()) {
            const AlarmEvent& event = queue_.front();
            if (!database_.transaction()) { break; }
            const QString payload = QString::fromUtf8(QJsonDocument(recordJson(event.record)).toJson(QJsonDocument::Compact));
            QSqlQuery occurrence(database_);
            occurrence.prepare(QStringLiteral("INSERT INTO %1 AS current_occurrence (id, revision, closed, raised_at, payload) VALUES (:id,:revision,:closed,:raised,CAST(:payload AS jsonb)) ON CONFLICT (id) DO UPDATE SET revision=EXCLUDED.revision, closed=EXCLUDED.closed, payload=EXCLUDED.payload WHERE current_occurrence.revision <= EXCLUDED.revision").arg(recordsTable_));
            occurrence.bindValue(":id", event.record.occurrenceId.toString(QUuid::WithoutBraces));
            occurrence.bindValue(":revision", static_cast<qlonglong>(event.record.revision));
            occurrence.bindValue(":closed", event.record.state() == AlarmState::Closed); occurrence.bindValue(":raised", event.record.raisedAtUtc);
            occurrence.bindValue(":payload", payload);
            QSqlQuery eventQuery(database_);
            eventQuery.prepare(QStringLiteral("INSERT INTO %1 (id,occurrence_id,event_at,event_type,payload) VALUES (:id,:occurrence,:at,:type,CAST(:payload AS jsonb)) ON CONFLICT (id) DO NOTHING").arg(eventsTable_));
            eventQuery.bindValue(":id", event.eventId.toString(QUuid::WithoutBraces));
            eventQuery.bindValue(":occurrence", event.record.occurrenceId.toString(QUuid::WithoutBraces));
            eventQuery.bindValue(":at", event.eventAtUtc);
            eventQuery.bindValue(":type", static_cast<int>(event.record.state()));
            eventQuery.bindValue(":payload", payload);
            if (!occurrence.exec() || !eventQuery.exec() || !database_.commit()) {
                database_.rollback(); database_.close();
                emit statusChanged(QStringLiteral("报警写库失败，事件保留在有界重试队列"), false); return;
            }
            queue_.removeFirst();
        }
        while (!summaries_.isEmpty()) {
            const QJsonObject& summary = summaries_.front();
            QSqlQuery query(database_);
            query.prepare(QStringLiteral("INSERT INTO %1 (experiment_name,payload) VALUES (:name,CAST(:payload AS jsonb)) ON CONFLICT (experiment_name) DO UPDATE SET payload=EXCLUDED.payload").arg(summariesTable_));
            query.bindValue(":name", summary.value("experimentName").toString());
            query.bindValue(":payload", QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Compact)));
            if (!query.exec()) { database_.close(); emit statusChanged(QStringLiteral("温度峰值摘要写库失败，等待重试"), false); return; }
            summaries_.removeFirst();
        }
        emit statusChanged(overflow_ ? QStringLiteral("报警队列曾溢出，部分事件未保存，请检修")
            : (queue_.isEmpty() ? QStringLiteral("报警存储就绪（最近 1000 条历史及全部待处理记录）") : QStringLiteral("报警存储等待重试")), !overflow_ && queue_.isEmpty());
    }
    QTimer* timer_ = nullptr;
    QSqlDatabase database_;
    QVector<AlarmEvent> queue_;
    QString recordsTable_;
    QString eventsTable_;
    bool restored_ = false;
    bool overflow_ = false;
    bool flushScheduled_ = false;
    QVector<QJsonObject> summaries_;
    QString summariesTable_;
};

class AlarmEngine final : public QObject
{
    Q_OBJECT
public:
    AlarmEngine() : monitor_(new AlarmMonitor(this))
    {
        registerDefinition({"TEMP_WARNING", QStringLiteral("温度预警"), AlarmSeverity::Warning});
        registerDefinition({"TEMP_FAULT", QStringLiteral("温度故障"), AlarmSeverity::Fault});
        registerDefinition({"SAFETY_CIRCUIT_FAULT", QStringLiteral("安全回路故障"), AlarmSeverity::Fault});
        connect(monitor_, &AlarmMonitor::conditionEvaluated, this, [this](const ConditionReport& report) { reportCondition(report); });
    }
    void start(const TemperatureConfiguration& config, const QUuid& safetySession)
    {
        monitor_->configure(config); monitor_->setSourceSession("safety/main", safetySession);
        timer_ = new QTimer(this); timer_->setInterval(100);
        connect(timer_, &QTimer::timeout, monitor_, &AlarmMonitor::tick); timer_->start();
    }
    void configure(const TemperatureConfiguration& config) { monitor_->configure(config); }
    void setSession(const QString& id, const QUuid& session) { monitor_->setSourceSession(id, session); }
    void update(const DeviceUpdate& update) { monitor_->update(update); }
    void stop() { timer_->stop(); }
    void setExperimentName(const QString& name) { experimentName_ = name; }
    void restore(const QVector<AlarmRecord>& records)
    {
        recordsDirty_ = true;
        for (AlarmRecord record : records) {
            if (find(record.occurrenceId) != nullptr) { continue; }
            if (!record.closedAtUtc.isValid()) {
                record.latestDetails.insert("restartVerificationRequired", true);
                if (!record.recoveredAtUtc.isValid()) { active_[keyFor(record.code, record.sourceId)] = record.occurrenceId; }
            }
            records_.append(record);
            if (closeIfReady(records_.last())) {
                ++records_.last().revision;
                event(records_.last());
            }
        }
        const QList<ConditionReport> current = conditions_.values();
        for (const ConditionReport& report : current) { reportCondition(report); }
        publish();
    }
    void reportCondition(const ConditionReport& report)
    {
        if (!definitions_.contains(report.code) || report.sourceId.isEmpty() || !report.observedAtUtc.isValid()) { return; }
        const QString key = keyFor(report.code, report.sourceId);
        conditions_[key] = report;

        // Only an occurrence that has not recovered receives further samples.
        // Recovered, unacknowledged faults remain independent pending records.
        AlarmRecord* record = find(active_.value(key));
        if (record == nullptr) {
            if (report.state == ConditionState::Abnormal) {
                raise(report, definitions_.value(report.code));
            }
            publish();
            return;
        }

        // Missing/stale input proves neither recovery nor a new occurrence.
        if (report.state == ConditionState::Unknown) { return; }
        for (auto it = report.details.cbegin(); it != report.details.cend(); ++it) {
            record->latestDetails.insert(it.key(), it.value());
        }
        if (report.state == ConditionState::Normal) {
            record->recoveredAtUtc = report.observedAtUtc;
            active_.remove(key);
            ++record->revision;
            closeIfReady(*record);
            event(*record);
        }
        publish();
    }
    void acknowledge(const QUuid& id, const QString& user)
    {
        AlarmRecord* record = find(id);
        if (record == nullptr || record->closedAtUtc.isValid() || record->acknowledged || user.trimmed().isEmpty()) { return; }
        record->acknowledged = true; record->acknowledgedBy = user; record->acknowledgedAtUtc = QDateTime::currentDateTimeUtc();
        ++record->revision;
        closeIfReady(*record);
        event(*record);
        publish();
    }
    void registerDefinition(const AlarmDefinition& definition)
    {
        static const QRegularExpression codePattern(QStringLiteral("^[A-Z][A-Z0-9_]+$"));
        if (!codePattern.match(definition.code).hasMatch() || definition.title.trimmed().isEmpty()) {
            qCWarning(logSafety) << "拒绝无效的报警定义" << definition.code; return;
        }
        definitions_.insert(definition.code, definition);
    }
signals:
    void recordsChanged(const QVector<AlarmRecord>& records);
    void eventPublished(const AlarmEvent& event);
    void alarmRaised(const AlarmRecord& record);
private:
    void raise(const ConditionReport& report, const AlarmDefinition& definition)
    {
        AlarmRecord record;
        record.occurrenceId = QUuid::createUuid();
        record.code = report.code;
        record.sourceId = report.sourceId;
        record.title = definition.title;
        record.severity = definition.severity;
        record.raisedAtUtc = report.observedAtUtc;
        record.triggerDetails = report.details;
        record.latestDetails = report.details;
        record.experimentName = experimentName_;
        records_.append(record);
        active_.insert(keyFor(record.code, record.sourceId), record.occurrenceId);
        event(records_.last());
        // Make the occurrence visible before subscribers perform device actions.
        publish();
        emit alarmRaised(record);
    }
    bool closeIfReady(AlarmRecord& record)
    {
        if (record.closedAtUtc.isValid()
            || !record.recoveredAtUtc.isValid()
            || (record.severity == AlarmSeverity::Fault && !record.acknowledged)) { return false; }
        record.closedAtUtc = QDateTime::currentDateTimeUtc();
        return true;
    }
    AlarmRecord* find(const QUuid& id)
    {
        if (id.isNull()) { return nullptr; }
        for (AlarmRecord& record : records_) { if (record.occurrenceId == id) { return &record; } }
        return nullptr;
    }
    void event(const AlarmRecord& record)
    {
        recordsDirty_ = true;
        qCInfo(logSafety) << "报警状态变更" << record.code << record.occurrenceId
                         << "state=" << static_cast<int>(record.state()) << "revision=" << record.revision;
        emit eventPublished({QUuid::createUuid(), record, QDateTime::currentDateTimeUtc()});
    }
    void publish()
    {
        if (recordsDirty_) {
            emit recordsChanged(records_);
            recordsDirty_ = false;
        }
    }
    AlarmMonitor* monitor_;
    QTimer* timer_ = nullptr;
    QVector<AlarmRecord> records_;
    QMap<QString, QUuid> active_;
    QMap<QString, ConditionReport> conditions_;
    QMap<QString, AlarmDefinition> definitions_;
    QString experimentName_;
    bool recordsDirty_ = true;
};

AlarmService& AlarmService::instance() { static AlarmService service; return service; }
AlarmService::AlarmService()
{
    qRegisterMetaType<DeviceUpdate>(); qRegisterMetaType<ConditionReport>(); qRegisterMetaType<QVector<AlarmRecord>>();
    qRegisterMetaType<AlarmEvent>(); qRegisterMetaType<AlarmRecord>();
    qRegisterMetaType<AlarmDefinition>();
}
AlarmService::~AlarmService() { shutdown(); }
QVector<AlarmRecord> AlarmService::records() const { const QMutexLocker lock(&mutex_); return records_; }
QString AlarmService::storageStatus() const { const QMutexLocker lock(&mutex_); return storageStatus_; }
void AlarmService::initialize(TemperatureDeviceService* temperature, SafetyMonitor* safety, AcsClient* client)
{
    if (initialized_) {
        return;
    }

    temperature_ = temperature;
    safety_ = safety;
    client_ = client;
    safetySession_ = QUuid::createUuid();

    createWorkers();
    connectAlarmOutputs();
    connectStorage();
    connectTemperature();
    connectSafety();
    startWorkers();
}

void AlarmService::createWorkers()
{
    engine_ = new AlarmEngine;
    engine_->moveToThread(&monitorThread_);
    storage_ = new AlarmStorageWorker;
    storage_->moveToThread(&storageThread_);

    connect(&monitorThread_, &QThread::finished, engine_, &QObject::deleteLater);
    connect(&storageThread_, &QThread::finished, storage_, &QObject::deleteLater);
}

void AlarmService::connectAlarmOutputs()
{
    connect(engine_, &AlarmEngine::recordsChanged,
            this, &AlarmService::publishRecords, Qt::DirectConnection);
    connect(engine_, &AlarmEngine::alarmRaised,
            this, &AlarmService::alarmRaised, Qt::DirectConnection);
    connect(engine_, &AlarmEngine::eventPublished,
            this, &AlarmService::eventPublished, Qt::DirectConnection);
}

void AlarmService::connectStorage()
{
    connect(engine_, &AlarmEngine::eventPublished,
            storage_, &AlarmStorageWorker::append, Qt::QueuedConnection);
    connect(storage_, &AlarmStorageWorker::restored,
            engine_, &AlarmEngine::restore, Qt::QueuedConnection);
    connect(storage_, &AlarmStorageWorker::statusChanged,
            this, &AlarmService::publishStorageStatus, Qt::DirectConnection);
}

void AlarmService::connectTemperature()
{
    connect(temperature_, &TemperatureDeviceService::configurationChanged,
            engine_, &AlarmEngine::configure, Qt::QueuedConnection);
    connect(temperature_, &TemperatureDeviceService::experimentTemperatureFinalized,
            storage_, &AlarmStorageWorker::storeTemperatureSummary, Qt::QueuedConnection);
    connect(temperature_, &TemperatureDeviceService::sessionStarted,
            engine_, &AlarmEngine::setSession, Qt::QueuedConnection);
    connect(temperature_, &TemperatureDeviceService::snapshotChanged, engine_,
            [engine = engine_](const TemperatureSnapshot& snapshot) {
                engine->update(temperatureUpdate(snapshot));
            }, Qt::QueuedConnection);
}

void AlarmService::connectSafety()
{
    connect(safety_, &SafetyMonitor::safetyStatusObserved,
            this, &AlarmService::handleSafetySnapshot, Qt::DirectConnection);
    connect(client_, &AcsClient::connectionChanged,
            this, &AlarmService::resetSafetySession, Qt::DirectConnection);
}

void AlarmService::startWorkers()
{
    monitorThread_.start();
    storageThread_.start();
    initialized_ = true;

    const TemperatureConfiguration config = temperature_->configuration();
    const QUuid session = safetySession_;
    QMetaObject::invokeMethod(engine_, [engine = engine_, config, session] {
        engine->start(config, session);
    }, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(storage_, &AlarmStorageWorker::initialize, Qt::QueuedConnection);
}

void AlarmService::publishRecords(const QVector<AlarmRecord>& records)
{
    {
        const QMutexLocker lock(&mutex_);
        records_ = records;
    }
    emit recordsChanged(records);
}

void AlarmService::publishStorageStatus(const QString& status)
{
    {
        const QMutexLocker lock(&mutex_);
        storageStatus_ = status;
    }
    emit storageStatusChanged(status);
}

void AlarmService::handleSafetySnapshot(const SafetyStatusSnapshot& snapshot)
{
    // Keep session selection and enqueueing ordered with resetSafetySession/shutdown.
    const QMutexLocker lock(&mutex_);
    if (!initialized_) {
        return;
    }

    const SafetyPointState state = snapshot.hardwareEmergencyStop.state;
    const bool valid = state == SafetyPointState::Triggered || state == SafetyPointState::Normal;
    DeviceUpdate update;
    update.sourceId = "safety/main";
    update.sessionId = safetySession_;
    update.points.insert(PointId::SafetyCircuit, {
        state == SafetyPointState::Triggered,
        valid ? DataQuality::Good : DataQuality::Invalid,
        snapshot.sampledAtUtc, deviceObservationMonoMs(), ++safetySequence_});
    QMetaObject::invokeMethod(engine_, [engine = engine_, update] {
        engine->update(update);
    }, Qt::QueuedConnection);
}

void AlarmService::resetSafetySession()
{
    const QMutexLocker lock(&mutex_);
    if (!initialized_) {
        return;
    }

    safetySession_ = QUuid::createUuid();
    safetySequence_ = 0;
    const QUuid session = safetySession_;
    QMetaObject::invokeMethod(engine_, [engine = engine_, session] {
        engine->setSession("safety/main", session);
    }, Qt::QueuedConnection);
}

void AlarmService::acknowledge(const QUuid& id, const QString& user)
{
    if (initialized_) { QMetaObject::invokeMethod(engine_, [engine = engine_, id, user] { engine->acknowledge(id, user); }, Qt::QueuedConnection); }
}
void AlarmService::setExperimentName(const QString& name)
{
    if (initialized_) { QMetaObject::invokeMethod(engine_, [engine = engine_, name] { engine->setExperimentName(name); }, Qt::QueuedConnection); }
}
void AlarmService::reportCondition(const ConditionReport& report)
{
    if (initialized_) { QMetaObject::invokeMethod(engine_, [engine = engine_, report] { engine->reportCondition(report); }, Qt::QueuedConnection); }
}
void AlarmService::registerDefinition(const AlarmDefinition& definition)
{
    if (initialized_) { QMetaObject::invokeMethod(engine_, [engine = engine_, definition] { engine->registerDefinition(definition); }, Qt::QueuedConnection); }
}
void AlarmService::shutdown()
{
    { const QMutexLocker lock(&mutex_); if (!initialized_.exchange(false)) { return; } }
    disconnect(temperature_, nullptr, engine_, nullptr); disconnect(safety_, nullptr, this, nullptr);
    disconnect(temperature_, nullptr, storage_, nullptr);
    disconnect(client_, nullptr, this, nullptr); disconnect(client_, nullptr, engine_, nullptr);
    QMetaObject::invokeMethod(engine_, &AlarmEngine::stop, Qt::BlockingQueuedConnection);
    monitorThread_.quit(); monitorThread_.wait(); engine_ = nullptr;
    QMetaObject::invokeMethod(storage_, &AlarmStorageWorker::shutdown, Qt::BlockingQueuedConnection);
    storageThread_.quit(); storageThread_.wait(); storage_ = nullptr; initialized_ = false;
}

#include "AlarmService.moc"
