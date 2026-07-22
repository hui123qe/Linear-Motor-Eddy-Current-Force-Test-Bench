#include "base_logger.h"

#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace logging {
namespace {

constexpr uint16_t supportedVersion = 1;

void appendEscaped(std::string_view value, std::string& output)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20) {
                output += "\\u00";
                output.push_back(hex[ch >> 4]);
                output.push_back(hex[ch & 0x0f]);
            } else {
                output.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    output.push_back('"');
}

template<typename T>
void appendInteger(T value, std::string& output)
{
    char buffer[std::numeric_limits<T>::digits10 + 4];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    if (result.ec != std::errc()) {
        throw std::runtime_error("cannot encode structured event integer");
    }
    output.append(buffer, result.ptr);
}

void appendDouble(double value, std::string& output)
{
    if (!std::isfinite(value)) {
        throw std::invalid_argument("structured event doubles must be finite");
    }

    char buffer[64];
    const auto result = std::to_chars(
        std::begin(buffer), std::end(buffer), value, std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc()) {
        throw std::runtime_error("cannot encode structured event double");
    }
    output.append(buffer, result.ptr);
}

void appendField(const Field& fieldValue, std::string& output)
{
    output += "{\"key\":";
    appendEscaped(fieldValue.key, output);
    output += ",\"type\":";

    std::visit(
        [&output](const auto& value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, std::nullptr_t>) {
                output += "\"null\",\"value\":null";
            } else if constexpr (std::is_same_v<Value, bool>) {
                output += value ? "\"bool\",\"value\":true" : "\"bool\",\"value\":false";
            } else if constexpr (std::is_same_v<Value, int64_t>) {
                output += "\"int64\",\"value\":";
                appendInteger(value, output);
            } else if constexpr (std::is_same_v<Value, uint64_t>) {
                output += "\"uint64\",\"value\":";
                appendInteger(value, output);
            } else if constexpr (std::is_same_v<Value, double>) {
                output += "\"double\",\"value\":";
                appendDouble(value, output);
            } else {
                output += "\"string\",\"value\":";
                appendEscaped(value, output);
            }
        },
        fieldValue.value);
    output.push_back('}');
}

class JsonReader
{
public:
    explicit JsonReader(std::string_view input) : input_(input) {}

    bool parse(StructuredEvent& event)
    {
        skipWhitespace();
        if (!consume('{') ||
            !parseKey("version") ||
            !parseUnsigned16(event.version) ||
            !consumeMemberSeparator() ||
            !parseKey("eventName") ||
            !parseString(event.eventName) ||
            !consumeMemberSeparator() ||
            !parseKey("message") ||
            !parseString(event.message) ||
            !consumeMemberSeparator() ||
            !parseKey("fields") ||
            !parseFields(event.fields) ||
            !consume('}')) {
            return false;
        }
        skipWhitespace();
        return position_ == input_.size() && event.version == supportedVersion;
    }

private:
    void skipWhitespace()
    {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++position_;
        }
    }

    bool consume(char expected)
    {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool consumeLiteral(std::string_view literal)
    {
        skipWhitespace();
        if (input_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    bool consumeMemberSeparator()
    {
        return consume(',');
    }

    bool parseKey(std::string_view expected)
    {
        std::string key;
        return parseString(key) && key == expected && consume(':');
    }

    static int hexValue(char ch)
    {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    bool parseHex4(uint32_t& value)
    {
        if (input_.size() - position_ < 4) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const int digit = hexValue(input_[position_++]);
            if (digit < 0) return false;
            value = (value << 4) | static_cast<uint32_t>(digit);
        }
        return true;
    }

    static void appendUtf8(uint32_t codePoint, std::string& output)
    {
        if (codePoint <= 0x7f) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else if (codePoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }

    bool parseUnicodeEscape(std::string& output)
    {
        uint32_t codePoint = 0;
        if (!parseHex4(codePoint)) return false;
        if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
            if (input_.size() - position_ < 6 ||
                input_[position_] != '\\' ||
                input_[position_ + 1] != 'u') {
                return false;
            }
            position_ += 2;
            uint32_t low = 0;
            if (!parseHex4(low) || low < 0xdc00 || low > 0xdfff) return false;
            codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
        } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
            return false;
        }
        appendUtf8(codePoint, output);
        return true;
    }

    bool parseString(std::string& output)
    {
        skipWhitespace();
        if (position_ >= input_.size() || input_[position_++] != '"') return false;
        output.clear();
        while (position_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch != '\\') {
                output.push_back(static_cast<char>(ch));
                continue;
            }
            if (position_ >= input_.size()) return false;
            switch (input_[position_++]) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
                if (!parseUnicodeEscape(output)) return false;
                break;
            default: return false;
            }
        }
        return false;
    }

    template<typename T>
    bool parseInteger(T& output)
    {
        skipWhitespace();
        const char* begin = input_.data() + position_;
        const char* end = input_.data() + input_.size();
        const auto result = std::from_chars(begin, end, output);
        if (result.ec != std::errc() || result.ptr == begin) return false;
        position_ = static_cast<std::size_t>(result.ptr - input_.data());
        return true;
    }

    bool parseUnsigned16(uint16_t& output)
    {
        uint64_t value = 0;
        if (!parseInteger(value) || value > std::numeric_limits<uint16_t>::max()) return false;
        output = static_cast<uint16_t>(value);
        return true;
    }

    bool parseDouble(double& output)
    {
        skipWhitespace();
        const char* begin = input_.data() + position_;
        const char* end = input_.data() + input_.size();
        const auto result = std::from_chars(begin, end, output, std::chars_format::general);
        if (result.ec != std::errc() || result.ptr == begin || !std::isfinite(output)) return false;
        position_ = static_cast<std::size_t>(result.ptr - input_.data());
        return true;
    }

    bool parseValue(std::string_view type, LogValue& value)
    {
        if (type == "null") {
            if (!consumeLiteral("null")) return false;
            value = nullptr;
            return true;
        }
        if (type == "bool") {
            if (consumeLiteral("true")) {
                value = true;
                return true;
            }
            if (consumeLiteral("false")) {
                value = false;
                return true;
            }
            return false;
        }
        if (type == "int64") {
            int64_t parsed = 0;
            if (!parseInteger(parsed)) return false;
            value = parsed;
            return true;
        }
        if (type == "uint64") {
            uint64_t parsed = 0;
            if (!parseInteger(parsed)) return false;
            value = parsed;
            return true;
        }
        if (type == "double") {
            double parsed = 0;
            if (!parseDouble(parsed)) return false;
            value = parsed;
            return true;
        }
        if (type == "string") {
            std::string parsed;
            if (!parseString(parsed)) return false;
            value = std::move(parsed);
            return true;
        }
        return false;
    }

    bool parseField(Field& fieldValue)
    {
        std::string type;
        return consume('{') &&
               parseKey("key") &&
               parseString(fieldValue.key) &&
               consumeMemberSeparator() &&
               parseKey("type") &&
               parseString(type) &&
               consumeMemberSeparator() &&
               parseKey("value") &&
               parseValue(type, fieldValue.value) &&
               consume('}');
    }

    bool parseFields(std::vector<Field>& fields)
    {
        if (!consume('[')) return false;
        fields.clear();
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return true;
        }

        while (true) {
            Field fieldValue;
            if (!parseField(fieldValue)) return false;
            fields.emplace_back(std::move(fieldValue));
            skipWhitespace();
            if (position_ < input_.size() && input_[position_] == ']') {
                ++position_;
                return true;
            }
            if (!consume(',')) return false;
        }
    }

    std::string_view input_;
    std::size_t position_{0};
};

} // namespace

