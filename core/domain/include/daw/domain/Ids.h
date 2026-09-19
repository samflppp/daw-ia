#pragma once

#include "daw/domain/Result.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace daw::domain
{

// 128-bit identifier in ULID form: 48 bits of millisecond timestamp followed by
// 80 random bits, written as 26 Crockford base32 characters.
//
// Chosen over juce::Uuid (forbidden here: the domain links no framework) and
// over a per-process counter (meaningless once a command is replayed in another
// process). Two useful properties: identifiers sort by creation time, and the
// textual form is a plain ASCII key, ready for SQLite and for JSON-RPC.
class Ulid
{
public:
    static constexpr std::size_t textLength = 26;

    Ulid() noexcept = default;
    explicit Ulid(std::array<std::uint8_t, 16> bytes) noexcept
        : bytes_{bytes}
    {
    }

    [[nodiscard]] static Ulid generate();
    [[nodiscard]] static Result<Ulid> parse(std::string_view text);

    [[nodiscard]] std::string toString() const;
    [[nodiscard]] bool isNil() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& bytes() const noexcept { return bytes_; }

    friend bool operator==(const Ulid& lhs, const Ulid& rhs) noexcept { return lhs.bytes_ == rhs.bytes_; }
    friend bool operator!=(const Ulid& lhs, const Ulid& rhs) noexcept { return !(lhs == rhs); }
    friend bool operator<(const Ulid& lhs, const Ulid& rhs) noexcept { return lhs.bytes_ < rhs.bytes_; }

private:
    std::array<std::uint8_t, 16> bytes_{};
};

// Distinct identifier types so a ClipId never silently passes as a TrackId.
template <typename Tag>
class EntityId
{
public:
    EntityId() noexcept = default;
    explicit EntityId(Ulid value) noexcept
        : value_{value}
    {
    }

    [[nodiscard]] static EntityId generate() { return EntityId{Ulid::generate()}; }

    [[nodiscard]] static Result<EntityId> parse(std::string_view text)
    {
        auto parsed = Ulid::parse(text);
        if (!parsed)
            return parsed.error();
        return EntityId{parsed.value()};
    }

    [[nodiscard]] std::string toString() const { return value_.toString(); }
    [[nodiscard]] bool isNil() const noexcept { return value_.isNil(); }
    [[nodiscard]] const Ulid& value() const noexcept { return value_; }

    friend bool operator==(const EntityId& lhs, const EntityId& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const EntityId& lhs, const EntityId& rhs) noexcept { return !(lhs == rhs); }

    friend bool operator<(const EntityId& lhs, const EntityId& rhs) noexcept
    {
        return lhs.value_ < rhs.value_;
    }

private:
    Ulid value_{};
};

struct CommandIdTag;
struct GestureIdTag;
struct TrackIdTag;
struct ClipIdTag;
struct NoteIdTag;
struct PluginIdTag;

using CommandId = EntityId<CommandIdTag>;
using GestureId = EntityId<GestureIdTag>;
using TrackId = EntityId<TrackIdTag>;
using ClipId = EntityId<ClipIdTag>;
using NoteId = EntityId<NoteIdTag>;

// Identifies one plugin *instance* on a track, not the plugin binary: two
// copies of the same synth on the same track are two PluginIds. The binary is
// named by PluginRef, which is not an identity the project engenders.
using PluginId = EntityId<PluginIdTag>;

} // namespace daw::domain
