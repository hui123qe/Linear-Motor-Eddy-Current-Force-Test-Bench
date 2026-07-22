#pragma once

#include <spdlog/sinks/sink.h>

#include <memory>
#include <string>

namespace logging {

class SpecificLogSink final : public spdlog::sinks::sink {
public:
    explicit SpecificLogSink(std::string name);

    void log(const spdlog::details::log_msg& message) override;
    void flush() override;
    void set_pattern(const std::string& pattern) override;
    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override;

    const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
};

} // namespace logging
