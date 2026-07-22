#include "logsink.h"

#include "specificlogsink.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <exception>
#include <memory>

namespace logging {

spdlog::sink_ptr LogSink::create(const SinkConfig& config, std::string* error)
{
    try {
        spdlog::sink_ptr sink;
        switch (config.type) {
        case SinkType::console:
            sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            break;
        case SinkType::basicFile:
            sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.path, false);
            break;
        case SinkType::rotatingFile:
            sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config.path, config.maxSizeBytes, config.maxFiles);
            break;
        case SinkType::specific:
            sink = std::make_shared<SpecificLogSink>(config.name);
            break;
        }
        if (error) error->clear();
        return sink;
    } catch (const std::exception& exception) {
        if (error) {
            *error = "cannot create sink '" + config.name + "': " + exception.what();
        }
        return {};
    }
}

} // namespace logging
