#pragma once

#include "AcsClient.h"

#include <QObject>
#include <QThread>

struct TestParameters;

enum class MachineMode
{
    Automatic,
    Maintenance
};

Q_DECLARE_METATYPE(MachineMode)

inline constexpr double kMaintenancePtpVelocityMillimetersPerSecond = 50.0;
inline constexpr double kMaintenanceJogVelocityMillimetersPerSecond = 30.0;

class MotionControlService final : public QObject
{
    Q_OBJECT

public:
    static MotionControlService& instance();

    void initialize();
    void shutdown();

    void connectController();
    [[nodiscard]] bool disconnectController(QString* errorMessage = nullptr);
    void tareForceSensor();
    [[nodiscard]] MachineMode machineMode() const;
    [[nodiscard]] bool setMachineMode(MachineMode mode,
                                      QString* errorMessage = nullptr);
    [[nodiscard]] bool enableAxis(QString* errorMessage = nullptr);
    [[nodiscard]] bool disableAxis(QString* errorMessage = nullptr);
    [[nodiscard]] bool moveToZero(QString* errorMessage = nullptr);
    [[nodiscard]] bool moveRelative(double distanceMillimeters,
                                    QString* errorMessage = nullptr);
    [[nodiscard]] bool moveAbsolute(double positionMillimeters,
                                    QString* errorMessage = nullptr);
    [[nodiscard]] bool startJog(int direction,
                                QString* errorMessage = nullptr);
    [[nodiscard]] bool haltMaintenanceMotion(
        QString* errorMessage = nullptr);
    [[nodiscard]] AcsClient* acsClient() const;
    [[nodiscard]] bool start(const TestParameters& parameters,
                             QString* errorMessage = nullptr);
    [[nodiscard]] bool stop(QString* errorMessage = nullptr);

signals:
    void connectionChanged(bool connected, const QString& message);
    void motionStateChanged(int state, int errorCode);
    void recordProcessingEntered(int state);
    void completedRecordCountChanged(int completedCount);
    void axisEnabledChanged(bool enabled);
    void positionFeedbackChanged(double positionMillimeters);
    void velocityFeedbackChanged(double velocityMillimetersPerSecond);
    void machineModeChanged(MachineMode mode);
    void sensorReadingsChanged(const AcsSensorReadings& readings);
    void forceTareWritten();
    void forceTareFailed(const QString& message);
    void maintenanceCommandCompleted(MaintenanceCommand command);
    void maintenanceCommandFailed(MaintenanceCommand command,
                                  const QString& message);
    void startRequestWritten();
    void stopRequestWritten();
    void commandFailed(const QString& message);

private:
    MotionControlService();
    ~MotionControlService() override;

    MotionControlService(const MotionControlService&) = delete;
    MotionControlService& operator=(const MotionControlService&) = delete;

    [[nodiscard]] bool validateMaintenanceCommand(
        bool requireEnabled,
        QString* errorMessage) const;

    AcsClient* client_ = nullptr;
    QThread workerThread_;
    AcsMotionStatus currentStatus_;
    bool hasStatusSnapshot_ = false;
    MachineMode machineMode_ = MachineMode::Automatic;
    bool maintenanceCommandPending_ = false;
    bool connected_ = false;
    bool initialized_ = false;
};
