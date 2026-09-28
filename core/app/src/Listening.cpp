#include "Listening.h"

#include <algorithm>
#include <limits>

namespace daw::app
{

Listening::Listening(engine::ProjectProjector& projector, const domain::ProjectState& state)
    : projector_(projector)
    , state_(state)
{
    projector_.onListeningEnded = [this]
    {
        if (onEnded)
            onEnded();
    };
}

Listening::~Listening()
{
    projector_.onListeningEnded = nullptr;
}

std::string Listening::listen(const std::vector<Line>& lines)
{
    if (lines.empty())
    {
        stop();
        return {};
    }

    // Where the pattern sits on the Edit's timeline.
    const auto& transport = state_.transport();
    const auto patternId = lines.front().pattern;
    double offset = 0.0;
    if (transport.mode == domain::PlayMode::pattern)
    {
        if (transport.auditionedPattern != patternId)
            return "Ce pattern n'est pas celui que joue le mode PAT.";
    }
    else
    {
        const auto placements = state_.placementsOf(patternId);
        if (placements.empty())
            return "Ce pattern n'est pas dans la playlist : passe en mode PAT pour l'écouter.";
        offset = (*std::min_element(placements.begin(),
                                    placements.end(),
                                    [](const auto* lhs, const auto* rhs)
                                    { return lhs->startBeats < rhs->startBeats; }))
                     ->startBeats;
    }

    auto from = std::numeric_limits<double>::max();
    auto to = std::numeric_limits<double>::lowest();
    std::vector<engine::ProjectProjector::Audition> auditions;
    for (const auto& line : lines)
    {
        engine::ProjectProjector::Audition audition{
            line.track, line.pattern, line.fromBeats, line.toBeats, {}};
        for (const auto& ghost : line.notes)
        {
            domain::Note note{};
            note.pitch = ghost.pitch;
            note.velocity = ghost.velocity;
            note.startBeats = ghost.startBeats;
            note.lengthBeats = ghost.lengthBeats;
            audition.notes.push_back(note);
        }
        auditions.push_back(std::move(audition));
        from = std::min(from, line.fromBeats);
        to = std::max(to, line.toBeats);
    }

    projector_.listen(std::move(auditions), offset + from, offset + to);
    return {};
}

void Listening::stop()
{
    projector_.stopListening();
}

} // namespace daw::app
