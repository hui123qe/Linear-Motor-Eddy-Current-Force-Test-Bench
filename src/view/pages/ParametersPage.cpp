#include "ParametersPage.h"

#include "../../config/TestParameters.h"
#include "../../logging/AppLogger.h"
#include "../widgets/StatusPill.h"
#include "../widgets/ViewHelpers.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>

namespace {

QDoubleSpinBox* makeDoubleInput(double value,
                                double minimum,
                                double maximum,
                                const QString& suffix,
                                int decimals = 3)
{
    auto* input = new QDoubleSpinBox;
    input->setRange(minimum, maximum);
    input->setDecimals(decimals);
    input->setSingleStep(decimals > 1 ? 0.01 : 0.1);
    input->setValue(value);
    input->setSuffix(QStringLiteral(" ") + suffix);
    return input;
}

QSpinBox* makeRepeatInput()
{
    auto* input = new QSpinBox;
    input->setRange(1, 999);
    input->setValue(1);
    input->setSuffix(QStringLiteral(" 次"));
    return input;
}

QFrame* makeConstraintBox(const QString& title,
                          const QStringList& items,
                          const QString& level = QStringLiteral("warning"))
{
    auto* box = new QFrame;
    box->setObjectName(QStringLiteral("constraintBox"));
    box->setProperty("level", level);

    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(7);
    layout->addWidget(ViewHelpers::makeLabel(title, "constraintTitle"));
    for (const auto& item : items) {
        layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("- ") + item, "constraintText"));
    }
    return box;
}

void setConstraintState(QFrame* frame, const QString& level)
{
    frame->setProperty("level", level);
    frame->style()->unpolish(frame);
    frame->style()->polish(frame);
}

} // namespace

