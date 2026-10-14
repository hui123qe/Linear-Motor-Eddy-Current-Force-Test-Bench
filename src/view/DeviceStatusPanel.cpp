#include "DeviceStatusPanel.h"

#include "widgets/StatusPill.h"
#include "widgets/ViewHelpers.h"
#include "../machine/MachineStateManager.h"
#include "../motion/AcsVariableNames.h"
#include "../motion/MotionControlService.h"
#include "../safety/SafetyMonitor.h"
#include "../temperature/TemperatureDeviceService.h"

#include <QAbstractButton>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
#include <QDialog>
#include <QTimer>

namespace {

constexpr int kDevicePanelWidth = 310;

void updateSafetyPointDisplay(StatusPill* pill,
                              QLabel* detailLabel,
                              const SafetyPointStatus& status,
                              const QString& source)
{
    if (pill == nullptr || detailLabel == nullptr) {
        return;
    }

    switch (status.state) {
    case SafetyPointState::Normal:
        pill->setText(QStringLiteral("正常"));
        pill->setLevel(QStringLiteral("ok"));
        detailLabel->setText(QStringLiteral("值：%1").arg(status.rawValue));
        detailLabel->setToolTip(
            QStringLiteral("%1 = %2").arg(source).arg(status.rawValue));
        return;
    case SafetyPointState::Triggered:
        pill->setText(QStringLiteral("触发"));
        pill->setLevel(QStringLiteral("danger"));
        detailLabel->setText(QStringLiteral("值：%1").arg(status.rawValue));
        detailLabel->setToolTip(
            QStringLiteral("%1 = %2").arg(source).arg(status.rawValue));
        return;
    case SafetyPointState::Unknown:
        pill->setText(QStringLiteral("未知"));
        pill->setLevel(QStringLiteral("neutral"));
        detailLabel->setText(QStringLiteral("等待有效数据"));
        detailLabel->setToolTip(source);
        return;
    case SafetyPointState::Unconfigured:
        pill->setText(QStringLiteral("未配置"));
        pill->setLevel(QStringLiteral("neutral"));
        detailLabel->setText(source);
        return;
    }
}

SafetyPointStatus aggregateSafetyLights(
    const std::array<SafetyPointStatus, kSafetyLightCount>& lights)
{
    SafetyPointStatus aggregate;
    aggregate.state = SafetyPointState::Normal;
    for (const SafetyPointStatus& light : lights) {
        if (light.state == SafetyPointState::Triggered) {
            aggregate.state = SafetyPointState::Triggered;
            aggregate.rawValue = 1;
            return aggregate;
        }
        if (light.state != SafetyPointState::Normal) {
            aggregate.state = SafetyPointState::Unknown;
        }
    }
    return aggregate;
}

} // namespace

