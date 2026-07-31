#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(logApplication)
Q_DECLARE_LOGGING_CATEGORY(logMotion)
Q_DECLARE_LOGGING_CATEGORY(logAcquisition)
Q_DECLARE_LOGGING_CATEGORY(logDatabase)
Q_DECLARE_LOGGING_CATEGORY(logConfiguration)
Q_DECLARE_LOGGING_CATEGORY(logExport)
Q_DECLARE_LOGGING_CATEGORY(logCompletionReceipt)

namespace AppLogging {

[[nodiscard]] bool initialize(QString* errorMessage = nullptr);
void shutdown();
[[nodiscard]] QString logFilePath();

} // namespace AppLogging
