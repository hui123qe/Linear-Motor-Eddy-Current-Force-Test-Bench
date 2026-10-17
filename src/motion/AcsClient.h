#pragma once

#include "../acquisition/AcquisitionTypes.h"
#include "../safety/SafetyTypes.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>

#include <array>

class QTimer;

// Execution feedback belongs to motion control, independent of alarm lifecycle.
struct ProtectionStopRequest
{
    QUuid occurrenceId;
    int buffer = -1;
    QString reason;
};

struct ProtectionStopResult
{
    QUuid occurrenceId;
    int buffer = -1;
    bool bufferStopSucceeded = false;
    bool killAllSucceeded = false;
    QString detail;
};

Q_DECLARE_METATYPE(ProtectionStopResult)

struct MotionStartRequest
{
    double startPositionMeters = 0.0;
    double endPositionMeters = 0.0;
    double velocityMetersPerSecond = 0.0;
    int repeatCount = 0;
    double accelerationMetersPerSecondSquared = 0.0;
    double decelerationMetersPerSecondSquared = 0.0;
};

struct AcsMotionStatus
{
    int state = 0;
    int errorCode = 0;
    int currentCount = 0;
    bool homeDone = false;
    bool homeRunning = false;
    bool axisEnabled = false;
    bool axisMoving = false;
    double feedbackPositionMillimeters = 0.0;
    double feedbackVelocityMillimetersPerSecond = 0.0;
    bool bufferRunning = false;
};

enum class MaintenanceCommand
{
    EnableAxis,
    DisableAxis,
    HomeAxis,
    RelativeMove,
    AbsoluteMove,
    StartJog,
    Halt
};

struct AcsSensorReadings
{
    std::array<double, 5> pressureValues{};
    double forceValue = 0.0;
};

Q_DECLARE_METATYPE(AcsMotionStatus)
Q_DECLARE_METATYPE(AcsSensorReadings)
Q_DECLARE_METATYPE(MaintenanceCommand)

class AcsClient final : public QObject
{
    Q_OBJECT

public:
    explicit AcsClient(QObject* parent = nullptr);
    ~AcsClient() override;

public slots:
    void connectController();
    void disconnectController();
    void resetController();
    void stopConfiguredBufferAndKillAll(const ProtectionStopRequest& request);
    void tareForceSensor();
    void enableAxis();
    void disableAxis();
    void homeAxis();
    void moveRelative(double distanceMillimeters,
                      double velocityMillimetersPerSecond);
    void moveAbsolute(double positionMillimeters,
                      double velocityMillimetersPerSecond);
    void startJog(double velocityMillimetersPerSecond);
    void haltAxis();
    void moveToZero();
    void start(const MotionStartRequest& request);
    void stop();
    void setCollectionEnabled(bool enabled, quint64 generation = 0, int session = 0);
    void readCollectionMetadata(quint64 generation);
    void readCollectionBlock(int blockIndex, int expectedSequence, quint64 generation, int session);

signals:
    void connectionChanged(bool connected, const QString& message);
    void safetyStatusSampled(const SafetyStatusSnapshot& snapshot);
    void safetyStatusReadFailed(const QString& message);
    void controllerResetCompleted();
    void controllerResetFailed(const QString& message);
    void protectionStopResult(const ProtectionStopResult& result);
    void statusChanged(const AcsMotionStatus& status);
    void sensorReadingsChanged(const AcsSensorReadings& readings);
    void forceTareStarted();
    void forceTareFailed(const QString& message);
    void maintenanceCommandCompleted(MaintenanceCommand command);
    void maintenanceCommandFailed(MaintenanceCommand command,
                                  const QString& message);
    void zeroPositionReached();
    void startRequestWritten();
    void stopRequestWritten();
    void commandFailed(const QString& message);
    void collectionControlWritten(quint64 generation, bool enabled);
    void collectionMetadataRead(quint64 generation, const AcsCollectionMetadata& metadata);
    void collectionBlockRead(quint64 generation, const AcquisitionBlock& block);
    void collectionBlockRejected(quint64 generation, int blockIndex,
                                 int expectedSequence,
                                 const QString& message);
    void collectionCommandFailed(quint64 generation, const QString& message);

private slots:
    void pollStatus();
    void pollSensors();

private:
    [[nodiscard]] bool loadConfiguration(QString* errorMessage);
    [[nodiscard]] bool openConnection(QString* errorMessage);
    void closeConnection();
    [[nodiscard]] bool configureMaintenanceMotion(
        double velocityMillimetersPerSecond,
        QString* errorMessage);
    void executePointMotion(
        MaintenanceCommand command,
        int flags,
        double pointControllerUnits,
        double velocityMillimetersPerSecond);
    [[nodiscard]] bool readInteger(const char* variable,
                                   int* value,
                                   QString* errorMessage) const;
    [[nodiscard]] bool readReal(const char* variable,
                               double* value,
                               QString* errorMessage) const;
    [[nodiscard]] bool writeInteger(const char* variable,
                                    int value,
                                    QString* errorMessage) const;
    [[nodiscard]] bool writeReal(const char* variable,
                                 double value,
                                 QString* errorMessage) const;
    [[nodiscard]] bool readIntegerArray(const char* variable,
                                        int firstIndex,
                                        int lastIndex,
                                        int* values,
                                        QString* errorMessage) const;
    [[nodiscard]] bool readRealMatrix(const char* variable,
                                      int firstRow,
                                      int lastRow,
                                      int firstColumn,
                                      int lastColumn,
                                      double* values,
                                      QString* errorMessage) const;
    [[nodiscard]] QString sdkError(const QString& action) const;
    void handleCommunicationFailure(const QString& message);

    bool zeroMovePending_ = false;
    void* controllerHandle_;
    QTimer* pollTimer_ = nullptr;
    QTimer* sensorPollTimer_ = nullptr;
    QString connectionMode_;
    QString address_;
    int port_ = 0;
    int axis_ = 0;
    int pollIntervalMilliseconds_ = 50;
    double countsPerMillimeter_ = 0.0;
};
