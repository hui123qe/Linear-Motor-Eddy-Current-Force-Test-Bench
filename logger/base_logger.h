#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

namespace logging {

using LogValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    uint64_t,
    double,
    std::string>;

struct Field
{
    Field() = default;
    Field(std::string keyValue, LogValue fieldValue)
        : key(std::move(keyValue)), value(std::move(fieldValue)) {}
    Field(std::string keyValue, std::nullptr_t)
        : key(std::move(keyValue)), value(nullptr) {}
    Field(std::string keyValue, bool fieldValue)
        : key(std::move(keyValue)), value(fieldValue) {}
    Field(std::string keyValue, std::string fieldValue)
        : key(std::move(keyValue)), value(std::move(fieldValue)) {}
    Field(std::string keyValue, std::string_view fieldValue)
        : key(std::move(keyValue)), value(std::string(fieldValue)) {}
    Field(std::string keyValue, const char* fieldValue)
        : key(std::move(keyValue)),
          value(fieldValue ? LogValue(std::string(fieldValue)) : LogValue(nullptr)) {}

    template<typename T,
             std::enable_if_t<std::is_integral_v<T> &&
                                  std::is_signed_v<T> &&
                                  !std::is_same_v<std::remove_cv_t<T>, bool>,
                              int> = 0>
    Field(std::string keyValue, T fieldValue)
        : key(std::move(keyValue)), value(static_cast<int64_t>(fieldValue)) {}

    template<typename T,
             std::enable_if_t<std::is_integral_v<T> &&
                                  std::is_unsigned_v<T> &&
                                  !std::is_same_v<std::remove_cv_t<T>, bool>,
                              int> = 0>
    Field(std::string keyValue, T fieldValue)
        : key(std::move(keyValue)), value(static_cast<uint64_t>(fieldValue)) {}

    template<typename T,
             std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    Field(std::string keyValue, T fieldValue)
        : key(std::move(keyValue)), value(static_cast<double>(fieldValue)) {}

    std::string key;
    LogValue value;
};

inline Field field(std::string key, std::nullptr_t)
{
    return {std::move(key), nullptr};
}

inline Field field(std::string key, bool value)
{
    return {std::move(key), value};
}

template<typename T,
         std::enable_if_t<std::is_integral_v<T> &&
                              std::is_signed_v<T> &&
                              !std::is_same_v<std::remove_cv_t<T>, bool>,
                          int> = 0>
Field field(std::string key, T value)
{
    return {
        std::move(key),
        static_cast<int64_t>(value)
    };
}

template<typename T,
         std::enable_if_t<std::is_integral_v<T> &&
                              std::is_unsigned_v<T> &&
                              !std::is_same_v<std::remove_cv_t<T>, bool>,
                          int> = 0>
Field field(std::string key, T value)
{
    return {
        std::move(key),
        static_cast<uint64_t>(value)
    };
}

template<typename T,
         std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
Field field(std::string key, T value)
{
    return {std::move(key), static_cast<double>(value)};
}

inline Field field(std::string key, std::string value)
{
    return {std::move(key), std::move(value)};
}

inline Field field(std::string key, std::string_view value)
{
    return {std::move(key), std::string(value)};
}

inline Field field(std::string key, const char *value)
{
    return {
        std::move(key),
        value ? LogValue(std::string(value)) : LogValue(nullptr)
    };
}

struct StructuredEvent
{
    uint16_t version{1};

    std::string eventName;
    std::string message;

    std::vector<Field> fields;
};


class StructuredCodec
{
public:
    static constexpr std::string_view Marker = "@structured:v1:";

    static std::string encode(const StructuredEvent &event);

    static std::optional<StructuredEvent> decode(std::string_view payload);

    static bool isStructured(std::string_view payload) noexcept
    {
        return payload.size() >= Marker.size() &&
               payload.substr(0, Marker.size()) == Marker;
    }
};


class EventBuilder;

class Logger
{
public:
  /* This is simple mapping between Logger severity levels and spdlog severity levels.
   * The only reason for this mapping is to go around the fact that spdlog defines level as err
   * but the method to log at err level is called LOGGER.error not LOGGER.err. All other level are
   * fine spdlog::info corresponds to LOGGER.info method.
   */
  using Levels = enum {
    trace = spdlog::level::trace,       // NOLINT(readability-identifier-naming)
    debug = spdlog::level::debug,       // NOLINT(readability-identifier-naming)
    info = spdlog::level::info,         // NOLINT(readability-identifier-naming)
    warn = spdlog::level::warn,         // NOLINT(readability-identifier-naming)
    error = spdlog::level::err,         // NOLINT(readability-identifier-naming)
    critical = spdlog::level::critical, // NOLINT(readability-identifier-naming)
    off = spdlog::level::off            // NOLINT(readability-identifier-naming)
  };