ParametersPage::ParametersPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto* heading = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("参数设置"), "pageTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(
        QStringLiteral("按测试项填写必要参数，速度和加速度约束在界面侧即时校验"),
        "pageDescription"));
    heading->addLayout(titleBox);
    heading->addStretch();
    saveButton_ = ViewHelpers::makeButton(QStringLiteral("保存配置"));
    resetButton_ = ViewHelpers::makeButton(QStringLiteral("重置配置"));
    confirmButton_ = ViewHelpers::makeButton(QStringLiteral("确定配置"), QStringLiteral("primary"));
    heading->addWidget(saveButton_);
    heading->addWidget(resetButton_);
    heading->addWidget(confirmButton_);
    layout->addLayout(heading);

    connect(saveButton_, &QPushButton::clicked, this, &ParametersPage::saveParameters);
    connect(resetButton_, &QPushButton::clicked, this, &ParametersPage::resetParameters);
    connect(confirmButton_, &QPushButton::clicked, this, &ParametersPage::confirmParameters);

    auto* batchPanel = new QFrame;
    batchPanel->setObjectName(QStringLiteral("globalConfigPanel"));
    auto* batchLayout = new QGridLayout(batchPanel);
    batchLayout->setContentsMargins(14, 12, 14, 12);
    batchLayout->setHorizontalSpacing(14);
    batchLayout->setVerticalSpacing(8);

    auto* batchTitle = ViewHelpers::makeLabel(QStringLiteral("批次设定"), "globalConfigTitle");
    batchLayout->addWidget(batchTitle, 0, 0, 1, 4);

    motorModelInput_ = new QLineEdit(QStringLiteral("HIT-HSM-02"));
    motorModelInput_->setPlaceholderText(QStringLiteral("填写电机型号"));
    specimenIdInput_ = new QLineEdit(QStringLiteral("HIT-EC-017"));
    specimenIdInput_->setPlaceholderText(QStringLiteral("填写试验件编号"));
    batchLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("电机型号"), "fieldLabel"), 1, 0);
    batchLayout->addWidget(motorModelInput_, 1, 1);
    batchLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("试验件编号"), "fieldLabel"), 1, 2);
    batchLayout->addWidget(specimenIdInput_, 1, 3);

    batchLayout->setColumnStretch(1, 2);
    batchLayout->setColumnStretch(3, 2);
    layout->addWidget(batchPanel);

    auto* repeatPanel = new QFrame;
    repeatPanel->setObjectName(QStringLiteral("globalConfigPanel"));
    auto* repeatLayout = new QGridLayout(repeatPanel);
    repeatLayout->setContentsMargins(14, 12, 14, 12);
    repeatLayout->setHorizontalSpacing(14);
    repeatLayout->setVerticalSpacing(8);
    repeatLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("重复次数"), "globalConfigTitle"), 0, 0, 1, 2);
    repeatLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("执行次数"), "fieldLabel"), 1, 0);
    repeatCountInput_ = makeRepeatInput();
    repeatLayout->addWidget(repeatCountInput_, 1, 1);
    repeatLayout->setColumnStretch(1, 1);
    repeatLayout->setColumnStretch(2, 4);
    layout->addWidget(repeatPanel);

    auto* testTypePanel = new QFrame;
    testTypePanel->setObjectName(QStringLiteral("globalConfigPanel"));
    auto* testTypeLayout = new QGridLayout(testTypePanel);
    testTypeLayout->setContentsMargins(14, 12, 14, 12);
    testTypeLayout->setHorizontalSpacing(14);
    testTypeLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("测试项目"), "globalConfigTitle"), 0, 0);

    testTypeInput_ = new QComboBox;
    testTypeInput_->addItem(QStringLiteral("额定速度涡流力测试"),
                            static_cast<int>(EddyCurrentTestType::RatedSpeed));
    testTypeInput_->addItem(QStringLiteral("不同速度涡流力测试"),
                            static_cast<int>(EddyCurrentTestType::VariableSpeed));
    testTypeLayout->addWidget(testTypeInput_, 0, 1);
    testTypeLayout->setColumnStretch(1, 1);
    layout->addWidget(testTypePanel);

    testPages_ = new QStackedWidget;

    auto* ratedTab = new QWidget;
    auto* ratedLayout = new QHBoxLayout(ratedTab);
    ratedLayout->setContentsMargins(14, 14, 14, 14);
    ratedLayout->setSpacing(12);

    auto* ratedFormPanel = ViewHelpers::makePanel(
        QStringLiteral("额定速度涡流力测试"),
        QStringLiteral("根据加速距离计算对称的 PTP 加减速度"));
    auto* ratedFormLayout = qobject_cast<QVBoxLayout*>(ratedFormPanel->layout());
    auto* ratedForm = new QFormLayout;
    ratedForm->setHorizontalSpacing(28);
    ratedForm->setVerticalSpacing(14);
    ratedAccelerationStartInput_ =
        makeDoubleInput(0.0, -1000.0, 1000.0, QStringLiteral("m"));
    ratedAccelerationDistanceInput_ =
        makeDoubleInput(0.5, 0.001, 1000.0, QStringLiteral("m"));
    ratedEndPositionInput_ =
        makeDoubleInput(1.7, -1000.0, 1000.0, QStringLiteral("m"));
    ratedSpeedInput_ = makeDoubleInput(
        3.0, 0.01, kMaximumTestSpeedMetersPerSecond, QStringLiteral("m/s"));
    ratedForm->addRow(QStringLiteral("开始加速位置"), ratedAccelerationStartInput_);
    ratedForm->addRow(QStringLiteral("加速距离"), ratedAccelerationDistanceInput_);
    ratedForm->addRow(QStringLiteral("结束位置"), ratedEndPositionInput_);
    ratedForm->addRow(QStringLiteral("速度"), ratedSpeedInput_);
    ratedFormLayout->addLayout(ratedForm);

    auto* ratedComputedGrid = new QGridLayout;
    ratedComputedGrid->setHorizontalSpacing(12);
    ratedComputedGrid->setVerticalSpacing(8);
    ratedComputedGrid->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("PTP 加速度 / 减速度"), "fieldLabel"),
        0,
        0);
    ratedAccelerationValue_ =
        ViewHelpers::makeLabel(QStringLiteral("-- m/s2"), "computedValue");
    ratedComputedGrid->addWidget(ratedAccelerationValue_, 0, 1);
    ratedComputedGrid->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("执行判定"), "fieldLabel"), 1, 0);
    ratedStatus_ =
        new StatusPill(QStringLiteral("允许执行"), QStringLiteral("ok"), ratedTab);
    ratedComputedGrid->addWidget(ratedStatus_, 1, 1);
    ratedFormLayout->addLayout(ratedComputedGrid);

    ratedHint_ = ViewHelpers::makeLabel(
        QStringLiteral("运动参数满足 PTP 轨迹约束。"), "constraintText");
    ratedFormLayout->addWidget(ratedHint_);
    ratedCheck_ = makeConstraintBox(
        QStringLiteral("读写限制与执行约束"),
        {QStringLiteral("最大速度：5 m/s。"),
         QStringLiteral("最大加速度/减速度：25 m/s2。"),
         QStringLiteral("对称加减速：加速度 = 减速度 = v^2 / (2 x 加速距离)。"),
         QStringLiteral("总行程必须不小于两倍加速距离。")},
        QStringLiteral("ok"));
    ratedFormLayout->addStretch();
    ratedLayout->addWidget(ratedFormPanel, 3);
    ratedLayout->addWidget(ratedCheck_, 2);

    for (QDoubleSpinBox* input : {ratedAccelerationStartInput_,
                                  ratedAccelerationDistanceInput_,
                                  ratedEndPositionInput_,
                                  ratedSpeedInput_}) {
        connect(input,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                &ParametersPage::updateRatedSpeedState);
    }
    updateRatedSpeedState();

    auto* variableTab = new QWidget;
    auto* variableLayout = new QHBoxLayout(variableTab);
    variableLayout->setContentsMargins(14, 14, 14, 14);
    variableLayout->setSpacing(12);

    auto* variableFormPanel = ViewHelpers::makePanel(
        QStringLiteral("不同速度涡流力测试"),
        QStringLiteral("根据加速距离计算对称的 PTP 加减速度"));
    auto* variableFormLayout = qobject_cast<QVBoxLayout*>(variableFormPanel->layout());
    auto* variableForm = new QFormLayout;
    variableForm->setHorizontalSpacing(28);
    variableForm->setVerticalSpacing(14);

    variableAccelerationStartInput_ =
        makeDoubleInput(0.0, -1000.0, 1000.0, QStringLiteral("m"));
    variableAccelerationDistanceInput_ =
        makeDoubleInput(0.5, 0.001, 1000.0, QStringLiteral("m"));
    variableEndPositionInput_ =
        makeDoubleInput(1.7, -1000.0, 1000.0, QStringLiteral("m"));
    variableSpeedInput_ = makeDoubleInput(
        3.0, 0.01, kMaximumTestSpeedMetersPerSecond, QStringLiteral("m/s"));
    variableForm->addRow(QStringLiteral("开始加速位置"), variableAccelerationStartInput_);
    variableForm->addRow(QStringLiteral("加速距离"), variableAccelerationDistanceInput_);
    variableForm->addRow(QStringLiteral("结束位置"), variableEndPositionInput_);
    variableForm->addRow(QStringLiteral("速度"), variableSpeedInput_);
    variableFormLayout->addLayout(variableForm);

    auto* computedGrid = new QGridLayout;
    computedGrid->setHorizontalSpacing(12);
    computedGrid->setVerticalSpacing(8);
    computedGrid->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("PTP 加速度 / 减速度"), "fieldLabel"),
        0,
        0);
    variableAccelerationValue_ =
        ViewHelpers::makeLabel(QStringLiteral("-- m/s2"), "computedValue");
    computedGrid->addWidget(variableAccelerationValue_, 0, 1);
    computedGrid->addWidget(ViewHelpers::makeLabel(QStringLiteral("执行判定"), "fieldLabel"), 1, 0);
    variableStatus_ = new StatusPill(QStringLiteral("允许执行"), QStringLiteral("ok"), variableTab);
    computedGrid->addWidget(variableStatus_, 1, 1);
    variableFormLayout->addLayout(computedGrid);

    variableHint_ = ViewHelpers::makeLabel(
        QStringLiteral("运动参数满足 PTP 轨迹约束。"), "constraintText");
    variableFormLayout->addWidget(variableHint_);
    variableFormLayout->addStretch();
    variableLayout->addWidget(variableFormPanel, 3);

    variableCheck_ = makeConstraintBox(
        QStringLiteral("读写限制与执行约束"),
        {QStringLiteral("最大速度：5 m/s。"),
         QStringLiteral("最大加速度/减速度：25 m/s2。"),
         QStringLiteral("对称加减速：加速度 = 减速度 = v^2 / (2 x 加速距离)。"),
         QStringLiteral("总行程必须不小于两倍加速距离。")},
        QStringLiteral("ok"));
    variableLayout->addWidget(variableCheck_, 2);

    for (QDoubleSpinBox* input : {variableAccelerationStartInput_,
                                  variableAccelerationDistanceInput_,
                                  variableEndPositionInput_,
                                  variableSpeedInput_}) {
        connect(input,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                &ParametersPage::updateVariableState);
    }
    updateVariableState();

    testPages_->addWidget(ratedTab);
    testPages_->addWidget(variableTab);
    connect(testTypeInput_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            testPages_,
            &QStackedWidget::setCurrentIndex);
    layout->addWidget(testPages_, 1);

    auto* footer = makeConstraintBox(
        QStringLiteral("全局说明"),
        {QStringLiteral("本页面只负责参数录入、约束提示和锁定前检查，不直接驱动设备。"),
         QStringLiteral("真实执行前仍应由控制模块再次校验速度、位置、加速度和联锁状态。")},
        QStringLiteral("warning"));
    layout->addWidget(footer);

    setConfigurationLocked(false);
    QTimer::singleShot(0, this, &ParametersPage::loadConfigurationAtStartup);
}

