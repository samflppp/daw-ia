#include "EngineTestSupport.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/serialization/Json.h"
#include "daw/engine/PitchDetection.h"
#include "daw/ui/model/GhostProposal.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;
using daw::testing::EngineHarness;
using daw::ui::GhostProposal;

// The proof of S14 is a render, not a state. The notes a proposal writes are
// played by the 4OSC fallback, rendered offline, and listened to: where each
// attack starts, and which pitch each note sounds at, measured on the audio.
// A pitch outside the key fails the test; it is not a warning.

namespace
{

constexpr double beatsPerMinute = 120.0;
constexpr double secondsPerStep = 60.0 / beatsPerMinute * stepBeats;

struct Heard
{
    std::vector<int> onsets;  // in sixteenths
    std::vector<int> pitches; // one per written note, measured; -1 when none
};

// Onsets on the sixteenth grid, the way the verification listens since S5: a
// window much louder than the one before it. Then, for each written note, the
// pitch of the audio in its first sixteenth, skipping the attack.
Heard listen(tracktion::Edit& edit, const std::vector<Note>& written)
{
    const auto rendered = tracktion::test_utilities::renderToAudioBuffer(edit);
    Heard heard{};
    REQUIRE(rendered.sampleRate > 0.0);

    const auto& audio = rendered.buffer;
    const auto samplesPerStep = static_cast<int>(std::lround(secondsPerStep * rendered.sampleRate));
    const auto steps = audio.getNumSamples() / samplesPerStep;

    // Per sixteenth: its level, and the pitch it sounds at past its first
    // few milliseconds. A change of pitch class counts, an octave jump does
    // not (see below).
    const auto skip = static_cast<int>(0.012 * rendered.sampleRate);
    std::vector<float> levels;
    std::vector<int> stepPitches;
    for (int step = 0; step < steps; ++step)
    {
        levels.push_back(audio.getRMSLevel(0, step * samplesPerStep, samplesPerStep));
        const auto hertz = daw::engine::fundamentalOf(audio.getReadPointer(0, step * samplesPerStep + skip),
                                                      samplesPerStep - skip,
                                                      rendered.sampleRate,
                                                      30.0,
                                                      1500.0);
        stepPitches.push_back(hertz > 0.0 ? daw::engine::midiPitchOf(hertz) : -1);
    }

    // An attack is a sixteenth much louder than the one before it, or a
    // sounding sixteenth at another pitch than the one before it: 4OSC holds
    // its level from one legato note to the next, and only the pitch says a
    // new one began.
    const auto loudest = levels.empty() ? 0.0f : *std::max_element(levels.begin(), levels.end());
    for (std::size_t step = 0; step < levels.size(); ++step)
    {
        const auto before = step == 0 ? 0.0f : levels[step - 1];
        const auto sounding = levels[step] > loudest * 0.05f;
        const auto louder = levels[step] > before * 1.25f;
        const auto moved = step > 0 && stepPitches[step] >= 0 && stepPitches[step - 1] >= 0 &&
                           stepPitches[step] % 12 != stepPitches[step - 1] % 12;
        if (sounding && (louder || moved))
            heard.onsets.push_back(static_cast<int>(step));
    }

    for (const auto& note : written)
    {
        const auto start =
            static_cast<int>(std::lround(note.startBeats * 60.0 / beatsPerMinute * rendered.sampleRate));
        const auto length =
            std::min(static_cast<int>(note.lengthBeats * 60.0 / beatsPerMinute * rendered.sampleRate),
                     static_cast<int>(0.12 * rendered.sampleRate)) -
            skip;
        if (length <= 0 || start + skip + length > audio.getNumSamples())
        {
            heard.pitches.push_back(-1);
            continue;
        }

        const auto hertz = daw::engine::fundamentalOf(
            audio.getReadPointer(0, start + skip), length, rendered.sampleRate, 30.0, 1500.0);
        heard.pitches.push_back(hertz > 0.0 ? daw::engine::midiPitchOf(hertz) : -1);
    }
    return heard;
}

// A proposal accepted the way Tab accepts it, and what it wrote, in time order.
std::vector<Note> acceptInto(EngineHarness& harness, ClipId row, const char* text, int variant)
{
    // The proposal keeps a reference to its model: it has to outlive it.
    static const auto model = StyleModel::fallback();
    auto opened = GhostProposal::open(harness.state,
                                      ProjectState::patternIdForClip(row),
                                      harness.trackId,
                                      0.0,
                                      16.0,
                                      LocalInterpreter::parse(text),
                                      model);
    REQUIRE(opened.ok());
    auto proposal = std::move(opened).value();
    static_cast<void>(proposal.shift(variant));

    auto acceptance = proposal.accept(harness.state, row);
    REQUIRE(harness.bus.executeGroup(std::move(acceptance.commands), acceptance.group).ok());

    auto notes = harness.state.findClip(row)->notes;
    std::sort(
        notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.startBeats < b.startBeats; });
    return notes;
}

