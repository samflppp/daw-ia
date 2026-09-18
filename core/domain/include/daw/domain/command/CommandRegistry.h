#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/Command.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace daw::domain
{

// Maps a wire type name onto a factory. Without it a serialized command is a
// piece of text; with it, it is executable again — in another process, another
// day, or from an MCP call.
class CommandRegistry
{
public:
    using Factory = std::function<Result<std::unique_ptr<Command>>(const Value& payload)>;

    // The three commands of S2, ready to use.
    [[nodiscard]] static CommandRegistry withBuiltinCommands();

    Result<void> add(std::string type, Factory factory);

    // Registers a command class exposing:
    //     static constexpr std::string_view commandType;
    //     static Result<std::unique_ptr<Command>> fromPayload(const Value&);
    template <typename CommandType>
    Result<void> add()
    {
        return add(std::string{CommandType::commandType},
                   [](const Value& payload) { return CommandType::fromPayload(payload); });
    }

    [[nodiscard]] bool contains(std::string_view type) const noexcept;
    [[nodiscard]] std::vector<std::string> types() const;

    [[nodiscard]] Result<std::unique_ptr<Command>> create(std::string_view type, const Value& payload) const;

private:
    struct Entry
    {
        std::string type;
        Factory factory;
    };

    std::vector<Entry> entries_;
};

} // namespace daw::domain
