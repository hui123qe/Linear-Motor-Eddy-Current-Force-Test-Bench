#include "ChartWidget.h"

#include "CurveSelector.h"
#include "PlotToolBar.h"
#include "axis/axistickerdatetime.h"
#include "core.h"
#include "items/item-line.h"
#include "items/item-text.h"
#include "layoutelements/layoutelement-axisrect.h"
#include "layoutelements/layoutelement-legend.h"
#include "plottables/plottable-graph.h"
#include "scatterstyle.h"

#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int scrollIntervalMilliseconds = 200;
constexpr int autoScrollThreshold = 10000;
constexpr int autoScrollVisiblePointCount = 50;

} // namespace

ChartWidget::ChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("chartWidget"));

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(0, 0, 0, 0);
    rootLayout_->setSpacing(0);

    contentWidget_ = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(contentWidget_);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(4);

    toolBar_ = new PlotToolBar(contentWidget_);
    curveSelector_ = new CurveSelector(contentWidget_);
    plot_ = new QCustomPlot(contentWidget_);
    contentLayout->addWidget(toolBar_);
    contentLayout->addWidget(curveSelector_);
    contentLayout->addWidget(plot_, 1);
    curveSelector_->hide();
    rootLayout_->addWidget(contentWidget_);

    scrollTimer_ = new QTimer(this);
    scrollTimer_->setInterval(scrollIntervalMilliseconds);
    connect(scrollTimer_, &QTimer::timeout, this, &ChartWidget::scrollToLatest);

    initializePlot();
    initializeToolBarConnections();
}

ChartWidget::~ChartWidget() = default;

bool ChartWidget::addCurve(const ChartCurveConfig& config)
{
    if (config.id.isEmpty() || config.displayName.isEmpty() || curves_.contains(config.id)) {
        return false;
    }

    QCPGraph* graph = plot_->addGraph();
    graph->setName(config.displayName);
    graph->setPen(QPen(config.color, std::max(1, config.lineWidth)));
    graph->setLineStyle(QCPGraph::lsLine);
    graph->setScatterStyle(QCPScatterStyle::ssNone);
    graph->setSelectable(QCP::stNone);
    graph->setVisible(config.visible);

    CurveEntry entry;
    entry.graph = graph;
    entry.maximumPointCount = std::max(1, config.maximumPointCount);
    curves_.insert(config.id, entry);
    curveSelector_->addCurve(config.id, config.displayName, config.color, config.visible);
    replot();
    return true;
}

bool ChartWidget::removeCurve(const QString& curveId)
{
    const auto iterator = curves_.find(curveId);
    if (iterator == curves_.end()) {
        return false;
    }

    plot_->removeGraph(iterator->graph);
    curves_.erase(iterator);
    curveSelector_->removeCurve(curveId);
    replot();
    return true;
}

bool ChartWidget::containsCurve(const QString& curveId) const
{
    return curves_.contains(curveId);
}

void ChartWidget::start(bool startScrolling)
{
    if (startScrolling) {
        scrollTimer_->start();
    } else {
        scrollTimer_->stop();
    }
    toolBar_->uncheckAllActions();
}

void ChartWidget::setCurveName(const QString& curveId, const QString& displayName)
{
    const auto iterator = curves_.find(curveId);
    if (iterator == curves_.end() || displayName.isEmpty()) {
        return;
    }

    iterator->graph->setName(displayName);
    curveSelector_->setCurveName(curveId, displayName);
    replot();
}

void ChartWidget::setCurveVisible(const QString& curveId, bool visible)
{
    const auto iterator = curves_.find(curveId);
    if (iterator == curves_.end()) {
        return;
    }

    iterator->graph->setVisible(visible);
    curveSelector_->setCurveVisible(curveId, visible);
    replot();
}

void ChartWidget::setXAxisMode(ChartXAxisMode mode)
{
    xAxisMode_ = mode;
    if (mode == ChartXAxisMode::DateTime) {
        QSharedPointer<QCPAxisTickerDateTime> dateTimeTicker =
            QSharedPointer<QCPAxisTickerDateTime>::create();
        dateTimeTicker->setDateTimeFormat(QStringLiteral("HH:mm:ss"));
        plot_->xAxis->setTicker(dateTimeTicker);
    } else {
        plot_->xAxis->setTicker(QSharedPointer<QCPAxisTicker>::create());
    }
    replot();
}