// The attacks heard, by sixteenth of the whole range.
std::vector<int> provePlayed(const char* text, Key key, int variant)
{
    EngineHarness harness;
    const auto row = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(row, 0.0, 16.0)).ok());

    const auto written = acceptInto(harness, row, text, variant);
    REQUIRE_FALSE(written.empty());
    const auto heard = listen(harness.host.edit(), written);

    // Where the attacks are.
    std::set<int> starts;
    for (const auto& note : written)
        starts.insert(static_cast<int>(std::lround(note.startBeats / stepBeats)));

    std::string onsetText;
    int offNote = 0;
    for (const auto onset : heard.onsets)
    {
        onsetText += std::to_string(onset) + " ";
        if (starts.count(onset) == 0)
            ++offNote;
    }
    MESSAGE(std::string{text} << ", variant " << variant << ": " << written.size()
                              << " notes, onsets heard at " << onsetText);

    CHECK(offNote == 0); // no attack where no note starts

    // Every start that can be heard is heard. The one that cannot: a note
    // tied to the one before it, at the same pitch class, since 4OSC then
    // neither rises nor changes pitch.
    std::set<int> audible;
    for (std::size_t i = 0; i < written.size(); ++i)
    {
        const auto tied =
            i > 0 && written[i - 1].pitch % 12 == written[i].pitch % 12 &&
            written[i - 1].startBeats + written[i - 1].lengthBeats >= written[i].startBeats - 1e-6;
        if (!tied)
            audible.insert(static_cast<int>(std::lround(written[i].startBeats / stepBeats)));
    }
    int missed = 0;
    for (const auto start : audible)
    {
        if (std::find(heard.onsets.begin(), heard.onsets.end(), start) == heard.onsets.end())
            ++missed;
    }
    MESSAGE(audible.size() << " audible starts, " << missed << " missed");
    CHECK(missed == 0);

    // What each note sounds at.
    std::size_t measured = 0;
    for (std::size_t i = 0; i < written.size(); ++i)
    {
        CHECK(inScale(written[i].pitch, key));
        if (heard.pitches[i] < 0)
            continue;
        ++measured;
        CHECK(inScale(heard.pitches[i], key)); // out of the key is a failure, not a warning
        // The class exactly; the octave within one. Under 100 Hz YIN can lock
        // on twice the period of 4OSC's wave, which is a fact about the
        // detector, not about the note: the class is what the key judges.
        CHECK(heard.pitches[i] % 12 == written[i].pitch % 12);
        CHECK(std::abs(heard.pitches[i] - written[i].pitch) <= 12);
    }
    MESSAGE(measured << " of " << written.size() << " pitches measured");
    CHECK(measured * 10 >= written.size() * 9);
    return heard.onsets;
}

// The attacks heard in one bar, inside it. The downbeat is left out: a bar
// that starts on the pitch class the bar before ended on, tied to it, gives
// no attack there, and that says nothing about the rhythm of the bar.
std::set<int> heardInBar(const std::vector<int>& onsets, int bar)
{
    std::set<int> out;
    for (const auto onset : onsets)
    {
        if (onset > bar * 16 && onset < (bar + 1) * 16)
            out.insert(onset - bar * 16);
    }
    return out;
}

} // namespace

