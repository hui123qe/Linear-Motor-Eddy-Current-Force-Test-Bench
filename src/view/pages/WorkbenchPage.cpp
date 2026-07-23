#include "WorkbenchPage.h"

#include "../../acquisition/DataAcquisitionService.h"
#include "../../database/AcquisitionDatabaseService.h"
#include "../../motion/MotionControlService.h"
#include "../../workflow/TestExecutionService.h"
#include "../components/chart/ChartWidget.h"
#include "../widgets/MetricCard.h"
#include "../widgets/StatusPill.h"
#include "../widgets/ViewHelpers.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <optional>

namespace {

const QString& eddyForceCurveId()
{
    static const QString curveId = QStringLiteral("eddy_force");
    return curveId;
}

QString testTypeText(EddyCurrentTestType testType)
{
    switch (testType) {
    case EddyCurrentTestType::RatedSpeed:
        return QStringLiteral("额定速度涡流力测试");
    case EddyCurrentTestType::VariableSpeed:
        return QStringLiteral("不同速度涡流力测试");
    }

    return QStringLiteral("----");
}

std::optional<TestResultTargets> requestTestResultTargets(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("设置测试目标"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(12);

    const TestResultTargets defaults;
    auto* averageInput = new QDoubleSpinBox(&dialog);
    averageInput->setRange(0.001, 1000000.0);
    averageInput->setDecimals(3);
    averageInput->setValue(defaults.averageForceNewtons);
    averageInput->setSuffix(QStringLiteral(" N"));
    form->addRow(QStringLiteral("目标平均值"), averageInput);

    auto* fluctuationRateInput = new QDoubleSpinBox(&dialog);
    fluctuationRateInput->setRange(0.0, 100.0);
    fluctuationRateInput->setDecimals(2);
    fluctuationRateInput->setValue(defaults.fluctuationRatePercent);
    fluctuationRateInput->setSuffix(QStringLiteral(" %"));
    form->addRow(QStringLiteral("目标波动率"), fluctuationRateInput);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    TestResultTargets targets;
    targets.averageForceNewtons = averageInput->value();
    targets.fluctuationRatePercent = fluctuationRateInput->value();
    return targets;
}

QString forceText(double value)
{
    return QStringLiteral("%1 N").arg(value, 0, 'f', 2);
}

QString rateText(double value)
{
    return QStringLiteral("%1 %").arg(value, 0, 'f', 2);
}

QString signedForceText(double value)
{
    return QStringLiteral("%1%2 N")
        .arg(value >= 0.0 ? QStringLiteral("+") : QString(),
             QString::number(value, 'f', 2));
}

QString signedRateDeviationText(double value)
{
    return QStringLiteral("%1%2 个百分点")
        .arg(value >= 0.0 ? QStringLiteral("+") : QString(),
             QString::number(value, 'f', 2));
}

bool isActiveMotionState(int state)
{
    return state == 10 || state == 20 || state == 30
           || state == 40 || state == 50 || state == 60;
}

QString motionStateText(int state, int errorCode, int currentCount)
{
    switch (state) {
    case 0:
        if (errorCode == 2001) {
            return QStringLiteral("已停止");
        }
        if (errorCode == 2002) {
            return QStringLiteral("快速停止");
        }
        return QStringLiteral("空闲");
    case 10:
        return QStringLiteral("参数检查");
    case 20:
        return QStringLiteral("轴使能");
    case 30:
        return QStringLiteral("运动到零点");
    case 40:
        return QStringLiteral("运动到测试起点");
    case 50:
        return QStringLiteral("测试运动（第 %1 次）").arg(currentCount + 1);
    case 60:
        return QStringLiteral("返回零点");
    case 100:
        return QStringLiteral("正常完成");
    case -1:
        return QStringLiteral("参数错误（%1）").arg(errorCode);
    case -2:
        return QStringLiteral("已停止");
    case -3:
        return QStringLiteral("快速停止");
    case -4:
        return QStringLiteral("程序异常（%1）").arg(errorCode);
    default:
        return QStringLiteral("未知状态 %1").arg(state);
    }
}

} // namespace

WorkbenchPage::WorkbenchPage(QWidget* parent)
    : QWidget(parent)
{
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(18, 14, 18, 14);
    pageLayout->setSpacing(12);

    auto* heading = new QHBoxLayout;
    auto* headingText = new QVBoxLayout;
    headingText->setSpacing(1);
    headingText->addWidget(ViewHelpers::makeLabel(QStringLiteral("测试工作台"), "pageTitle"));
    headingText->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("实时掌握当前测试项、运动阶段和关键结果"),
        "pageDescription"));
    heading->addLayout(headingText);
    heading->addStretch();
    configurationStatus_ = new StatusPill(
        QStringLiteral("参数未锁定"), QStringLiteral("neutral"), this);
    heading->addWidget(configurationStatus_);
    pageLayout->addLayout(heading);

    auto* taskBar = new QFrame;
    taskBar->setObjectName(QStringLiteral("taskBar"));
    auto* taskLayout = new QHBoxLayout(taskBar);
    taskLayout->setContentsMargins(14, 10, 14, 10);
    taskLayout->setSpacing(12);
    testItemCard_ = new MetricCard(
        QStringLiteral("当前测试项"), QStringLiteral("----"), this);
    batchCard_ = new MetricCard(
        QStringLiteral("当前批次"), QStringLiteral("----"), this);
    taskLayout->addWidget(testItemCard_);
    taskLayout->addWidget(batchCard_);
    pageLayout->addWidget(taskBar);

    auto* primaryRow = new QHBoxLayout;
    primaryRow->setSpacing(12);

    auto* curvePanel = ViewHelpers::makePanel(
        QStringLiteral("实时曲线"),
        QStringLiteral("显示后台传入的真实涡流力数据，当前不生成模拟数据"));
    curvePanel->setObjectName(QStringLiteral("curvePanel"));
    auto* curveLayout = qobject_cast<QVBoxLayout*>(curvePanel->layout());
    chartWidget_ = new ChartWidget(curvePanel);
    chartWidget_->setXAxisMode(ChartXAxisMode::DateTime);
    chartWidget_->setAxisLabels(QStringLiteral("时间"), QStringLiteral("涡流力 / N"));

    ChartCurveConfig forceCurve;
    forceCurve.id = eddyForceCurveId();
    forceCurve.displayName = QStringLiteral("涡流力");
    forceCurve.color = QColor(QStringLiteral("#1f78d1"));
    forceCurve.maximumPointCount = 100000;
    chartWidget_->addCurve(forceCurve);
    curveLayout->addWidget(chartWidget_, 1);
    primaryRow->addWidget(curvePanel, 5);

    auto* controlPanel = ViewHelpers::makePanel(
        QStringLiteral("控制与执行"),
        QStringLiteral("启动与停止通过 ACS 控制器执行，回零尚未接入"));
    auto* controlLayout = qobject_cast<QVBoxLayout*>(controlPanel->layout());
    startButton_ = ViewHelpers::makeButton(
        QStringLiteral("启动测试"), QStringLiteral("primary"));
    stopButton_ = ViewHelpers::makeButton(
        QStringLiteral("停止"), QStringLiteral("warning"));
    auto* homing = ViewHelpers::makeButton(QStringLiteral("执行回零"));
    controlLayout->addWidget(startButton_);
    controlLayout->addWidget(stopButton_);
    controlLayout->addWidget(homing);
    controlLayout->addWidget(ViewHelpers::makeDivider());
    controlLayout->addStretch();

    primaryRow->addWidget(controlPanel, 1);
    pageLayout->addLayout(primaryRow, 4);

    auto* lowerRow = new QHBoxLayout;
    lowerRow->setSpacing(12);

    auto* motionPanel = ViewHelpers::makePanel(
        QStringLiteral("当前运动状态"),
        QStringLiteral("状态来自 ACS Buffer 的 G_STATE"));
    auto* motionLayout = qobject_cast<QVBoxLayout*>(motionPanel->layout());
    auto* stateCard = new QFrame;
    stateCard->setObjectName(QStringLiteral("motionStateCard"));
    auto* stateLayout = new QVBoxLayout(stateCard);
    stateLayout->setContentsMargins(14, 12, 14, 12);
    stateLayout->setSpacing(5);
    stateLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("运动状态"), "fieldLabel"));
    currentStateValue_ = ViewHelpers::makeLabel(QStringLiteral("空闲"), "motionStateValue");
    currentStateValue_->setAlignment(Qt::AlignCenter);
    stateLayout->addWidget(currentStateValue_);
    auto* stateHelp = ViewHelpers::makeLabel(
        QStringLiteral("状态集合：空闲 / 参数检查 / 轴使能 / 到零点 / 到测试起点 / 测试运动 / 返回零点 / 完成 / 故障"),
        "motionStateHelp");
    stateHelp->setWordWrap(true);
    stateLayout->addWidget(stateHelp);
    motionLayout->addWidget(stateCard);
    motionLayout->addStretch();
    lowerRow->addWidget(motionPanel, 1);

    auto* resultPanel = ViewHelpers::makePanel(QStringLiteral("关键结果"), QStringLiteral("测试完成后自动更新"));
    auto* resultLayout = qobject_cast<QVBoxLayout*>(resultPanel->layout());
    auto* resultGrid = new QGridLayout;
    resultGrid->setHorizontalSpacing(12);
    resultGrid->setVerticalSpacing(10);
    averageForceCard_ = new MetricCard(QStringLiteral("实际平均值 avg"), QStringLiteral("----"));
    forceRangeCard_ = new MetricCard(QStringLiteral("实际波动值 max-min"), QStringLiteral("----"));
    fluctuationRateCard_ = new MetricCard(QStringLiteral("实际波动率 (max-min)/avg"), QStringLiteral("----"));
    averageDeviationCard_ = new MetricCard(QStringLiteral("平均值偏差"), QStringLiteral("----"));
    fluctuationRateDeviationCard_ = new MetricCard(QStringLiteral("波动率偏差"), QStringLiteral("----"));
    resultGrid->addWidget(averageForceCard_, 0, 0);
    resultGrid->addWidget(forceRangeCard_, 0, 1);
    resultGrid->addWidget(fluctuationRateCard_, 0, 2);
    resultGrid->addWidget(averageDeviationCard_, 1, 0);
    resultGrid->addWidget(fluctuationRateDeviationCard_, 1, 1, 1, 2);
    resultLayout->addLayout(resultGrid);
    resultLayout->addStretch();
    lowerRow->addWidget(resultPanel, 3);

    pageLayout->addLayout(lowerRow, 1);

    connect(startButton_,
            &QPushButton::clicked,
            this,
            &WorkbenchPage::onStartButtonClicked);
    connect(stopButton_,
            &QPushButton::clicked,
            this,
            &WorkbenchPage::onStopButtonClicked);
    connect(homing,
            &QPushButton::clicked,
            this,
            &WorkbenchPage::onHomingButtonClicked);

    MotionControlService& motionControlService = MotionControlService::instance();
    connect(&motionControlService,
            &MotionControlService::connectionChanged,
            this,
            &WorkbenchPage::setControllerConnected);
    connect(&motionControlService,
            &MotionControlService::motionStatusChanged,
            this,
            [this](const AcsMotionStatus& status) {
                setMotionStatus(
                    status.state, status.errorCode, status.currentCount);
            });
    connect(&motionControlService,
            &MotionControlService::startRequestWritten,
            this,
            [this] {
                setMotionCommandPending(false);
                if (!pendingResultTargets_.has_value()) {
                    return;
                }

                QString errorMessage;
                if (!resultService_.beginTest(
                        *pendingResultTargets_, &errorMessage)) {
                    QMessageBox::warning(
                        this, QStringLiteral("启动测试失败"), errorMessage);
                }
                pendingResultTargets_.reset();
            });
    connect(&motionControlService,
            &MotionControlService::stopRequestWritten,
            this,
            [this] {
                setMotionCommandPending(false);
                QMessageBox::information(
                    this,
                    QStringLiteral("停止测试"),
                    QStringLiteral("停止请求已发送。"));
            });
    DataAcquisitionService& dataAcquisitionService =
        DataAcquisitionService::instance();
    connect(&dataAcquisitionService,
            &DataAcquisitionService::readinessChanged,
            this,
            [this](bool ready, const QString& message) {
                acquisitionReady_ = ready;
                if (!ready && controllerConnected_) {
                    currentStateValue_->setToolTip(message);
                }
                updateControlAvailability();
            });
    connect(&dataAcquisitionService,
            &DataAcquisitionService::forceSamplesReady,
            this,
            &WorkbenchPage::appendEddyForceSamples);

    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();
    connect(&databaseService,
            &AcquisitionDatabaseService::readinessChanged,
            this,
            [this](bool ready, const QString& message) {
                databaseReady_ = ready;
                if (!ready) {
                    currentStateValue_->setToolTip(message);
                }
                updateControlAvailability();
            });

    TestExecutionService& executionService = TestExecutionService::instance();
    connect(&executionService,
            &TestExecutionService::executionFinished,
            this,
            [this] {
                setMotionCommandPending(false);
            });
    connect(&executionService,
            &TestExecutionService::executionStopped,
            this,
            [this] {
                setMotionCommandPending(false);
            });
    connect(&executionService,
            &TestExecutionService::executionFailed,
            this,
            [this](const QString& message) {
                pendingResultTargets_.reset();
                setMotionCommandPending(false);
                QMessageBox::warning(
                    this, QStringLiteral("测试流程失败"), message);
            });
    connect(&resultService_,
            &TestResultService::resultCleared,
            this,
            &WorkbenchPage::clearResults);
    connect(&resultService_,
            &TestResultService::resultUpdated,
            this,
            &WorkbenchPage::setResultComparison);

    updateControlAvailability();
}

