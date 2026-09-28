#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/generation/Generator.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace daw::ui
{

// Hearing a proposal before writing it (S16), as a panel is allowed to see it.
//
// The engine and its projection stop at the application, as they do for the
// plugins and the samples: a panel asks for lines to be heard and is told
// when that ended. Nothing it asks enters the project or the history.
class ListeningHost
{
public:
    struct Line
    {
        domain::TrackId track;
        domain::PatternId pattern;
        double fromBeats{0.0}; // in the pattern
        double toBeats{0.0};
        std::vector<domain::generation::GhostNote> notes;

        // Where this laying of the pattern starts in the song (S17, a zone of
        // the playlist spans several patterns, each at its own beat). Absent,
        // the pattern is heard where the piano roll's zone always was: its
        // first placement, or beat 0 in pattern mode.
        std::optional<double> songBeats{};

        // For a pattern the proposal would create: how long it is. It is then
        // heard at songBeats, which it requires.
        bool isNew{false};
        double lengthBeats{0.0};
    };

    ListeningHost() = default;
    virtual ~ListeningHost() = default;

    ListeningHost(const ListeningHost&) = delete;
    ListeningHost& operator=(const ListeningHost&) = delete;
    ListeningHost(ListeningHost&&) = delete;
    ListeningHost& operator=(ListeningHost&&) = delete;

    // Starts, or changes what is heard: the lines loop over their range.
    // Returns an empty string when they are heard, or why not, in French.
    [[nodiscard]] virtual std::string listen(const std::vector<Line>& lines) = 0;

    virtual void stop() = 0;
    [[nodiscard]] virtual bool listening() const = 0;

    // Called when the listening ended without being asked to stop here: the
    // person pressed play, stop, or moved the playhead.
    std::function<void()> onEnded;
};

} // namespace daw::ui