void ParametersPage::updateRatedSpeedState()
{
    updateMotionState(ratedAccelerationStartInput_,
                      ratedAccelerationDistanceInput_,
                      ratedEndPositionInput_,
                      ratedSpeedInput_,
                      ratedAccelerationValue_,
                      ratedStatus_,
                      ratedCheck_,
                      ratedHint_);
}

void ParametersPage::updateVariableState()
{
    updateMotionState(variableAccelerationStartInput_,
                      variableAccelerationDistanceInput_,
                      variableEndPositionInput_,
                      variableSpeedInput_,
                      variableAccelerationValue_,
                      variableStatus_,
                      variableCheck_,
                      variableHint_);
}

void ParametersPage::updateMotionState(QDoubleSpinBox* accelerationStartInput,
                                       QDoubleSpinBox* accelerationDistanceInput,
                                       QDoubleSpinBox* endPositionInput,
                                       QDoubleSpinBox* speedInput,
                                       QLabel* accelerationValue,
                                       StatusPill* status,
                                       QFrame* constraintBox,
                                       QLabel* hint)
{
    TestMotionParameters parameters;
    parameters.accelerationStartMeters = accelerationStartInput->value();
    parameters.accelerationDistanceMeters = accelerationDistanceInput->value();
    parameters.endPositionMeters = endPositionInput->value();
    parameters.speedMetersPerSecond = speedInput->value();

    PtpMotionParameters motionParameters;
    QString errorMessage;
    const bool valid = calculatePtpMotionParameters(
        parameters, &motionParameters, &errorMessage);

    accelerationValue->setText(
        valid
            ? QStringLiteral("%1 m/s2")
                  .arg(motionParameters.accelerationMetersPerSecondSquared, 0, 'f', 3)
            : QStringLiteral("-- m/s2"));
    status->setText(
        valid ? QStringLiteral("允许执行") : QStringLiteral("拒绝执行"));
    status->setLevel(
        valid ? QStringLiteral("ok") : QStringLiteral("danger"));
    setConstraintState(
        constraintBox, valid ? QStringLiteral("ok") : QStringLiteral("danger"));
    hint->setText(
        valid
            ? QStringLiteral("运动参数满足 PTP 轨迹约束。")
            : QStringLiteral("拒绝执行：%1").arg(errorMessage));
}