bool WorkbenchPage::isConfigurationLocked() const
{
    return configurationLocked_;
}

void WorkbenchPage::onStartButtonClicked()
{
    if (!configurationLocked_ || !configuration_.has_value()) {
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            QStringLiteral("请先在参数设置页确定并锁定配置。"));
        return;
    }

    const std::optional<TestResultTargets> targets = requestTestResultTargets(this);
    if (targets.has_value()) {
        setMotionCommandPending(true);
        beginTest(*targets);
    }
}

void WorkbenchPage::onStopButtonClicked()
{
    setMotionCommandPending(true);
    stopTest();
}

void WorkbenchPage::onHomingButtonClicked()
{
    QMessageBox::information(
        this,
        QStringLiteral("执行回零"),
        QStringLiteral("执行回零尚未接入控制器，未发送设备指令。"));
}

void WorkbenchPage::setConfiguration(const TestParameters& parameters)
{
    configuration_ = parameters;
    testItemCard_->setValue(testTypeText(parameters.selectedTestType));

    const QString batchName = testBatchName(parameters);
    batchCard_->setValue(batchName.isEmpty() ? QStringLiteral("----") : batchName);
}

void WorkbenchPage::clearConfiguration()
{
    configuration_.reset();
    testItemCard_->setValue(QStringLiteral("----"));
    batchCard_->setValue(QStringLiteral("----"));
}