void ChartWidget::setAxisLabels(const QString& xAxisLabel, const QString& yAxisLabel)
{
    plot_->xAxis->setLabel(xAxisLabel);
    plot_->yAxis->setLabel(yAxisLabel);
    replot();
}

void ChartWidget::setLegendVisible(bool visible)
{
    plot_->legend->setVisible(visible);
    replot();
}

void ChartWidget::setGridVisible(bool visible)
{
    plot_->xAxis->grid()->setVisible(visible);
    plot_->yAxis->grid()->setVisible(visible);
    replot();
}

void ChartWidget::appendPoint(const QString& curveId, double x, double y)
{
    appendPoints(curveId, {{x, y}});
}

void ChartWidget::appendPoints(const QString& curveId, const QVector<QPointF>& points)
{
    if (!isUiThread()) {
        qDebug() << "ChartWidget rejected a data update from a non-UI thread; use a queued Qt connection";
        return;
    }

    const auto iterator = curves_.find(curveId);
    if (iterator == curves_.end() || points.isEmpty()) {
        return;
    }

    QVector<double> keys;
    QVector<double> values;
    keys.reserve(points.size());
    values.reserve(points.size());
    for (const QPointF& point : points) {
        keys.push_back(point.x());
        values.push_back(point.y());
    }

    iterator->graph->addData(keys, values, false);
    trimCurve(iterator.value());
}

void ChartWidget::clearCurve(const QString& curveId)
{
    const auto iterator = curves_.find(curveId);
    if (iterator == curves_.end()) {
        return;
    }

    iterator->graph->data()->clear();
    replot();
}

void ChartWidget::clearAll()
{
    for (CurveEntry& curve : curves_) {
        curve.graph->data()->clear();
    }
    hideCrosshair();
    replot();
}

bool ChartWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == plot_ && event->type() == QEvent::Leave) {
        hideCrosshair();
        replot();
    }
    return QWidget::eventFilter(watched, event);
}

bool ChartWidget::isUiThread() const
{
    return QThread::currentThread() == thread();
}

bool ChartWidget::isEmpty() const
{
    for (const CurveEntry& curve : curves_) {
        if (!curve.graph->data()->isEmpty()) {
            return false;
        }
    }
    return true;
}

void ChartWidget::initializePlot()
{
    plot_->setBackground(QBrush(QColor(QStringLiteral("#ffffff"))));
    plot_->setMouseTracking(true);
    plot_->installEventFilter(this);
    plot_->legend->setVisible(false);
    plot_->legend->setBrush(QColor(255, 255, 255, 220));
    plot_->axisRect()->setBackground(QBrush(QColor(QStringLiteral("#fbfcfe"))));
    plot_->xAxis->grid()->setPen(QPen(QColor(QStringLiteral("#e4e9ef")), 1, Qt::DashLine));
    plot_->yAxis->grid()->setPen(QPen(QColor(QStringLiteral("#e4e9ef")), 1, Qt::DashLine));
    plot_->xAxis->setRange(0.0, 10.0);
    plot_->yAxis->setRange(-1.0, 1.0);

    QPen crosshairPen(QColor(QStringLiteral("#e2534b")), 1, Qt::DashLine);
    horizontalCrosshair_ = new QCPItemLine(plot_);
    horizontalCrosshair_->setPen(crosshairPen);
    horizontalCrosshair_->start->setType(QCPItemPosition::ptPlotCoords);
    horizontalCrosshair_->end->setType(QCPItemPosition::ptPlotCoords);
    horizontalCrosshair_->setVisible(false);

    verticalCrosshair_ = new QCPItemLine(plot_);
    verticalCrosshair_->setPen(crosshairPen);
    verticalCrosshair_->start->setType(QCPItemPosition::ptPlotCoords);
    verticalCrosshair_->end->setType(QCPItemPosition::ptPlotCoords);
    verticalCrosshair_->setVisible(false);

    crosshairText_ = new QCPItemText(plot_);
    crosshairText_->position->setType(QCPItemPosition::ptPlotCoords);
    crosshairText_->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    crosshairText_->setPadding(QMargins(5, 3, 5, 3));
    crosshairText_->setBrush(QColor(255, 255, 255, 225));
    crosshairText_->setColor(QColor(QStringLiteral("#26384a")));
    crosshairText_->setVisible(false);

    connect(plot_, &QCustomPlot::mouseMove, this, &ChartWidget::updateCrosshair);
}

