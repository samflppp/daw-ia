#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/domain/tidy/Roles.h"
#include "daw/ui/model/ListeningHost.h"

#include <memory>
#include <string>
#include <vector>

namespace daw::ui
{

// A zone of the playlist over several lines, and what one prompt proposes for
// it: a part per line, each in the role its name or its content says (S17).
//
// It is GhostProposal a line at a time, and nothing more is asked of the
// generator: no new rule, no new constraint. What makes the parts one piece of
// music is the order they are drawn in — chords, then bass, then melody, then
// rhythm — and what each one hears while it is drawn. The generator reads the
// harmony in the other rows of the pattern it writes into; the zone gives it
// the parts already drawn there, on a copy of the project the proposal owns.
// The bass then lands on the chords' roots, the melody on their tones, and a
// key asked for once is the key of every part.
//
// Like GhostProposal, nothing here enters the project before accept(): no
// command, no history entry. One identifier is drawn early — the pattern of a
// line that has no block under the zone — because it has to be heard before
// it is written, and a pattern is heard by its identifier.
class ZoneProposal
{
public:
    struct Zone
    {
        std::vector<domain::LaneId> lanes; // top to bottom
        double fromBeats{0.0};             // in the song
        double toBeats{0.0};
    };

    // One line of the zone, and what is proposed on it.
    struct Part
    {
        domain::LaneId lane;
        domain::tidy::Guess guess; // why this role: « d'après le nom de la ligne « Basse » »
        domain::generation::Role role{domain::generation::Role::melody};
        domain::TrackId track;

        // Where it is written: the pattern of the block under the zone, or a
        // pattern the proposal creates (isNew), laid at songBeats.
        domain::PatternId pattern;
        bool isNew{false};
        std::string name;        // the new pattern's name: « Basse »
        double songBeats{0.0};   // where the pattern's laying starts in the song
        double lengthBeats{0.0}; // the new pattern's length
        double fromBeats{0.0};   // in the pattern
        double toBeats{0.0};

        domain::generation::ResolvedConstraints constraints{};
        std::vector<domain::generation::GhostNote> notes; // in the pattern
    };

    // A line of the zone nothing is proposed for, and why, in French.
    struct Skipped
    {
        domain::LaneId lane;
        std::string why;
    };

    [[nodiscard]] static domain::Result<ZoneProposal>
    open(const domain::ProjectState& state,
         Zone zone,
         domain::generation::Interpretation interpretation,
         std::shared_ptr<const domain::generation::StyleModel> model,
         const domain::tidy::PresetNames& presets = {});

    [[nodiscard]] const std::vector<Part>& parts() const noexcept { return parts_; }
    [[nodiscard]] const std::vector<Skipped>& skipped() const noexcept { return skipped_; }
    [[nodiscard]] const Zone& zone() const noexcept { return zone_; }

    // The next or the previous variant of the whole: every part moves, and
    // the later ones are drawn again over the earlier ones. Returns the rank.
    int shift(int delta);
    [[nodiscard]] int rank() const noexcept { return rank_; }
    [[nodiscard]] int drawn() const noexcept { return drawn_; }

    // Whether the project changed under the zone since the proposal was
    // drawn: a line, a block, a pattern it reads. The caller closes it then.
    [[nodiscard]] bool stale(const domain::ProjectState& state) const;

    // One group, one history entry, whatever the number of lines: the
    // patterns created, their rows, their layings, and every note.
    struct Acceptance
    {
        std::vector<std::unique_ptr<domain::Command>> commands;
        domain::GroupOptions group;
    };
    [[nodiscard]] Acceptance accept(const domain::ProjectState& state) const;

    // What ▶ Écouter plays: every part, where it would be.
    [[nodiscard]] std::vector<ListeningHost::Line> lines() const;

    // « quatre mesures en Fa# mineur : accords sur Accords, basse sur Basse,
    // mélodie sur Lead ».
    [[nodiscard]] std::string sentence(double beatsPerBar) const;

private:
    ZoneProposal() = default;

    void draw(const domain::ProjectState& state);
    [[nodiscard]] std::string signature(const domain::ProjectState& state) const;

    Zone zone_;
    domain::generation::Interpretation interpretation_;
    std::shared_ptr<const domain::generation::StyleModel> model_;
    std::vector<Part> parts_;
    std::vector<Skipped> skipped_;
    std::string signature_;
    int rank_{0};
    int drawn_{1};

    // The project as it was when opened, kept to draw the other variants: a
    // copy, never a pointer into the project.
    std::shared_ptr<const domain::ProjectState> origin_;
};

} // namespace daw::ui
