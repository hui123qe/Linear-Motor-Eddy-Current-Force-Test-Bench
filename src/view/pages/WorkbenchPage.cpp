#include "WorkbenchPage.h"

#include "../../acquisition/DataAcquisitionService.h"
#include "../../database/AcquisitionDatabaseService.h"
#include "../../experimentlog/ExperimentLogService.h"
#include "../../logging/AppLogger.h"
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

#include <algorithm>
#include <optional>

namespace {

constexpr qsizetype maximumChartPointCount = 100000;

const QString& eddyForceCurveId()
{
    static const QString curveId = QStringLiteral("eddy_force");
    return curveId;
}

QString testTypeText(EddyCurrentTestType testType)
{
    switch (testType) {
    case EddyCurrentTestType::RatedSpeed:
        return QStringLiteral("标准件涡流力测试");
    case EddyCurrentTestType::VariableSpeed:
        return QStringLiteral("非标准件涡流力测试");
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

QString motionStateText(int state, int errorCode)
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
        return QStringLiteral("正向测试");
    case 55:
        return QStringLiteral("正向结果处理");
    case 60:
        return QStringLiteral("返回零点");
    case 70:
        return QStringLiteral("反向测试");
    case 75:
        return QStringLiteral("反向结果处理");
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

bool isActiveMotionState(int state)
{
    return state == 10 || state == 20 || state == 30
           || state == 40 || state == 50 || state == 55
           || state == 60 || state == 70 || state == 75;
}

} // namespace

WorkbenchPage::WorkbenchPage(QWidget* parent)
    : QWidget(parent)
{
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(18, 14, 18, 14);
    pageLayout->setSpacing(12);

    initializePageHeader(pageLayout);
    initializeTaskBar(pageLayout);
    initializePrimaryArea(pageLayout);
    initializeLowerArea(pageLayout);
    initializeConnections();
    updateMotionStateDisplay();
}

void WorkbenchPage::initializePageHeader(QVBoxLayout* pageLayout)
{
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
}

void WorkbenchPage::initializeTaskBar(QVBoxLayout* pageLayout)
{
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
}

void WorkbenchPage::initializePrimaryArea(QVBoxLayout* pageLayout)
{
    auto* primaryRow = new QHBoxLayout;
    primaryRow->setSpacing(12);

    auto* curvePanel = ViewHelpers::makePanel(
        QStringLiteral("实时曲线"),
        QStringLiteral("显示当前单次正向或反向实验的位移—涡流力关系"));
    curvePanel->setObjectName(QStringLiteral("curvePanel"));
    auto* curveLayout = qobject_cast<QVBoxLayout*>(curvePanel->layout());
    chartWidget_ = new ChartWidget(curvePanel);
    chartWidget_->setXAxisMode(ChartXAxisMode::Numeric);
    chartWidget_->setAxisLabels(QStringLiteral("位移 / m"),
                                QStringLiteral("涡流力 / N"));

    ChartCurveConfig forceCurve;
    forceCurve.id = eddyForceCurveId();
    forceCurve.displayName = QStringLiteral("涡流力");
    forceCurve.color = QColor(QStringLiteral("#1f78d1"));
    forceCurve.maximumPointCount = static_cast<int>(maximumChartPointCount);
    chartWidget_->addCurve(forceCurve);
    curveLayout->addWidget(chartWidget_, 1);
    primaryRow->addWidget(curvePanel, 5);

    auto* controlPanel = ViewHelpers::makePanel(
        QStringLiteral("控制与执行"),
        QStringLiteral("自动模式下启动或停止测试"));
    auto* controlLayout = qobject_cast<QVBoxLayout*>(controlPanel->layout());
    startButton_ = ViewHelpers::makeButton(
        QStringLiteral("启动测试"), QStringLiteral("primary"));
    stopButton_ = ViewHelpers::makeButton(
        QStringLiteral("停止"), QStringLiteral("warning"));
    homeButton_ = ViewHelpers::makeButton(
        QStringLiteral("回零"), QStringLiteral("primary"));
    resetButton_ = ViewHelpers::makeButton(
        QStringLiteral("复位"), QStringLiteral("warning"));
    controlLayout->addWidget(startButton_);
    controlLayout->addWidget(stopButton_);
    controlLayout->addWidget(ViewHelpers::makeDivider());
    controlLayout->addWidget(homeButton_);
    controlLayout->addWidget(resetButton_);
    controlLayout->addStretch();

    primaryRow->addWidget(controlPanel, 1);
    pageLayout->addLayout(primaryRow, 4);
}

void WorkbenchPage::initializeLowerArea(QVBoxLayout* pageLayout)
{
    auto* lowerRow = new QHBoxLayout;
    lowerRow->setSpacing(12);

    auto* motionPanel = ViewHelpers::makePanel(
        QStringLiteral("当前运动状态"));
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
    motionLayout->addWidget(stateCard);
    displacementCard_ = new MetricCard(
        QStringLiteral("位移"), QStringLiteral("-- mm"));
    motionLayout->addWidget(displacementCard_);
    motionLayout->addStretch();
    lowerRow->addWidget(motionPanel, 1);

    auto* resultPanel = ViewHelpers::makePanel(
        QStringLiteral("关键结果"),
        QStringLiteral("每段正向或反向实验完成后更新左栏，全部循环完成后更新右栏"));
    auto* resultLayout = qobject_cast<QVBoxLayout*>(resultPanel->layout());
    auto* resultColumns = new QHBoxLayout;
    resultColumns->setSpacing(12);

    auto* cycleResultPanel = ViewHelpers::makePanel(
        QStringLiteral("最近一次实验结果"),
        QStringLiteral("显示最近完成的正向或反向实验关键指标"));
    auto* cycleResultLayout =
        qobject_cast<QVBoxLayout*>(cycleResultPanel->layout());
    auto* cycleResultGrid = new QGridLayout;
    cycleResultGrid->setHorizontalSpacing(10);
    cycleResultGrid->setVerticalSpacing(10);
    latestRecordLabel_ = ViewHelpers::makeLabel(
        QStringLiteral("尚无已完成实验"), "fieldLabel");
    cycleResultLayout->addWidget(latestRecordLabel_);
    averageForceCard_ = new MetricCard(
        QStringLiteral("平均涡流力"), QStringLiteral("----"));
    eddyForceCoefficientCard_ = new MetricCard(
        QStringLiteral("涡流力系数"), QStringLiteral("----"));
    forceRangeCard_ = new MetricCard(
        QStringLiteral("涡流力波动值"), QStringLiteral("----"));
    fluctuationRateCard_ = new MetricCard(
        QStringLiteral("涡流力波动率"), QStringLiteral("----"));
    cycleResultGrid->addWidget(averageForceCard_, 0, 0);
    cycleResultGrid->addWidget(eddyForceCoefficientCard_, 0, 1);
    cycleResultGrid->addWidget(forceRangeCard_, 1, 0);
    cycleResultGrid->addWidget(fluctuationRateCard_, 1, 1);
    cycleResultLayout->addLayout(cycleResultGrid);
    cycleResultLayout->addStretch();
    resultColumns->addWidget(cycleResultPanel, 1);

    auto* finalResultPanel = ViewHelpers::makePanel(
        QStringLiteral("最终实验结果"),
        QStringLiteral("全部循环完成后的多次平均值"));
    auto* finalResultLayout =
        qobject_cast<QVBoxLayout*>(finalResultPanel->layout());
    auto* finalResultGrid = new QGridLayout;
    finalResultGrid->setHorizontalSpacing(10);
    finalResultGrid->setVerticalSpacing(10);
    multipleAverageForceCard_ = new MetricCard(
        QStringLiteral("多次平均涡流力"), QStringLiteral("----"));
    multipleAverageForceCoefficientCard_ = new MetricCard(
        QStringLiteral("多次平均涡流力系数"), QStringLiteral("----"));
    finalResultGrid->addWidget(multipleAverageForceCard_, 0, 0);
    finalResultGrid->addWidget(multipleAverageForceCoefficientCard_, 1, 0);
    finalResultLayout->addLayout(finalResultGrid);
    finalResultLayout->addStretch();
    resultColumns->addWidget(finalResultPanel, 1);

    resultLayout->addLayout(resultColumns);
    resultLayout->addStretch();
    lowerRow->addWidget(resultPanel, 3);

    pageLayout->addLayout(lowerRow, 1);
}

void WorkbenchPage::initializeConnections()
{
    connect(startButton_,
            &QPushButton::clicked,
            this,
            &WorkbenchPage::onStartButtonClicked);
    connect(stopButton_,
            &QPushButton::clicked,
            this,
            &WorkbenchPage::onStopButtonClicked);
    connect(homeButton_,
            &QPushButton::clicked,
            this,
            &WorkbenchPage::onHomeButtonClicked);
    connect(resetButton_,
            &QPushButton::clicked,
            this,
            &WorkbenchPage::onResetButtonClicked);
    MotionControlService& motionControlService = MotionControlService::instance();
    connect(&motionControlService,
            &MotionControlService::connectionChanged,
            this,
            &WorkbenchPage::setControllerConnected);
    connect(&motionControlService,
            &MotionControlService::motionStateChanged,
            this,
            &WorkbenchPage::setMotionState);
    connect(&motionControlService,
            &MotionControlService::positionFeedbackChanged,
            this,
            &WorkbenchPage::setDisplacement);
    connect(&motionControlService,
            &MotionControlService::homeDoneChanged,
            this,
            &WorkbenchPage::setHomeDone);
    connect(&motionControlService,
            &MotionControlService::homeRunningChanged,
            this,
            &WorkbenchPage::setHomeRunning);
    connect(&motionControlService,
            &MotionControlService::maintenanceCommandCompleted,
            this,
            [this](MaintenanceCommand command) {
                if (command == MaintenanceCommand::HomeAxis
                    && homeCommandRequested_) {
                    homeCommandRequested_ = false;
                }
            });
    connect(&motionControlService,
            &MotionControlService::maintenanceCommandFailed,
            this,
            [this](MaintenanceCommand command, const QString& message) {
                if (command != MaintenanceCommand::HomeAxis
                    || !homeCommandRequested_) {
                    return;
                }
                homeCommandRequested_ = false;
                QMessageBox::warning(
                    this, QStringLiteral("回零失败"), message);
            });
    connect(&motionControlService,
            &MotionControlService::controllerResetCompleted,
            this,
            [this] {
                if (!resetCommandRequested_) {
                    return;
                }
                resetCommandRequested_ = false;
                updateMotionStateDisplay();
                QMessageBox::information(
                    this,
                    QStringLiteral("控制器复位"),
                    QStringLiteral(
                        "ACS 控制器已重启，请重新连接并执行回零。"));
            });
    connect(&motionControlService,
            &MotionControlService::controllerResetFailed,
            this,
            [this](const QString& message) {
                if (!resetCommandRequested_) {
                    return;
                }
                resetCommandRequested_ = false;
                updateMotionStateDisplay();
                QMessageBox::warning(
                    this, QStringLiteral("控制器复位失败"), message);
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
                if (!ready && controllerConnected_) {
                    currentStateValue_->setToolTip(message);
                }
            });
    connect(&dataAcquisitionService,
            &DataAcquisitionService::collectionStarted,
            this,
            &WorkbenchPage::beginChartCollection);
    connect(&dataAcquisitionService,
            &DataAcquisitionService::collectionStopped,
            this,
            &WorkbenchPage::commitChartCollection);
    connect(&dataAcquisitionService,
            &DataAcquisitionService::forcePositionSamplesReady,
            this,
            &WorkbenchPage::appendForcePositionSamples);

    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();
    connect(&databaseService,
            &AcquisitionDatabaseService::readinessChanged,
            this,
            [this](bool ready, const QString& message) {
                if (!ready) {
                    currentStateValue_->setToolTip(message);
                }
            });

    TestExecutionService& executionService = TestExecutionService::instance();
    connect(&executionService,
            &TestExecutionService::executionStarted,
            this,
            [this](qint64 executionId, const QString&) {
                activeExecutionId_ = executionId;
                qCInfo(logCompletionReceipt)
                    << "[工作台][执行上下文] 设置 activeExecutionId"
                    << "executionId=" << activeExecutionId_;
                clearResults();
            });
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
    ExperimentLogService& experimentLogService =
        ExperimentLogService::instance();
    connect(&experimentLogService,
            &ExperimentLogService::experimentRecordSaved,
            this,
            &WorkbenchPage::setExperimentRecord);
    connect(&experimentLogService,
            &ExperimentLogService::experimentRecordProcessingFailed,
            this,
            [this](qint64 executionId,
                   int repetitionIndex,
                   const QString& message) {
                qCWarning(logCompletionReceipt).noquote()
                    << "[工作台][单次结果] 收到处理失败信号"
                    << "executionId=" << executionId
                    << "activeExecutionId=" << activeExecutionId_
                    << "repetition=" << repetitionIndex
                    << "reason=" << message;
                if (executionId == activeExecutionId_) {
                    currentStateValue_->setToolTip(message);
                }
            });
    connect(&experimentLogService,
            &ExperimentLogService::experimentSummarySaved,
            this,
            &WorkbenchPage::setExperimentSummary);
    connect(&experimentLogService,
            &ExperimentLogService::experimentSummaryProcessingFailed,
            this,
            [this](qint64 executionId, const QString& message) {
                qCWarning(logCompletionReceipt).noquote()
                    << "[工作台][整组结果] 收到处理失败信号"
                    << "executionId=" << executionId
                    << "activeExecutionId=" << activeExecutionId_
                    << "reason=" << message;
                if (executionId == activeExecutionId_) {
                    currentStateValue_->setToolTip(message);
                }
            });
    connect(&resultService_,
            &TestResultService::resultCleared,
            this,
            &WorkbenchPage::clearResults);
    connect(&resultService_,
            &TestResultService::resultUpdated,
            this,
            &WorkbenchPage::setResultComparison);
}

bool WorkbenchPage::isConfigurationLocked() const
{
    return configurationLocked_;
}

void WorkbenchPage::onStartButtonClicked()
{
    if (motionCommandPending_ || homeCommandRequested_
        || resetCommandRequested_) {
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            QStringLiteral("控制命令正在处理中，请稍后再试。"));
        return;
    }
    if (!controllerConnected_) {
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            QStringLiteral("ACS 控制器未连接。"));
        return;
    }
    if (homeRunning_) {
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            QStringLiteral("机器正在回零，不能启动测试。"));
        return;
    }
    if (!homeDone_) {
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            QStringLiteral("机器尚未完成回零，不能启动测试。"));
        return;
    }
    if (motionState_ != 0) {
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            QStringLiteral("机器当前状态为 %1，只有空闲状态 0 可以启动。")
                .arg(motionState_));
        return;
    }
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
    if (motionCommandPending_) {
        QMessageBox::warning(
            this,
            QStringLiteral("停止测试失败"),
            QStringLiteral("控制命令正在处理中，请稍后再试。"));
        return;
    }

    setMotionCommandPending(true);
    stopTest();
}