void ChartWidget::initializeToolBarConnections()
{
    connect(toolBar_->horizontalZoomAction, &QAction::toggled, this, &ChartWidget::setHorizontalZooming);
    connect(toolBar_->verticalZoomAction, &QAction::toggled, this, &ChartWidget::setVerticalZooming);
    connect(toolBar_->zoomAction, &QAction::toggled, this, &ChartWidget::setZooming);
    connect(toolBar_->rectangleZoomAction, &QAction::toggled, this, &ChartWidget::setSelectionZooming);
    connect(toolBar_->dragAction, &QAction::toggled, this, &ChartWidget::setRangeDrag);
    connect(toolBar_->fullScreenAction, &QAction::triggered, this, &ChartWidget::enterFullScreen);
    connect(toolBar_->restoreAction,
            &QAction::triggered,
            this,
            &ChartWidget::restoreFromFullScreen);
}

void ChartWidget::restoreFromFullScreen()
{
    if (!fullScreenDialog_.isNull()) {
        fullScreenDialog_->close();
    }
}

void ChartWidget::setupInteraction(int change)
{
    userInteracting_ += change;
}

void ChartWidget::setHorizontalZooming(bool enabled)
{
    if (enabled) {
        plot_->axisRect()->setRangeZoom(Qt::Horizontal);
        plot_->setInteraction(QCP::iRangeZoom, true);
        setupInteraction(-1);
    } else {
        QCP::Interactions interactions = plot_->interactions();
        interactions &= ~QCP::iRangeZoom;
        plot_->setInteractions(interactions);
        setupInteraction(1);
    }
}

void ChartWidget::setVerticalZooming(bool enabled)
{
    if (enabled) {
        plot_->axisRect()->setRangeZoom(Qt::Vertical);
        plot_->setInteraction(QCP::iRangeZoom, true);
        setupInteraction(-1);
    } else {
        QCP::Interactions interactions = plot_->interactions();
        interactions &= ~QCP::iRangeZoom;
        plot_->setInteractions(interactions);
        setupInteraction(1);
    }
}

void ChartWidget::setZooming(bool enabled)
{
    if (enabled) {
        plot_->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
        plot_->setInteraction(QCP::iRangeZoom, true);
        setupInteraction(-1);
    } else {
        QCP::Interactions interactions = plot_->interactions();
        interactions &= ~QCP::iRangeZoom;
        plot_->setInteractions(interactions);
        setupInteraction(1);
    }
}

void ChartWidget::setSelectionZooming(bool enabled)
{
    if (enabled) {
        plot_->axisRect()->setRangeZoomAxes(plot_->xAxis, plot_->yAxis);
        plot_->setSelectionRectMode(QCP::srmZoom);
        setupInteraction(-1);
    } else {
        plot_->setSelectionRectMode(QCP::srmNone);
        setupInteraction(1);
    }
}

void ChartWidget::setRangeDrag(bool enabled)
{
    if (enabled) {
        plot_->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
        plot_->axisRect()->setRangeDragAxes(plot_->xAxis, plot_->yAxis);
        plot_->setInteraction(QCP::iRangeDrag, true);
        setupInteraction(-1);
    } else {
        QCP::Interactions interactions = plot_->interactions();
        interactions &= ~QCP::iRangeDrag;
        plot_->setInteractions(interactions);
        setupInteraction(1);
    }
}

void ChartWidget::setScrollPolicy(ScrollPolicy policy)
{
    scrollPolicy_ = policy;
}

void ChartWidget::scrollToLatest()
{
    if (curves_.isEmpty() || isEmpty() || userInteracting_ < 0) {
        return;
    }

    if (scrollPolicy_ == ScrollPolicy::AutoScroll) {
        updateAutoScrollRange();
    } else {
        updateOverviewRange();
    }
    replot();
}

void ChartWidget::updateOverviewRange()
{
    bool firstVisibleHandled = false;
    for (int index = 0; index < plot_->graphCount(); ++index) {
        QCPGraph* graph = plot_->graph(index);
        if (graph != nullptr && graph->visible()) {
            graph->rescaleAxes(firstVisibleHandled);
            firstVisibleHandled = true;
        }
    }
}

