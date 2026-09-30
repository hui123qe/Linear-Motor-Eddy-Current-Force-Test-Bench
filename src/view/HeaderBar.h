#pragma once

#include <QWidget>

class QEvent;
class QPushButton;

class HeaderBar final : public QWidget
{
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onEmergencyStopClicked();
    void handleEmergencyStopCompleted();
    void handleEmergencyStopFailed(const QString& message);

private:
    QPushButton* emergencyStopButton_ = nullptr;
};
