#include "logconfig.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace logging {
namespace {

class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

    explicit JsonValue(Storage value = nullptr) : value_(std::move(value)) {}

    const Object* object() const { return std::get_if<Object>(&value_); }
    const Array* array() const { return std::get_if<Array>(&value_); }
    const std::string* string() const { return std::get_if<std::string>(&value_); }
    const bool* boolean() const { return std::get_if<bool>(&value_); }
    const double* number() const { return std::get_if<double>(&value_); }

private:
    Storage value_;
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    JsonValue parse()
    {
        skipSpace();
        auto value = parseValue();
        skipSpace();
        if (position_ != text_.size()) fail("unexpected trailing content");
        return value;
    }

private:
    JsonValue parseValue()
    {
        if (position_ >= text_.size()) fail("unexpected end of input");
        switch (text_[position_]) {
        case '{': return parseObject();
        case '[': return parseArray();
        case '"': return JsonValue(parseString());
        case 't': consumeLiteral("true"); return JsonValue(true);
        case 'f': consumeLiteral("false"); return JsonValue(false);
        case 'n': consumeLiteral("null"); return JsonValue(nullptr);
        default:
            if (text_[position_] == '-' ||
                std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                return JsonValue(parseNumber());
            }
            fail("unsupported value");
        }
    }

    JsonValue parseObject()
    {
        ++position_;
        JsonValue::Object object;
        skipSpace();
        if (consume('}')) return JsonValue(std::move(object));
        for (;;) {
            skipSpace();
            if (position_ >= text_.size() || text_[position_] != '"') {
                fail("expected object key");
            }
            auto key = parseString();
            skipSpace();
            if (!consume(':')) fail("expected ':'");
            skipSpace();
            if (!object.emplace(std::move(key), parseValue()).second) {
                fail("duplicate object key");
            }
            skipSpace();
            if (consume('}')) break;
            if (!consume(',')) fail("expected ',' or '}'");
        }
        return JsonValue(std::move(object));
    }

    JsonValue parseArray()
    {
        ++position_;
        JsonValue::Array array;
        skipSpace();
        if (consume(']')) return JsonValue(std::move(array));
        for (;;) {
            skipSpace();
            array.emplace_back(parseValue());
            skipSpace();
            if (consume(']')) break;
            if (!consume(',')) fail("expected ',' or ']'");
        }
        return JsonValue(std::move(array));
    }

    std::string parseString()
    {
        ++position_;
        std::string result;
        while (position_ < text_.size()) {
            const char ch = text_[position_++];
            if (ch == '"') return result;
            if (static_cast<unsigned char>(ch) < 0x20) {
                fail("control character in string");
            }
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }
            if (position_ >= text_.size()) fail("incomplete escape");
            switch (text_[position_++]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: fail("unsupported string escape; use UTF-8 directly");
            }
        }
        fail("unterminated string");
    }

    double parseNumber()
    {
        const char* begin = text_.c_str() + position_;
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE || !std::isfinite(value)) {
            fail("invalid number");
        }
        position_ = static_cast<std::size_t>(end - text_.c_str());
        return value;
    }

    void consumeLiteral(const char* literal)
    {
        const std::string value(literal);
        if (text_.compare(position_, value.size(), value) != 0) {
            fail("invalid literal");
        }
        position_ += value.size();
    }

    bool consume(char expected)
    {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skipSpace()
    {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    [[noreturn]] void fail(const std::string& message) const
    {
        throw std::runtime_error(message + " at byte " + std::to_string(position_));
    }

    std::string text_;
    std::size_t position_{0};
};

const JsonValue* member(const JsonValue::Object& object, const std::string& key)
{
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &found->second;
}

const JsonValue::Object& requiredObject(
    const JsonValue::Object& parent,
    const std::string& name)
{
    const auto* value = member(parent, name);
    if (!value || !value->object()) {
        throw std::runtime_error("'" + name + "' must be an object");
    }
    return *value->object();
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string readString(
    const JsonValue::Object& object,
    const std::string& name,
    std::string fallback = {})
{
    const auto* value = member(object, name);
    if (!value) return fallback;
    const auto* result = value->string();
    if (!result) throw std::runtime_error("'" + name + "' must be a string");
    return *result;
}

bool readBool(
    const JsonValue::Object& object,
    const std::string& name,
    bool fallback)
{
    const auto* value = member(object, name);
    if (!value) return fallback;
    if (const auto* result = value->boolean()) return *result;
    if (const auto* text = value->string()) {
        const auto normalized = lowercase(*text);
        if (normalized == "true") return true;
        if (normalized == "false") return false;
    }
    throw std::runtime_error("'" + name + "' must be a boolean");
}

std::size_t readSize(
    const JsonValue::Object& object,
    const std::string& name,
    std::size_t fallback)
{
    const auto* value = member(object, name);
    if (!value) return fallback;
    const auto* number = value->number();
    if (!number || *number <= 0.0 ||
        std::floor(*number) != *number ||
        *number > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("'" + name + "' must be a positive integer");
    }
    return static_cast<std::size_t>(*number);
}

std::size_t parseByteSize(std::string text)
{
    text.erase(
        std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }),
        text.end());
    text = lowercase(std::move(text));
    if (text.empty()) throw std::runtime_error("'max_size' cannot be empty");

    std::size_t multiplier = 1;
    std::string suffix;
    while (!text.empty() && std::isalpha(static_cast<unsigned char>(text.back()))) {
        suffix.insert(suffix.begin(), text.back());
        text.pop_back();
    }
    if (suffix == "kb") multiplier = 1024U;
    else if (suffix == "mb") multiplier = 1024U * 1024U;
    else if (suffix == "gb") multiplier = 1024U * 1024U * 1024U;
    else if (!suffix.empty() && suffix != "b") {
        throw std::runtime_error("unsupported max_size suffix: " + suffix);
    }

    if (text.empty() ||
        !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        throw std::runtime_error("'max_size' must contain an integer");
    }
    const auto number = std::stoull(text);
    if (number == 0 ||
        number > std::numeric_limits<std::size_t>::max() / multiplier) {
        throw std::runtime_error("'max_size' is out of range");
    }
    return static_cast<std::size_t>(number) * multiplier;
}

