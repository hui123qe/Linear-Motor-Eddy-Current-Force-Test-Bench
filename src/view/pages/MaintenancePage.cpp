#include "MaintenancePage.h"

#include "../../motion/MotionControlService.h"
#include "../widgets/MetricCard.h"
#include "../widgets/StatusPill.h"
#include "../widgets/ViewHelpers.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QDoubleSpinBox* makePositionInput(double value)
{
    auto* input = new QDoubleSpinBox;
    input->setRange(-1.0e12, 1.0e12);
    input->setDecimals(3);
    input->setSingleStep(1.0);
    input->setValue(value);
    input->setSuffix(QStringLiteral(" mm"));
    return input;
}

QFrame* makeLimitBox()
{
    auto* box = new QFrame;
    box->setObjectName(QStringLiteral("constraintBox"));
    box->setProperty("level", QStringLiteral("warning"));

    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(7);
    layout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("维修模式约束"), "constraintTitle"));
    layout->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("- PTP 速度固定为 50 mm/s，JOG 速度固定为 30 mm/s"),
        "constraintText"));
    layout->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("- 当前未配置软件位置限位，运动前必须确认机械行程安全"),
        "constraintText"));
    layout->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("- JOG 按下启动、松开停止；页面隐藏或应用失焦也会发送 HALT"),
        "constraintText"));
    return box;
}

QString maintenanceCommandText(MaintenanceCommand command)
{
    switch (command) {
    case MaintenanceCommand::EnableAxis:
        return QStringLiteral("上使能");
    case MaintenanceCommand::DisableAxis:
        return QStringLiteral("下使能");
    case MaintenanceCommand::HomeAxis:
        return QStringLiteral("回零");
    case MaintenanceCommand::RelativeMove:
        return QStringLiteral("相对运动");
    case MaintenanceCommand::AbsoluteMove:
        return QStringLiteral("绝对运动");
    case MaintenanceCommand::StartJog:
        return QStringLiteral("JOG");
    case MaintenanceCommand::Halt:
        return QStringLiteral("停止运动");
    }

    return QStringLiteral("维修命令");
}

} // namespace

