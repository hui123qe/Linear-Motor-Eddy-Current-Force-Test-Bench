#include "BottomStatusBar.h"

#include "widgets/ViewHelpers.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

BottomStatusBar::BottomStatusBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bottomBar"));
    setFixedHeight(38);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(16);

    layout->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("版本  %1").arg(QCoreApplication::applicationVersion()),
        "statusText"));
    layout->addStretch();
    clockLabel_ = ViewHelpers::makeLabel({}, "statusText");
    layout->addWidget(clockLabel_);

    auto* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &BottomStatusBar::updateClock);
    clockTimer->start(1000);
    updateClock();
}

void BottomStatusBar::updateClock()
{
    clockLabel_->setText(
        QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
}
