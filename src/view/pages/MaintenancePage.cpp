#include "MaintenancePage.h"

#include "../widgets/MetricCard.h"
#include "../widgets/StatusPill.h"
#include "../widgets/ViewHelpers.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr double kPtpMaxSpeed = 50.0;
constexpr double kJogSpeed = 30.0;

QDoubleSpinBox* makePositionInput(double value)
{
    auto* input = new QDoubleSpinBox;
    input->setRange(-100000.0, 100000.0);
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
    layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("维护模式约束"), "constraintTitle"));
    layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("- PTP 相对/绝对运动最大速度固定为 50 mm/s"), "constraintText"));
    layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("- JOG 运动速度固定为 30 mm/s"), "constraintText"));
    layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("- 本页面仅提供维护操作入口，真实下发必须经过控制模块和联锁校验"), "constraintText"));
    return box;
}

} // namespace

MaintenancePage::MaintenancePage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto* heading = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("维护界面"), "pageTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("龙门轴维护操作入口，所有动作仅做界面模拟提示"),
        "pageDescription"));
    heading->addLayout(titleBox);
    heading->addStretch();
    heading->addWidget(new StatusPill(QStringLiteral("维护模式"), QStringLiteral("warning"), this));
    layout->addLayout(heading);

    auto* statusBar = new QFrame;
    statusBar->setObjectName(QStringLiteral("taskBar"));
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(14, 10, 14, 10);
    statusLayout->setSpacing(12);
    statusLayout->addWidget(new MetricCard(QStringLiteral("连接状态"), QStringLiteral("未连接")));
    statusLayout->addWidget(new MetricCard(QStringLiteral("使能状态"), QStringLiteral("下使能")));
    statusLayout->addWidget(new MetricCard(QStringLiteral("当前位置"), QStringLiteral("-- mm")));
    statusLayout->addWidget(new MetricCard(QStringLiteral("速度限制"), QStringLiteral("PTP 50 / JOG 30")));
    layout->addWidget(statusBar);

    auto* content = new QHBoxLayout;
    content->setSpacing(12);

    auto* connectionPanel = ViewHelpers::makePanel(QStringLiteral("连接与使能"), QStringLiteral("仅发出维护意图，不直接操作硬件"));
    auto* connectionLayout = qobject_cast<QVBoxLayout*>(connectionPanel->layout());
    auto* connectButton = ViewHelpers::makeButton(QStringLiteral("连接"), QStringLiteral("primary"));
    auto* disconnectButton = ViewHelpers::makeButton(QStringLiteral("断开连接"));
    auto* enableButton = ViewHelpers::makeButton(QStringLiteral("上使能"), QStringLiteral("primary"));
    auto* disableButton = ViewHelpers::makeButton(QStringLiteral("下使能"), QStringLiteral("warning"));
    connectionLayout->addWidget(connectButton);
    connectionLayout->addWidget(disconnectButton);
    connectionLayout->addWidget(ViewHelpers::makeDivider());
    connectionLayout->addWidget(enableButton);
    connectionLayout->addWidget(disableButton);
    connectionLayout->addStretch();
    content->addWidget(connectionPanel, 1);

    auto* ptpPanel = ViewHelpers::makePanel(QStringLiteral("PTP 运动"), QStringLiteral("相对/绝对运动最大速度 50 mm/s"));
    auto* ptpLayout = qobject_cast<QVBoxLayout*>(ptpPanel->layout());
    auto* relativeForm = new QFormLayout;
    relativeForm->setHorizontalSpacing(20);
    relativeForm->setVerticalSpacing(12);
    auto* relativeDistance = makePositionInput(10.0);
    relativeForm->addRow(QStringLiteral("相对位移"), relativeDistance);
    ptpLayout->addLayout(relativeForm);
    auto* relativeMove = ViewHelpers::makeButton(QStringLiteral("执行相对运动"), QStringLiteral("primary"));
    ptpLayout->addWidget(relativeMove);
    ptpLayout->addWidget(ViewHelpers::makeDivider());

    auto* absoluteForm = new QFormLayout;
    absoluteForm->setHorizontalSpacing(20);
    absoluteForm->setVerticalSpacing(12);
    auto* absolutePosition = makePositionInput(0.0);
    absoluteForm->addRow(QStringLiteral("目标位置"), absolutePosition);
    ptpLayout->addLayout(absoluteForm);
    auto* absoluteMove = ViewHelpers::makeButton(QStringLiteral("执行绝对运动"), QStringLiteral("primary"));
    ptpLayout->addWidget(absoluteMove);
    ptpLayout->addStretch();
    content->addWidget(ptpPanel, 2);

    auto* jogPanel = ViewHelpers::makePanel(QStringLiteral("JOG 运动"), QStringLiteral("JOG 速度固定为 30 mm/s"));
    auto* jogLayout = qobject_cast<QVBoxLayout*>(jogPanel->layout());
    auto* jogSpeedCard = new MetricCard(QStringLiteral("JOG 速度"), QStringLiteral("30 mm/s"));
    jogLayout->addWidget(jogSpeedCard);
    auto* jogButtons = new QHBoxLayout;
    auto* jogNegative = ViewHelpers::makeButton(QStringLiteral("JOG-"), QStringLiteral("warning"));
    auto* jogPositive = ViewHelpers::makeButton(QStringLiteral("JOG+"), QStringLiteral("primary"));
    jogButtons->addWidget(jogNegative);
    jogButtons->addWidget(jogPositive);
    jogLayout->addLayout(jogButtons);
    auto* jogHint = ViewHelpers::makeLabel(QStringLiteral("按住持续运动，松开应停止；真实逻辑需由控制层实现。"), "constraintText");
    jogHint->setWordWrap(true);
    jogLayout->addWidget(jogHint);
    jogLayout->addStretch();
    content->addWidget(jogPanel, 1);

    layout->addLayout(content, 1);
    layout->addWidget(makeLimitBox());

    connect(connectButton,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onConnectButtonClicked);
    connect(disconnectButton,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onDisconnectButtonClicked);
    connect(enableButton,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onEnableButtonClicked);
    connect(disableButton,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onDisableButtonClicked);
    connect(relativeMove,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onRelativeMoveClicked);
    connect(absoluteMove,
            &QPushButton::clicked,
            this,
            &MaintenancePage::onAbsoluteMoveClicked);
    connect(jogNegative,
            &QPushButton::pressed,
            this,
            &MaintenancePage::onJogNegativePressed);
    connect(jogPositive,
            &QPushButton::pressed,
            this,
            &MaintenancePage::onJogPositivePressed);
}

void MaintenancePage::onConnectButtonClicked()
{
    emitActionMessage(QStringLiteral("连接龙门轴"));
}

void MaintenancePage::onDisconnectButtonClicked()
{
    emitActionMessage(QStringLiteral("断开龙门轴连接"));
}

void MaintenancePage::onEnableButtonClicked()
{
    emitActionMessage(QStringLiteral("龙门轴上使能"));
}

void MaintenancePage::onDisableButtonClicked()
{
    emitActionMessage(QStringLiteral("龙门轴下使能"));
}

void MaintenancePage::onRelativeMoveClicked()
{
    emitActionMessage(QStringLiteral("执行相对运动"));
}

void MaintenancePage::onAbsoluteMoveClicked()
{
    emitActionMessage(QStringLiteral("执行绝对运动"));
}

void MaintenancePage::onJogNegativePressed()
{
    emitActionMessage(QStringLiteral("JOG-"));
}

void MaintenancePage::onJogPositivePressed()
{
    emitActionMessage(QStringLiteral("JOG+"));
}

void MaintenancePage::emitActionMessage(const QString& action)
{
    emit messageRequested(
        action + QStringLiteral("（维护界面模拟，未下发设备指令）"));
}
