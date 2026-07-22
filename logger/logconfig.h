#pragma once

#include <spdlog/common.h>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace logging {

enum class OverflowPolicy {
    block,
    overrunOldest
};

enum class SinkType {
    console,
    basicFile,
    rotatingFile,
    specific
};

struct AsyncConfig {
    std::size_t queueSize{8192};
    std::size_t threadCount{1};
    OverflowPolicy overflowPolicy{OverflowPolicy::block};
};

struct SinkConfig {
    std::string name;
    SinkType type{SinkType::specific};
    std::string path;
    std::size_t maxSizeBytes{10U * 1024U * 1024U};
    std::size_t maxFiles{5};
};

struct DomainConfig {
    std::string name;
    bool enabled{true};
    spdlog::level::level_enum level{spdlog::level::info};
    bool async{false};
    std::vector<std::string> sinks;
};

struct LoggingConfig {
    AsyncConfig async;
    std::map<std::string, SinkConfig> sinks;
    std::map<std::string, DomainConfig> domains;
};

class LogConfig final {
public:
    bool load(const std::string& path, std::string* error = nullptr);

    const LoggingConfig& value() const noexcept { return value_; }
    const std::string& path() const noexcept { return path_; }

private:
    LoggingConfig value_;
    std::string path_;
};

} // namespace logging
