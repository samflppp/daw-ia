#include "daw/domain/copilot/Usage.h"

namespace daw::domain::copilot
{

namespace
{

constexpr const char* inputKey = "inputTokens";
constexpr const char* outputKey = "outputTokens";
constexpr const char* cacheReadKey = "cacheReadTokens";
constexpr const char* cacheWriteKey = "cacheWriteTokens";

std::int64_t tokens(const Value& value, const char* key)
{
    const auto found = value.intAt(key);
    return found ? found.value() : std::int64_t{0};
}

} // namespace

Usage Usage::fromValue(const Value& value)
{
    Usage usage;
    usage.inputTokens = tokens(value, inputKey);
    usage.outputTokens = tokens(value, outputKey);
    usage.cacheReadTokens = tokens(value, cacheReadKey);
    usage.cacheWriteTokens = tokens(value, cacheWriteKey);
    return usage;
}

Value Usage::toValue() const
{
    return Value::object({{inputKey, Value{inputTokens}},
                          {outputKey, Value{outputTokens}},
                          {cacheReadKey, Value{cacheReadTokens}},
                          {cacheWriteKey, Value{cacheWriteTokens}}});
}

Usage Usage::plus(const Usage& other) const
{
    return Usage{inputTokens + other.inputTokens,
                 outputTokens + other.outputTokens,
                 cacheReadTokens + other.cacheReadTokens,
                 cacheWriteTokens + other.cacheWriteTokens};
}

} // namespace daw::domain::copilot
