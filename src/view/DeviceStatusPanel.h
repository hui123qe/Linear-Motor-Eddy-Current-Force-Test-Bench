#pragma once

#include <QWidget>

#include <array>

class QLabel;
class QEvent;
class QPushButton;
class StatusPill;
struct AcsSensorReadings;
enum class MachineMode;
enum class MachineState;

class DeviceStatusPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceStatusPanel(QWidget* parent = nullptr);

signals:
    void messageRequested(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onInspectButtonClicked();
    void updateAcsConnectionDisplay(bool connected, const QString&);
    void updateSensorReadings(const AcsSensorReadings& readings);
    void handleForceTareWritten();
    void handleForceTareFailed(const QString& message);

private:
    void updateMachineStateDisplay(MachineState state,
                                   const QString& reason);
    void updateMachineModeDisplay(MachineMode mode);
    QWidget* createPressureValuesCard();
    QWidget* createForceValueCard();
    void clearSensorReadings();
    void setSensorValue(QLabel* label, const QString& text, bool valid);
    void showAcsConnectionDialog();
    void showMachineModeDialog();
    void showForceTareDialog();
    QWidget* createStateItem(const QString& name,
                             const QString& state,
                             const QString& detail,
                             const QString& level = QStringLiteral("ok")) const;

    StatusPill* machineStatePill_ = nullptr;
    QLabel* machineStateReason_ = nullptr;
    QPushButton* machineModeButton_ = nullptr;
    QWidget* acsStateItem_ = nullptr;
    StatusPill* acsConnectionPill_ = nullptr;
    std::array<QLabel*, 5> pressureValueLabels_{};
    QWidget* forceSensorCard_ = nullptr;
    QLabel* forceValueLabel_ = nullptr;
    bool acsConnected_ = false;
    bool forceTarePending_ = false;
};
