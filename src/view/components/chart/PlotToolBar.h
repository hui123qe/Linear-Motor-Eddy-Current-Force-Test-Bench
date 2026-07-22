#pragma once

#include <QToolBar>

class QAction;
class QExclusiveActionGroup;

class PlotToolBar final : public QToolBar
{
    Q_OBJECT

public:
    explicit PlotToolBar(QWidget* parent = nullptr);

    void uncheckAllActions();

    QAction* horizontalZoomAction = nullptr;
    QAction* verticalZoomAction = nullptr;
    QAction* zoomAction = nullptr;
    QAction* rectangleZoomAction = nullptr;
    QAction* dragAction = nullptr;
    QAction* fullScreenAction = nullptr;
    QAction* restoreAction = nullptr;

private slots:
    void onFullScreenTriggered();
    void onRestoreTriggered();

private:
    void initializeActions();
    void addVisualSeparator();

    QExclusiveActionGroup* zoomGroup_ = nullptr;
    QExclusiveActionGroup* pressGroup_ = nullptr;
};
