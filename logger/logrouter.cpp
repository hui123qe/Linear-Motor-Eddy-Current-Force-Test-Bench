#include "logrouter.h"

#include "logsink.h"

namespace logging {

bool LogRouter::configure(const LoggingConfig& config, std::string* error)
{
    std::map<std::string, spdlog::sink_ptr> candidateSinks;
    std::map<std::string, std::vector<spdlog::sink_ptr>> candidateRoutes;

    for (const auto& entry : config.sinks) {
        auto created = LogSink::create(entry.second, error);
        if (!created) return false;
        candidateSinks.emplace(entry.first, std::move(created));
    }

    for (const auto& domainEntry : config.domains) {
        candidateRoutes.emplace(domainEntry.first, std::vector<spdlog::sink_ptr>{});
    }

    LogRouter candidate;
    candidate.sinks_ = std::move(candidateSinks);
    candidate.routes_ = std::move(candidateRoutes);
    for (const auto& domainEntry : config.domains) {
        if (!candidate.bind(domainEntry.first, domainEntry.second.sinks, error)) return false;
    }
    sinks_ = std::move(candidate.sinks_);
    routes_ = std::move(candidate.routes_);
    if (error) error->clear();
    return true;
}

bool LogRouter::bind(
    const std::string& domain,
    const std::vector<std::string>& sinkNames,
    std::string* error)
{
    std::vector<spdlog::sink_ptr> route;
    for (const auto& sinkName : sinkNames) {
        auto found = sinks_.find(sinkName);
        if (found == sinks_.end()) {
            SinkConfig specific;
            specific.name = sinkName;
            specific.type = SinkType::specific;
            auto created = LogSink::create(specific, error);
            if (!created) return false;
            found = sinks_.emplace(sinkName, std::move(created)).first;
        }
        route.push_back(found->second);
    }
    routes_[domain] = std::move(route);
    if (error) error->clear();
    return true;
}

void LogRouter::clear() noexcept
{
    routes_.clear();
    sinks_.clear();
}

const std::vector<spdlog::sink_ptr>& LogRouter::sinksFor(
    const std::string& domain) const noexcept
{
    static const std::vector<spdlog::sink_ptr> empty;
    const auto found = routes_.find(domain);
    return found == routes_.end() ? empty : found->second;
}

spdlog::sink_ptr LogRouter::sink(const std::string& name) const noexcept
{
    const auto found = sinks_.find(name);
    return found == sinks_.end() ? spdlog::sink_ptr{} : found->second;
}

} // namespace logging
