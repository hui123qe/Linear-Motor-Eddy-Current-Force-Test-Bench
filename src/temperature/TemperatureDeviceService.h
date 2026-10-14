#pragma once

#include "TemperatureTypes.h"

#include <QMutex>
#include <QObject>
#include <QThread>

class TemperatureWorker;

class ITemperatureDevice : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void connectDevice(const TemperatureConfiguration& config) = 0;
    virtual void disconnectDevice() = 0;
signals:
    void connectionChanged(TemperatureConnectionState state, const QString& detail);
    void sessionStarted(const QString& sourceId, const QUuid& sessionId);
    void snapshotReady(const TemperatureSnapshot& snapshot);
};

class TemperatureDeviceService final : public QObject
{
    Q_OBJECT
public:
    static TemperatureDeviceService& instance();
    void initialize();
    void shutdown();
    void beginExperimentRecording(const QString& experimentName);
    void finishExperimentRecording();
    [[nodiscard]] TemperatureConfiguration configuration() const;
    [[nodiscard]] TemperatureSnapshot snapshot() const;
    [[nodiscard]] TemperatureConnectionState connectionState() const;
    [[nodiscard]] QString connectionDetail() const;
    [[nodiscard]] bool connectDevice(QString* errorMessage = nullptr);
    [[nodiscard]] bool disconnectDevice(QString* errorMessage = nullptr);
    [[nodiscard]] static bool validateConfiguration(const TemperatureConfiguration& config,
                                                    QString* errorMessage = nullptr);
signals:
    void configurationChanged(const TemperatureConfiguration& config);
    void connectionChanged(TemperatureConnectionState state, const QString& detail);
    void sessionStarted(const QString& sourceId, const QUuid& sessionId);
    void snapshotChanged(const TemperatureSnapshot& snapshot);
    void experimentTemperatureFinalized(const TemperatureExperimentSummary& summary);
private:
    TemperatureDeviceService();
    ~TemperatureDeviceService() override;
    mutable QMutex mutex_;
    TemperatureConfiguration configuration_;
    TemperatureSnapshot snapshot_;
    TemperatureConnectionState state_ = TemperatureConnectionState::Disconnected;
    QString detail_ = QStringLiteral("未连接");
    QThread workerThread_;
    TemperatureWorker* worker_ = nullptr;
    bool initialized_ = false;
    TemperatureExperimentSummary experiment_;
    qint64 experimentStartedMs_ = 0;
};
