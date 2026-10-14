#pragma once

#include "AlarmTypes.h"

#include <QMutex>
#include <QObject>
#include <QThread>
#include <atomic>

class AlarmEngine;
class AlarmStorageWorker;
class SafetyMonitor;
class AcsClient;
class TemperatureDeviceService;
struct SafetyStatusSnapshot;

class AlarmService final : public QObject
{
    Q_OBJECT
public:
    static AlarmService& instance();
    void initialize(TemperatureDeviceService* temperature, SafetyMonitor* safety, AcsClient* client);
    void shutdown();
    [[nodiscard]] QVector<AlarmRecord> records() const;
    [[nodiscard]] QString storageStatus() const;
    void acknowledge(const QUuid& occurrenceId, const QString& user);
    void setExperimentName(const QString& name);
    void reportCondition(const ConditionReport& report);
    void registerDefinition(const AlarmDefinition& definition);
signals:
    void recordsChanged(const QVector<AlarmRecord>& records);
    void eventPublished(const AlarmEvent& event);
    void alarmRaised(const AlarmRecord& record);
    void operationRejected(const QString& message);
    void storageStatusChanged(const QString& status);
private:
    AlarmService();
    ~AlarmService() override;

    void createWorkers();
    void connectAlarmOutputs();
    void connectStorage();
    void connectTemperature();
    void connectSafety();
    void startWorkers();

    // Direct signal handlers run on producer threads; protect shared state.
    void publishRecords(const QVector<AlarmRecord>& records);
    void publishStorageStatus(const QString& status);
    void handleSafetySnapshot(const SafetyStatusSnapshot& snapshot);
    void resetSafetySession();

    mutable QMutex mutex_;
    QVector<AlarmRecord> records_;
    QString storageStatus_ = QStringLiteral("报警存储未初始化");
    QUuid safetySession_;
    quint64 safetySequence_ = 0;
    QThread monitorThread_;
    QThread storageThread_;
    AlarmEngine* engine_ = nullptr;
    AlarmStorageWorker* storage_ = nullptr;
    TemperatureDeviceService* temperature_ = nullptr;
    SafetyMonitor* safety_ = nullptr;
    AcsClient* client_ = nullptr;
    std::atomic<bool> initialized_{false};
};
