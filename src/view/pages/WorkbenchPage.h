#pragma once

#include "../../config/TestParameters.h"
#include "../../result/TestResultService.h"

#include <QPointF>
#include <QVector>
#include <QWidget>

class QLabel;
class ChartWidget;
class MetricCard;
class QPushButton;
class StatusPill;
class QVBoxLayout;
struct ExperimentRecord;
struct ExperimentSummaryRecord;
enum class MachineState;

class WorkbenchPage final : public QWidget
{
    Q_OBJECT

public:
    explicit WorkbenchPage(QWidget* parent = nullptr);

public slots:
    void setConfiguration(const TestParameters& parameters);
    void clearConfiguration();
    void setConfigurationLocked(bool locked);
    void setControllerConnected(bool connected, const QString& message);
    void setMotionState(int state, int errorCode);
    void setHomeRunning(bool running);
    void setDisplacement(double positionMillimeters);
    void appendForcePositionSamples(const QVector<QPointF>& samples);
    void clearChartData();
    void clearResults();
    void setExperimentRecord(const ExperimentRecord& record);
    void setExperimentSummary(const ExperimentSummaryRecord& summary);
    void setResultComparison(const TestResultComparison& comparison);

private slots:
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onHomeButtonClicked();
    void onResetButtonClicked();
    void beginChartCollection();
    void commitChartCollection();

private:
    void initializePageHeader(QVBoxLayout* pageLayout);
    void initializeTaskBar(QVBoxLayout* pageLayout);
    void initializePrimaryArea(QVBoxLayout* pageLayout);
    void initializeLowerArea(QVBoxLayout* pageLayout);
    void initializeConnections();
    void beginTest(const TestResultTargets& targets);
    void stopTest();
    void updateMachineStateDisplay(MachineState state, const QString& reason);

    MetricCard* testItemCard_ = nullptr;
    MetricCard* batchCard_ = nullptr;
    MetricCard* averageForceCard_ = nullptr;
    MetricCard* eddyForceCoefficientCard_ = nullptr;
    MetricCard* forceRangeCard_ = nullptr;
    MetricCard* fluctuationRateCard_ = nullptr;
    MetricCard* multipleAverageForceCard_ = nullptr;
    MetricCard* multipleAverageForceCoefficientCard_ = nullptr;
    QLabel* latestRecordLabel_ = nullptr;
    ChartWidget* chartWidget_ = nullptr;
    StatusPill* configurationStatus_ = nullptr;
    QLabel* currentStateValue_ = nullptr;
    MetricCard* displacementCard_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* homeButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
    bool chartCollectionActive_ = false;
    qint64 activeExecutionId_ = 0;
    QVector<QPointF> pendingChartSamples_;
};
