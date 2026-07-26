#include "AppLogger.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

Q_LOGGING_CATEGORY(logApplication, "eddy.application")
Q_LOGGING_CATEGORY(logMotion, "eddy.motion")
Q_LOGGING_CATEGORY(logAcquisition, "eddy.acquisition")
Q_LOGGING_CATEGORY(logDatabase, "eddy.database")
Q_LOGGING_CATEGORY(logConfiguration, "eddy.configuration")

namespace {

struct LogState
{
    QMutex mutex;
    QFile file;
    QString filePath;
    QtMessageHandler previousHandler = nullptr;
    bool installed = false;
};

LogState& logState()
{
    static LogState state;
    return state;
}

QString messageLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("UNKNOWN");
}

QString sourceText(const QMessageLogContext& context)
{
    if (context.file == nullptr || context.line <= 0) {
        return QStringLiteral("-");
    }

    return QStringLiteral("%1:%2")
        .arg(QFileInfo(QString::fromUtf8(context.file)).fileName())
        .arg(context.line);
}

void fileMessageHandler(QtMsgType type,
                        const QMessageLogContext& context,
                        const QString& message)
{
    LogState& state = logState();
    const quintptr threadId =
        reinterpret_cast<quintptr>(QThread::currentThreadId());
    const QString category = context.category != nullptr
                                 ? QString::fromUtf8(context.category)
                                 : QStringLiteral("default");
    const QString line = QStringLiteral("%1 [%2] [thread 0x%3] [%4] [%5] %6\n")
                             .arg(QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  messageLevel(type),
                                  QString::number(threadId, 16),
                                  category,
                                  sourceText(context),
                                  message);

    QtMessageHandler previousHandler = nullptr;
    {
        QMutexLocker locker(&state.mutex);
        if (state.file.isOpen()) {
            state.file.write(line.toUtf8());
            state.file.flush();
        }
        previousHandler = state.previousHandler;
    }

    if (previousHandler != nullptr) {
        previousHandler(type, context, message);
    }
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

} // namespace

namespace AppLogging {

bool initialize(QString* errorMessage)
{
    LogState& state = logState();
    QMutexLocker locker(&state.mutex);
    if (state.installed) {
        return true;
    }

    QDir applicationDirectory(QCoreApplication::applicationDirPath());
    if (!applicationDirectory.mkpath(QStringLiteral("logs"))) {
        setError(
            errorMessage,
            QStringLiteral("无法创建日志目录：%1")
                .arg(applicationDirectory.filePath(QStringLiteral("logs"))));
        return false;
    }

    QDir logDirectory(applicationDirectory.filePath(QStringLiteral("logs")));
    state.filePath = logDirectory.filePath(
        QStringLiteral("高速涡流测试台-%1.log")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
    state.file.setFileName(state.filePath);
    if (!state.file.open(QIODevice::WriteOnly| QIODevice::Text)) {
        setError(
            errorMessage,
            QStringLiteral("无法打开日志文件：%1\n%2")
                .arg(state.filePath, state.file.errorString()));
        state.filePath.clear();
        return false;
    }

    state.previousHandler = qInstallMessageHandler(fileMessageHandler);
    state.installed = true;
    return true;
}

void shutdown()
{
    LogState& state = logState();
    QMutexLocker locker(&state.mutex);
    if (!state.installed) {
        return;
    }

    qInstallMessageHandler(state.previousHandler);
    state.previousHandler = nullptr;
    state.installed = false;
    state.file.flush();
    state.file.close();
}

QString logFilePath()
{
    LogState& state = logState();
    QMutexLocker locker(&state.mutex);
    return state.filePath;
}

} // namespace AppLogging