DeviceStatusPanel::DeviceStatusPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("deviceRail"));
    setFixedWidth(kDevicePanelWidth);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 14, 14, 14);
    outer->setSpacing(10);

    auto* titleRow = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("设备状态"), "railTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("状态面板始终可见"), "railSubtitle"));
    titleRow->addLayout(titleBox);
    titleRow->addStretch();
    titleRow->addWidget(new StatusPill(QStringLiteral("状态监测"), QStringLiteral("neutral"), this));
    outer->addLayout(titleRow);

    auto* machineStateCard = new QFrame;
    machineStateCard->setObjectName(QStringLiteral("machineStateCard"));
    auto* machineStateLayout = new QVBoxLayout(machineStateCard);
    machineStateLayout->setContentsMargins(12, 10, 12, 10);
    machineStateLayout->setSpacing(6);
    machineStateLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("机器状态"), "deviceGroupTitle"));

    auto* machineStateRow = new QHBoxLayout;
    machineStateRow->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("状态"), "stateName"));
    machineStateRow->addStretch();
    machineStatePill_ = new StatusPill(
        QStringLiteral("故障"), QStringLiteral("danger"), machineStateCard);
    machineStateRow->addWidget(machineStatePill_);
    machineStateLayout->addLayout(machineStateRow);

    machineStateReason_ = ViewHelpers::makeLabel(
        QStringLiteral("ACS 控制器未连接"), "stateDetail");
    machineStateReason_->setWordWrap(true);
    machineStateReason_->setMaximumHeight(
        machineStateReason_->fontMetrics().lineSpacing() * 2 + 2);
    machineStateLayout->addWidget(machineStateReason_);

    auto* machineModeRow = new QHBoxLayout;
    machineModeRow->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("模式"), "stateName"));
    machineModeRow->addStretch();
    machineModeButton_ =
        ViewHelpers::makeButton(QStringLiteral("自动模式"), QStringLiteral("primary"));
    connect(machineModeButton_,
            &QPushButton::clicked,
            this,
            &DeviceStatusPanel::showMachineModeDialog);
    machineModeRow->addWidget(machineModeButton_);
    machineStateLayout->addLayout(machineModeRow);
    outer->addWidget(machineStateCard);
    MachineStateManager& machineStateManager = MachineStateManager::instance();
    connect(&machineStateManager,
            &MachineStateManager::machineStateChanged,
            this,
            &DeviceStatusPanel::updateMachineStateDisplay);
    updateMachineStateDisplay(machineStateManager.state(),
                              machineStateManager.reason());

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 2, 0);
    contentLayout->setSpacing(10);

    auto* safetyGroup = new QFrame;
    safetyGroup->setObjectName(QStringLiteral("deviceGroup"));
    auto* safetyLayout = new QVBoxLayout(safetyGroup);
    safetyLayout->setContentsMargins(10, 10, 10, 10);
    safetyLayout->setSpacing(7);
    safetyLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("安全链"), "deviceGroupTitle"));
    safetyLayout->addWidget(createStateItem(
        QStringLiteral("硬件急停回路"),
        QStringLiteral("未知"),
        QStringLiteral("等待有效数据"),
        QStringLiteral("neutral"),
        &hardwareEmergencyStopPill_,
        &hardwareEmergencyStopDetail_));
    safetyLayout->addWidget(createStateItem(
        QStringLiteral("光栅"),
        QStringLiteral("未知"),
        QStringLiteral("等待有效数据"),
        QStringLiteral("neutral"),
        &safetyLightPill_,
        &safetyLightDetail_));
    safetyLayout->addWidget(createStateItem(
        QStringLiteral("安全门锁"),
        QStringLiteral("未知"),
        QStringLiteral("等待有效数据"),
        QStringLiteral("neutral"),
        &safetyDoorLockPill_,
        &safetyDoorLockDetail_));
    contentLayout->addWidget(safetyGroup);

    SafetyMonitor& safetyMonitor = SafetyMonitor::instance();
    connect(&safetyMonitor,
            &SafetyMonitor::safetySnapshotChanged,
            this,
            &DeviceStatusPanel::updateSafetyStatusDisplay);
    updateSafetyStatusDisplay(safetyMonitor.snapshot());

    auto* communicationGroup = new QFrame;
    communicationGroup->setObjectName(QStringLiteral("deviceGroup"));
    auto* communicationLayout = new QVBoxLayout(communicationGroup);
    communicationLayout->setContentsMargins(10, 10, 10, 10);
    communicationLayout->setSpacing(7);
    communicationLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("控制与通信"), "deviceGroupTitle"));

    acsStateItem_ = createStateItem(
        QStringLiteral("ACS 控制器"),
        QStringLiteral("离线"),
        QString(),
        QStringLiteral("neutral"));
    acsStateItem_->setProperty("interactive", true);
    acsStateItem_->setCursor(Qt::PointingHandCursor);
    acsStateItem_->installEventFilter(this);
    const QList<QWidget*> acsItemChildren =
        acsStateItem_->findChildren<QWidget*>();
    for (QWidget* child : acsItemChildren) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    acsConnectionPill_ = acsStateItem_->findChild<StatusPill*>();
    communicationLayout->addWidget(acsStateItem_);
    contentLayout->addWidget(communicationGroup);

    MotionControlService& motionControlService =
        MotionControlService::instance();
    connect(&motionControlService,
            &MotionControlService::connectionChanged,
            this,
            &DeviceStatusPanel::updateAcsConnectionDisplay);
    connect(&motionControlService,
            &MotionControlService::machineModeChanged,
            this,
            [this](MachineMode mode) {
                updateMachineModeDisplay(mode);
            });
    updateMachineModeDisplay(motionControlService.machineMode());
    connect(&motionControlService,
            &MotionControlService::sensorReadingsChanged,
            this,
            &DeviceStatusPanel::updateSensorReadings);
    connect(&motionControlService,
            &MotionControlService::forceTareStarted,
            this,
            &DeviceStatusPanel::handleForceTareStarted);
    connect(&motionControlService,
            &MotionControlService::forceTareFailed,
            this,
            &DeviceStatusPanel::handleForceTareFailed);

    auto* sensorGroup = new QFrame;
    sensorGroup->setObjectName(QStringLiteral("deviceGroup"));
    auto* sensorLayout = new QVBoxLayout(sensorGroup);
    sensorLayout->setContentsMargins(10, 10, 10, 10);
    sensorLayout->setSpacing(7);
    sensorLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("传感器"), "deviceGroupTitle"));
    sensorLayout->addWidget(createPressureValuesCard());
    forceSensorCard_ = createForceValueCard();
    forceSensorCard_->setProperty("interactive", true);
    forceSensorCard_->setCursor(Qt::PointingHandCursor);
    forceSensorCard_->installEventFilter(this);
    const QList<QWidget*> forceCardChildren =
        forceSensorCard_->findChildren<QWidget*>();
    for (QWidget* child : forceCardChildren) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    sensorLayout->addWidget(forceSensorCard_);
    auto* temperatureFrame = new QFrame;
    temperatureCard_ = temperatureFrame;
    temperatureFrame->setObjectName(QStringLiteral("deviceGroup"));
    temperatureFrame->setProperty("interactive", true);
    temperatureFrame->setCursor(Qt::PointingHandCursor);
    temperatureFrame->installEventFilter(this);
    auto* temperatureLayout = new QVBoxLayout(temperatureFrame);
    auto* temperatureTitle = new QHBoxLayout;
    temperatureTitle->addWidget(ViewHelpers::makeLabel(QStringLiteral("温度采集模块"), "deviceGroupTitle"));
    temperatureTitle->addStretch();
    temperaturePill_ = new StatusPill(QStringLiteral("离线"), QStringLiteral("neutral"), temperatureFrame);
    temperatureTitle->addWidget(temperaturePill_); temperatureLayout->addLayout(temperatureTitle);
    temperatureRows_ = new QVBoxLayout;
    temperatureLayout->addLayout(temperatureRows_);
    maximumTemperatureLabel_ = ViewHelpers::makeLabel(QStringLiteral("当前最高：--"), "stateDetail");
    maximumTemperatureLabel_->setWordWrap(true); temperatureLayout->addWidget(maximumTemperatureLabel_);
    sensorLayout->addWidget(temperatureFrame);
    TemperatureDeviceService& temperatureService = TemperatureDeviceService::instance();
    connect(&temperatureService, &TemperatureDeviceService::configurationChanged,
            this, &DeviceStatusPanel::rebuildTemperatureRows, Qt::QueuedConnection);
    connect(&temperatureService, &TemperatureDeviceService::snapshotChanged,
            this, &DeviceStatusPanel::updateTemperatureReadings, Qt::QueuedConnection);
    connect(&temperatureService, &TemperatureDeviceService::connectionChanged, this,
        [this](TemperatureConnectionState state, const QString& detail) {
            const QString backend = TemperatureDeviceService::instance().configuration().backend == "simulator"
                ? QStringLiteral("虚拟") : QStringLiteral("真实");
            const QStringList names{QStringLiteral("离线"), QStringLiteral("连接中"), QStringLiteral("待数据"), QStringLiteral("在线"), QStringLiteral("异常")};
            temperaturePill_->setText(backend + QStringLiteral("·") + names.at(static_cast<int>(state)));
            temperaturePill_->setLevel(state == TemperatureConnectionState::Ready ? "ok"
                : (state == TemperatureConnectionState::Error ? "danger" : "neutral"));
            temperatureCard_->setToolTip(detail);
        }, Qt::QueuedConnection);
    rebuildTemperatureRows(temperatureService.configuration());
    auto* freshnessTimer = new QTimer(this); freshnessTimer->setInterval(500);
    connect(freshnessTimer, &QTimer::timeout, this, [this] {
        updateTemperatureReadings(TemperatureDeviceService::instance().snapshot());
    }); freshnessTimer->start();
    contentLayout->addWidget(sensorGroup);
    contentLayout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    auto* inspect = ViewHelpers::makeButton(QStringLiteral("执行设备自检"));
    connect(inspect,
            &QPushButton::clicked,
            this,
            &DeviceStatusPanel::onInspectButtonClicked);
    outer->addWidget(inspect);
}

