#pragma once

#include "AcsClient.h"

#include <QObject>
#include <QThread>

struct TestParameters;

class MotionControlService final : public QObject
{
    Q_OBJECT

public:
    static MotionControlService& instance();

    void initialize();
    void shutdown();

    void connectController();
    void disconnectController();
    [[nodiscard]] AcsClient* acsClient() const;
    [[nodiscard]] bool start(const TestParameters& parameters,
                             QString* errorMessage = nullptr);
    [[nodiscard]] bool stop(QString* errorMessage = nullptr);

signals:
    void connectionChanged(bool connected, const QString& message);
    void motionStatusChanged(const AcsMotionStatus& status);
    void startRequestWritten();
    void stopRequestWritten();
    void commandFailed(const QString& message);

private:
    MotionControlService();
    ~MotionControlService() override;

    MotionControlService(const MotionControlService&) = delete;
    MotionControlService& operator=(const MotionControlService&) = delete;

    AcsClient* client_ = nullptr;
    QThread workerThread_;
    AcsMotionStatus currentStatus_;
    bool connected_ = false;
    bool initialized_ = false;
};
