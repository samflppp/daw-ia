#include "TestSupport.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/model/ZoneProposal.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;
using daw::testing::Harness;
using daw::ui::ZoneProposal;

namespace
{

// Three channels named as a producer names them, and three empty lines named
// after what should go on them.
struct Session
{
    Harness harness;
    TrackId chords{TrackId::generate()};
    TrackId bass{TrackId::generate()};
    TrackId lead{TrackId::generate()};
    LaneId chordLine{LaneId::generate()};
    LaneId bassLine{LaneId::generate()};
    LaneId leadLine{LaneId::generate()};
    std::shared_ptr<const StyleModel> model{std::make_shared<const StyleModel>(StyleModel::fallback())};

    Session()
    {
        REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(chords, "Keys", 0.0)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(bass, "808", 0.0)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(lead, "Pluck", 0.0)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(chordLine, "Accords", 0)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(bassLine, "Basse", 1)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(leadLine, "Mélodie", 2)).ok());
    }

    [[nodiscard]] ZoneProposal open(std::vector<LaneId> lanes, const char* prompt = "boucle trap F#m")
    {
        auto opened = ZoneProposal::open(
            harness.state, {std::move(lanes), 0.0, 16.0}, LocalInterpreter::parse(prompt), model);
        REQUIRE(opened.ok());
        return std::move(opened).value();
    }
};

const ZoneProposal::Part& partOf(const ZoneProposal& proposal, Role role)
{
    for (const auto& part : proposal.parts())
    {
        if (part.role == role)
            return part;
    }
    FAIL("no part for the role");
    return proposal.parts().front();
}

} // namespace

TEST_CASE("a zone over three lines proposes chords, bass and melody, each on its line and its track")
{
    Session session;
    const auto before = json::write(session.harness.state.toValue());
    const auto depth = session.harness.bus.undoDepth();

    auto proposal = session.open({session.chordLine, session.bassLine, session.leadLine});
    REQUIRE(proposal.parts().size() == 3);
    CHECK(proposal.skipped().empty());

    const auto& chords = partOf(proposal, Role::chords);
    const auto& bass = partOf(proposal, Role::bass);
    const auto& melody = partOf(proposal, Role::melody);
    CHECK(chords.lane == session.chordLine);
    CHECK(chords.track == session.chords);
    CHECK(bass.track == session.bass);
    CHECK(melody.track == session.lead);
    CHECK(bass.isNew);
    CHECK(bass.name == "Basse");

    // One key asked for once, and every note in it.
    const Key fSharpMinor{6, Mode::minor};
    for (const auto& part : proposal.parts())
    {
        CHECK(part.constraints.key.value == fSharpMinor);
        REQUIRE_FALSE(part.notes.empty());
        for (const auto& note : part.notes)
            CHECK(inScale(note.pitch, fSharpMinor));
    }
    CHECK(proposal.sentence(4.0) == "quatre mesures en Fa# mineur : accords, basse et mélodie");

    // Nothing is written before it is accepted.
    CHECK(json::write(session.harness.state.toValue()) == before);
    CHECK(session.harness.bus.undoDepth() == depth);

    // One entry for the three lines, and one Ctrl+Z takes them all.
    auto acceptance = proposal.accept(session.harness.state);
    REQUIRE(session.harness.bus.executeGroup(std::move(acceptance.commands), acceptance.group).ok());
    CHECK(session.harness.bus.undoDepth() == depth + 1);
    CHECK(session.harness.state.patterns().size() == 3);

    std::map<LaneId, std::size_t> laid;
    for (const auto& placement : session.harness.state.arrangement())
        ++laid[placement.laneId];
    CHECK(laid[session.chordLine] == 1);
    CHECK(laid[session.bassLine] == 1);
    CHECK(laid[session.leadLine] == 1);

    const auto* written = session.harness.state.findPattern(bass.pattern);
    REQUIRE(written != nullptr);
    REQUIRE(written->findClipForTrack(session.bass) != nullptr);
    CHECK(written->findClipForTrack(session.bass)->notes.size() == bass.notes.size());

    REQUIRE(session.harness.bus.undo().ok());
    CHECK(json::write(session.harness.state.toValue()) == before);
}

