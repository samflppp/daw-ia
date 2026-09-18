#include "daw/domain/Value.h"

#include <algorithm>

namespace daw::domain
{
namespace
{

// Members are stored sorted by key, so equal values always serialize the same.
void sortMembers(Value::Object& members)
{
    std::sort(members.begin(),
              members.end(),
              [](const Value::Member& lhs, const Value::Member& rhs) { return lhs.first < rhs.first; });
}

std::string wrongKind(std::string_view expected, Value::Kind actual)
{
    return std::string{"expected "} + std::string{expected} + ", found " +
           std::string{Value::describe(actual)};
}

} // namespace

Value::Value() noexcept = default;
Value::~Value() = default;
Value::Value(Value&& other) noexcept = default;
Value& Value::operator=(Value&& other) noexcept = default;

Value::Value(std::nullptr_t) noexcept {}

Value::Value(const Value& other)
{
    *this = other;
}

Value& Value::operator=(const Value& other)
{
    if (this == &other)
        return *this;

    // The variant holds unique pointers, so it is not copyable as a whole:
    // arrays and objects are duplicated, scalars are copied one by one.
    switch (other.kind())
    {
    case Kind::null:
        storage_ = std::monostate{};
        break;
    case Kind::boolean:
        storage_ = std::get<bool>(other.storage_);
        break;
    case Kind::integer:
        storage_ = std::get<std::int64_t>(other.storage_);
        break;
    case Kind::number:
        storage_ = std::get<double>(other.storage_);
        break;
    case Kind::text:
        storage_ = std::get<std::string>(other.storage_);
        break;
    case Kind::array:
        storage_ = std::make_unique<Array>(*other.asArray());
        break;
    case Kind::object:
        storage_ = std::make_unique<Object>(*other.asObject());
        break;
    }

    return *this;
}

Value::Value(bool value) noexcept
    : storage_{value}
{
}

Value::Value(int value) noexcept
    : storage_{static_cast<std::int64_t>(value)}
{
}

Value::Value(std::int64_t value) noexcept
    : storage_{value}
{
}

Value::Value(double value) noexcept
    : storage_{value}
{
}

Value::Value(std::string value)
    : storage_{std::move(value)}
{
}

Value::Value(std::string_view value)
    : storage_{std::string{value}}
{
}

Value::Value(const char* value)
    : storage_{std::string{value != nullptr ? value : ""}}
{
}

Value Value::array(Array items)
{
    Value result;
    result.storage_ = std::make_unique<Array>(std::move(items));
    return result;
}

Value Value::object(Object members)
{
    sortMembers(members);
    Value result;
    result.storage_ = std::make_unique<Object>(std::move(members));
    return result;
}

Value::Kind Value::kind() const noexcept
{
    switch (storage_.index())
    {
    case 0:
        return Kind::null;
    case 1:
        return Kind::boolean;
    case 2:
        return Kind::integer;
    case 3:
        return Kind::number;
    case 4:
        return Kind::text;
    case 5:
        return Kind::array;
    default:
        return Kind::object;
    }
}

std::string_view Value::describe(Kind kind) noexcept
{
    switch (kind)
    {
    case Kind::null:
        return "null";
    case Kind::boolean:
        return "bool";
    case Kind::integer:
        return "int";
    case Kind::number:
        return "number";
    case Kind::text:
        return "string";
    case Kind::array:
        return "array";
    case Kind::object:
        return "object";
    }
    return "unknown";
}

Result<bool> Value::asBool() const
{
    if (const auto* value = std::get_if<bool>(&storage_))
        return *value;
    return fail(ErrorCode::typeMismatch, wrongKind("bool", kind()));
}

Result<std::int64_t> Value::asInt() const
{
    if (const auto* value = std::get_if<std::int64_t>(&storage_))
        return *value;
    return fail(ErrorCode::typeMismatch, wrongKind("int", kind()));
}

Result<double> Value::asDouble() const
{
    if (const auto* value = std::get_if<double>(&storage_))
        return *value;
    if (const auto* integer = std::get_if<std::int64_t>(&storage_))
        return static_cast<double>(*integer);
    return fail(ErrorCode::typeMismatch, wrongKind("number", kind()));
}

Result<std::string> Value::asString() const
{
    if (const auto* value = std::get_if<std::string>(&storage_))
        return *value;
    return fail(ErrorCode::typeMismatch, wrongKind("string", kind()));
}

const Value::Array* Value::asArray() const noexcept
{
    if (const auto* items = std::get_if<std::unique_ptr<Array>>(&storage_))
        return items->get();
    return nullptr;
}

const Value::Object* Value::asObject() const noexcept
{
    if (const auto* members = std::get_if<std::unique_ptr<Object>>(&storage_))
        return members->get();
    return nullptr;
}

const Value* Value::find(std::string_view key) const noexcept
{
    const auto* members = asObject();
    if (members == nullptr)
        return nullptr;

    for (const auto& member : *members)
    {
        if (member.first == key)
            return &member.second;
    }
    return nullptr;
}

bool Value::contains(std::string_view key) const noexcept
{
    return find(key) != nullptr;
}

namespace
{

Result<const Value*> memberOrError(const Value& self, std::string_view key)
{
    if (!self.isObject())
        return fail(ErrorCode::typeMismatch,
                    std::string{"cannot read key "} + std::string{key} + ": value is a " +
                        std::string{Value::describe(self.kind())});

    const auto* member = self.find(key);
    if (member == nullptr)
        return fail(ErrorCode::invalidPayload, std::string{"missing key "} + std::string{key});

    return member;
}

std::string atKey(std::string_view key, const Error& error)
{
    return std::string{"key "} + std::string{key} + ": " + error.message;
}

} // namespace

Result<bool> Value::boolAt(std::string_view key) const
{
    auto member = memberOrError(*this, key);
    if (!member)
        return member.error();

    auto value = member.value()->asBool();
    if (!value)
        return fail(value.error().code, atKey(key, value.error()));
    return value.value();
}

Result<std::int64_t> Value::intAt(std::string_view key) const
{
    auto member = memberOrError(*this, key);
    if (!member)
        return member.error();

    auto value = member.value()->asInt();
    if (!value)
        return fail(value.error().code, atKey(key, value.error()));
    return value.value();
}

Result<double> Value::doubleAt(std::string_view key) const
{
    auto member = memberOrError(*this, key);
    if (!member)
        return member.error();

    auto value = member.value()->asDouble();
    if (!value)
        return fail(value.error().code, atKey(key, value.error()));
    return value.value();
}

Result<std::string> Value::stringAt(std::string_view key) const
{
    auto member = memberOrError(*this, key);
    if (!member)
        return member.error();

    auto value = member.value()->asString();
    if (!value)
        return fail(value.error().code, atKey(key, value.error()));
    return value.value();
}

Result<void> Value::set(std::string key, Value value)
{
    if (isNull())
        storage_ = std::make_unique<Object>();

    auto* members = std::get_if<std::unique_ptr<Object>>(&storage_);
    if (members == nullptr)
        return fail(ErrorCode::typeMismatch, wrongKind("object", kind()));

    for (auto& member : **members)
    {
        if (member.first == key)
        {
            member.second = std::move(value);
            return {};
        }
    }

    (*members)->emplace_back(std::move(key), std::move(value));
    sortMembers(**members);
    return {};
}

Result<void> Value::push(Value value)
{
    if (isNull())
        storage_ = std::make_unique<Array>();

    auto* items = std::get_if<std::unique_ptr<Array>>(&storage_);
    if (items == nullptr)
        return fail(ErrorCode::typeMismatch, wrongKind("array", kind()));

    (*items)->push_back(std::move(value));
    return {};
}

std::size_t Value::size() const noexcept
{
    if (const auto* items = asArray())
        return items->size();
    if (const auto* members = asObject())
        return members->size();
    return 0;
}

bool operator==(const Value& lhs, const Value& rhs)
{
    if (lhs.kind() != rhs.kind())
        return false;

    switch (lhs.kind())
    {
    case Value::Kind::null:
        return true;
    case Value::Kind::boolean:
        return std::get<bool>(lhs.storage_) == std::get<bool>(rhs.storage_);
    case Value::Kind::integer:
        return std::get<std::int64_t>(lhs.storage_) == std::get<std::int64_t>(rhs.storage_);
    case Value::Kind::number:
        return std::get<double>(lhs.storage_) == std::get<double>(rhs.storage_);
    case Value::Kind::text:
        return std::get<std::string>(lhs.storage_) == std::get<std::string>(rhs.storage_);
    case Value::Kind::array:
        return *lhs.asArray() == *rhs.asArray();
    case Value::Kind::object:
        return *lhs.asObject() == *rhs.asObject();
    }
    return false;
}

} // namespace daw::domain
