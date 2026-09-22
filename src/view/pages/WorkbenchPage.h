#pragma once

#include "../../config/TestParameters.h"
#include "../../result/TestResultService.h"

#include <QPointF>
#include <QVector>
#include <QWidget>

#include <optional>

class QLabel;
class ChartWidget;
class MetricCard;
class QPushButton;
class StatusPill;
class QVBoxLayout;
struct ExperimentRecord;
struct ExperimentSummaryRecord;

class WorkbenchPage final : public QWidget
{
    Q_OBJECT

public:
    explicit WorkbenchPage(QWidget* parent = nullptr);
    [[nodiscard]] bool isConfigurationLocked() const;

public slots:
    void setConfiguration(const TestParameters& parameters);
    void clearConfiguration();
    void setConfigurationLocked(bool locked);
    void setControllerConnected(bool connected, const QString& message);
    void setMotionStatus(int state, int errorCode, int currentCount);
    void setMotionCommandPending(bool pending);
    void appendForcePositionSamples(const QVector<QPointF>& samples);
    void clearChartData();
    void clearResults();
    void setExperimentRecord(const ExperimentRecord& record);
    void setExperimentSummary(const ExperimentSummaryRecord& summary);
    void setResultComparison(const TestResultComparison& comparison);

private slots:
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onHomingButtonClicked();
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
    void updateControlAvailability();

    TestResultService resultService_;
    std::optional<TestParameters> configuration_;
    std::optional<TestResultTargets> pendingResultTargets_;
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
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* homingButton_ = nullptr;
    bool configurationLocked_ = false;
    bool controllerConnected_ = false;
    bool motionCommandPending_ = false;
    bool chartCollectionActive_ = false;
    int motionState_ = 0;
    qint64 activeExecutionId_ = 0;
    QVector<QPointF> pendingChartSamples_;
};