void WorkbenchPage::onHomeButtonClicked()
{
    if (motionCommandPending_ || homeCommandRequested_
        || resetCommandRequested_) {
        QMessageBox::warning(
            this,
            QStringLiteral("回零失败"),
            QStringLiteral("控制命令正在处理中，请稍后再试。"));
        return;
    }
    if (!controllerConnected_) {
        QMessageBox::warning(
            this,
            QStringLiteral("回零失败"),
            QStringLiteral("ACS 控制器未连接。"));
        return;
    }
    if (homeRunning_) {
        QMessageBox::warning(
            this,
            QStringLiteral("回零失败"),
            QStringLiteral("机器正在回零。"));
        return;
    }
    if (motionState_ != 0) {
        QMessageBox::warning(
            this,
            QStringLiteral("回零失败"),
            QStringLiteral("机器当前状态为 %1，不能回零。")
                .arg(motionState_));
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().homeAxis(&errorMessage)) {
        QMessageBox::warning(
            this, QStringLiteral("回零失败"), errorMessage);
        return;
    }

    homeCommandRequested_ = true;
}

void WorkbenchPage::onResetButtonClicked()
{
    if (motionCommandPending_ || homeCommandRequested_
        || resetCommandRequested_) {
        QMessageBox::warning(
            this,
            QStringLiteral("控制器复位失败"),
            QStringLiteral("控制命令正在处理中，请稍后再试。"));
        return;
    }
    if (!controllerConnected_) {
        QMessageBox::warning(
            this,
            QStringLiteral("控制器复位失败"),
            QStringLiteral("ACS 控制器未连接。"));
        return;
    }
    if (homeRunning_) {
        QMessageBox::warning(
            this,
            QStringLiteral("控制器复位失败"),
            QStringLiteral("机器正在回零，不能复位控制器。"));
        return;
    }
    if (motionState_ != 0) {
        QMessageBox::warning(
            this,
            QStringLiteral("控制器复位失败"),
            QStringLiteral("机器当前状态为 %1，不能复位控制器。")
                .arg(motionState_));
        return;
    }

    const QMessageBox::StandardButton confirmation = QMessageBox::question(
        this,
        QStringLiteral("确认复位控制器"),
        QStringLiteral(
            "复位将重启 ACS 控制器并中断当前连接。\n"
            "重启后必须重新连接并执行回零。\n\n"
            "确定继续吗？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirmation != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!MotionControlService::instance().resetController(&errorMessage)) {
        QMessageBox::warning(
            this, QStringLiteral("控制器复位失败"), errorMessage);
        return;
    }

    resetCommandRequested_ = true;
    updateMotionStateDisplay();
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
}

void WorkbenchPage::setControllerConnected(bool connected, const QString& message)
{
    controllerConnected_ = connected;
    currentStateValue_->setToolTip(message);
    if (!connected) {
        motionCommandPending_ = false;
        homeCommandRequested_ = false;
        homeDone_ = false;
        homeRunning_ = false;
        displacementCard_->setValue(QStringLiteral("-- mm"));
        chartWidget_->start(false);
    }
    updateMotionStateDisplay();
}

void WorkbenchPage::setMotionState(int state, int errorCode)
{
    const int previousState = motionState_;
    motionState_ = state;
    motionErrorCode_ = errorCode;
    if (state != previousState) {
        motionCommandPending_ = false;
    }

    updateMotionStateDisplay();
    chartWidget_->start(controllerConnected_ && (state == 50 || state == 70));
}

void WorkbenchPage::setHomeDone(bool done)
{
    homeDone_ = done;
}

void WorkbenchPage::setHomeRunning(bool running)
{
    homeRunning_ = running;
    updateMotionStateDisplay();
}

void WorkbenchPage::setDisplacement(double positionMillimeters)
{
    displacementCard_->setValue(
        controllerConnected_
            ? QStringLiteral("%1 mm")
                  .arg(positionMillimeters, 0, 'f', 3)
            : QStringLiteral("-- mm"));
}

void WorkbenchPage::setMotionCommandPending(bool pending)
{
    motionCommandPending_ = pending;
}

void WorkbenchPage::updateMotionStateDisplay()
{
    if (!controllerConnected_) {
        currentStateValue_->setText(QStringLiteral("ACS 未连接"));
        return;
    }
    if (resetCommandRequested_) {
        currentStateValue_->setText(QStringLiteral("控制器复位中"));
        return;
    }
    if (homeRunning_) {
        currentStateValue_->setText(QStringLiteral("回零中"));
        return;
    }

    currentStateValue_->setText(
        motionStateText(motionState_, motionErrorCode_));
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

void WorkbenchPage::appendForcePositionSamples(
    const QVector<QPointF>& samples)
{
    if (!chartCollectionActive_ || !configuration_.has_value()) {
        return;
    }

    const TestMotionParameters motionParameters =
        selectedTestMotionParameters(*configuration_);
    const double minimumPosition =
        std::min(motionParameters.acquisitionStartMeters,
                 motionParameters.acquisitionEndMeters);
    const double maximumPosition =
        std::max(motionParameters.acquisitionStartMeters,
                 motionParameters.acquisitionEndMeters);
    for (const QPointF& sample : samples) {
        if (sample.x() >= minimumPosition && sample.x() <= maximumPosition) {
            pendingChartSamples_.append(sample);
        }
    }

    const qsizetype overflow =
        pendingChartSamples_.size() - maximumChartPointCount;
    if (overflow > 0) {
        pendingChartSamples_.remove(0, overflow);
    }
}

void WorkbenchPage::beginChartCollection()
{
    pendingChartSamples_.clear();
    chartCollectionActive_ = true;
}

void WorkbenchPage::commitChartCollection()
{
    if (!chartCollectionActive_) {
        return;
    }

    chartCollectionActive_ = false;
    chartWidget_->replacePoints(eddyForceCurveId(), pendingChartSamples_);
    pendingChartSamples_.clear();
}

void WorkbenchPage::clearChartData()
{
    chartCollectionActive_ = false;
    pendingChartSamples_.clear();
    chartWidget_->clearCurve(eddyForceCurveId());
}

void WorkbenchPage::clearResults()
{
    latestRecordLabel_->setText(QStringLiteral("尚无已完成实验"));
    averageForceCard_->setValue(QStringLiteral("----"));
    eddyForceCoefficientCard_->setValue(QStringLiteral("----"));
    forceRangeCard_->setValue(QStringLiteral("----"));
    fluctuationRateCard_->setValue(QStringLiteral("----"));
    multipleAverageForceCard_->setValue(QStringLiteral("----"));
    multipleAverageForceCoefficientCard_->setValue(QStringLiteral("----"));
}

void WorkbenchPage::setExperimentRecord(const ExperimentRecord& record)
{
    qCInfo(logCompletionReceipt)
        << "[工作台][单次 UI 信号] 收到 experimentRecordSaved"
        << "executionId=" << record.executionId
        << "activeExecutionId=" << activeExecutionId_
        << "repetition=" << record.repetitionIndex
        << "recordId=" << record.id;
    if (record.executionId != activeExecutionId_) {
        qCWarning(logCompletionReceipt)
            << "[工作台][单次 UI 更新] 忽略：executionId 不匹配"
            << "executionId=" << record.executionId
            << "activeExecutionId=" << activeExecutionId_
            << "repetition=" << record.repetitionIndex;
        return;
    }

    latestRecordLabel_->setText(
        QStringLiteral("第 %1 循环 · %2")
            .arg(record.cycleIndex)
            .arg(experimentMotionDirectionDisplayText(record.direction)));

    averageForceCard_->setValue(
        formatAverageForce(record.statistics.averageForceNewtons));
    eddyForceCoefficientCard_->setValue(formatForceCoefficient(
        record.statistics.forceCoefficientNewtonSecondsPerMeter));
    forceRangeCard_->setValue(
        formatForceRange(record.statistics.forceRangeNewtons));
    fluctuationRateCard_->setValue(formatFluctuationRate(
        record.statistics.fluctuationRatePercent));
    qCInfo(logCompletionReceipt)
        << "[工作台][单次 UI 更新] 已更新每次循环结果卡片"
        << "executionId=" << record.executionId
        << "repetition=" << record.repetitionIndex
        << "recordId=" << record.id;
}

void WorkbenchPage::setExperimentSummary(
    const ExperimentSummaryRecord& summary)
{
    qCInfo(logCompletionReceipt)
        << "[工作台][整组 UI 信号] 收到 experimentSummarySaved"
        << "executionId=" << summary.executionId
        << "activeExecutionId=" << activeExecutionId_
        << "summaryId=" << summary.id;
    if (summary.executionId != activeExecutionId_) {
        qCWarning(logCompletionReceipt)
            << "[工作台][整组 UI 更新] 忽略：executionId 不匹配"
            << "executionId=" << summary.executionId
            << "activeExecutionId=" << activeExecutionId_
            << "summaryId=" << summary.id;
        return;
    }

    multipleAverageForceCard_->setValue(formatAverageForce(
        summary.statistics.multipleAverageForceNewtons));
    multipleAverageForceCoefficientCard_->setValue(
        formatForceCoefficient(
            summary.statistics
                .multipleAverageForceCoefficientNewtonSecondsPerMeter));
    qCInfo(logCompletionReceipt)
        << "[工作台][整组 UI 更新] 已更新最终实验结果卡片"
        << "executionId=" << summary.executionId
        << "summaryId=" << summary.id;
}

void WorkbenchPage::setResultComparison(const TestResultComparison& comparison)
{
    averageForceCard_->setValue(forceText(comparison.actual.averageForceNewtons));
    forceRangeCard_->setValue(forceText(comparison.actual.forceRangeNewtons));
    fluctuationRateCard_->setValue(rateText(comparison.actualFluctuationRatePercent));
}
