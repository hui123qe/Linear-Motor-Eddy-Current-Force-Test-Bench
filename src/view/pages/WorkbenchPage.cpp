#include "WorkbenchPage.h"

#include "../../acquisition/DataAcquisitionService.h"
#include "../../database/AcquisitionDatabaseService.h"
#include "../../experimentlog/ExperimentLogService.h"
#include "../../experimentlog/ExperimentResultCalculator.h"
#include "../../logging/AppLogger.h"
#include "../../machine/MachineStateManager.h"
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

#include <limits>
#include <optional>

namespace {

constexpr int maximumChartPointCount = std::numeric_limits<int>::max();

void setResultIndicator(QLabel* indicator, const std::optional<bool>& passed)
{
    const QString color = !passed.has_value() ? QStringLiteral("#94a3b8")
        : *passed ? QStringLiteral("#16a34a") : QStringLiteral("#dc2626");
    indicator->setText(!passed.has_value() ? QStringLiteral("待测")
        : *passed ? QStringLiteral("合格") : QStringLiteral("不合格"));
    indicator->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; color: white; border-radius: 26px; font-weight: 600; }")
                                .arg(color));
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
    auto* form = new QGridLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(12);
    form->addWidget(new QLabel(QStringLiteral("目标值"), &dialog), 0, 1);
    form->addWidget(new QLabel(QStringLiteral("允许偏差"), &dialog), 0, 3);

    TestResultTargets defaults;
    QString loadError;
    if (!TestResultService::instance().loadSavedTargets(&defaults, &loadError)) {
        QMessageBox::warning(parent, QStringLiteral("读取测试目标失败"), loadError);
    }
    auto* averageInput = new QDoubleSpinBox(&dialog);
    averageInput->setRange(0.001, 1000000.0);
    averageInput->setDecimals(3);
    averageInput->setValue(defaults.averageForceNewtons);
    auto* averageToleranceInput = new QDoubleSpinBox(&dialog);
    averageToleranceInput->setRange(0.0, 1000000.0);
    averageToleranceInput->setDecimals(3);
    averageToleranceInput->setValue(defaults.averageForceToleranceNewtons);
    form->addWidget(new QLabel(QStringLiteral("平均涡流力"), &dialog), 1, 0);
    form->addWidget(averageInput, 1, 1);
    form->addWidget(new QLabel(QStringLiteral("±"), &dialog), 1, 2);
    form->addWidget(averageToleranceInput, 1, 3);
    form->addWidget(new QLabel(QStringLiteral("N"), &dialog), 1, 4);

    auto* coefficientInput = new QDoubleSpinBox(&dialog);
    coefficientInput->setRange(0.0, 1000000.0);
    coefficientInput->setDecimals(3);
    coefficientInput->setValue(defaults.forceCoefficientNewtonSecondsPerMeter);
    auto* coefficientToleranceInput = new QDoubleSpinBox(&dialog);
    coefficientToleranceInput->setRange(0.0, 1000000.0);
    coefficientToleranceInput->setDecimals(3);
    coefficientToleranceInput->setValue(defaults.forceCoefficientToleranceNewtonSecondsPerMeter);
    form->addWidget(new QLabel(QStringLiteral("平均涡流力系数"), &dialog), 2, 0);
    form->addWidget(coefficientInput, 2, 1);
    form->addWidget(new QLabel(QStringLiteral("±"), &dialog), 2, 2);
    form->addWidget(coefficientToleranceInput, 2, 3);
    form->addWidget(new QLabel(QStringLiteral("N·s/m"), &dialog), 2, 4);
    for (QDoubleSpinBox* input : {averageInput, coefficientInput,
                                  averageToleranceInput, coefficientToleranceInput}) {
        input->setFixedWidth(170);
        input->setMinimumHeight(36);
        input->setAlignment(Qt::AlignRight);
        const bool tolerance = input == averageToleranceInput || input == coefficientToleranceInput;
        input->setStyleSheet(tolerance
            ? QStringLiteral("QDoubleSpinBox { background: #f3f4f6; color: #64748b; }")
            : QStringLiteral("QDoubleSpinBox { background: #eff6ff; color: #1e40af; font-weight: 600; }"));
    }
    layout->addLayout(form);

    const auto readTargets = [&] {
        TestResultTargets targets;
        targets.averageForceNewtons = averageInput->value();
        targets.averageForceToleranceNewtons = averageToleranceInput->value();
        targets.forceCoefficientNewtonSecondsPerMeter = coefficientInput->value();
        targets.forceCoefficientToleranceNewtonSecondsPerMeter = coefficientToleranceInput->value();
        return targets;
    };
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton* saveButton = buttons->addButton(
        QStringLiteral("保存"), QDialogButtonBox::ActionRole);
    saveButton->setAutoDefault(false);
    QObject::connect(saveButton, &QPushButton::clicked, &dialog, [&] {
        QString saveError;
        if (!TestResultService::instance().saveTargets(readTargets(), &saveError)) {
            QMessageBox::warning(&dialog, QStringLiteral("保存测试目标失败"), saveError);
            return;
        }
        QMessageBox::information(&dialog, QStringLiteral("保存测试目标"),
                                 QStringLiteral("已保存，下次打开时自动读取。"));
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return readTargets();
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
    currentStateValue_->setText(QStringLiteral("ACS 未连接"));
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
        QStringLiteral("测试曲线"),
        QStringLiteral("各次正反向曲线同屏显示，整组结束后可缩放查看"));
    curvePanel->setObjectName(QStringLiteral("curvePanel"));
    auto* curveLayout = qobject_cast<QVBoxLayout*>(curvePanel->layout());
    chartWidget_ = new ChartWidget(curvePanel);
    chartWidget_->setXAxisMode(ChartXAxisMode::Numeric);
    chartWidget_->setLegendVisible(false);
    chartWidget_->setAxisLabels(QStringLiteral("位移 / m"),
                                QStringLiteral("涡流力 / N"));

    chartWidget_->setInteractionEnabled(false);
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
    forceResultIndicator_ = new QLabel;
    coefficientResultIndicator_ = new QLabel;
    for (QLabel* indicator : {forceResultIndicator_, coefficientResultIndicator_}) {
        indicator->setFixedSize(52, 52);
        indicator->setAlignment(Qt::AlignCenter);
        setResultIndicator(indicator, std::nullopt);
    }
    finalResultGrid->addWidget(forceResultIndicator_, 0, 1);
    finalResultGrid->addWidget(coefficientResultIndicator_, 1, 1);
    finalResultGrid->setColumnStretch(0, 1);
    finalResultLayout->addLayout(finalResultGrid);
    targetComparisonLabel_ = ViewHelpers::makeLabel(
        QStringLiteral("目标判定：待测试"), "fieldLabel");
    targetComparisonLabel_->setWordWrap(true);
    finalResultLayout->addWidget(targetComparisonLabel_);
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
            &MotionControlService::homeRunningChanged,
            this,
            &WorkbenchPage::setHomeRunning);
    connect(&motionControlService,
            &MotionControlService::maintenanceCommandFailed,
            this,
            [this](MaintenanceCommand command, const QString& message) {
                if (command != MaintenanceCommand::HomeAxis) {
                    return;
                }
                QMessageBox::warning(
                    this, QStringLiteral("回零失败"), message);
            });
    connect(&motionControlService,
            &MotionControlService::controllerResetCompleted,
            this,
            [this] {
                QMessageBox::information(
                    this,
                    QStringLiteral("复位"),
                    QStringLiteral("复位完成。"));
            });
    connect(&motionControlService,
            &MotionControlService::controllerResetFailed,
            this,
            [this](const QString& message) {
                QMessageBox::warning(
                    this, QStringLiteral("控制器复位失败"), message);
            });
    connect(&motionControlService,
            &MotionControlService::stopRequestWritten,
            this,
            [this] {
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
                if (!ready) {
                    currentStateValue_->setToolTip(message);
                }
            });
    connect(&dataAcquisitionService,
            &DataAcquisitionService::collectionStarted,
            this,
            &WorkbenchPage::beginChartCollection);
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

    MachineStateManager& machineStateManager = MachineStateManager::instance();
    connect(&machineStateManager,
            &MachineStateManager::machineStateChanged,
            this,
            &WorkbenchPage::updateMachineStateDisplay);
    TestExecutionService& executionService = TestExecutionService::instance();
    connect(&executionService,
            &TestExecutionService::experimentFinalized,
            this,
            &WorkbenchPage::commitChartCollection);
    connect(&executionService,
            &TestExecutionService::experimentGroupFinalized,
            this,
            &WorkbenchPage::finishChartCollection);
    connect(&executionService,
            &TestExecutionService::chartSamplesReady,
            this,
            &WorkbenchPage::appendForcePositionSamples);
    connect(&executionService,
            &TestExecutionService::executionStarted,
            this,
            [this](qint64 executionId, const QString&) {
                activeExecutionId_ = executionId;
                clearChartData();
                qCInfo(logCompletionReceipt)
                    << "[工作台][执行上下文] 设置 activeExecutionId"
                    << "executionId=" << activeExecutionId_;
                clearResults();
            });
    connect(&executionService,
            &TestExecutionService::executionFailed,
            this,
            [this](const QString& message) {
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
    TestResultService& resultService = TestResultService::instance();
    connect(&resultService,
            &TestResultService::resultCleared,
            this,
            &WorkbenchPage::clearResults);
    connect(&resultService,
            &TestResultService::resultUpdated,
            this,
            &WorkbenchPage::setResultComparison);
}

void WorkbenchPage::onStartButtonClicked()
{
    QString errorMessage;
    if (!TestExecutionService::instance().canStart(&errorMessage)) {
        QMessageBox::warning(
            this,
            QStringLiteral("启动测试失败"),
            errorMessage);
        return;
    }

    const std::optional<TestResultTargets> targets = requestTestResultTargets(this);
    if (targets.has_value()) {
        beginTest(*targets);
    }
}

void WorkbenchPage::onStopButtonClicked()
{
    stopTest();
}

void WorkbenchPage::onHomeButtonClicked()
{
    QString errorMessage;
    if (!MotionControlService::instance().homeAxis(&errorMessage)) {
        QMessageBox::warning(
            this, QStringLiteral("回零失败"), errorMessage);
    }
}

void WorkbenchPage::onResetButtonClicked()
{
    TestExecutionService& executionService = TestExecutionService::instance();
    const QMessageBox::StandardButton confirmation = QMessageBox::question(
        this,
        QStringLiteral("确认复位"),
        QStringLiteral("确定复位？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirmation != QMessageBox::Yes) {
        return;
    }

    QString errorMessage;
    if (!executionService.reset(&errorMessage)) {
        QMessageBox::warning(
            this, QStringLiteral("控制器复位失败"), errorMessage);
    }
}

void WorkbenchPage::setConfiguration(const TestParameters& parameters)
{
    testItemCard_->setValue(testTypeText(parameters.selectedTestType));

    const QString batchName = testBatchName(parameters);
    batchCard_->setValue(batchName.isEmpty() ? QStringLiteral("----") : batchName);
}

void WorkbenchPage::clearConfiguration()
{
    testItemCard_->setValue(QStringLiteral("----"));
    batchCard_->setValue(QStringLiteral("----"));
}

void WorkbenchPage::setConfigurationLocked(bool locked)
{
    configurationStatus_->setText(
        locked ? QStringLiteral("参数已锁定") : QStringLiteral("参数未锁定"));
    configurationStatus_->setLevel(
        locked ? QStringLiteral("ok") : QStringLiteral("neutral"));
}

void WorkbenchPage::setControllerConnected(bool connected, const QString& message)
{
    currentStateValue_->setToolTip(message);
    if (!connected) {
        currentStateValue_->setText(QStringLiteral("ACS 未连接"));
        displacementCard_->setValue(QStringLiteral("-- mm"));
        return;
    }

    currentStateValue_->setText(QStringLiteral("等待 ACS 状态"));
}

void WorkbenchPage::setMotionState(int state, int errorCode)
{
    currentStateValue_->setText(motionStateText(state, errorCode));
}

void WorkbenchPage::setHomeRunning(bool running)
{
    if (running) {
        currentStateValue_->setText(QStringLiteral("回零中"));
        return;
    }

    const MachineStateManager& machineStateManager =
        MachineStateManager::instance();
    updateMachineStateDisplay(machineStateManager.state(),
                              machineStateManager.reason());
}

void WorkbenchPage::setDisplacement(double positionMillimeters)
{
    displacementCard_->setValue(
        QStringLiteral("%1 mm").arg(positionMillimeters, 0, 'f', 3));
}

void WorkbenchPage::updateMachineStateDisplay(
    MachineState state,
    const QString& reason)
{
    currentStateValue_->setToolTip(reason);
    switch (state) {
    case MachineState::Error:
        currentStateValue_->setText(QStringLiteral("故障"));
        break;
    case MachineState::Idle:
        currentStateValue_->setText(QStringLiteral("空闲"));
        break;
    case MachineState::Running:
        currentStateValue_->setText(QStringLiteral("运行"));
        break;
    case MachineState::Resetting:
        currentStateValue_->setText(QStringLiteral("复位中"));
        break;
    }
}

void WorkbenchPage::beginTest(const TestResultTargets& targets)
{
    QString errorMessage;
    if (!TestExecutionService::instance().start(targets, &errorMessage)) {
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

    QMessageBox::warning(
        this, QStringLiteral("停止测试失败"), errorMessage);
}

void WorkbenchPage::appendForcePositionSamples(
    const QVector<QPointF>& samples)
{
    if (!chartCollectionActive_) {
        return;
    }
    for (const QPointF& sample : samples) {
        pendingChartSamples_.append(sample);
    }
}

void WorkbenchPage::beginChartCollection()
{
    pendingChartSamples_.clear();
    chartCollectionActive_ = true;
}

void WorkbenchPage::commitChartCollection(const ExperimentFinalContext& context)
{
    if (!chartCollectionActive_) {
        return;
    }

    chartCollectionActive_ = false;

    ChartCurveConfig curve;
    curve.id = QStringLiteral("eddy_force_%1").arg(context.repetitionIndex);
    curve.displayName = QStringLiteral("第 %1 次·%2")
                            .arg(context.cycleIndex)
                            .arg(experimentMotionDirectionDisplayText(context.direction));
    curve.color = context.direction == ExperimentMotionDirection::Forward
        ? QColor(QStringLiteral("#1f78d1"))
        : QColor(QStringLiteral("#e67e22"));
    curve.maximumPointCount = maximumChartPointCount;
    chartWidget_->addCurve(curve);
    chartCurveIds_.append(curve.id);
    chartWidget_->replacePoints(curve.id, pendingChartSamples_);
    pendingChartSamples_.clear();
}

void WorkbenchPage::finishChartCollection()
{
    chartCollectionActive_ = false;
    chartWidget_->setInteractionEnabled(true);
}

void WorkbenchPage::clearChartData()
{
    chartCollectionActive_ = false;
    pendingChartSamples_.clear();
    for (const QString& curveId : chartCurveIds_) {
        chartWidget_->removeCurve(curveId);
    }
    chartCurveIds_.clear();
    chartWidget_->setInteractionEnabled(false);
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
    targetComparisonLabel_->setText(QStringLiteral("目标判定：待测试"));
    setResultIndicator(forceResultIndicator_, std::nullopt);
    setResultIndicator(coefficientResultIndicator_, std::nullopt);
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
    const std::optional<TestResultComparison> comparison =
        ExperimentResultCalculator::compareSummary(summary);
    if (comparison.has_value()) {
        setResultComparison(*comparison);
    } else {
        targetComparisonLabel_->setText(QStringLiteral("目标判定：未完成或无有效结果"));
        setResultIndicator(forceResultIndicator_, std::nullopt);
        setResultIndicator(coefficientResultIndicator_, std::nullopt);
    }
    qCInfo(logCompletionReceipt)
        << "[工作台][整组 UI 更新] 已更新最终实验结果卡片"
        << "executionId=" << summary.executionId
        << "summaryId=" << summary.id;
}

void WorkbenchPage::setResultComparison(const TestResultComparison& comparison)
{
    setResultIndicator(forceResultIndicator_, comparison.averageForcePassed);
    setResultIndicator(coefficientResultIndicator_, comparison.forceCoefficientPassed);
    targetComparisonLabel_->setText(
        QStringLiteral("平均涡流力：%1（目标 %2 ± %3 N）\n"
                       "平均涡流力系数：%4（目标 %5 ± %6 N·s/m）")
            .arg(comparison.averageForcePassed ? QStringLiteral("合格") : QStringLiteral("不合格"))
            .arg(comparison.targets.averageForceNewtons, 0, 'f', 3)
            .arg(comparison.targets.averageForceToleranceNewtons, 0, 'f', 3)
            .arg(comparison.forceCoefficientPassed ? QStringLiteral("合格") : QStringLiteral("不合格"))
            .arg(comparison.targets.forceCoefficientNewtonSecondsPerMeter, 0, 'f', 3)
            .arg(comparison.targets.forceCoefficientToleranceNewtonSecondsPerMeter, 0, 'f', 3));
}
