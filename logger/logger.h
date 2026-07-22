#pragma once

#include "base_logger.h"
#include "registry.h"

namespace logging {

template<Id id>
class Loggable {
protected:
    static Logger& __log_do_not_use_read_comment()
    {
        static Logger& instance = Registry::getLog(id);
        return instance;
    }
};

} // namespace logging

#define IFRAME_SPDLOG_LEVEL(LEVEL)                                                        \
    (static_cast<::spdlog::level::level_enum>(::logging::Logger::LEVEL))

#define IFRAME_LOG_SOURCE ::spdlog::source_loc { __FILE__, __LINE__, __func__ }

#define APP_LOG_TO_LOGGER(LOGGER, LEVEL, ...)                                             \
    do {                                                                                  \
        auto& app_logger__ = (LOGGER);                                                    \
        constexpr auto app_level__ = IFRAME_SPDLOG_LEVEL(LEVEL);                         \
        if (app_logger__.shouldLog(app_level__)) {                                        \
            app_logger__.log(IFRAME_LOG_SOURCE, app_level__, __VA_ARGS__);                \
        }                                                                                 \
    } while (false)

#define APP_LOG(LEVEL, ...)                                                               \
    APP_LOG_TO_LOGGER(__log_do_not_use_read_comment(), LEVEL, __VA_ARGS__)

#define APP_EVENT_TO_LOGGER(LOGGER, LEVEL, ...)                                           \
    (LOGGER).event(IFRAME_LOG_SOURCE, IFRAME_SPDLOG_LEVEL(LEVEL), ##__VA_ARGS__)

#define APP_EVENT(LEVEL, ...)                                                             \
    APP_EVENT_TO_LOGGER(__log_do_not_use_read_comment(), LEVEL, ##__VA_ARGS__)

#define APP_LOG_TRACE(...) APP_LOG(trace, __VA_ARGS__)
#define APP_LOG_DEBUG(...) APP_LOG(debug, __VA_ARGS__)
#define APP_LOG_INFO(...) APP_LOG(info, __VA_ARGS__)
#define APP_LOG_WARN(...) APP_LOG(warn, __VA_ARGS__)
#define APP_LOG_ERROR(...) APP_LOG(error, __VA_ARGS__)
#define APP_LOG_CRITICAL(...) APP_LOG(critical, __VA_ARGS__)

#define APP_EVENT_TRACE(...) APP_EVENT(trace, ##__VA_ARGS__)
#define APP_EVENT_DEBUG(...) APP_EVENT(debug, ##__VA_ARGS__)
#define APP_EVENT_INFO(...) APP_EVENT(info, ##__VA_ARGS__)
#define APP_EVENT_WARN(...) APP_EVENT(warn, ##__VA_ARGS__)
#define APP_EVENT_ERROR(...) APP_EVENT(error, ##__VA_ARGS__)
#define APP_EVENT_CRITICAL(...) APP_EVENT(critical, ##__VA_ARGS__)
