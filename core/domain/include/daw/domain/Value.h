#pragma once

#include "daw/domain/Result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace daw::domain
{

// Value is the only data format the domain exchanges with the outside world:
// command payloads, undo records, project snapshots. It carries no dependency
// on a JSON library — serialization/Json.h turns it into text and back.
//
// Objects keep their members sorted by key, so two equal values always produce
// the same text. That property is what later lets the versioning layer hash a
// command or a project state and compare the hashes.
class Value
{
public:
    enum class Kind : std::uint8_t
    {
        null,
        boolean,
        integer,
        number,
        text,
        array,
        object,
    };

    using Array = std::vector<Value>;
    using Member = std::pair<std::string, Value>;
    using Object = std::vector<Member>;

    Value() noexcept;
    Value(std::nullptr_t) noexcept;
    Value(bool value) noexcept;
    Value(int value) noexcept;
    Value(std::int64_t value) noexcept;
    Value(double value) noexcept;
    Value(std::string value);
    Value(std::string_view value);
    Value(const char* value);

    // Array and Object are held behind a pointer: Value is recursive, and a
    // standard container of an incomplete type is not. The special members are
    // defined out of line, where Array and Object are complete.
    ~Value();
    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;

    [[nodiscard]] static Value array(Array items);
    [[nodiscard]] static Value object(Object members); // sorted on construction

    [[nodiscard]] Kind kind() const noexcept;
    [[nodiscard]] static std::string_view describe(Kind kind) noexcept;

    [[nodiscard]] bool isNull() const noexcept { return kind() == Kind::null; }
    [[nodiscard]] bool isBool() const noexcept { return kind() == Kind::boolean; }
    [[nodiscard]] bool isInt() const noexcept { return kind() == Kind::integer; }
    [[nodiscard]] bool isNumber() const noexcept { return kind() == Kind::number; }
    [[nodiscard]] bool isString() const noexcept { return kind() == Kind::text; }
    [[nodiscard]] bool isArray() const noexcept { return kind() == Kind::array; }
    [[nodiscard]] bool isObject() const noexcept { return kind() == Kind::object; }

    // Scalar readers. asDouble() also accepts an integer: JSON writers are free
    // to drop a trailing ".0", so a round-trip must not change the meaning.
    [[nodiscard]] Result<bool> asBool() const;
    [[nodiscard]] Result<std::int64_t> asInt() const;
    [[nodiscard]] Result<double> asDouble() const;
    [[nodiscard]] Result<std::string> asString() const;

    [[nodiscard]] const Array* asArray() const noexcept;
    [[nodiscard]] const Object* asObject() const noexcept;

    // Object access. find() returns nullptr when absent or when this is not an
    // object; the *At() readers say which key failed and why.
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;
    [[nodiscard]] bool contains(std::string_view key) const noexcept;
    [[nodiscard]] Result<bool> boolAt(std::string_view key) const;
    [[nodiscard]] Result<std::int64_t> intAt(std::string_view key) const;
    [[nodiscard]] Result<double> doubleAt(std::string_view key) const;
    [[nodiscard]] Result<std::string> stringAt(std::string_view key) const;

    // Mutation. set() turns a null value into an object, push() into an array;
    // on any other kind they do nothing and report it.
    Result<void> set(std::string key, Value value);
    Result<void> push(Value value);

    [[nodiscard]] std::size_t size() const noexcept; // array items or object members

    friend bool operator==(const Value& lhs, const Value& rhs);
    friend bool operator!=(const Value& lhs, const Value& rhs) { return !(lhs == rhs); }

private:
    std::variant<std::monostate,
                 bool,
                 std::int64_t,
                 double,
                 std::string,
                 std::unique_ptr<Array>,
                 std::unique_ptr<Object>>
        storage_;
};

// Small helper so command payloads read as one expression.
[[nodiscard]] inline Value makeObject(Value::Object members)
{
    return Value::object(std::move(members));
}

} // namespace daw::domain
