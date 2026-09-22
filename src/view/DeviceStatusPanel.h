#pragma once

#include <QWidget>

class QLabel;
class StatusPill;
enum class MachineState;

class DeviceStatusPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceStatusPanel(QWidget* parent = nullptr);

signals:
    void messageRequested(const QString& message);

private slots:
    void onInspectButtonClicked();

private:
    void updateMachineStateDisplay(MachineState state,
                                   const QString& reason);
    QWidget* createStateItem(const QString& name,
                             const QString& state,
                             const QString& detail,
                             const QString& level = QStringLiteral("ok")) const;

    StatusPill* machineStatePill_ = nullptr;
    QLabel* machineStateReason_ = nullptr;
};
