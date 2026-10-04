#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Transform.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::ui
{

// How the generation window reads what was typed in it.
//
// One language (S16): the prompt is read by the copilot's model, the one that
// already turns "des accords tristes" into constraints for pattern.generate.
// The local interpreter of S14 is what answers when that model cannot: no
// process, no network, no answer in time. It is the fallback, not the way in.
//
// The window never learns which one answered except through `notice`, which
// says so in plain words when it matters.
class PromptReader
{
public:
    // What the reader is told about the zone, and nothing of the project: no
    // note, no learned count. A few words to read a prompt with, not a state.
    struct Zone
    {
        double lengthBeats{16.0};
        double beatsPerBar{4.0};
        bool hasNotes{false};            // notes selected, or already in the range
        std::vector<std::string> tracks; // the names of the tracks the zone covers
    };

    struct Reading
    {
        domain::generation::Interpretation interpretation;

        // When the zone holds notes: how the prompt asks to rework them.
        // Nothing when it does not say; the window then keeps the rhythm.
        std::optional<domain::generation::Transform> transform;

        // True when the copilot's model read it.
        bool remote{false};

        // Empty when everything went as it should. Otherwise one sentence for
        // the window: "Hors ligne : j'ai lu ta demande avec le lecteur simple."
        std::string notice;
    };

    using Done = std::function<void(Reading)>;

    PromptReader() = default;
    virtual ~PromptReader() = default;

    PromptReader(const PromptReader&) = delete;
    PromptReader& operator=(const PromptReader&) = delete;
    PromptReader(PromptReader&&) = delete;
    PromptReader& operator=(PromptReader&&) = delete;

    // Reads a prompt. `done` is called once, on the message thread, possibly
    // before this returns. A second read cancels the first: its `done` is
    // never called.
    virtual void read(std::string text, Zone zone, Done done) = 0;

    // Forgets the read in flight, if any: its `done` is never called.
    virtual void cancel() = 0;

    // True between read() and done().
    [[nodiscard]] virtual bool reading() const = 0;
};

// The S14 words, answered at once, and the words that ask to rework notes.
class LocalPromptReader final : public PromptReader
{
public:
    // The words that ask to rework notes ("plus sombre") are a transform when
    // the zone holds notes. Over an empty zone there is nothing to rework:
    // they are no constraint, and are said ignored like any other (S22).
    [[nodiscard]] static Reading parse(std::string_view text, const Zone& zone);

    void read(std::string text, Zone zone, Done done) override;
    void cancel() override {}
    [[nodiscard]] bool reading() const override { return false; }
};

// The copilot's model first, the local words when it cannot answer.
//
// It knows nothing of sockets or timers: the application hands it a way to ask
// and a way to know whether asking is worth it, and calls expire() when the
// wait has lasted long enough. Everything here runs on the message thread, so
// the rules can be tested without a process, a clock or a network.
//
//   the copilot is not there    local at once, and the window says so
//   it answers                  its reading, nothing said
//   it fails                    local, and the window says so
//   expire() before its answer  local, and the window says so; the late
//                               answer is dropped
class RoutedPromptReader final : public PromptReader
{
public:
    using Answered = std::function<void(std::uint64_t ticket, domain::Result<Reading>)>;

    struct Remote
    {
        // True when asking the copilot can succeed: its process is there.
        std::function<bool()> available;

        // Sends the prompt. `answered` is called once, on the message thread,
        // with the ticket it was given.
        std::function<void(
            std::uint64_t ticket, const std::string& text, const Zone& zone, Answered answered)>
            ask;
    };

    explicit RoutedPromptReader(Remote remote);

    void read(std::string text, Zone zone, Done done) override;
    void cancel() override;
    [[nodiscard]] bool reading() const override { return pending_.has_value(); }

    // The wait is over: the local words answer.
    void expire();

    // The words the window shows when the local reader answered instead.
    static constexpr const char* offlineNotice =
        "Hors ligne : j'ai lu ta demande avec le lecteur simple, certains mots ont pu m'échapper.";
    static constexpr const char* failedNotice =
        "Le copilote n'a pas pu lire ta demande : j'ai utilisé le lecteur simple.";
    static constexpr const char* slowNotice = "Le copilote tardait : j'ai utilisé le lecteur simple.";

private:
    struct Pending
    {
        std::uint64_t ticket{0};
        std::string text;
        Zone zone;
        Done done;
    };

    void answered(std::uint64_t ticket, domain::Result<Reading> result);
    void local(std::string_view notice);

    Remote remote_;
    std::optional<Pending> pending_;
    std::uint64_t nextTicket_{1};
};

} // namespace daw::ui
