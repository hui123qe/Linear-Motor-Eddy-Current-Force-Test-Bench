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
    void appendEddyForceSample(double timestampSeconds, double forceNewtons);
    void appendEddyForceSamples(const QVector<QPointF>& samples);
    void clearChartData();
    void clearResults();
    void setResultComparison(const TestResultComparison& comparison);

private slots:
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onHomingButtonClicked();

private:
    void beginTest(const TestResultTargets& targets);
    void stopTest();
    void updateControlAvailability();

    TestResultService resultService_;
    std::optional<TestParameters> configuration_;
    std::optional<TestResultTargets> pendingResultTargets_;
    MetricCard* testItemCard_ = nullptr;
    MetricCard* batchCard_ = nullptr;
    MetricCard* averageForceCard_ = nullptr;
    MetricCard* forceRangeCard_ = nullptr;
    MetricCard* fluctuationRateCard_ = nullptr;
    MetricCard* averageDeviationCard_ = nullptr;
    MetricCard* fluctuationRateDeviationCard_ = nullptr;
    ChartWidget* chartWidget_ = nullptr;
    StatusPill* configurationStatus_ = nullptr;
    QLabel* currentStateValue_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    bool configurationLocked_ = false;
    bool controllerConnected_ = false;
    bool acquisitionReady_ = false;
    bool databaseReady_ = false;
    bool motionCommandPending_ = false;
    int motionState_ = 0;
};
