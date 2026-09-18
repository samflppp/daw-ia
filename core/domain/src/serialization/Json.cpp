#include "daw/domain/serialization/Json.h"

#include <nlohmann/json.hpp>

namespace daw::domain::json
{
namespace
{

using Json = nlohmann::json;

Json toJson(const Value& value)
{
    switch (value.kind())
    {
    case Value::Kind::null:
        return Json{};
    case Value::Kind::boolean:
        return Json(value.asBool().value());
    case Value::Kind::integer:
        return Json(value.asInt().value());
    case Value::Kind::number:
        return Json(value.asDouble().value());
    case Value::Kind::text:
        return Json(value.asString().value());
    case Value::Kind::array:
    {
        auto array = Json::array();
        for (const auto& item : *value.asArray())
            array.push_back(toJson(item));
        return array;
    }
    case Value::Kind::object:
    {
        auto object = Json::object();
        for (const auto& member : *value.asObject())
            object[member.first] = toJson(member.second);
        return object;
    }
    }
    return Json{};
}

Value fromJson(const Json& json)
{
    if (json.is_boolean())
        return Value{json.get<bool>()};
    if (json.is_number_integer() || json.is_number_unsigned())
        return Value{json.get<std::int64_t>()};
    if (json.is_number_float())
        return Value{json.get<double>()};
    if (json.is_string())
        return Value{json.get<std::string>()};

    if (json.is_array())
    {
        Value::Array items;
        items.reserve(json.size());
        for (const auto& item : json)
            items.push_back(fromJson(item));
        return Value::array(std::move(items));
    }

    if (json.is_object())
    {
        Value::Object members;
        members.reserve(json.size());
        for (const auto& member : json.items())
            members.emplace_back(member.key(), fromJson(member.value()));
        return Value::object(std::move(members));
    }

    return Value{};
}

} // namespace

std::string write(const Value& value)
{
    return toJson(value).dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string writePretty(const Value& value)
{
    return toJson(value).dump(2, ' ', false, Json::error_handler_t::replace);
}

Result<Value> read(std::string_view text)
{
    // No exceptions cross the domain boundary: parse() reports failure by
    // returning a discarded value.
    const auto parsed = Json::parse(text, nullptr, false);
    if (parsed.is_discarded())
        return fail(ErrorCode::serialisationError, "malformed JSON");

    return fromJson(parsed);
}

} // namespace daw::domain::json
