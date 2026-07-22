#pragma once

#include "logconfig.h"
#include "logrouter.h"

#include <mutex>
#include <string>

namespace logging {

class LogContext final {
public:
    static LogContext& instance();

    bool load(const std::string& path, std::string* error = nullptr);
    bool activate(std::string* error = nullptr);
    bool initialize(const std::string& path, std::string* error = nullptr);
    bool reload(std::string* error = nullptr);
    void shutdown() noexcept;

    LoggingConfig config() const;
    bool active() const noexcept;

private:
    LogContext() = default;

    mutable std::mutex mutex_;
    LogConfig pendingConfig_;
    LogRouter router_;
    bool loaded_{false};
    bool active_{false};
};

} // namespace logging
