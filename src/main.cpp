#include "acquisition/DataAcquisitionService.h"
#include "database/AcquisitionDatabaseService.h"
#include "experimentlog/ExperimentExportService.h"
#include "experimentlog/ExperimentLogService.h"
#include "logging/AppLogger.h"
#include "machine/MachineStateManager.h"
#include "motion/MotionControlService.h"
#include "safety/SafetyMonitor.h"
#include "view/MainWindow.h"
#include "workflow/TestExecutionService.h"
#include "temperature/TemperatureDeviceService.h"
#include "alarm/AlarmService.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QMessageBox>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("高速涡流测试台"));
    application.setOrganizationName(QStringLiteral("哈尔滨工业大学"));
    application.setApplicationVersion(QStringLiteral("1.0.0"));
    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));

    QString loggingError;
    if (!AppLogging::initialize(&loggingError)) {
        QMessageBox::warning(
            nullptr, QStringLiteral("日志初始化失败"), loggingError);
    }
    qCInfo(logApplication).noquote()
        << "应用启动，版本" << application.applicationVersion();
    if (!AppLogging::logFilePath().isEmpty()) {
        qCInfo(logApplication).noquote()
            << "日志文件" << AppLogging::logFilePath();
    }

    QFile styleFile(QStringLiteral(":/styles.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    } else {
        qCWarning(logApplication).noquote()
            << "界面样式加载失败" << styleFile.errorString();
    }

    ExperimentLogService::instance();
    ExperimentExportService::instance();
    MainWindow window;
    MotionControlService& motionControlService = MotionControlService::instance();
    DataAcquisitionService& dataAcquisitionService =
        DataAcquisitionService::instance();
    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();
    SafetyMonitor& safetyMonitor = SafetyMonitor::instance();
    MachineStateManager& machineStateManager = MachineStateManager::instance();
    TestExecutionService& executionService = TestExecutionService::instance();
    qCInfo(logApplication) << "初始化 ACS 电机服务";
    motionControlService.initialize();
    safetyMonitor.initialize(motionControlService.acsClient());
    machineStateManager.initialize(&safetyMonitor);
    TemperatureDeviceService& temperatureService = TemperatureDeviceService::instance();
    AlarmService& alarmService = AlarmService::instance();
    QObject::connect(&alarmService, &AlarmService::alarmRaised,
                     &motionControlService, [&motionControlService](const AlarmRecord& record) {
        if (record.code == QStringLiteral("TEMP_FAULT")) {
            static_cast<void>(motionControlService.emergencyStop());
        }
    }, Qt::DirectConnection);
    alarmService.initialize(&temperatureService, &safetyMonitor, motionControlService.acsClient());
    temperatureService.initialize();
    QObject::connect(&executionService, &TestExecutionService::executionFinished,
                     &alarmService, [&alarmService] { alarmService.setExperimentName({}); });
    QObject::connect(&executionService, &TestExecutionService::executionStopped,
                     &alarmService, [&alarmService] { alarmService.setExperimentName({}); });
    dataAcquisitionService.initialize(motionControlService.acsClient());
    databaseService.initialize();
    motionControlService.connectController();
    window.show();

    const int exitCode = application.exec();
    qCInfo(logApplication) << "应用事件循环结束，退出码" << exitCode;
    executionService.shutdown();
    temperatureService.shutdown();
    alarmService.shutdown();
    dataAcquisitionService.shutdown();
    ExperimentExportService::instance().shutdown();
    databaseService.shutdown();
    machineStateManager.shutdown();
    safetyMonitor.shutdown();
    motionControlService.shutdown();
    qCInfo(logApplication) << "应用退出";
    AppLogging::shutdown();
    return exitCode;
}