std::string StructuredCodec::encode(const StructuredEvent& event)
{
    if (event.version != supportedVersion) {
        throw std::invalid_argument("unsupported structured event version");
    }

    std::string output;
    output.reserve(Marker.size() + event.eventName.size() + event.message.size() +
                   event.fields.size() * 48 + 64);
    output.append(Marker.data(), Marker.size());
    output += "{\"version\":";
    appendInteger(event.version, output);
    output += ",\"eventName\":";
    appendEscaped(event.eventName, output);
    output += ",\"message\":";
    appendEscaped(event.message, output);
    output += ",\"fields\":[";
    bool first = true;
    for (const auto& fieldValue : event.fields) {
        if (!first) output.push_back(',');
        first = false;
        appendField(fieldValue, output);
    }
    output += "]}";
    return output;
}

std::optional<StructuredEvent> StructuredCodec::decode(std::string_view payload)
{
    if (!isStructured(payload)) return std::nullopt;

    StructuredEvent event;
    JsonReader reader(payload.substr(Marker.size()));
    if (!reader.parse(event)) return std::nullopt;
    return event;
}

void Logger::writeEvent(spdlog::source_loc source,
                        spdlog::level::level_enum level,
                        StructuredEvent event) noexcept
{
    try {
        const auto payload = StructuredCodec::encode(event);
        const auto logger = std::atomic_load(&logger_);
        if (logger) logger->log(source, level, "{}", payload);
    } catch (...) {
    }
}

spdlog::string_view_t Logger::levelString() const
{
    return spdlog::level::level_string_views[level()];
}

std::string Logger::name() const
{
    const auto logger = std::atomic_load(&logger_);
    return logger ? logger->name() : std::string();
}

void Logger::setLevel(spdlog::level::level_enum level) noexcept
{
    const auto logger = std::atomic_load(&logger_);
    if (logger) logger->set_level(level);
}

spdlog::level::level_enum Logger::level() const noexcept
{
    const auto logger = std::atomic_load(&logger_);
    return logger ? logger->level() : spdlog::level::off;
}

bool Logger::shouldLog(spdlog::level::level_enum level) const noexcept
{
    const auto logger = std::atomic_load(&logger_);
    return logger && logger->should_log(level);
}

void Logger::replaceBackend(std::shared_ptr<spdlog::logger> logger) noexcept
{
    std::atomic_store(&logger_, std::move(logger));
}

Logger::Logger(std::shared_ptr<spdlog::logger> logger) : logger_(std::move(logger)) {
  if (!logger_) return;
  logger_->set_pattern(DEFAULT_LOG_FORMAT);
  logger_->set_level(spdlog::level::trace);

  // Ensure that critical errors, especially ASSERT/PANIC, get flushed
  logger_->flush_on(spdlog::level::critical);
}

const char* Logger::DEFAULT_LOG_FORMAT = "[%Y-%m-%d %H:%M:%S] [thread %t] [%l] %v";

} // namespace logging