MaintenancePage::MaintenancePage(QWidget* parent)
    : QWidget(parent)
    , machineMode_(MotionControlService::instance().machineMode())
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto* heading = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("维修界面"), "pageTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("维修模式下直接控制 ACS 轴"), "pageDescription"));
    heading->addLayout(titleBox);
    heading->addStretch();
    heading->addWidget(new StatusPill(
        QStringLiteral("实际控制"), QStringLiteral("warning"), this));
    layout->addLayout(heading);

    auto* statusBar = new QFrame;
    statusBar->setObjectName(QStringLiteral("taskBar"));
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(14, 10, 14, 10);
    statusLayout->setSpacing(12);
    connectionCard_ = new MetricCard(
        QStringLiteral("连接状态"), QStringLiteral("未连接"));
    enableCard_ = new MetricCard(
        QStringLiteral("使能状态"), QStringLiteral("无效"));
    positionCard_ = new MetricCard(
        QStringLiteral("当前位置"), QStringLiteral("无效"));
    modeCard_ = new MetricCard(
        QStringLiteral("机器模式"),
        machineMode_ == MachineMode::Automatic
            ? QStringLiteral("自动")
            : QStringLiteral("维修"));
    statusLayout->addWidget(connectionCard_);
    statusLayout->addWidget(enableCard_);
    statusLayout->addWidget(positionCard_);
    statusLayout->addWidget(modeCard_);
    layout->addWidget(statusBar);

    auto* content = new QHBoxLayout;
    content->setSpacing(12);

    auto* connectionPanel = ViewHelpers::makePanel(
        QStringLiteral("连接与使能"),
        QStringLiteral("连接、轴使能与维修运动停止"));
    auto* connectionLayout =
        qobject_cast<QVBoxLayout*>(connectionPanel->layout());
    connectButton_ = ViewHelpers::makeButton(
        QStringLiteral("连接"), QStringLiteral("primary"));
    disconnectButton_ = ViewHelpers::makeButton(QStringLiteral("断开连接"));
    enableButton_ = ViewHelpers::makeButton(
        QStringLiteral("上使能"), QStringLiteral("primary"));
    disableButton_ = ViewHelpers::makeButton(
        QStringLiteral("下使能"), QStringLiteral("warning"));
    haltButton_ = ViewHelpers::makeButton(
        QStringLiteral("停止运动"), QStringLiteral("warning"));
    connectionLayout->addWidget(connectButton_);
    connectionLayout->addWidget(disconnectButton_);
    connectionLayout->addWidget(ViewHelpers::makeDivider());
    connectionLayout->addWidget(enableButton_);
    connectionLayout->addWidget(disableButton_);
    connectionLayout->addWidget(haltButton_);
    connectionLayout->addStretch();
    content->addWidget(connectionPanel, 1);

    auto* ptpPanel = ViewHelpers::makePanel(
        QStringLiteral("PTP 运动"),
        QStringLiteral("速度固定为 50 mm/s，不配置软件位置限位"));
    auto* ptpLayout = qobject_cast<QVBoxLayout*>(ptpPanel->layout());
    homeButton_ = ViewHelpers::makeButton(
        QStringLiteral("回零"), QStringLiteral("primary"));
    ptpLayout->addWidget(homeButton_);
    ptpLayout->addWidget(ViewHelpers::makeDivider());

    auto* relativeForm = new QFormLayout;
    relativeForm->setHorizontalSpacing(20);
    relativeForm->setVerticalSpacing(12);
    relativeDistanceInput_ = makePositionInput(10.0);
    relativeForm->addRow(QStringLiteral("相对位移"), relativeDistanceInput_);
    ptpLayout->addLayout(relativeForm);
    relativeMoveButton_ = ViewHelpers::makeButton(
        QStringLiteral("执行相对运动"), QStringLiteral("primary"));
    ptpLayout->addWidget(relativeMoveButton_);
    ptpLayout->addWidget(ViewHelpers::makeDivider());

    auto* absoluteForm = new QFormLayout;
    absoluteForm->setHorizontalSpacing(20);
    absoluteForm->setVerticalSpacing(12);
    absolutePositionInput_ = makePositionInput(0.0);
    absoluteForm->addRow(QStringLiteral("目标位置"), absolutePositionInput_);
    ptpLayout->addLayout(absoluteForm);
    absoluteMoveButton_ = ViewHelpers::makeButton(
        QStringLiteral("执行绝对运动"), QStringLiteral("primary"));
    ptpLayout->addWidget(absoluteMoveButton_);
    ptpLayout->addStretch();
    content->addWidget(ptpPanel, 2);

    auto* jogPanel = ViewHelpers::makePanel(
        QStringLiteral("JOG 运动"),
        QStringLiteral("按住运动，松开停止"));
    auto* jogLayout = qobject_cast<QVBoxLayout*>(jogPanel->layout());
    jogLayout->addWidget(new MetricCard(
        QStringLiteral("JOG 速度"), QStringLiteral("30 mm/s")));
    auto* jogButtons = new QHBoxLayout;
    jogNegativeButton_ = ViewHelpers::makeButton(
        QStringLiteral("JOG-"), QStringLiteral("warning"));
    jogPositiveButton_ = ViewHelpers::makeButton(
        QStringLiteral("JOG+"), QStringLiteral("primary"));
    jogButtons->addWidget(jogNegativeButton_);
    jogButtons->addWidget(jogPositiveButton_);
    jogLayout->addLayout(jogButtons);
    auto* jogHint = ViewHelpers::makeLabel(
        QStringLiteral("按下后持续运动，任一 JOG 按钮松开即发送 HALT。"),
        "constraintText");
    jogHint->setWordWrap(true);
    jogLayout->addWidget(jogHint);
    jogLayout->addStretch();
    content->addWidget(jogPanel, 1);

    layout->addLayout(content, 1);
    layout->addWidget(makeLimitBox());

    connect(connectButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onConnectButtonClicked);
    connect(disconnectButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onDisconnectButtonClicked);
    connect(enableButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onEnableButtonClicked);
    connect(disableButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onDisableButtonClicked);
    connect(haltButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onHaltButtonClicked);
    connect(homeButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onHomeButtonClicked);
    connect(relativeMoveButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onRelativeMoveClicked);
    connect(absoluteMoveButton_,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onAbsoluteMoveClicked);
    connect(jogNegativeButton_,
            &QPushButton::pressed,
            this,
            &MaintenancePage::onJogNegativePressed);
    connect(jogPositiveButton_,
            &QPushButton::pressed,
            this,
            &MaintenancePage::onJogPositivePressed);
    connect(jogNegativeButton_,
            &QPushButton::released,
            this,
            &MaintenancePage::onJogReleased);
    connect(jogPositiveButton_,
            &QPushButton::released,
            this,
            &MaintenancePage::onJogReleased);

    MotionControlService& motionService = MotionControlService::instance();
    connect(&motionService,
            &MotionControlService::connectionChanged,
            this,
            &MaintenancePage::setControllerConnected);
    connect(&motionService,
            &MotionControlService::machineModeChanged,
            this,
            &MaintenancePage::setMachineMode);
    connect(&motionService,
            &MotionControlService::axisEnabledChanged,
            this,
            &MaintenancePage::setAxisEnabled);
    connect(&motionService,
            &MotionControlService::motionStateChanged,
            this,
            &MaintenancePage::setMotionState);
    connect(&motionService,
            &MotionControlService::homeRunningChanged,
            this,
            &MaintenancePage::setHomeRunning);
    connect(&motionService,
            &MotionControlService::positionFeedbackChanged,
            this,
            &MaintenancePage::setPosition);
    connect(&motionService,
            &MotionControlService::maintenanceCommandCompleted,
            this,
            &MaintenancePage::handleMaintenanceCommandCompleted);
    connect(&motionService,
            &MotionControlService::maintenanceCommandFailed,
            this,
            &MaintenancePage::handleMaintenanceCommandFailed);
    connect(qApp,
            &QGuiApplication::applicationStateChanged,
            this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) {
                    stopJog(false);
                }
            });

    updateHomeButtonState();
}