void ChartWidget::updateAutoScrollRange()
{
    QCPGraph* primaryGraph = plot_->graph(0);
    if (primaryGraph == nullptr || primaryGraph->data()->isEmpty()) {
        return;
    }

    const QSharedPointer<QCPGraphDataContainer> dataContainer = primaryGraph->data();
    const int dataCount = dataContainer->size();
    double startTime = dataContainer->constBegin()->key;
    if (dataCount > autoScrollThreshold) {
        startTime = (dataContainer->constEnd() - autoScrollVisiblePointCount)->key;
    }
    const double endTime = (dataContainer->constEnd() - 1)->key;
    plot_->xAxis->setRange(startTime, endTime);

    for (int index = 0; index < plot_->graphCount(); ++index) {
        QCPGraph* graph = plot_->graph(index);
        if (graph != nullptr && graph->visible()) {
            graph->rescaleValueAxis(index != 0);
        }
    }
}

void ChartWidget::trimCurve(CurveEntry& curve)
{
    const QSharedPointer<QCPGraphDataContainer> dataContainer = curve.graph->data();
    const int overflow = dataContainer->size() - curve.maximumPointCount;
    if (overflow <= 0) {
        return;
    }

    const double firstRetainedKey = dataContainer->at(overflow)->key;
    dataContainer->removeBefore(firstRetainedKey);
    if (dataContainer->size() <= curve.maximumPointCount) {
        return;
    }

    QVector<QCPGraphData> retainedData;
    retainedData.reserve(curve.maximumPointCount);
    auto iterator = dataContainer->constEnd() - curve.maximumPointCount;
    for (; iterator != dataContainer->constEnd(); ++iterator) {
        retainedData.push_back(*iterator);
    }
    dataContainer->set(retainedData, true);
}

void ChartWidget::updateCrosshair(QMouseEvent* event)
{
    const QPoint pixelPosition = event->position().toPoint();
    if (!plot_->axisRect()->rect().contains(pixelPosition)) {
        hideCrosshair();
        replot();
        return;
    }

    const double x = plot_->xAxis->pixelToCoord(pixelPosition.x());
    const double y = plot_->yAxis->pixelToCoord(pixelPosition.y());
    horizontalCrosshair_->start->setCoords(plot_->xAxis->range().lower, y);
    horizontalCrosshair_->end->setCoords(plot_->xAxis->range().upper, y);
    verticalCrosshair_->start->setCoords(x, plot_->yAxis->range().lower);
    verticalCrosshair_->end->setCoords(x, plot_->yAxis->range().upper);

    const QString xText = xAxisMode_ == ChartXAxisMode::DateTime
        ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(x * 1000.0)).toString(QStringLiteral("HH:mm:ss.zzz"))
        : QString::number(x, 'f', 3);
    crosshairText_->setText(QStringLiteral("X: %1\nY: %2").arg(xText, QString::number(y, 'f', 3)));
    crosshairText_->position->setCoords(x, y);

    horizontalCrosshair_->setVisible(true);
    verticalCrosshair_->setVisible(true);
    crosshairText_->setVisible(true);
    replot();
}

void ChartWidget::hideCrosshair()
{
    horizontalCrosshair_->setVisible(false);
    verticalCrosshair_->setVisible(false);
    crosshairText_->setVisible(false);
}

void ChartWidget::replot()
{
    plot_->replot(QCustomPlot::rpImmediateRefresh);
}

void ChartWidget::enterFullScreen()
{
    if (!fullScreenDialog_.isNull()) {
        return;
    }

    auto* dialog = new QDialog(this);
    fullScreenDialog_ = dialog;
    dialog->setWindowTitle(QStringLiteral("CusQCharView"));

    QScreen* currentScreen = QGuiApplication::screenAt(QCursor::pos());
    if (currentScreen == nullptr) {
        qDebug() << "ChartWidget cannot enter full screen because no screen is available";
        fullScreenDialog_.clear();
        dialog->deleteLater();
        return;
    }

    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    dialog->setGeometry(currentScreen->geometry());
    contentWidget_->setParent(dialog);

    auto* dialogLayout = new QVBoxLayout(dialog);
    dialogLayout->setContentsMargins(0, 0, 0, 0);
    dialogLayout->addWidget(contentWidget_);
    contentWidget_->show();
    setScrollPolicy(ScrollPolicy::AutoScroll);

    connect(dialog, &QDialog::finished, this, [this, dialog](int) {
        contentWidget_->setParent(this);
        rootLayout_->addWidget(contentWidget_);
        contentWidget_->show();
        setScrollPolicy(ScrollPolicy::Overview);
        fullScreenDialog_.clear();
        dialog->deleteLater();
    });

    dialog->exec();
}
