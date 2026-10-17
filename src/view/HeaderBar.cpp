#include "HeaderBar.h"

#include "../motion/MotionControlService.h"
#include "widgets/ViewHelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

HeaderBar::HeaderBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("header"));
    setFixedHeight(78);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 12, 20, 12);

    auto* titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(2);
    titleLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("高速涡流测试台"), "projectTitle"));
    titleLayout->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("双气浮导轨 · 高速运动控制 · 涡流特性采集"),
        "projectSubtitle"));
    layout->addLayout(titleLayout);
    layout->addStretch();

    emergencyStopButton_ = ViewHelpers::makeButton(
        QStringLiteral("■ 软件急停"), QStringLiteral("danger"));
    emergencyStopButton_->setObjectName(QStringLiteral("emergencyStop"));
    emergencyStopButton_->setMinimumSize(132, 46);
    emergencyStopButton_->setToolTip(QStringLiteral("单击执行软件急停"));
    connect(emergencyStopButton_, &QPushButton::clicked,
            this, &HeaderBar::onEmergencyStopClicked);
    layout->addWidget(emergencyStopButton_);

    MotionControlService& motionService = MotionControlService::instance();
    connect(&motionService,
            &MotionControlService::emergencyStopCompleted,
            this,
            &HeaderBar::handleEmergencyStopCompleted,
            Qt::QueuedConnection);
    connect(&motionService,
            &MotionControlService::emergencyStopFailed,
            this,
            &HeaderBar::handleEmergencyStopFailed,
            Qt::QueuedConnection);
}

void HeaderBar::onEmergencyStopClicked()
{
    QString errorMessage;
    if (MotionControlService::instance().emergencyStop(&errorMessage)) {
        return;
    }

    QMessageBox::critical(
        this,
        QStringLiteral("软件急停失败"),
        errorMessage);
}

void HeaderBar::handleEmergencyStopCompleted()
{
    QMessageBox::warning(
        this,
        QStringLiteral("软件急停"),
        QStringLiteral("指定 Buffer 已停止，Kill All 已执行，全轴运动已终结。"));
}

void HeaderBar::handleEmergencyStopFailed(const QString& message)
{
    QMessageBox::critical(
        this,
        QStringLiteral("软件急停失败"),
        message);
}