TEST_CASE("the bass of a zone lands on the roots of the chords drawn above it")
{
    Session session;
    auto proposal = session.open({session.chordLine, session.bassLine});
    const auto& chords = partOf(proposal, Role::chords);
    const auto& bass = partOf(proposal, Role::bass);
    const Key key{6, Mode::minor};

    // Bar by bar: the chord the chords part plays, and the first bass note of
    // the bar, which has to be one of its tones.
    int bars = 0;
    int onChord = 0;
    for (double bar = 0.0; bar < 16.0; bar += 4.0)
    {
        std::vector<WeightedPitch> heard;
        for (const auto& note : chords.notes)
        {
            if (note.startBeats >= bar && note.startBeats < bar + 4.0)
                heard.push_back({note.pitch, note.lengthBeats});
        }
        const auto chord = detectChord(heard, key);
        const GhostNote* first = nullptr;
        for (const auto& note : bass.notes)
        {
            if (note.startBeats >= bar && note.startBeats < bar + 4.0 &&
                (first == nullptr || note.startBeats < first->startBeats))
                first = &note;
        }
        if (!chord.has_value() || first == nullptr)
            continue;
        ++bars;
        onChord += isChordTone(first->pitch, *chord, key) ? 1 : 0;
    }
    MESSAGE("bars with a chord and a bass: " << bars << ", bass on a chord tone: " << onChord);
    REQUIRE(bars > 0);
    CHECK(onChord == bars);
}

TEST_CASE("a line nobody can read is said and not guessed, and a block under the zone is written into")
{
    Session session;
    const auto fx = LaneId::generate();
    REQUIRE(session.harness.bus.execute(std::make_unique<CreateLane>(fx, "Risers", 3)).ok());

    // A bass pattern already laid on the bass line, over the whole zone.
    const auto existing = PatternId::generate();
    const auto row = ClipId::generate();
    REQUIRE(
        session.harness.bus.execute(std::make_unique<CreatePattern>(existing, "Old bass", 16.0, false)).ok());
    REQUIRE(session.harness.bus.execute(std::make_unique<AddPatternTrack>(existing, row, session.bass)).ok());
    REQUIRE(
        session.harness.bus
            .execute(std::make_unique<PlacePattern>(PlacementId::generate(), existing, 0.0, session.bassLine))
            .ok());

    auto proposal = session.open({session.bassLine, fx});
    REQUIRE(proposal.parts().size() == 1);
    CHECK_FALSE(proposal.parts().front().isNew);
    CHECK(proposal.parts().front().pattern == existing);
    REQUIRE(proposal.skipped().size() == 1);
    CHECK(proposal.skipped().front().lane == fx);

    const auto patterns = session.harness.state.patterns().size();
    auto acceptance = proposal.accept(session.harness.state);
    REQUIRE(session.harness.bus.executeGroup(std::move(acceptance.commands), acceptance.group).ok());
    CHECK(session.harness.state.patterns().size() == patterns); // written into, not beside
    CHECK_FALSE(session.harness.state.findClip(row)->notes.empty());
}

TEST_CASE("a zone knows when the project changed under it, and not when it changed elsewhere")
{
    Session session;
    auto proposal = session.open({session.chordLine, session.bassLine});
    CHECK_FALSE(proposal.stale(session.harness.state));

    REQUIRE(session.harness.bus.execute(std::make_unique<RenameLane>(session.leadLine, "Lead")).ok());
    CHECK_FALSE(proposal.stale(session.harness.state)); // not in the zone

    REQUIRE(session.harness.bus.execute(std::make_unique<RenameLane>(session.bassLine, "Sub")).ok());
    CHECK(proposal.stale(session.harness.state));
}

TEST_CASE("another variant of a zone moves every part, and the later ones follow the earlier")
{
    Session session;
    auto proposal = session.open({session.chordLine, session.bassLine});
    const auto chordsBefore = partOf(proposal, Role::chords).notes;
    const auto bassBefore = partOf(proposal, Role::bass).notes;

    CHECK(proposal.shift(+1) == 1);
    CHECK(partOf(proposal, Role::chords).notes != chordsBefore);
    CHECK(partOf(proposal, Role::bass).notes != bassBefore);

    CHECK(proposal.shift(-1) == 0);
    CHECK(partOf(proposal, Role::chords).notes == chordsBefore);
    CHECK(partOf(proposal, Role::bass).notes == bassBefore);
}
