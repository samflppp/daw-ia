#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/CommandRegistry.h"

#include <string>
#include <string_view>
#include <vector>

namespace daw::domain::copilot
{

// A command of the registry, described so that a model can call it.
//
// The description is in French, like the interface, because the model answers
// in French and a tool named in one language and described in another is a
// tool it uses badly. The schema is the payload the command already reads —
// not a second dialect invented for the occasion: whatever the model sends
// goes through CommandRegistry::create exactly as a replayed journal row does,
// and an invalid payload is refused exactly as loudly.
//
// The copilot has no privilege of any kind. It supplies the identifiers of
// what it creates, as every caller must, or a replayed history would build a
// different project.
struct Tool
{
    std::string name;    // the command type, "track.add"
    std::string summary; // one line, read by the model
    Value schema;        // JSON Schema of the payload

    // False for the commands only the application can call — capturing the
    // state of a plugin needs its bytes, and the copilot has none. Those stay
    // in the table, because the table answers for the registry, and out of the
    // list handed to the model.
    bool offeredToModel{true};

    [[nodiscard]] Value toValue() const;
};

// Every command the registry holds, described.
//
// The table lives in one file rather than next to each command, and a test
// checks it against CommandRegistry::types() in both directions: a command
// added without its description fails the suite, and a description left behind
// by a removed command fails it too. That is what keeps one source of truth
// without writing a schema builder into every command class.
[[nodiscard]] std::vector<Tool> builtinTools();

// The tools as the JSON-RPC layer serves them, ready for the provider. Only
// the ones the model may call.
[[nodiscard]] Value toValue(const std::vector<Tool>& tools);

// The one tool that is not a command: pattern.generate asks the DAW's own
// generator (rules, form, the style learned from the person) to write a line,
// and is turned into note.remove and note.add before anything reaches the bus.
// Its payload is the constraints of the S14 contract (Constraints.h), plus
// where to write. A model that writes forty chords note by note runs out of
// room; one that says "chords, minor, sparse" does not, and cannot write a
// note out of the key.
inline constexpr std::string_view generationToolName = "pattern.generate";
[[nodiscard]] Tool generationTool();

// The tools the registry actually holds, or the mismatch, named.
[[nodiscard]] Result<std::vector<Tool>> toolsFor(const CommandRegistry& registry);

} // namespace daw::domain::copilot
