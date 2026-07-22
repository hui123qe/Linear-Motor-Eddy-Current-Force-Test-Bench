#pragma once

#include "logconfig.h"

#include <spdlog/common.h>

#include <string>

namespace logging {

class LogSink final {
public:
    static spdlog::sink_ptr create(const SinkConfig& config, std::string* error = nullptr);
};

} // namespace logging
