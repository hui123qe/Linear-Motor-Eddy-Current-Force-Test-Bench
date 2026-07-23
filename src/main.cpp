#include "acquisition/DataAcquisitionService.h"
#include "database/AcquisitionDatabaseService.h"
#include "logging/AppLogger.h"
#include "motion/MotionControlService.h"
#include "view/MainWindow.h"
#include "workflow/TestExecutionService.h"

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

    MainWindow window;
    MotionControlService& motionControlService = MotionControlService::instance();
    DataAcquisitionService& dataAcquisitionService =
        DataAcquisitionService::instance();
    AcquisitionDatabaseService& databaseService =
        AcquisitionDatabaseService::instance();
    TestExecutionService& executionService = TestExecutionService::instance();
    qCInfo(logApplication) << "初始化 ACS 电机服务";
    motionControlService.initialize();
    dataAcquisitionService.initialize(motionControlService.acsClient());
    databaseService.initialize();
    motionControlService.connectController();
    window.show();

    const int exitCode = application.exec();
    qCInfo(logApplication) << "应用事件循环结束，退出码" << exitCode;
    executionService.shutdown();
    dataAcquisitionService.shutdown();
    databaseService.shutdown();
    motionControlService.shutdown();
    qCInfo(logApplication) << "应用退出";
    AppLogging::shutdown();
    return exitCode;
}
