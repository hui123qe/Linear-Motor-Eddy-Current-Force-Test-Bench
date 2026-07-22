#include "registry.h"

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>

#include <exception>
#include <set>
#include <utility>
#include <vector>

namespace logging {

const char* loggerName(Id id) noexcept
{
    switch (id) {
#define IFRAME_LOGGER_ID(name) case Id::name: return #name;
#include "loggerids.def"
#undef IFRAME_LOGGER_ID
    case Id::count: break;
    }
    return "UI";
}

Registry& Registry::instance()
{
    static Registry registry;
    return registry;
}

Logger& Registry::ensureLogger(const std::string& domain)
{
    auto& logger = loggers_[domain];
    if (!logger) logger = std::unique_ptr<Logger>(new Logger(nullptr));
    return *logger;
}

Logger& Registry::getLog(Id id)
{
    return getLog(loggerName(id));
}

Logger& Registry::getLog(const std::string& domain)
{
    auto& registry = instance();
    std::lock_guard<std::mutex> lock(registry.mutex_);
    return registry.ensureLogger(domain);
}

void Registry::setLogger(
    const std::string& domain,
    std::shared_ptr<spdlog::logger> backend)
{
    auto& registry = instance();
    std::lock_guard<std::mutex> lock(registry.mutex_);
    registry.ensureLogger(domain).replaceBackend(std::move(backend));
}

void Registry::setLevel(
    const std::string& domain,
    spdlog::level::level_enum level)
{
    getLog(domain).setLevel(level);
}

bool Registry::buildLoggers(
    const LoggingConfig& config,
    const LogRouter& router,
    std::string* error)
{
    try {
        bool needsAsync = false;
        for (const auto& entry : config.domains) {
            needsAsync = needsAsync || (entry.second.enabled && entry.second.async);
        }

        std::shared_ptr<spdlog::details::thread_pool> threadPool;
        if (needsAsync) {
            threadPool = std::make_shared<spdlog::details::thread_pool>(
                config.async.queueSize, config.async.threadCount);
        }

        std::map<std::string, std::shared_ptr<spdlog::logger>> backends;
        for (const auto& entry : config.domains) {
            const auto& domain = entry.second;
            if (!domain.enabled) {
                backends.emplace(entry.first, nullptr);
                continue;
            }

            const auto& sinks = router.sinksFor(entry.first);
            std::shared_ptr<spdlog::logger> backend;
            if (domain.async) {
                const auto policy =
                    config.async.overflowPolicy == OverflowPolicy::block
                    ? spdlog::async_overflow_policy::block
                    : spdlog::async_overflow_policy::overrun_oldest;
                backend = std::make_shared<spdlog::async_logger>(
                    entry.first,
                    sinks.begin(),
                    sinks.end(),
                    threadPool,
                    policy);
            } else {
                backend = std::make_shared<spdlog::logger>(
                    entry.first, sinks.begin(), sinks.end());
            }
            backend->set_level(domain.level);
            backends.emplace(entry.first, std::move(backend));
        }

        auto& registry = instance();
        std::lock_guard<std::mutex> lock(registry.mutex_);
        std::set<std::string> activeDomains;
        for (auto& entry : backends) {
            activeDomains.insert(entry.first);
            registry.ensureLogger(entry.first).replaceBackend(std::move(entry.second));
        }
        for (auto& entry : registry.loggers_) {
            if (activeDomains.find(entry.first) == activeDomains.end()) {
                entry.second->replaceBackend(nullptr);
            }
        }
        registry.threadPool_ = std::move(threadPool);
        if (error) error->clear();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = std::string("cannot activate loggers: ") + exception.what();
        return false;
    }
}

void Registry::disableAll() noexcept
{
    auto& registry = instance();
    std::lock_guard<std::mutex> lock(registry.mutex_);
    for (auto& entry : registry.loggers_) {
        entry.second->replaceBackend(nullptr);
    }
    registry.threadPool_.reset();
}

} // namespace logging