void MaintenancePage::hideEvent(QHideEvent* event)
{
    if (machineMode_ == MachineMode::Maintenance
        && jogCommandActive_) {
        QString ignoredError;
        static_cast<void>(MotionControlService::instance().haltMaintenanceMotion(&ignoredError));
        jogCommandActive_ = false;
    }
    QWidget::hideEvent(event);
}

void MaintenancePage::onConnectButtonClicked()
{
    if (controllerConnected_) {
        QMessageBox::information(
            this,
            QStringLiteral("连接 ACS 控制器"),
            QStringLiteral("ACS 控制器当前已经连接。"));
        return;
    }
    MotionControlService::instance().connectController();
}

void MaintenancePage::onDisconnectButtonClicked()
{
    if (!controllerConnected_) {
        QMessageBox::information(
            this,
            QStringLiteral("断开 ACS 控制器"),
            QStringLiteral("ACS 控制器当前未连接。"));
        return;
    }
    QString errorMessage;
    if (!MotionControlService::instance().disconnectController(&errorMessage)) {
        showCommandFailure(QStringLiteral("断开连接失败"), errorMessage);
    }
}

void MaintenancePage::onEnableButtonClicked()
{
    QString errorMessage;
    if (!MotionControlService::instance().enableAxis(&errorMessage)) {
        showCommandFailure(QStringLiteral("上使能失败"), errorMessage);
    }
}

void MaintenancePage::onDisableButtonClicked()
{
    QString errorMessage;
    if (!MotionControlService::instance().disableAxis(&errorMessage)) {
        showCommandFailure(QStringLiteral("下使能失败"), errorMessage);
    }
}

void MaintenancePage::onHaltButtonClicked()
{
    QString errorMessage;
    if (!MotionControlService::instance().haltMaintenanceMotion(&errorMessage)) {
        showCommandFailure(QStringLiteral("停止维修运动失败"), errorMessage);
        return;
    }
    jogCommandActive_ = false;
}

void MaintenancePage::onHomeButtonClicked()
{
    if (machineMode_ != MachineMode::Maintenance) {
        showCommandFailure(
            QStringLiteral("回零失败"),
            QStringLiteral("机器不在维修模式。"));
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().homeAxis(&errorMessage)) {
        showCommandFailure(QStringLiteral("回零失败"), errorMessage);
        return;
    }
    homeCommandRequested_ = true;
    updateHomeButtonState();
}

void MaintenancePage::onRelativeMoveClicked()
{
    QString errorMessage;
    if (!MotionControlService::instance().moveRelative(
            relativeDistanceInput_->value(), &errorMessage)) {
        showCommandFailure(QStringLiteral("相对运动失败"), errorMessage);
    }
}

void MaintenancePage::onAbsoluteMoveClicked()
{
    QString errorMessage;
    if (!MotionControlService::instance().moveAbsolute(
            absolutePositionInput_->value(), &errorMessage)) {
        showCommandFailure(QStringLiteral("绝对运动失败"), errorMessage);
    }
}