void WorkbenchPage::setConfigurationLocked(bool locked)
{
    configurationLocked_ = locked;
    configurationStatus_->setText(
        locked ? QStringLiteral("参数已锁定") : QStringLiteral("参数未锁定"));
    configurationStatus_->setLevel(
        locked ? QStringLiteral("ok") : QStringLiteral("neutral"));
    updateControlAvailability();
}

void WorkbenchPage::setControllerConnected(bool connected, const QString& message)
{
    controllerConnected_ = connected;
    currentStateValue_->setToolTip(message);
    if (!connected) {
        acquisitionReady_ = false;
        motionCommandPending_ = false;
        currentStateValue_->setText(QStringLiteral("ACS 未连接"));
        chartWidget_->start(false);
    } else {
        currentStateValue_->setText(motionStateText(motionState_, 0, 0));
    }
    updateControlAvailability();
}

void WorkbenchPage::setMotionStatus(int state, int errorCode, int currentCount)
{
    const int previousState = motionState_;
    motionState_ = state;
    if (state != previousState) {
        motionCommandPending_ = false;
    }

    currentStateValue_->setText(motionStateText(state, errorCode, currentCount));
    chartWidget_->start(state == 50);

    updateControlAvailability();
}

void WorkbenchPage::setMotionCommandPending(bool pending)
{
    motionCommandPending_ = pending;
    updateControlAvailability();
}