bool DeviceStatusPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == temperatureCard_ && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) { showTemperatureDialog(); return true; }
    }
    if (watched == acsStateItem_
        && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            showAcsConnectionDialog();
            return true;
        }
    }
    if (watched == forceSensorCard_
        && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            showForceTareDialog();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void DeviceStatusPanel::rebuildTemperatureRows(const TemperatureConfiguration& config)
{
    temperatureValues_.clear();
    while (QLayoutItem* item = temperatureRows_->takeAt(0)) {
        delete item->widget(); delete item;
    }
    for (const int channel : config.channels) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("通道 %1").arg(channel), "stateName"));
        layout->addStretch();
        QLabel* value = ViewHelpers::makeLabel(QStringLiteral("--"), "stateDetail");
        layout->addWidget(value); temperatureValues_.insert(channel, value); temperatureRows_->addWidget(row);
    }
    for (QWidget* child : temperatureCard_->findChildren<QWidget*>()) { child->setAttribute(Qt::WA_TransparentForMouseEvents); }
    updateTemperatureReadings(TemperatureSnapshot{});
}

void DeviceStatusPanel::updateTemperatureReadings(const TemperatureSnapshot& snapshot)
{
    for (QLabel* label : temperatureValues_) { setSensorValue(label, QStringLiteral("--"), false); }
    const TemperatureConfiguration config = TemperatureDeviceService::instance().configuration();
    const bool stale = snapshot.observedMonoMs == 0 || deviceObservationMonoMs() - snapshot.observedMonoMs >= config.staleAfterMs;
    std::optional<double> maximum; int maximumChannel = 0;
    for (const TemperatureChannelSample& sample : snapshot.channels) {
        QLabel* label = temperatureValues_.value(sample.channel, nullptr);
        if (sample.quality != DataQuality::Good) { setSensorValue(label, QStringLiteral("探头故障/无效"), false); }
        else if (stale) { setSensorValue(label, QStringLiteral("数据过期"), false); }
        else {
            setSensorValue(label, QStringLiteral("%1 ℃").arg(sample.celsius, 0, 'f', 1), true);
            if (!maximum || sample.celsius > *maximum) { maximum = sample.celsius; maximumChannel = sample.channel; }
        }
    }
    QString text = maximum ? QStringLiteral("当前最高：%1 ℃（通道 %2）").arg(*maximum, 0, 'f', 1).arg(maximumChannel) : QStringLiteral("当前最高：--");
    if (snapshot.sessionPeakCelsius) { text += QStringLiteral("\n会话峰值：%1 ℃（通道 %2）").arg(*snapshot.sessionPeakCelsius, 0, 'f', 1).arg(snapshot.sessionPeakChannel); }
    if (snapshot.experimentPeakCelsius) { text += QStringLiteral("\n实验峰值：%1 ℃（通道 %2）").arg(*snapshot.experimentPeakCelsius, 0, 'f', 1).arg(snapshot.experimentPeakChannel); }
    maximumTemperatureLabel_->setText(text);
}