TEST_CASE("A generated melody sounds at its attacks and at its pitches, all in the key")
{
    for (int variant = 0; variant < 3; ++variant)
        provePlayed("Am mélodie", Key{9, Mode::minor}, variant);
}

TEST_CASE("A generated bass sounds at its attacks and at its pitches, all in the key")
{
    for (int variant = 0; variant < 2; ++variant)
        provePlayed("F#m basse croches", Key{6, Mode::minor}, variant);
}

TEST_CASE("An AABA melody is heard as one bar three times and another bar in third place")
{
    for (int variant = 0; variant < 3; ++variant)
    {
        const auto onsets = provePlayed("Am mélodie AABA", Key{9, Mode::minor}, variant);
        const auto first = heardInBar(onsets, 0);
        CHECK(heardInBar(onsets, 1) == first);
        CHECK(heardInBar(onsets, 3) == first);
        CHECK(heardInBar(onsets, 2) != first);
    }
}

// --- S16: listening before writing ----------------------------------------------------
//
// The proof is a render, and the state around it. While a proposal is listened
// to, the render has its attacks and its pitches in the range, and not the
// notes it would replace; the notes outside the range still sound. The project
// and the history do not move by one byte. When the listening stops, the
// render is the one from before.

namespace
{

Note plain(int pitch, double start, double length)
{
    Note out{};
    out.pitch = pitch;
    out.velocity = 100;
    out.startBeats = start;
    out.lengthBeats = length;
    return out;
}

std::set<int> onsetsOf(tracktion::Edit& edit)
{
    const auto heard = listen(edit, {});
    return {heard.onsets.begin(), heard.onsets.end()};
}

std::string spelled(const std::set<int>& onsets)
{
    std::string out;
    for (const auto onset : onsets)
        out += std::to_string(onset) + " ";
    return out;
}

} // namespace

TEST_CASE("Listening: the proposal is heard in its range, the project and its history do not move, and "
          "stopping gives the render back")
{
    EngineHarness harness;
    const auto row = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(row, 0.0, 16.0)).ok());

    // The person's notes: one before the range, one inside it.
    auto kept = plain(57, 0.0, 1.0);
    kept.id = NoteId::generate();
    auto replaced = plain(64, 8.0, 1.0);
    replaced.id = NoteId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(row, kept)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(row, replaced)).ok());

    const auto before = onsetsOf(harness.host.edit());
    MESSAGE("before: " << spelled(before));
    CHECK(before == std::set<int>{0, 32});

    const auto bytes = json::write(harness.state.toValue());
    const auto depth = harness.bus.undoDepth();

    // Two notes proposed in beats 4 to 12: at 4 and at 6.
    daw::engine::ProjectProjector::Audition audition{harness.trackId,
                                                     ProjectState::patternIdForClip(row),
                                                     4.0,
                                                     12.0,
                                                     {plain(67, 4.0, 1.0), plain(69, 6.0, 1.0)}};
    harness.projector.listen({audition}, 4.0, 12.0);
    CHECK(harness.projector.listening());

    const std::vector<Note> heardNotes{plain(57, 0.0, 1.0), plain(67, 4.0, 1.0), plain(69, 6.0, 1.0)};
    const auto heard = listen(harness.host.edit(), heardNotes);
    const std::set<int> during{heard.onsets.begin(), heard.onsets.end()};
    MESSAGE("listening: " << spelled(during));
    CHECK(during == std::set<int>{0, 16, 24}); // the note kept, the two proposed; not the one replaced
    CHECK(heard.pitches[1] % 12 == 67 % 12);
    CHECK(heard.pitches[2] % 12 == 69 % 12);

    CHECK(json::write(harness.state.toValue()) == bytes); // the project, to the byte
    CHECK(harness.bus.undoDepth() == depth);              // no history entry

    // Another variant: the same range, other notes.
    audition.notes = {plain(72, 10.0, 1.0)};
    harness.projector.listen({audition}, 4.0, 12.0);
    CHECK(onsetsOf(harness.host.edit()) == std::set<int>{0, 40});

    harness.projector.stopListening();
    CHECK_FALSE(harness.projector.listening());
    CHECK(onsetsOf(harness.host.edit()) == before);
    CHECK(json::write(harness.state.toValue()) == bytes);
    CHECK(harness.bus.undoDepth() == depth);
}