void WorkbenchPage::beginTest(const TestResultTargets& targets)
{
    if (!configurationLocked_ || !configuration_.has_value()) {
        setMotionCommandPending(false);
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            QStringLiteral("请先在参数设置页确定并锁定配置。"));
        return;
    }

    pendingResultTargets_ = targets;
    clearChartData();
    QString errorMessage;
    if (!TestExecutionService::instance().start(
            *configuration_, &errorMessage)) {
        pendingResultTargets_.reset();
        setMotionCommandPending(false);
        QMessageBox::warning(
            this, QStringLiteral("启动测试失败"), errorMessage);
    }
}

void WorkbenchPage::stopTest()
{
    QString errorMessage;
    if (TestExecutionService::instance().stop(&errorMessage)) {
        return;
    }

    setMotionCommandPending(false);
    QMessageBox::warning(
        this, QStringLiteral("停止测试失败"), errorMessage);
}

void WorkbenchPage::updateControlAvailability()
{
    if (startButton_ == nullptr || stopButton_ == nullptr) {
        return;
    }

    startButton_->setEnabled(controllerConnected_
                             && acquisitionReady_
                             && databaseReady_
                             && configurationLocked_
                             && motionState_ == 0
                             && !motionCommandPending_);
    stopButton_->setEnabled(controllerConnected_
                            && isActiveMotionState(motionState_)
                            && !motionCommandPending_);
}

