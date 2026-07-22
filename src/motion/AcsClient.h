#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>

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
};

Q_DECLARE_METATYPE(AcsMotionStatus)

class AcsClient final : public QObject
{
    Q_OBJECT

public:
    explicit AcsClient(QObject* parent = nullptr);
    ~AcsClient() override;

public slots:
    void connectController();
    void disconnectController();
    void start(const MotionStartRequest& request);
    void stop();

signals:
    void connectionChanged(bool connected, const QString& message);
    void statusChanged(const AcsMotionStatus& status);
    void startRequestWritten();
    void stopRequestWritten();
    void commandFailed(const QString& message);

private slots:
    void pollStatus();

private:
    [[nodiscard]] bool loadConfiguration(QString* errorMessage);
    [[nodiscard]] bool openConnection(QString* errorMessage);
    void closeConnection();
    [[nodiscard]] bool readInteger(const char* variable,
                                   int* value,
                                   QString* errorMessage) const;
    [[nodiscard]] bool writeInteger(const char* variable,
                                    int value,
                                    QString* errorMessage) const;
    [[nodiscard]] bool writeReal(const char* variable,
                                 double value,
                                 QString* errorMessage) const;
    [[nodiscard]] QString sdkError(const QString& action) const;
    void handleCommunicationFailure(const QString& message);

    void* controllerHandle_;
    QTimer* pollTimer_ = nullptr;
    QString connectionMode_;
    QString address_;
    int port_ = 0;
    int axis_ = 0;
    int pollIntervalMilliseconds_ = 50;
    double countsPerMillimeter_ = 0.0;
};