void DeviceStatusPanel::showTemperatureDialog()
{
    TemperatureDeviceService& service = TemperatureDeviceService::instance();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("温度设备"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* status = new QLabel(service.connectionDetail());
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* buttons = new QHBoxLayout;
    QPushButton* connectButton = ViewHelpers::makeButton(QStringLiteral("连接"), "primary");
    QPushButton* disconnectButton = ViewHelpers::makeButton(QStringLiteral("断开连接"));
    buttons->addWidget(connectButton);
    buttons->addWidget(disconnectButton);
    layout->addLayout(buttons);
    const auto refresh = [connectButton, disconnectButton](TemperatureConnectionState state) {
        connectButton->setEnabled(state == TemperatureConnectionState::Disconnected
                                  || state == TemperatureConnectionState::Error);
        disconnectButton->setEnabled(state != TemperatureConnectionState::Disconnected);
    };
    refresh(service.connectionState());
    connect(&service, &TemperatureDeviceService::connectionChanged, &dialog,
        [status, refresh](TemperatureConnectionState state, const QString& detail) {
            status->setText(detail);
            refresh(state);
        }, Qt::QueuedConnection);
    connect(connectButton, &QPushButton::clicked, &dialog, [&service, &dialog] {
        QString error;
        if (!service.connectDevice(&error)) { QMessageBox::warning(&dialog, QStringLiteral("温度连接"), error); }
    });
    connect(disconnectButton, &QPushButton::clicked, &dialog, [&service, &dialog] {
        QString error;
        if (!service.disconnectDevice(&error)) { QMessageBox::warning(&dialog, QStringLiteral("温度连接"), error); }
    });
    dialog.exec();
}

void DeviceStatusPanel::onInspectButtonClicked()
{
    emit messageRequested(QStringLiteral("ACS：%1；安全回路：%2；温度设备：%3")
        .arg(acsConnected_ ? QStringLiteral("已连接") : QStringLiteral("未连接"),
             SafetyMonitor::instance().state() == SafetyState::Normal
                 ? QStringLiteral("正常") : (SafetyMonitor::instance().state() == SafetyState::Triggered
                     ? QStringLiteral("已触发") : QStringLiteral("未知")),
             TemperatureDeviceService::instance().connectionDetail()));
}

void DeviceStatusPanel::updateAcsConnectionDisplay(
    bool connected,
    const QString&)
{
    acsConnected_ = connected;
    if (acsConnectionPill_ == nullptr) {
        return;
    }

    acsConnectionPill_->setText(
        connected ? QStringLiteral("在线") : QStringLiteral("离线"));
    acsConnectionPill_->setLevel(
        connected ? QStringLiteral("ok") : QStringLiteral("danger"));
    if (!connected) {
        forceTarePending_ = false;
        clearSensorReadings();
    }
}

void DeviceStatusPanel::updateSafetyStatusDisplay(
    const SafetyStatusSnapshot& status)
{
    updateSafetyPointDisplay(hardwareEmergencyStopPill_,
                             hardwareEmergencyStopDetail_,
                             status.hardwareEmergencyStop,
                             QStringLiteral("ACS Hardware Emergency Stop"));
    updateSafetyPointDisplay(safetyLightPill_,
                             safetyLightDetail_,
                             aggregateSafetyLights(status.safetyLights),
                             QStringLiteral("4 路光栅汇总"));
    updateSafetyPointDisplay(
        safetyDoorLockPill_,
        safetyDoorLockDetail_,
        status.safetyDoorLock,
        QString::fromLatin1(AcsVariableNames::safetyDoorLock));
}

void DeviceStatusPanel::updateSensorReadings(
    const AcsSensorReadings& readings)
{
    for (std::size_t index = 0; index < pressureValueLabels_.size(); ++index) {
        setSensorValue(
            pressureValueLabels_.at(index),
            QStringLiteral("%1 L/min")
                .arg(QString::number(
                    readings.pressureValues.at(index), 'g', 3)),
            true);
    }
    setSensorValue(
        forceValueLabel_,
        QStringLiteral("%1 N")
            .arg(QString::number(readings.forceValue, 'g', 3)),
        true);
}

void DeviceStatusPanel::handleForceTareStarted()
{
    forceTarePending_ = false;
    QMessageBox::information(
        this,
        QStringLiteral("力传感器去皮"),
        QStringLiteral("ACS Buffer 8 去皮程序已启动。"));
}

void DeviceStatusPanel::handleForceTareFailed(const QString& message)
{
    forceTarePending_ = false;
    QMessageBox::warning(
        this, QStringLiteral("力传感器去皮失败"), message);
}

void DeviceStatusPanel::showAcsConnectionDialog()
{
    MachineStateManager& machineStateManager =
        MachineStateManager::instance();
    if (acsConnected_
        && machineStateManager.state() == MachineState::Running) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法断开 ACS 控制器"),
            QStringLiteral("机器当前处于运行状态，请先停止测试后再断开连接。"));
        return;
    }

    const bool disconnectRequested = acsConnected_;
    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Question);
    dialog.setWindowTitle(QStringLiteral("ACS 控制器连接"));
    dialog.setText(
        disconnectRequested
            ? QStringLiteral("ACS 控制器当前在线，是否断开连接？")
            : QStringLiteral("ACS 控制器当前离线，是否发起连接？"));
    QAbstractButton* connectionButton = dialog.addButton(
        disconnectRequested ? QStringLiteral("断开连接")
                            : QStringLiteral("连接"),
        disconnectRequested ? QMessageBox::DestructiveRole
                            : QMessageBox::AcceptRole);
    dialog.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    dialog.exec();
    if (dialog.clickedButton() != connectionButton) {
        return;
    }

    MotionControlService& motionControlService =
        MotionControlService::instance();
    if (disconnectRequested) {
        if (machineStateManager.state() == MachineState::Running) {
            QMessageBox::warning(
                this,
                QStringLiteral("无法断开 ACS 控制器"),
                QStringLiteral(
                    "机器当前处于运行状态，请先停止测试后再断开连接。"));
            return;
        }
        QString errorMessage;
        if (!motionControlService.disconnectController(&errorMessage)) {
            QMessageBox::warning(
                this,
                QStringLiteral("断开 ACS 控制器失败"),
                errorMessage);
        }
    } else {
        motionControlService.connectController();
    }
}

