#pragma once

#include "../../config/TestConfigurationService.h"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class StatusPill;
struct TestParameters;

class ParametersPage final : public QWidget
{
    Q_OBJECT

public:
    explicit ParametersPage(QWidget* parent = nullptr);

public slots:
    void setConfiguration(const TestParameters& parameters);
    void setConfigurationLocked(bool locked);

signals:
    void configurationChanged(const TestParameters& parameters);
    void configurationCleared();
    void configurationLockChanged(bool locked);

private slots:
    void updateRatedSpeedState();
    void updateVariableState();

private:
    [[nodiscard]] TestParameters currentParameters() const;
    void initializePageHeader(QVBoxLayout* pageLayout);
    void initializeBatchArea(QVBoxLayout* pageLayout);
    void initializeRepeatArea(QVBoxLayout* pageLayout);
    void initializeTestTypeArea(QVBoxLayout* pageLayout);
    void initializeTestPages(QVBoxLayout* pageLayout);
    [[nodiscard]] QWidget* createStandardSpecimenPage();
    [[nodiscard]] QWidget* createNonStandardSpecimenPage();
    void initializeFooter(QVBoxLayout* pageLayout);
    void initializeConnections();
    void loadConfigurationAtStartup();
    void publishDisplayConfiguration();
    void saveParameters();
    void confirmParameters();
    void resetParameters();
    void updateMotionState(QDoubleSpinBox* accelerationStartInput,
                           QDoubleSpinBox* accelerationDistanceInput,
                           QDoubleSpinBox* endPositionInput,
                           QDoubleSpinBox* speedInput,
                           QLabel* accelerationValue,
                           StatusPill* status,
                           QFrame* constraintBox,
                           QLabel* hint);

    QPushButton* saveButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
    QPushButton* confirmButton_ = nullptr;
    QLineEdit* motorModelInput_ = nullptr;
    QLineEdit* specimenIdInput_ = nullptr;
    QComboBox* testTypeInput_ = nullptr;
    QStackedWidget* testPages_ = nullptr;
    QSpinBox* repeatCountInput_ = nullptr;
    QDoubleSpinBox* ratedAccelerationStartInput_ = nullptr;
    QDoubleSpinBox* ratedAccelerationDistanceInput_ = nullptr;
    QDoubleSpinBox* ratedEndPositionInput_ = nullptr;
    QDoubleSpinBox* ratedSpeedInput_ = nullptr;
    QDoubleSpinBox* ratedAcquisitionStartInput_ = nullptr;
    QDoubleSpinBox* ratedAcquisitionEndInput_ = nullptr;
    QLabel* ratedAccelerationValue_ = nullptr;
    StatusPill* ratedStatus_ = nullptr;
    QFrame* ratedCheck_ = nullptr;
    QLabel* ratedHint_ = nullptr;
    QDoubleSpinBox* variableAccelerationStartInput_ = nullptr;
    QDoubleSpinBox* variableAccelerationDistanceInput_ = nullptr;
    QDoubleSpinBox* variableEndPositionInput_ = nullptr;
    QDoubleSpinBox* variableSpeedInput_ = nullptr;
    QDoubleSpinBox* variableAcquisitionStartInput_ = nullptr;
    QDoubleSpinBox* variableAcquisitionEndInput_ = nullptr;
    QLabel* variableAccelerationValue_ = nullptr;
    StatusPill* variableStatus_ = nullptr;
    QFrame* variableCheck_ = nullptr;
    QLabel* variableHint_ = nullptr;
    TestConfigurationService configurationService_;
    bool configurationLocked_ = false;
};