TestParameters ParametersPage::currentParameters() const
{
    TestParameters parameters;
    parameters.selectedTestType = static_cast<EddyCurrentTestType>(
        testTypeInput_->currentData().toInt());
    parameters.motorModel = motorModelInput_->text().trimmed();
    parameters.specimenId = specimenIdInput_->text().trimmed();
    parameters.repeatCount = repeatCountInput_->value();
    parameters.ratedSpeedTest.accelerationStartMeters =
        ratedAccelerationStartInput_->value();
    parameters.ratedSpeedTest.accelerationDistanceMeters =
        ratedAccelerationDistanceInput_->value();
    parameters.ratedSpeedTest.endPositionMeters = ratedEndPositionInput_->value();
    parameters.ratedSpeedTest.speedMetersPerSecond = ratedSpeedInput_->value();
    parameters.variableSpeedTest.accelerationStartMeters =
        variableAccelerationStartInput_->value();
    parameters.variableSpeedTest.accelerationDistanceMeters =
        variableAccelerationDistanceInput_->value();
    parameters.variableSpeedTest.endPositionMeters =
        variableEndPositionInput_->value();
    parameters.variableSpeedTest.speedMetersPerSecond =
        variableSpeedInput_->value();
    return parameters;
}

void ParametersPage::setConfiguration(const TestParameters& parameters)
{
    const int testTypeIndex = testTypeInput_->findData(
        static_cast<int>(parameters.selectedTestType));
    if (testTypeIndex >= 0) {
        testTypeInput_->setCurrentIndex(testTypeIndex);
    }
    motorModelInput_->setText(parameters.motorModel);
    specimenIdInput_->setText(parameters.specimenId);
    repeatCountInput_->setValue(parameters.repeatCount);
    ratedAccelerationStartInput_->setValue(
        parameters.ratedSpeedTest.accelerationStartMeters);
    ratedAccelerationDistanceInput_->setValue(
        parameters.ratedSpeedTest.accelerationDistanceMeters);
    ratedEndPositionInput_->setValue(parameters.ratedSpeedTest.endPositionMeters);
    ratedSpeedInput_->setValue(parameters.ratedSpeedTest.speedMetersPerSecond);
    variableAccelerationStartInput_->setValue(
        parameters.variableSpeedTest.accelerationStartMeters);
    variableAccelerationDistanceInput_->setValue(
        parameters.variableSpeedTest.accelerationDistanceMeters);
    variableEndPositionInput_->setValue(
        parameters.variableSpeedTest.endPositionMeters);
    variableSpeedInput_->setValue(
        parameters.variableSpeedTest.speedMetersPerSecond);
}

