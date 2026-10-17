#pragma once

#include "../safety/SafetyTypes.h"
#include "../temperature/TemperatureTypes.h"

#include <QWidget>
#include <QMap>

#include <array>

class QLabel;
class QEvent;
class QPushButton;
class QVBoxLayout;
class StatusPill;
struct AcsSensorReadings;
enum class MachineMode;
enum class MachineState;

class DeviceStatusPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceStatusPanel(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void updateAcsConnectionDisplay(bool connected, const QString&);
    void updateSensorReadings(const AcsSensorReadings& readings);
    void updateSafetyStatusDisplay(const SafetyStatusSnapshot& status);
    void handleForceTareStarted();
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
    void showTemperatureDialog();
    void rebuildTemperatureRows(const TemperatureConfiguration& configuration);
    void updateTemperatureReadings(const TemperatureSnapshot& snapshot);
    QWidget* createStateItem(const QString& name,
                             const QString& state,
                             const QString& detail,
                             const QString& level = QStringLiteral("ok"),
                             StatusPill** statePill = nullptr,
                             QLabel** detailLabel = nullptr) const;

    StatusPill* machineStatePill_ = nullptr;
    QLabel* machineStateReason_ = nullptr;
    QPushButton* machineModeButton_ = nullptr;
    QWidget* acsStateItem_ = nullptr;
    StatusPill* acsConnectionPill_ = nullptr;
    StatusPill* hardwareEmergencyStopPill_ = nullptr;
    QLabel* hardwareEmergencyStopDetail_ = nullptr;
    StatusPill* safetyLightPill_ = nullptr;
    QLabel* safetyLightDetail_ = nullptr;
    StatusPill* safetyDoorLockPill_ = nullptr;
    QLabel* safetyDoorLockDetail_ = nullptr;
    std::array<QLabel*, 5> pressureValueLabels_{};
    QWidget* forceSensorCard_ = nullptr;
    QLabel* forceValueLabel_ = nullptr;
    bool acsConnected_ = false;
    bool forceTarePending_ = false;
    QWidget* temperatureCard_ = nullptr;
    QVBoxLayout* temperatureRows_ = nullptr;
    StatusPill* temperaturePill_ = nullptr;
    QLabel* maximumTemperatureLabel_ = nullptr;
    QMap<int, QLabel*> temperatureValues_;
};
