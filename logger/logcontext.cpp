#include "logcontext.h"

#include "registry.h"

namespace logging {

LogContext& LogContext::instance()
{
    static LogContext context;
    return context;
}

bool LogContext::load(const std::string& path, std::string* error)
{
    LogConfig candidate;
    if (!candidate.load(path, error)) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    pendingConfig_ = std::move(candidate);
    loaded_ = true;
    return true;
}

bool LogContext::activate(std::string* error)
{
    LoggingConfig config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!loaded_) {
            if (error) *error = "logging configuration has not been loaded";
            return false;
        }
        config = pendingConfig_.value();
    }

    LogRouter candidateRouter;
    if (!candidateRouter.configure(config, error)) return false;
    if (!Registry::buildLoggers(config, candidateRouter, error)) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    router_ = std::move(candidateRouter);
    active_ = true;
    return true;
}

bool LogContext::initialize(const std::string& path, std::string* error)
{
    return load(path, error) && activate(error);
}

bool LogContext::reload(std::string* error)
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path = pendingConfig_.path();
    }
    if (path.empty()) {
        if (error) *error = "logging configuration path is empty";
        return false;
    }
    return load(path, error) && activate(error);
}

void LogContext::shutdown() noexcept
{
    Registry::disableAll();
    std::lock_guard<std::mutex> lock(mutex_);
    router_.clear();
    active_ = false;
}

LoggingConfig LogContext::config() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingConfig_.value();
}

bool LogContext::active() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

} // namespace logging
