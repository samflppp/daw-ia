#include "TestSupport.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/ui/model/GhostProposal.h"

#include <memory>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;
using daw::testing::Harness;
using daw::testing::RecordingObserver;
using daw::ui::GhostProposal;

namespace
{

// One row of four bars, with two notes the generator may replace: one at the
// start of bar 2, one in bar 4, outside the range the tests use.
struct Row
{
    Row()
    {
        REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 16.0)).ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clipId, inside, 69)).ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clipId, outside, 64)).ok());

        // addNote puts both at beat 0: move them where the tests want them.
        REQUIRE(harness.bus.execute(std::make_unique<MoveNote>(clipId, inside, 69, 4.0)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<MoveNote>(clipId, outside, 64, 13.0)).ok());
        harness.bus.clearHistory();
    }

    [[nodiscard]] PatternId pattern() const { return ProjectState::patternIdForClip(clipId); }

    [[nodiscard]] GhostProposal propose(const char* text = "Am")
    {
        auto opened = GhostProposal::open(
            harness.state, pattern(), harness.trackId, 4.0, 12.0, LocalInterpreter::parse(text), model);
        REQUIRE(opened.ok());
        return std::move(opened).value();
    }

    Harness harness;
    StyleModel model{StyleModel::fallback()};
    ClipId clipId{ClipId::generate()};
    NoteId inside{NoteId::generate()};
    NoteId outside{NoteId::generate()};
};

} // namespace

TEST_CASE("a proposal changes nothing until it is accepted")
{
    Row row;
    RecordingObserver observer;
    const auto token = row.harness.bus.addObserver(observer);
    const auto before = row.harness.state.toValue();
    const auto journal = row.harness.bus.journal();

    // Twice, without accepting, and every variant looked at.
    auto first = row.propose();
    auto second = row.propose("doubles dense");
    CHECK_FALSE(first.notes().empty());
    CHECK_FALSE(second.notes().empty());
    for (int i = 0; i < 5; ++i)
        static_cast<void>(second.shift(+1));
    CHECK_FALSE(second.notes().empty());

    CHECK(row.harness.state.toValue() == before);
    CHECK(row.harness.bus.journal() == journal);
    CHECK(row.harness.bus.undoDepth() == 0);
    CHECK(observer.executed.empty());
    row.harness.bus.removeObserver(token);
}

TEST_CASE("Tab writes one group from the generator, and one Ctrl+Z gives the project back")
{
    Row row;
    const auto before = row.harness.state.toValue();
    auto proposal = row.propose();
    const auto shown = proposal.notes();
    REQUIRE_FALSE(shown.empty());

    auto acceptance = proposal.accept(row.harness.state, row.clipId);
    const auto added = acceptance.added;
    auto receipts = row.harness.bus.executeGroup(std::move(acceptance.commands), acceptance.group);
    REQUIRE(receipts.ok());
    CHECK(row.harness.bus.undoDepth() == 1);
    for (const auto& receipt : receipts.value())
        CHECK(receipt.origin.actor == Actor::generator);
    CHECK(receipts.value().front().group->label == "génération : Am mélodie");

    // The note inside the range is gone, the one outside stays, and what is
    // written is what was shown.
    const auto* clip = row.harness.state.findClip(row.clipId);
    REQUIRE(clip != nullptr);
    CHECK(row.harness.state.noteIndex(row.clipId, row.inside).code() == ErrorCode::notFound);
    CHECK(row.harness.state.noteIndex(row.clipId, row.outside).ok());
    CHECK(clip->notes.size() == shown.size() + 1);
    for (std::size_t i = 0; i < added.size(); ++i)
    {
        const auto index = row.harness.state.noteIndex(row.clipId, added[i]);
        REQUIRE(index.ok());
        const auto& written = clip->notes[index.value()];
        CHECK(written.pitch == shown[i].pitch);
        CHECK(written.startBeats == shown[i].startBeats);
        CHECK(inScale(written.pitch, Key{9, Mode::minor}));
    }

    REQUIRE(row.harness.bus.undo().ok());
    CHECK(row.harness.state.toValue() == before);
}

TEST_CASE("an undo of something else regenerates only when it touched the context")
{
    Row row;
    auto proposal = row.propose();
    const auto shown = proposal.notes();

    // A mix change is not the context.
    REQUIRE(row.harness.bus.execute(row.harness.setVolume(-6.0)).ok());
    CHECK(proposal.refresh(row.harness.state) == GhostProposal::Refresh::unchanged);
    REQUIRE(row.harness.bus.undo().ok());
    CHECK(proposal.refresh(row.harness.state) == GhostProposal::Refresh::unchanged);
    CHECK(proposal.notes() == shown);

    // A note of the row is.
    REQUIRE(row.harness.bus.execute(Harness::addNote(row.clipId, NoteId::generate(), 72)).ok());
    CHECK(proposal.refresh(row.harness.state) == GhostProposal::Refresh::regenerated);
    REQUIRE(row.harness.bus.undo().ok());
    CHECK(proposal.refresh(row.harness.state) == GhostProposal::Refresh::regenerated);
    CHECK(proposal.notes() == shown); // the same context gives the same notes back

    // The pattern gone closes it.
    REQUIRE(row.harness.bus.execute(std::make_unique<RemovePattern>(row.pattern())).ok());
    CHECK(proposal.refresh(row.harness.state) == GhostProposal::Refresh::closed);
}

TEST_CASE("the wheel walks through distinct variants and stops at the ends")
{
    Row row;
    auto proposal = row.propose("doubles");
    const auto first = proposal.notes();
    CHECK(proposal.shift(-1) == 0);
    CHECK(proposal.shift(+1) == 1);
    CHECK(proposal.notes() != first);
    CHECK(proposal.shift(+100) <= 15);
    CHECK(proposal.shift(-100) == 0);
    CHECK(proposal.notes() == first);
}
