#pragma once

#include "base_logger.h"
#include "logconfig.h"
#include "logrouter.h"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace spdlog::details {
class thread_pool;
}

namespace logging {

enum class Id : std::size_t {
#define IFRAME_LOGGER_ID(name) name,
#include "loggerids.def"
#undef IFRAME_LOGGER_ID
    count
};

const char* loggerName(Id id) noexcept;

class Registry final {
public:
    static Registry& instance();

    static Logger& getLog(Id id);
    static Logger& getLog(const std::string& domain);
    static void setLogger(
        const std::string& domain,
        std::shared_ptr<spdlog::logger> backend);
    static void setLevel(const std::string& domain, spdlog::level::level_enum level);
    static bool buildLoggers(
        const LoggingConfig& config,
        const LogRouter& router,
        std::string* error = nullptr);
    static void disableAll() noexcept;

private:
    Registry() = default;

    Logger& ensureLogger(const std::string& domain);

    std::map<std::string, std::unique_ptr<Logger>> loggers_;
    std::shared_ptr<spdlog::details::thread_pool> threadPool_;
    std::mutex mutex_;
};

} // namespace logging
