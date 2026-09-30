#pragma once

#include "../acquisition/AcquisitionTypes.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <array>

class QTimer;

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
    double feedbackPositionMillimeters = 0.0;
    double feedbackVelocityMillimetersPerSecond = 0.0;
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
    void rebootController();
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
    void start(const MotionStartRequest& request);
    void stop();
    void setCollectionEnabled(bool enabled);
    void readCollectionMetadata();
    void readCollectionBlock(int blockIndex, int expectedSequence);

signals:
    void connectionChanged(bool connected, const QString& message);
    void controllerRebootCompleted();
    void controllerRebootFailed(const QString& message);
    void statusChanged(const AcsMotionStatus& status);
    void sensorReadingsChanged(const AcsSensorReadings& readings);
    void forceTareWritten();
    void forceTareFailed(const QString& message);
    void maintenanceCommandCompleted(MaintenanceCommand command);
    void maintenanceCommandFailed(MaintenanceCommand command,
                                  const QString& message);
    void startRequestWritten();
    void stopRequestWritten();
    void commandFailed(const QString& message);
    void collectionControlWritten(bool enabled);
    void collectionMetadataRead(const AcsCollectionMetadata& metadata);
    void collectionBlockRead(const AcquisitionBlock& block);
    void collectionBlockRejected(int blockIndex,
                                 int expectedSequence,
                                 const QString& message);
    void collectionCommandFailed(const QString& message);

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
