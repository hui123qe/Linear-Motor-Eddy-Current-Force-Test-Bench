#pragma once

#include "../../motion/MotionControlService.h"

#include <QWidget>

class QDoubleSpinBox;
class QHideEvent;
class MetricCard;
class QPushButton;

class MaintenancePage final : public QWidget
{
    Q_OBJECT

public:
    explicit MaintenancePage(QWidget* parent = nullptr);

protected:
    void hideEvent(QHideEvent* event) override;

private slots:
    void onConnectButtonClicked();
    void onDisconnectButtonClicked();
    void onEnableButtonClicked();
    void onDisableButtonClicked();
    void onHaltButtonClicked();
    void onMoveToZeroClicked();
    void onRelativeMoveClicked();
    void onAbsoluteMoveClicked();
    void onJogNegativePressed();
    void onJogPositivePressed();
    void onJogReleased();
    void setControllerConnected(bool connected, const QString& message);
    void setMachineMode(MachineMode mode);
    void setMotionStatus(const AcsMotionStatus& status);
    void handleMaintenanceCommandCompleted(MaintenanceCommand command);
    void handleMaintenanceCommandFailed(MaintenanceCommand command,
                                        const QString& message);

private:
    void stopJog(bool showFailure);
    void showCommandFailure(const QString& title, const QString& message);

    MetricCard* connectionCard_ = nullptr;
    MetricCard* enableCard_ = nullptr;
    MetricCard* positionCard_ = nullptr;
    MetricCard* modeCard_ = nullptr;
    QDoubleSpinBox* relativeDistanceInput_ = nullptr;
    QDoubleSpinBox* absolutePositionInput_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QPushButton* enableButton_ = nullptr;
    QPushButton* disableButton_ = nullptr;
    QPushButton* haltButton_ = nullptr;
    QPushButton* moveToZeroButton_ = nullptr;
    QPushButton* relativeMoveButton_ = nullptr;
    QPushButton* absoluteMoveButton_ = nullptr;
    QPushButton* jogNegativeButton_ = nullptr;
    QPushButton* jogPositiveButton_ = nullptr;
    MachineMode machineMode_;
    bool controllerConnected_ = false;
    bool axisEnabled_ = false;
    bool axisMoving_ = false;
    bool jogCommandActive_ = false;
};
