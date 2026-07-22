#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(logApplication)
Q_DECLARE_LOGGING_CATEGORY(logMotion)
Q_DECLARE_LOGGING_CATEGORY(logConfiguration)

namespace AppLogging {

[[nodiscard]] bool initialize(QString* errorMessage = nullptr);
void shutdown();
[[nodiscard]] QString logFilePath();

} // namespace AppLogging
