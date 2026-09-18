#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace daw::domain
{

// The domain never throws. Every operation that can fail returns Result<T>.
enum class ErrorCode : std::uint8_t
{
    none = 0,
    invalidArgument,    // caller passed a value the domain refuses
    invalidPayload,     // serialized form is malformed or has the wrong shape
    unknownCommandType, // no factory registered for that command type
    notFound,           // referenced entity does not exist
    conflict,           // entity already exists, or state forbids the change
    nothingToUndo,
    nothingToRedo,
    gestureClosed, // the gesture referenced by the caller is not the open one
    reentrantCall, // mutation attempted from inside an observer notification
    wrongThread,   // the bus was called from a thread that does not own it
    typeMismatch,  // Value holds another kind than the one asked for
    serialisationError,
};

[[nodiscard]] std::string_view describe(ErrorCode code) noexcept;

struct Error
{
    ErrorCode code{ErrorCode::none};
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string m)
        : code{c}
        , message{std::move(m)}
    {
    }
};

template <typename T>
class [[nodiscard]] Result
{
public:
    static_assert(!std::is_same_v<T, Error>, "Result<Error> is ambiguous");
    static_assert(!std::is_reference_v<T>, "Result cannot hold a reference");

    Result(T value)
        : storage_{std::move(value)}
    {
    }

    Result(Error error)
        : storage_{std::move(error)}
    {
    }

    Result(ErrorCode code, std::string message)
        : storage_{Error{code, std::move(message)}}
    {
    }

    [[nodiscard]] bool ok() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const T& value() const& noexcept { return *std::get_if<T>(&storage_); }
    [[nodiscard]] T& value() & noexcept { return *std::get_if<T>(&storage_); }
    [[nodiscard]] T&& value() && noexcept { return std::move(*std::get_if<T>(&storage_)); }

    [[nodiscard]] const Error& error() const& noexcept { return *std::get_if<Error>(&storage_); }
    [[nodiscard]] ErrorCode code() const noexcept { return ok() ? ErrorCode::none : error().code; }

private:
    std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void>
{
public:
    Result() = default;

    Result(Error error)
        : error_{std::move(error)}
    {
    }

    Result(ErrorCode code, std::string message)
        : error_{Error{code, std::move(message)}}
    {
    }

    [[nodiscard]] bool ok() const noexcept { return error_.code == ErrorCode::none; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Error& error() const noexcept { return error_; }
    [[nodiscard]] ErrorCode code() const noexcept { return error_.code; }

private:
    Error error_{};
};

// Shorthands for the failure side, so call sites read as one line.
[[nodiscard]] inline Error fail(ErrorCode code, std::string message)
{
    return Error{code, std::move(message)};
}

} // namespace daw::domain
