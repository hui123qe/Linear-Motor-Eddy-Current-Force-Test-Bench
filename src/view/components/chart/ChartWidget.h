#pragma once

#include "ChartTypes.h"

#include <QHash>
#include <QPointF>
#include <QPointer>
#include <QVector>
#include <QWidget>

class CurveSelector;
class PlotToolBar;
class QCustomPlot;
class QCPGraph;
class QCPItemLine;
class QCPItemText;
class QDialog;
class QEvent;
class QMouseEvent;
class QTimer;
class QVBoxLayout;

class ChartWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ChartWidget(QWidget* parent = nullptr);
    ~ChartWidget() override;

    bool addCurve(const ChartCurveConfig& config);
    bool removeCurve(const QString& curveId);
    [[nodiscard]] bool containsCurve(const QString& curveId) const;

    void start(bool startScrolling);
    void setCurveName(const QString& curveId, const QString& displayName);
    void setCurveVisible(const QString& curveId, bool visible);
    void setXAxisMode(ChartXAxisMode mode);
    void setAxisLabels(const QString& xAxisLabel, const QString& yAxisLabel);
    void setLegendVisible(bool visible);
    void setGridVisible(bool visible);

public slots:
    void appendPoint(const QString& curveId, double x, double y);
    void appendPoints(const QString& curveId, const QVector<QPointF>& points);
    void clearCurve(const QString& curveId);
    void clearAll();

private slots:
    void restoreFromFullScreen();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class ScrollPolicy
    {
        Overview,
        AutoScroll
    };

    struct CurveEntry
    {
        QCPGraph* graph = nullptr;
        int maximumPointCount = 100000;
    };

    [[nodiscard]] bool isUiThread() const;
    [[nodiscard]] bool isEmpty() const;
    void initializePlot();
    void initializeToolBarConnections();
    void setupInteraction(int change);
    void setHorizontalZooming(bool enabled);
    void setVerticalZooming(bool enabled);
    void setZooming(bool enabled);
    void setSelectionZooming(bool enabled);
    void setRangeDrag(bool enabled);
    void setScrollPolicy(ScrollPolicy policy);
    void scrollToLatest();
    void updateOverviewRange();
    void updateAutoScrollRange();
    void trimCurve(CurveEntry& curve);
    void updateCrosshair(QMouseEvent* event);
    void hideCrosshair();
    void replot();
    void enterFullScreen();

    QVBoxLayout* rootLayout_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    PlotToolBar* toolBar_ = nullptr;
    CurveSelector* curveSelector_ = nullptr;
    QCustomPlot* plot_ = nullptr;
    QTimer* scrollTimer_ = nullptr;
    QHash<QString, CurveEntry> curves_;
    QCPItemLine* horizontalCrosshair_ = nullptr;
    QCPItemLine* verticalCrosshair_ = nullptr;
    QCPItemText* crosshairText_ = nullptr;
    QPointer<QDialog> fullScreenDialog_;
    ChartXAxisMode xAxisMode_ = ChartXAxisMode::Numeric;
    ScrollPolicy scrollPolicy_ = ScrollPolicy::Overview;
    int userInteracting_ = 0;
};
