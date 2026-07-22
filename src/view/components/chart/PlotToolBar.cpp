#include "PlotToolBar.h"

#include "QExclusiveActionGroup.h"

#include <QAction>
#include <QFrame>
#include <QIcon>

PlotToolBar::PlotToolBar(QWidget* parent)
    : QToolBar(QStringLiteral("Plot Tools"), parent)
{
    setObjectName(QStringLiteral("PlotToolBar"));
    setIconSize(QSize(24, 24));
    setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    initializeActions();
    addVisualSeparator();

    connect(fullScreenAction,
            &QAction::triggered,
            this,
            &PlotToolBar::onFullScreenTriggered);
    connect(restoreAction,
            &QAction::triggered,
            this,
            &PlotToolBar::onRestoreTriggered);
}

void PlotToolBar::uncheckAllActions()
{
    for (QAction* action : zoomGroup_->actions()) {
        action->setChecked(false);
    }
    for (QAction* action : pressGroup_->actions()) {
        action->setChecked(false);
    }
    dragAction->setChecked(false);
}

void PlotToolBar::onFullScreenTriggered()
{
    fullScreenAction->setVisible(false);
    restoreAction->setVisible(true);
}

void PlotToolBar::onRestoreTriggered()
{
    fullScreenAction->setVisible(true);
    restoreAction->setVisible(false);
}

void PlotToolBar::initializeActions()
{
    zoomGroup_ = new QExclusiveActionGroup(this);
    pressGroup_ = new QExclusiveActionGroup(this);

    horizontalZoomAction = new QAction(
        QIcon(QStringLiteral(":/icons/hzoom.png")), QStringLiteral("横向缩放"), this);
    horizontalZoomAction->setChecked(true);
    zoomGroup_->addAction(horizontalZoomAction);
    addAction(horizontalZoomAction);

    verticalZoomAction = new QAction(
        QIcon(QStringLiteral(":/icons/vzoom.png")), QStringLiteral("纵向缩放"), this);
    verticalZoomAction->setCheckable(true);
    zoomGroup_->addAction(verticalZoomAction);
    addAction(verticalZoomAction);

    zoomAction = new QAction(
        QIcon(QStringLiteral(":/icons/zoom.png")), QStringLiteral("缩放"), this);
    zoomAction->setCheckable(true);
    zoomGroup_->addAction(zoomAction);
    addAction(zoomAction);

    addSeparator();

    rectangleZoomAction = new QAction(
        QIcon(QStringLiteral(":/icons/rectzoom.png")), QStringLiteral("框选缩放"), this);
    rectangleZoomAction->setCheckable(true);
    pressGroup_->addAction(rectangleZoomAction);
    addAction(rectangleZoomAction);

    addSeparator();

    dragAction = new QAction(
        QIcon(QStringLiteral(":/icons/drag.png")), QStringLiteral("开启拖动"), this);
    dragAction->setCheckable(true);
    pressGroup_->addAction(dragAction);
    addAction(dragAction);

    addSeparator();

    fullScreenAction = new QAction(
        QIcon(QStringLiteral(":/icons/fullscreen.png")), QStringLiteral("全屏显示"), this);
    addAction(fullScreenAction);

    restoreAction = new QAction(
        QIcon(QStringLiteral(":/icons/Restore.png")), QStringLiteral("Restore"), this);
    addAction(restoreAction);
    restoreAction->setVisible(false);
}

void PlotToolBar::addVisualSeparator()
{
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    addWidget(separator);
}
