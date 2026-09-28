#include "DeviceStatusPanel.h"

#include "widgets/StatusPill.h"
#include "widgets/ViewHelpers.h"
#include "../motion/MotionControlService.h"
#include "../workflow/TestExecutionService.h"

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

namespace {

constexpr int kDevicePanelWidth = 310;

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
    titleRow->addWidget(new StatusPill(QStringLiteral("自检通过"), QStringLiteral("ok"), this));
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
        QStringLiteral("错误"), QStringLiteral("danger"), machineStateCard);
    machineStateRow->addWidget(machineStatePill_);
    machineStateLayout->addLayout(machineStateRow);

    machineStateReason_ = ViewHelpers::makeLabel(
        QStringLiteral("ACS 控制器未连接"), "stateDetail");
    machineStateReason_->setWordWrap(true);
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

    TestExecutionService& executionService = TestExecutionService::instance();
    connect(&executionService,
            &TestExecutionService::machineStateChanged,
            this,
            &DeviceStatusPanel::updateMachineStateDisplay);
    updateMachineStateDisplay(executionService.machineState(),
                              executionService.machineStateReason());

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 2, 0);
    contentLayout->setSpacing(10);

    auto addGroup = [this, contentLayout](const QString& name, const QList<QStringList>& items) {
        auto* group = new QFrame;
        group->setObjectName(QStringLiteral("deviceGroup"));
        auto* layout = new QVBoxLayout(group);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(7);
        layout->addWidget(ViewHelpers::makeLabel(name, "deviceGroupTitle"));
        for (const auto& item : items) {
            layout->addWidget(createStateItem(item.at(0), item.at(1), item.at(2), item.at(3)));
        }
        contentLayout->addWidget(group);
    };

    addGroup(QStringLiteral("安全链"),
             {{QStringLiteral("防护门 / 光幕"), QStringLiteral("正常"), QStringLiteral("参与联锁 · 10:38:20"), QStringLiteral("ok")}});

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
            &MotionControlService::forceTareWritten,
            this,
            &DeviceStatusPanel::handleForceTareWritten);
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

void DeviceStatusPanel::onInspectButtonClicked()
{
    emit messageRequested(QStringLiteral("设备自检完成：全部项目通过（模拟）"));
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

void DeviceStatusPanel::handleForceTareWritten()
{
    forceTarePending_ = false;
    QMessageBox::information(
        this,
        QStringLiteral("力传感器去皮"),
        QStringLiteral("去皮请求已写入 ACS 控制器。"));
}

void DeviceStatusPanel::handleForceTareFailed(const QString& message)
{
    forceTarePending_ = false;
    QMessageBox::warning(
        this, QStringLiteral("力传感器去皮失败"), message);
}

void DeviceStatusPanel::showAcsConnectionDialog()
{
    TestExecutionService& executionService =
        TestExecutionService::instance();
    if (acsConnected_
        && executionService.machineState() == MachineState::Running) {
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
        if (executionService.machineState() == MachineState::Running) {
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
    const TestExecutionService& executionService =
        TestExecutionService::instance();
    if (targetMode == MachineMode::Maintenance
        && executionService.machineState() != MachineState::Idle) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法进入维修模式"),
            QStringLiteral("任务当前不是空闲状态：%1")
                .arg(executionService.machineStateReason()));
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
        && executionService.machineState() != MachineState::Idle) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法进入维修模式"),
            QStringLiteral("任务状态已经变化：%1")
                .arg(executionService.machineStateReason()));
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

    TestExecutionService& executionService =
        TestExecutionService::instance();
    if (executionService.machineState() == MachineState::Running) {
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
        QStringLiteral("置零"), QMessageBox::AcceptRole);
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
    if (executionService.machineState() == MachineState::Running) {
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
        text = QStringLiteral("错误");
        level = QStringLiteral("danger");
        break;
    case MachineState::Idle:
        text = QStringLiteral("空闲");
        level = QStringLiteral("ok");
        break;
    case MachineState::Running:
        text = QStringLiteral("运行中");
        level = QStringLiteral("info");
        break;
    }

    machineStatePill_->setText(text);
    machineStatePill_->setLevel(level);
    machineStatePill_->setToolTip(reason);
    machineStateReason_->setText(reason);
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
                                            const QString& level) const
{
    auto* item = new QFrame;
    item->setObjectName(QStringLiteral("stateItem"));
    auto* layout = new QGridLayout(item);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setHorizontalSpacing(5);
    layout->setVerticalSpacing(3);
    layout->addWidget(ViewHelpers::makeLabel(name, "stateName"), 0, 0);
    layout->addWidget(new StatusPill(state, level), 0, 1, Qt::AlignRight);
    if (!detail.isEmpty()) {
        layout->addWidget(
            ViewHelpers::makeLabel(detail, "stateDetail"), 1, 0, 1, 2);
    }
    return item;
}
