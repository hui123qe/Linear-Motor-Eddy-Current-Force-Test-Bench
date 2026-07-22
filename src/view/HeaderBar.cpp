#include "HeaderBar.h"

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

    auto* emergencyStop = ViewHelpers::makeButton(QStringLiteral("■ 软件急停"), QStringLiteral("danger"));
    emergencyStop->setObjectName(QStringLiteral("emergencyStop"));
    emergencyStop->setMinimumSize(132, 46);
    connect(emergencyStop,
            &QPushButton::clicked,
            this,
            &HeaderBar::onEmergencyStopClicked);
    layout->addWidget(emergencyStop);
}

void HeaderBar::onEmergencyStopClicked()
{
    QMessageBox::information(
        this,
        QStringLiteral("软件急停"),
        QStringLiteral("软件急停尚未接入控制器，未发送设备指令。"));
}