void DeviceStatusPanel::showMachineModeDialog()
{
    MotionControlService& motionControlService =
        MotionControlService::instance();
    const MachineMode currentMode = motionControlService.machineMode();
    const MachineMode targetMode =
        currentMode == MachineMode::Automatic
            ? MachineMode::Maintenance
            : MachineMode::Automatic;
    const QString targetText =
        targetMode == MachineMode::Automatic
            ? QStringLiteral("自动模式")
            : QStringLiteral("维修模式");
    const MachineStateManager& machineStateManager =
        MachineStateManager::instance();
    if (targetMode == MachineMode::Maintenance
        && machineStateManager.state() != MachineState::Idle) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法进入维修模式"),
            QStringLiteral("任务当前不是空闲状态：%1")
                .arg(machineStateManager.reason()));
        return;
    }

    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Question);
    dialog.setWindowTitle(QStringLiteral("切换机器模式"));
    dialog.setText(
        QStringLiteral("确认切换为%1？").arg(targetText));
    QAbstractButton* switchButton = dialog.addButton(
        QStringLiteral("切换"), QMessageBox::AcceptRole);
    dialog.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    dialog.exec();
    if (dialog.clickedButton() != switchButton) {
        return;
    }
    if (targetMode == MachineMode::Maintenance
        && machineStateManager.state() != MachineState::Idle) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法进入维修模式"),
            QStringLiteral("任务状态已经变化：%1")
                .arg(machineStateManager.reason()));
        return;
    }

    QString errorMessage;
    if (!motionControlService.setMachineMode(targetMode, &errorMessage)) {
        QMessageBox::warning(
            this, QStringLiteral("切换机器模式失败"), errorMessage);
    }
}

