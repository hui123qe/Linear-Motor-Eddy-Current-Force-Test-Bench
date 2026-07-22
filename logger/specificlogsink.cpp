#include "specificlogsink.h"

namespace logging {

SpecificLogSink::SpecificLogSink(std::string name) : name_(std::move(name)) {}

void SpecificLogSink::log(const spdlog::details::log_msg&)
{
}

void SpecificLogSink::flush()
{
}

void SpecificLogSink::set_pattern(const std::string&)
{
}

void SpecificLogSink::set_formatter(std::unique_ptr<spdlog::formatter>)
{
}

} // namespace logging