spdlog::level::level_enum parseLevel(std::string value)
{
    value = lowercase(std::move(value));
    if (value == "trace") return spdlog::level::trace;
    if (value == "debug") return spdlog::level::debug;
    if (value == "info") return spdlog::level::info;
    if (value == "warn" || value == "warning") return spdlog::level::warn;
    if (value == "error") return spdlog::level::err;
    if (value == "critical") return spdlog::level::critical;
    if (value == "off") return spdlog::level::off;
    throw std::runtime_error("unknown log level: " + value);
}

SinkConfig parseSink(const std::string& name, const JsonValue& value)
{
    const auto* object = value.object();
    if (!object) throw std::runtime_error("sink '" + name + "' must be an object");

    SinkConfig config;
    config.name = name;
    config.path = readString(*object, "path");
    const auto* strategyValue = member(*object, "strategy");

    if (!strategyValue && config.path.empty()) {
        config.type = name == "console" ? SinkType::console : SinkType::specific;
        return config;
    }
    if (!strategyValue || !strategyValue->object()) {
        throw std::runtime_error("sink '" + name + "'.strategy must be an object");
    }
    if (config.path.empty()) {
        throw std::runtime_error("sink '" + name + "'.path cannot be empty");
    }

    const auto& strategy = *strategyValue->object();
    const auto type = lowercase(readString(strategy, "type"));
    if (type == "basic") {
        config.type = SinkType::basicFile;
    } else if (type == "rotating") {
        config.type = SinkType::rotatingFile;
        config.maxSizeBytes =
            parseByteSize(readString(strategy, "max_size", "10MB"));
        config.maxFiles = readSize(strategy, "max_files", config.maxFiles);
    } else if (type == "specific") {
        config.type = SinkType::specific;
    } else {
        throw std::runtime_error("unknown strategy type for sink '" + name + "': " + type);
    }
    return config;
}

DomainConfig parseDomain(const std::string& name, const JsonValue& value)
{
    const auto* object = value.object();
    if (!object) throw std::runtime_error("domain '" + name + "' must be an object");

    DomainConfig config;
    config.name = name;
    config.enabled = readBool(*object, "enable", true);
    config.level = parseLevel(readString(*object, "level", "INFO"));
    config.async = readBool(*object, "async", false);

    const auto* sinksValue = member(*object, "sinks");
    if (!sinksValue || !sinksValue->array()) {
        throw std::runtime_error("domain '" + name + "'.sinks must be an array");
    }
    for (const auto& sinkValue : *sinksValue->array()) {
        const auto* sinkName = sinkValue.string();
        if (!sinkName || sinkName->empty()) {
            throw std::runtime_error(
                "domain '" + name + "'.sinks entries must be non-empty strings");
        }
        config.sinks.push_back(*sinkName);
    }
    return config;
}

LoggingConfig parseConfig(const JsonValue& root)
{
    const auto* object = root.object();
    if (!object) throw std::runtime_error("root must be an object");

    LoggingConfig config;
    const auto& global = requiredObject(*object, "global");
    const auto& async = requiredObject(global, "async");
    config.async.queueSize = readSize(async, "queue_size", config.async.queueSize);
    config.async.threadCount = readSize(async, "thread_count", config.async.threadCount);
    const auto policy = lowercase(readString(async, "overflow_policy", "block"));
    if (policy == "block") {
        config.async.overflowPolicy = OverflowPolicy::block;
    } else if (policy == "overrun_oldest") {
        config.async.overflowPolicy = OverflowPolicy::overrunOldest;
    } else {
        throw std::runtime_error(
            "'overflow_policy' must be 'block' or 'overrun_oldest'");
    }

    const auto& sinks = requiredObject(*object, "sinks");
    for (const auto& entry : sinks) {
        config.sinks.emplace(entry.first, parseSink(entry.first, entry.second));
    }

    const auto& domains = requiredObject(*object, "domains");
    for (const auto& entry : domains) {
        config.domains.emplace(entry.first, parseDomain(entry.first, entry.second));
    }
    return config;
}

} // namespace

bool LogConfig::load(const std::string& path, std::string* error)
{
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open file");
        std::ostringstream content;
        content << input.rdbuf();
        auto parsed = parseConfig(JsonParser(content.str()).parse());
        value_ = std::move(parsed);
        path_ = path;
        if (error) error->clear();
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = "invalid logging config '" + path + "': " + exception.what();
        }
        return false;
    }
}

} // namespace logging