void DeviceStatusPanel::showForceTareDialog()
{
    if (!acsConnected_) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法执行力传感器去皮"),
            QStringLiteral("ACS 控制器未连接。"));
        return;
    }

    MachineStateManager& machineStateManager =
        MachineStateManager::instance();
    if (machineStateManager.state() == MachineState::Running) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法执行力传感器去皮"),
            QStringLiteral("机器当前处于运行状态，请先停止测试。"));
        return;
    }
    if (forceTarePending_) {
        QMessageBox::information(
            this,
            QStringLiteral("力传感器去皮"),
            QStringLiteral("去皮请求正在处理中。"));
        return;
    }

    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Question);
    dialog.setWindowTitle(QStringLiteral("力传感器去皮"));
    dialog.setText(QStringLiteral("确认将当前力传感器值置零？"));
    QAbstractButton* tareButton = dialog.addButton(
        QStringLiteral("去皮"), QMessageBox::AcceptRole);
    dialog.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    dialog.exec();
    if (dialog.clickedButton() != tareButton) {
        return;
    }

    if (!acsConnected_) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法执行力传感器去皮"),
            QStringLiteral("ACS 控制器连接已断开。"));
        return;
    }
    if (machineStateManager.state() == MachineState::Running) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法执行力传感器去皮"),
            QStringLiteral("机器当前处于运行状态，请先停止测试。"));
        return;
    }

    forceTarePending_ = true;
    MotionControlService::instance().tareForceSensor();
}