void ParametersPage::saveParameters()
{
    if (configurationLocked_) {
        return;
    }

    QString errorMessage;
    if (!configurationService_.saveConfiguration(
            currentParameters(), &errorMessage)) {
        qCWarning(logConfiguration).noquote()
            << "保存测试配置失败：" << errorMessage;
        QMessageBox::warning(this, QStringLiteral("保存配置失败"), errorMessage);
        return;
    }

    publishDisplayConfiguration();
    qCInfo(logConfiguration).noquote()
        << "测试配置已保存" << configurationService_.configurationFilePath();
    QMessageBox::information(
        this,
        QStringLiteral("保存配置"),
        QStringLiteral("配置已保存到：\n%1")
            .arg(configurationService_.configurationFilePath()));
}

void ParametersPage::confirmParameters()
{
    const TestParameters parameters = currentParameters();
    QString errorMessage;
    if (!configurationService_.lockConfiguration(parameters, &errorMessage)) {
        qCWarning(logConfiguration).noquote()
            << "锁定测试配置失败：" << errorMessage;
        QMessageBox::warning(this, QStringLiteral("确定配置失败"), errorMessage);
        return;
    }

    setConfigurationLocked(true);
    publishDisplayConfiguration();
    emit configurationLockChanged(true);
    qCInfo(logConfiguration).noquote()
        << "测试配置已锁定，批次" << testBatchName(parameters);
}

void ParametersPage::resetParameters()
{
    if (!configurationLocked_) {
        return;
    }

    configurationService_.unlockConfiguration();
    setConfigurationLocked(false);
    publishDisplayConfiguration();
    emit configurationLockChanged(false);
    qCInfo(logConfiguration) << "测试配置已解锁";
}

void ParametersPage::loadConfigurationAtStartup()
{
    QString errorMessage;
    if (!configurationService_.loadSavedConfiguration(&errorMessage)) {
        qCWarning(logConfiguration).noquote()
            << "自动加载测试配置失败：" << errorMessage;
        emit configurationCleared();
        QMessageBox::warning(
            this,
            QStringLiteral("自动加载配置失败"),
            QStringLiteral("未能从以下文件加载配置，工作台将显示 ----：\n%1\n\n%2")
                .arg(configurationService_.configurationFilePath(), errorMessage));
        return;
    }

    const std::optional<TestParameters> savedConfiguration =
        configurationService_.savedConfiguration();
    if (savedConfiguration.has_value()) {
        setConfiguration(*savedConfiguration);
        qCInfo(logConfiguration).noquote()
            << "测试配置已加载" << configurationService_.configurationFilePath();
    } else {
        qCInfo(logConfiguration).noquote()
            << "未找到已保存的测试配置"
            << configurationService_.configurationFilePath();
    }
    publishDisplayConfiguration();
}

void ParametersPage::publishDisplayConfiguration()
{
    const std::optional<TestParameters> configuration =
        configurationService_.displayConfiguration();
    if (configuration.has_value()) {
        emit configurationChanged(*configuration);
        return;
    }

    emit configurationCleared();
}

void ParametersPage::setConfigurationLocked(bool locked)
{
    configurationLocked_ = locked;
    const bool editable = !locked;

    motorModelInput_->setEnabled(editable);
    specimenIdInput_->setEnabled(editable);
    repeatCountInput_->setEnabled(editable);
    testTypeInput_->setEnabled(editable);
    testPages_->setEnabled(editable);
    saveButton_->setEnabled(editable);
    confirmButton_->setEnabled(editable);
    resetButton_->setEnabled(locked);
}