TEST_CASE("Listening: a row the proposal would open is heard, and leaves with the listening")
{
    EngineHarness harness;
    const auto row = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(row, 0.0, 16.0)).ok());
    const auto pattern = ProjectState::patternIdForClip(row);

    // A second channel, with no row in the pattern yet.
    const auto lead = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(lead, "Lead", 0.0)).ok());
    REQUIRE(harness.state.findPattern(pattern)->findClipForTrack(lead) == nullptr);
    const auto bytes = json::write(harness.state.toValue());

    CHECK(onsetsOf(harness.host.edit()).empty());

    harness.projector.listen(
        {{lead, pattern, 0.0, 16.0, {plain(60, 2.0, 1.0), plain(62, 3.0, 1.0)}}}, 0.0, 16.0);
    CHECK(onsetsOf(harness.host.edit()) == std::set<int>{8, 12});
    CHECK(harness.state.findPattern(pattern)->findClipForTrack(lead) == nullptr);

    harness.projector.stopListening();
    CHECK(onsetsOf(harness.host.edit()).empty());
    CHECK(json::write(harness.state.toValue()) == bytes);
}

TEST_CASE("Listening: a transport command ends it, and the project is what plays")
{
    EngineHarness harness;
    const auto row = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(row, 0.0, 16.0)).ok());
    auto note = plain(60, 0.0, 1.0);
    note.id = NoteId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(row, note)).ok());

    int ended = 0;
    harness.projector.onListeningEnded = [&ended] { ++ended; };
    harness.projector.listen(
        {{harness.trackId, ProjectState::patternIdForClip(row), 0.0, 16.0, {plain(65, 4.0, 1.0)}}},
        0.0,
        16.0);
    CHECK(onsetsOf(harness.host.edit()) == std::set<int>{16});

    REQUIRE(harness.bus.execute(std::make_unique<TransportStop>()).ok());
    CHECK_FALSE(harness.projector.listening());
    CHECK(ended == 1);
    CHECK(onsetsOf(harness.host.edit()) == std::set<int>{0});
}

TEST_CASE(
    "Listening: patterns a zone would create are heard where they would be laid, several tracks at once")
{
    // S17: a zone of the playlist proposes a block per line, most of them in
    // patterns that do not exist yet. They are heard at the beat they would
    // be laid at, and leave with the listening; the project never holds them.
    EngineHarness harness;
    const auto room = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(room, 0.0, 16.0)).ok());
    const auto bass = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(bass, "Bass", 0.0)).ok());
    const auto bytes = json::write(harness.state.toValue());
    const auto depth = harness.bus.undoDepth();

    CHECK(onsetsOf(harness.host.edit()).empty());

    daw::engine::ProjectProjector::Audition chords{
        harness.trackId, PatternId::generate(), 0.0, 4.0, {plain(60, 0.0, 1.0)}};
    chords.newAtBeats = 8.0;
    chords.newLengthBeats = 4.0;
    daw::engine::ProjectProjector::Audition line{
        bass, PatternId::generate(), 0.0, 4.0, {plain(40, 2.0, 1.0)}};
    line.newAtBeats = 8.0;
    line.newLengthBeats = 4.0;

    harness.projector.listen({chords, line}, 8.0, 12.0);
    CHECK(onsetsOf(harness.host.edit()) == std::set<int>{32, 40}); // beats 8 and 10
    CHECK(json::write(harness.state.toValue()) == bytes);
    CHECK(harness.bus.undoDepth() == depth);

    harness.projector.stopListening();
    CHECK(onsetsOf(harness.host.edit()).empty());
    CHECK(json::write(harness.state.toValue()) == bytes);
}