void MaintenancePage::onJogNegativePressed()
{
    if (jogCommandActive_) {
        QMessageBox::information(
            this,
            QStringLiteral("JOG- 启动"),
            QStringLiteral("已有 JOG 命令正在执行。"));
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().startJog(-1, &errorMessage)) {
        showCommandFailure(QStringLiteral("JOG- 启动失败"), errorMessage);
        return;
    }
    jogCommandActive_ = true;
}

void MaintenancePage::onJogPositivePressed()
{
    if (jogCommandActive_) {
        QMessageBox::information(
            this,
            QStringLiteral("JOG+ 启动"),
            QStringLiteral("已有 JOG 命令正在执行。"));
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().startJog(1, &errorMessage)) {
        showCommandFailure(QStringLiteral("JOG+ 启动失败"), errorMessage);
        return;
    }
    jogCommandActive_ = true;
}

void MaintenancePage::onJogReleased()
{
    stopJog(true);
}

void MaintenancePage::setControllerConnected(bool connected, const QString&)
{
    controllerConnected_ = connected;
    connectionCard_->setValue(
        connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    if (!connected) {
        axisEnabled_ = false;
        jogCommandActive_ = false;
        homeCommandRequested_ = false;
        homeRunning_ = false;
        motionState_ = 0;
        enableCard_->setValue(QStringLiteral("无效"));
        positionCard_->setValue(QStringLiteral("无效"));
    }
    updateHomeButtonState();
}

void MaintenancePage::setMachineMode(MachineMode mode)
{
    machineMode_ = mode;
    modeCard_->setValue(
        mode == MachineMode::Automatic
            ? QStringLiteral("自动")
            : QStringLiteral("维修"));
    if (mode != MachineMode::Maintenance) {
        stopJog(false);
    }
    updateHomeButtonState();
}

void MaintenancePage::setAxisEnabled(bool enabled)
{
    axisEnabled_ = enabled;
    if (controllerConnected_) {
        enableCard_->setValue(
            axisEnabled_ ? QStringLiteral("上使能") : QStringLiteral("下使能"));
    }
}

void MaintenancePage::setMotionState(int state, int errorCode)
{
    Q_UNUSED(errorCode)
    motionState_ = state;
    updateHomeButtonState();
}

void MaintenancePage::setHomeRunning(bool running)
{
    homeRunning_ = running;
    updateHomeButtonState();
}

void MaintenancePage::setPosition(double positionMillimeters)
{
    if (controllerConnected_) {
        positionCard_->setValue(
            QStringLiteral("%1 mm")
                .arg(positionMillimeters, 0, 'f', 3));
    }
}

void MaintenancePage::handleMaintenanceCommandCompleted(
    MaintenanceCommand command)
{
    if (command == MaintenanceCommand::HomeAxis) {
        if (!homeCommandRequested_) {
            return;
        }
        homeCommandRequested_ = false;
        updateHomeButtonState();
    }
    if (command == MaintenanceCommand::Halt) {
        jogCommandActive_ = false;
    }
}

void MaintenancePage::handleMaintenanceCommandFailed(
    MaintenanceCommand command,
    const QString& message)
{
    if (command == MaintenanceCommand::HomeAxis) {
        if (!homeCommandRequested_) {
            return;
        }
        homeCommandRequested_ = false;
        updateHomeButtonState();
    }
    if (command == MaintenanceCommand::StartJog
        || command == MaintenanceCommand::Halt) {
        jogCommandActive_ = false;
    }
    showCommandFailure(
        QStringLiteral("%1失败").arg(maintenanceCommandText(command)),
        message);
}

void MaintenancePage::stopJog(bool showFailure)
{
    if (!jogCommandActive_) {
        return;
    }
    if (!controllerConnected_) {
        jogCommandActive_ = false;
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().haltMaintenanceMotion(&errorMessage)
        && showFailure) {
        showCommandFailure(QStringLiteral("停止维修运动失败"), errorMessage);
    }
    jogCommandActive_ = false;
}

void MaintenancePage::showCommandFailure(const QString& title,
                                         const QString& message)
{
    QMessageBox::warning(this, title, message);
}

void MaintenancePage::updateHomeButtonState()
{
    homeButton_->setEnabled(
        controllerConnected_
        && machineMode_ == MachineMode::Maintenance
        && motionState_ == 0
        && !homeRunning_
        && !homeCommandRequested_);
}
