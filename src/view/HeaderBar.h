#pragma once

#include <QWidget>

class QPushButton;

class HeaderBar final : public QWidget
{
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);

private slots:
    void onEmergencyStopClicked();
    void handleEmergencyStopCompleted();
    void handleEmergencyStopFailed(const QString& message);

private:
    QPushButton* emergencyStopButton_ = nullptr;
};