void WorkbenchPage::appendEddyForceSample(double timestampSeconds, double forceNewtons)
{
    chartWidget_->appendPoint(eddyForceCurveId(), timestampSeconds, forceNewtons);
}

void WorkbenchPage::appendEddyForceSamples(const QVector<QPointF>& samples)
{
    chartWidget_->appendPoints(eddyForceCurveId(), samples);
}

void WorkbenchPage::clearChartData()
{
    chartWidget_->clearCurve(eddyForceCurveId());
}

void WorkbenchPage::clearResults()
{
    averageForceCard_->setValue(QStringLiteral("----"));
    forceRangeCard_->setValue(QStringLiteral("----"));
    fluctuationRateCard_->setValue(QStringLiteral("----"));
    averageDeviationCard_->setValue(QStringLiteral("----"));
    fluctuationRateDeviationCard_->setValue(QStringLiteral("----"));
}

void WorkbenchPage::setResultComparison(const TestResultComparison& comparison)
{
    averageForceCard_->setValue(forceText(comparison.actual.averageForceNewtons));
    forceRangeCard_->setValue(forceText(comparison.actual.forceRangeNewtons));
    fluctuationRateCard_->setValue(rateText(comparison.actualFluctuationRatePercent));
    averageDeviationCard_->setValue(
        signedForceText(comparison.averageDeviationNewtons));
    fluctuationRateDeviationCard_->setValue(
        signedRateDeviationText(
            comparison.fluctuationRateDeviationPercentagePoints));
}