void DeviceStatusPanel::updateMachineStateDisplay(MachineState state,
                                                  const QString& reason)
{
    QString text;
    QString level;
    switch (state) {
    case MachineState::Error:
        text = QStringLiteral("故障");
        level = QStringLiteral("danger");
        break;
    case MachineState::Idle:
        text = QStringLiteral("空闲");
        level = QStringLiteral("ok");
        break;
    case MachineState::Running:
        text = QStringLiteral("运动");
        level = QStringLiteral("info");
        break;
    case MachineState::Resetting:
        text = QStringLiteral("复位中");
        level = QStringLiteral("info");
        break;
    }

    machineStatePill_->setText(text);
    machineStatePill_->setLevel(level);
    machineStatePill_->setToolTip(reason);
    machineStateReason_->setText(reason);
    machineStateReason_->setToolTip(reason);
}

void DeviceStatusPanel::updateMachineModeDisplay(MachineMode mode)
{
    const bool automatic = mode == MachineMode::Automatic;
    machineModeButton_->setText(
        automatic ? QStringLiteral("自动模式") : QStringLiteral("维修模式"));
    machineModeButton_->setProperty(
        "buttonStyle", automatic ? QStringLiteral("primary")
                                 : QStringLiteral("warning"));
    machineModeButton_->style()->unpolish(machineModeButton_);
    machineModeButton_->style()->polish(machineModeButton_);
}

QWidget* DeviceStatusPanel::createPressureValuesCard()
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("sensorValuesCard"));
    auto* layout = new QGridLayout(card);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);

    for (std::size_t index = 0; index < pressureValueLabels_.size(); ++index) {
        layout->addWidget(
            ViewHelpers::makeLabel(
                QStringLiteral("流量%1").arg(index + 1), "sensorName"),
            static_cast<int>(index),
            0);
        pressureValueLabels_.at(index) =
            ViewHelpers::makeLabel(QStringLiteral("无效"), "sensorValue");
        pressureValueLabels_.at(index)->setProperty("valid", false);
        pressureValueLabels_.at(index)->setAlignment(
            Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(
            pressureValueLabels_.at(index), static_cast<int>(index), 1);
    }

    return card;
}

QWidget* DeviceStatusPanel::createForceValueCard()
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("sensorValuesCard"));
    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);
    layout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("力传感器"), "sensorName"));
    layout->addStretch();
    forceValueLabel_ =
        ViewHelpers::makeLabel(QStringLiteral("无效"), "sensorValue");
    forceValueLabel_->setProperty("valid", false);
    forceValueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(forceValueLabel_);
    return card;
}

void DeviceStatusPanel::clearSensorReadings()
{
    for (QLabel* valueLabel : pressureValueLabels_) {
        if (valueLabel != nullptr) {
            setSensorValue(
                valueLabel, QStringLiteral("无效"), false);
        }
    }
    if (forceValueLabel_ != nullptr) {
        setSensorValue(
            forceValueLabel_, QStringLiteral("无效"), false);
    }
}

void DeviceStatusPanel::setSensorValue(QLabel* label,
                                       const QString& text,
                                       bool valid)
{
    label->setText(text);
    label->setProperty("valid", valid);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

QWidget* DeviceStatusPanel::createStateItem(const QString& name,
                                            const QString& state,
                                            const QString& detail,
                                            const QString& level,
                                            StatusPill** statePill,
                                            QLabel** detailLabel) const
{
    auto* item = new QFrame;
    item->setObjectName(QStringLiteral("stateItem"));
    auto* layout = new QGridLayout(item);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setHorizontalSpacing(5);
    layout->setVerticalSpacing(3);
    layout->addWidget(ViewHelpers::makeLabel(name, "stateName"), 0, 0);
    auto* pill = new StatusPill(state, level);
    layout->addWidget(pill, 0, 1, Qt::AlignRight);
    if (statePill != nullptr) {
        *statePill = pill;
    }
    if (!detail.isEmpty()) {
        QLabel* label = ViewHelpers::makeLabel(detail, "stateDetail");
        label->setWordWrap(true);
        label->setMaximumHeight(label->fontMetrics().lineSpacing() * 2 + 2);
        layout->addWidget(label, 1, 0, 1, 2);
        if (detailLabel != nullptr) {
            *detailLabel = label;
        }
    }
    return item;
}
