#include "TestSupport.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/tidy/Roles.h"

#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::tidy;
using daw::testing::Harness;

namespace
{

Note note(int pitch, double start, double length = 0.5)
{
    Note made{};
    made.id = NoteId::generate();
    made.pitch = pitch;
    made.velocity = 100;
    made.startBeats = start;
    made.lengthBeats = length;
    return made;
}

} // namespace

TEST_CASE("a name somebody chose says the role, whatever the notes")
{
    CHECK(familyOfName("808 Glide") == Family::bass);
    CHECK(familyOfName("Sub Bass 02") == Family::bass);
    CHECK(familyOfName("Basses") == Family::bass);
    CHECK(familyOfName("Kick01") == Family::kick);
    CHECK(familyOfName("Bass Drum") == Family::kick);
    CHECK(familyOfName("OpenHihat") == Family::hat);
    CHECK(familyOfName("Rhodes Chords") == Family::chords);
    CHECK(familyOfName("Mélodie") == Family::melody);
    CHECK(familyOfName("Dark Pad") == Family::pad);
    CHECK(familyOfName("Vox Chop") == Family::vocal);
    CHECK(familyOfName("Synth 3") == Family::unknown);

    // A word inside another is not a match below four letters: « shaded »
    // holds no hat, « subtle » is not a sub.
    CHECK(familyOfName("Shaded") == Family::unknown);
    CHECK(familyOfName("what") == Family::unknown);
}

TEST_CASE("a default track name says nothing, and the preset is read before the plugin")
{
    CHECK(isDefaultName(""));
    CHECK(isDefaultName("Piste 3"));
    CHECK(isDefaultName("Track 12"));
    CHECK_FALSE(isDefaultName("Piste basse"));
    CHECK_FALSE(isDefaultName("Kick"));

    Clues clues{};
    clues.trackName = "Piste 3";
    clues.presetName = "BA Sub Wobble";
    clues.pluginName = "Vital";
    auto guess = classify(clues);
    CHECK(guess.family == Family::bass);
    CHECK(guess.evidence == Evidence::presetName);
    CHECK(because(guess) == "d'après le preset « BA Sub Wobble »");

    // Chosen by a person, the track's name outranks its preset.
    clues.trackName = "Lead";
    CHECK(classify(clues).family == Family::melody);
    CHECK(classify(clues).evidence == Evidence::trackName);
}

TEST_CASE("without a name, the notes decide: stacked is chords, held stacked is a pad, low is a bass")
{
    Clues clues{};
    clues.trackName = "Piste 1";

    clues.notes = {note(60, 0.0), note(64, 0.0), note(67, 0.0)};
    CHECK(classify(clues).family == Family::chords);

    clues.notes = {note(60, 0.0, 4.0), note(64, 0.0, 4.0), note(67, 0.0, 4.0)};
    CHECK(classify(clues).family == Family::pad);

    clues.notes = {note(36, 0.0), note(38, 1.0), note(41, 2.0)};
    CHECK(classify(clues).family == Family::bass);
    CHECK(classify(clues).evidence == Evidence::notes);

    clues.notes = {note(72, 0.0), note(74, 1.0), note(76, 2.0)};
    CHECK(classify(clues).family == Family::melody);

    clues.notes.clear();
    CHECK(classify(clues).family == Family::unknown);
}

TEST_CASE("a line is read by its name, else by what is filed on it")
{
    Harness harness;
    REQUIRE(harness.bus.execute(std::make_unique<RenameTrack>(harness.trackId, "Piste 1")).ok());

    const auto patternId = PatternId::generate();
    const auto rowId = ClipId::generate();
    const auto line = LaneId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "", 4.0, false)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddPatternTrack>(patternId, rowId, harness.trackId)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(line, "", 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 0.0, line))
                .ok());
    for (const auto pitch : {33, 36, 40})
        REQUIRE(harness.bus.execute(std::make_unique<AddNote>(rowId, note(pitch, pitch - 33.0))).ok());

    CHECK(tracksOfLane(harness.state, line) == std::vector<TrackId>{harness.trackId});
    CHECK(classifyLane(harness.state, line).family == Family::bass);
    CHECK(classifyLane(harness.state, line).evidence == Evidence::notes);

    // The preset the engine reports outranks the notes.
    const PresetNames presets = [](TrackId) { return std::string{"Warm Keys"}; };
    CHECK(classifyLane(harness.state, line, presets).family == Family::chords);

    // And a name the user gave the line outranks everything.
    REQUIRE(harness.bus.execute(std::make_unique<RenameLane>(line, "Mélodie")).ok());
    CHECK(classifyLane(harness.state, line, presets).family == Family::melody);
    CHECK(classifyLane(harness.state, line).evidence == Evidence::lineName);
}

TEST_CASE("what the generator writes for each family, and nothing for the others")
{
    CHECK(generationRole(Family::bass) == generation::Role::bass);
    CHECK(generationRole(Family::pad) == generation::Role::chords);
    CHECK(generationRole(Family::hat) == generation::Role::rhythm);
    CHECK(generationRole(Family::melody) == generation::Role::melody);
    CHECK_FALSE(generationRole(Family::fx).has_value());
    CHECK_FALSE(generationRole(Family::unknown).has_value());
    CHECK(suggestedName(Family::hat) == "Hi-hat");
}