  spdlog::string_view_t levelString() const;
  std::string name() const;
  void setLevel(spdlog::level::level_enum level) noexcept;
  spdlog::level::level_enum level() const noexcept;
  bool shouldLog(spdlog::level::level_enum level) const noexcept;

  /*
   * Exposes the log method of the logger. See `spdlog::logger` log method.
   */
  template <typename... Args>
  void log(spdlog::source_loc loc, spdlog::level::level_enum lvl, spdlog::string_view_t fmt,
           const Args&... args) noexcept {
    try {
      const auto logger = std::atomic_load(&logger_);
      if (logger) logger->log(loc, lvl, fmt, args...);
    } catch (...) {
    }
  }

  EventBuilder event(spdlog::source_loc source,
                     spdlog::level::level_enum level);

  EventBuilder event(spdlog::source_loc source,
                     spdlog::level::level_enum level,
                     std::string eventName);

  template<typename... Args>
  EventBuilder event(spdlog::source_loc source,
                     spdlog::level::level_enum level,
                     std::string eventName,
                     spdlog::string_view_t messageFormat,
                     Args&&... args);

  static const char* DEFAULT_LOG_FORMAT;

protected:
  Logger(std::shared_ptr<spdlog::logger> logger);

private:
  void replaceBackend(std::shared_ptr<spdlog::logger> logger) noexcept;
  void writeEvent(spdlog::source_loc source,
                  spdlog::level::level_enum level,
                  StructuredEvent event) noexcept;

  std::shared_ptr<spdlog::logger> logger_;

  friend class EventBuilder;
  friend class Registry;
};

class EventBuilder final
{
public:
    EventBuilder(Logger& logger,
                 spdlog::source_loc source,
                 spdlog::level::level_enum level,
                 std::string eventName,
                 std::string message = {})
        : logger_(&logger),
          source_(source),
          level_(level),
          enabled_(logger.shouldLog(level))
    {
        event_.eventName = std::move(eventName);
        event_.message = std::move(message);
    }

    EventBuilder(const EventBuilder&) = delete;
    EventBuilder& operator=(const EventBuilder&) = delete;

    EventBuilder(EventBuilder&& other) noexcept
        : logger_(other.logger_),
          source_(other.source_),
          level_(other.level_),
          event_(std::move(other.event_)),
          enabled_(other.enabled_)
    {
        other.enabled_ = false;
    }

    ~EventBuilder() noexcept
    {
        if (enabled_) logger_->writeEvent(source_, level_, std::move(event_));
    }

    EventBuilder& operator<<(Field fieldValue)
    {
        if (enabled_) event_.fields.emplace_back(std::move(fieldValue));
        return *this;
    }

private:
    Logger* logger_;
    spdlog::source_loc source_;
    spdlog::level::level_enum level_;
    StructuredEvent event_;
    bool enabled_;
};

inline EventBuilder Logger::event(spdlog::source_loc source,
                                  spdlog::level::level_enum level)
{
    return EventBuilder(*this, source, level, source.funcname ? source.funcname : "");
}

inline EventBuilder Logger::event(spdlog::source_loc source,
                                  spdlog::level::level_enum level,
                                  std::string eventName)
{
    return EventBuilder(*this, source, level, std::move(eventName));
}

template<typename... Args>
EventBuilder Logger::event(spdlog::source_loc source,
                           spdlog::level::level_enum level,
                           std::string eventName,
                           spdlog::string_view_t messageFormat,
                           Args&&... args)
{
    std::string message;
    if (shouldLog(level)) {
        try {
            message = fmt::vformat(
                messageFormat,
                fmt::make_format_args(args...));
        } catch (const std::exception& exception) {
            message = std::string("event formatting failed: ") + exception.what();
        } catch (...) {
            message = "event formatting failed";
        }
    }
    return EventBuilder(
        *this, source, level, std::move(eventName), std::move(message));
}

} // namespace logging
