#pragma once

#include "logconfig.h"

#include <spdlog/common.h>

#include <map>
#include <string>
#include <vector>

namespace logging {

class LogRouter final {
public:
    bool configure(const LoggingConfig& config, std::string* error = nullptr);
    bool bind(
        const std::string& domain,
        const std::vector<std::string>& sinkNames,
        std::string* error = nullptr);
    void clear() noexcept;

    const std::vector<spdlog::sink_ptr>& sinksFor(const std::string& domain) const noexcept;
    spdlog::sink_ptr sink(const std::string& name) const noexcept;

private:
    std::map<std::string, spdlog::sink_ptr> sinks_;
    std::map<std::string, std::vector<spdlog::sink_ptr>> routes_;
};

} // namespace logging
