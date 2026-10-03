#include "TestSupport.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/mix/Decision.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/domain/serialization/Json.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::mix;
using daw::testing::Harness;

namespace
{

// A measure written by hand: a level, a crest, an octave profile, playing the
// whole time. The 10 ms peak follows from the loudness and the crest.
StreamMeasure
measure(double lufs, double crest, std::array<double, bandCount> bands, double correlation = 1.0)
{
    StreamMeasure out;
    out.integratedLufs = lufs;
    out.crestDb = crest;
    out.bandsDb = bands;
    out.correlation = correlation;
    out.activeShare = 1.0;
    out.loudestTenMs = std::pow(10.0, (lufs + crest) / 10.0);
    out.truePeakDb = lufs + crest + 1.0;
    for (int hop = 0; hop < 100; ++hop)
    {
        std::array<float, bandCount> levels{};
        for (std::size_t band = 0; band < bandCount; ++band)
            levels[band] = static_cast<float>(bands[band]);
        out.hopBands.push_back(levels);
        out.hopActive.push_back(true);
    }
    return out;
}

// A small session: a kick and a bass that sit on 63 Hz, hats, chords and a
// voice that meet around 2 kHz, and a track nothing names.
struct Session
{
    Session()
    {
        add(kick, "Kick", -14.0, 16.0, {-30, -12, -20, -30, -40, -45, -50, -55, -60, -70});
        add(bass, "Basse", -12.0, 14.0, {-28, -13, -18, -28, -40, -50, -60, -70, -80, -90});
        add(hats, "Hi-hat", -20.0, 10.0, {-50, -45, -50, -50, -45, -40, -32, -28, -25, -30}, 0.98);
        add(chords, "Accords", -18.0, 8.0, {-60, -55, -30, -28, -26, -25, -22, -30, -40, -50});
        add(voice, "Voix", -17.0, 16.0, {-90, -80, -45, -32, -28, -25, -22, -28, -38, -50});
    }

    void add(TrackId id,
             const std::string& name,
             double lufs,
             double crest,
             std::array<double, bandCount> bands,
             double correlation = 1.0)
    {
        Track track{};
        track.id = id;
        track.name = name;
        REQUIRE(state.addTrack(track).ok());
        measures[id.toString()] = measure(lufs, crest, bands, correlation);
    }

    [[nodiscard]] Brief brief(Axes axes = {}) const
    {
        return briefOf(state,
                       measures,
                       measure(-12.0, 10.0, {-30, -14, -18, -25, -28, -26, -22, -26, -28, -35}),
                       axes,
                       Target{});
    }

    ProjectState state;
    std::map<std::string, StreamMeasure> measures;
    TrackId kick{TrackId::generate()};
    TrackId bass{TrackId::generate()};
    TrackId hats{TrackId::generate()};
    TrackId chords{TrackId::generate()};
    TrackId voice{TrackId::generate()};
};

const Change* find(const Proposal& proposal, TrackId track, Change::Kind kind)
{
    for (const auto& change : proposal.changes)
    {
        if (change.track == track && change.kind == kind)
            return &change;
    }
    return nullptr;
}

std::function<PluginId()> ids()
{
    return [] { return PluginId::generate(); };
}

} // namespace

TEST_CASE("mix roles: what the person chose, then the names, then the sound")
{
    Session session;
    const auto brief = session.brief();
    CHECK(brief.find(session.kick)->role.role == MixRole::kick);
    CHECK(brief.find(session.bass)->role.role == MixRole::bass);
    CHECK(brief.find(session.hats)->role.role == MixRole::hats);
    CHECK(brief.find(session.voice)->role.role == MixRole::vocal);

    // Chosen beats named.
    REQUIRE(session.state.setTrackRole(session.voice, MixRole::melody).ok());
    const auto chosen = session.brief().find(session.voice)->role;
    CHECK(chosen.role == MixRole::melody);
    CHECK(chosen.chosen);

    // A name that says nothing: the sound decides, and says why.
    const auto unnamed = TrackId::generate();
    session.add(unnamed, "Piste 7", -15.0, 15.0, {-25, -12, -25, -40, -50, -60, -70, -80, -90, -100});
    const auto guessed = session.brief().find(unnamed)->role;
    CHECK(guessed.role == MixRole::kick);
    CHECK(guessed.because.find("d'après le son") == 0);
}

TEST_CASE("the base mix carves the bass under the kick, with a sentence that cites the overlap")
{
    Session session;
    const auto brief = session.brief();
    const auto proposal = baseMix(brief);
    CHECK(proposal.decidedBy == "règles");

    const auto* carve = find(proposal, session.bass, Change::Kind::equaliser);
    REQUIRE(carve != nullptr);
    CHECK(carve->parameters.at(std::string{internal::mid1Frequency}) == 63.0);
    CHECK(carve->parameters.at(std::string{internal::mid1Gain}) < -2.0);
    MESSAGE(carve->sentence);
    CHECK(carve->sentence.find("63 Hz") != std::string::npos);
    CHECK(carve->sentence.find("100 %") != std::string::npos);

    // The chords make room for the voice around 2 kHz.
    const auto* room = find(proposal, session.chords, Change::Kind::equaliser);
    REQUIRE(room != nullptr);
    CHECK(room->parameters.count(std::string{internal::mid2Gain}) == 1);

    // The voice, far above the crest it wants, is compressed.
    const auto* squeeze = find(proposal, session.voice, Change::Kind::compressor);
    REQUIRE(squeeze != nullptr);
    MESSAGE(squeeze->sentence);

    // And nothing the rules propose is out of bounds or cites a wrong number.
    const auto refused = check(brief, proposal);
    for (const auto& refusal : refused)
        MESSAGE(refusal.why);
    CHECK(refused.empty());
}

TEST_CASE("masking is judged through the faders: a bass faded under the kick is not carved")
{
    Session session;
    REQUIRE_FALSE(session.brief().overlaps.empty());
    REQUIRE(session.state.setTrackVolume(session.bass, -12.0).ok());
    const auto brief = session.brief();
    for (const auto& overlap : brief.overlaps)
    {
        const auto first = brief.strips[overlap.first].track;
        const auto second = brief.strips[overlap.second].track;
        CHECK_FALSE(((first == session.kick && second == session.bass) ||
                     (first == session.bass && second == session.kick)));
    }
    CHECK(find(baseMix(brief), session.bass, Change::Kind::equaliser) == nullptr);
}

TEST_CASE("the guards refuse what is out of bounds, and a sentence that cites a false number")
{
    Session session;
    const auto brief = session.brief();
    const auto lufs = std::round(brief.find(session.bass)->measure.integratedLufs * 10.0) / 10.0;
    const auto cite = [&](TrackId track, Change change, double value, const std::string& measureName)
    {
        change.track = track;
        change.evidence = {{measureName, value}};
        change.sentence = "mesuré " + std::to_string(static_cast<int>(value));
        return change;
    };

    Proposal proposal;
    proposal.changes.push_back(
        cite(session.bass, Change{{}, Change::Kind::volume, 10.0, {}, {}, {}}, lufs, "lufs"));
    proposal.changes.push_back(
        cite(session.kick, Change{{}, Change::Kind::pan, 0.9, {}, {}, {}}, -14.0, "lufs"));
    proposal.changes.push_back(
        cite(session.bass,
             Change{{}, Change::Kind::equaliser, 0.0, {{std::string{internal::mid1Gain}, -9.0}}, {}, {}},
             -12.0,
             "lufs"));
    proposal.changes.push_back(
        cite(session.voice,
             Change{{},
                    Change::Kind::compressor,
                    0.0,
                    {{std::string{internal::ratio}, 10.0}, {std::string{internal::threshold}, -30.0}},
                    {},
                    {}},
             -17.0,
             "lufs"));
    proposal.changes.push_back(cite(
        session.voice,
        Change{{}, Change::Kind::equaliser, 0.0, {{std::string{internal::highPassFrequency}, 500.0}}, {}, {}},
        -17.0,
        "lufs"));
    proposal.changes.push_back(
        cite(session.hats, Change{{}, Change::Kind::volume, -3.0, {}, {}, {}}, -35.0, "lufs"));

    const auto refused = check(brief, proposal);
    std::set<std::size_t> which;
    for (const auto& refusal : refused)
    {
        MESSAGE(refusal.why);
        which.insert(refusal.change);
    }
    CHECK(which == std::set<std::size_t>{0, 1, 2, 3, 4, 5});

    // Muted by the person: untouched.
    REQUIRE(session.state.setTrackMuted(session.chords, true).ok());
    Proposal muted;
    muted.changes.push_back(
        cite(session.chords, Change{{}, Change::Kind::volume, -2.0, {}, {}, {}}, -18.0, "lufs"));
    CHECK(check(session.brief(), muted).size() == 1);
    CHECK(without(proposal, refused).changes.empty());
}

TEST_CASE("compile: existing verbs, the user's plugins left alone, one equaliser per track however often")
{
    Harness harness;
    Session session;
    auto& state = harness.state;
    for (const auto& track : session.state.tracks())
        REQUIRE(state.addTrack(track).ok());

    PluginInstance hosted{};
    hosted.id = PluginId::generate();
    hosted.ref = PluginRef{std::string{PluginRef::vst3Format}, "uid", "Saturateur"};
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(session.bass, hosted, 0)).ok());

    const auto before = json::write(state.toValue());
    const auto proposal =
        baseMix(briefOf(state, session.measures, Session{}.brief().master, Axes{}, Target{}));
    auto commands = compile(state, proposal, ids());
    REQUIRE_FALSE(commands.empty());
    for (const auto& command : commands)
    {
        const auto type = std::string{command->type()};
        CHECK((type == "track.set_volume" || type == "track.set_pan" || type == "plugin.insert" ||
               type == "plugin.set_parameter"));
    }
    GroupOptions group{};
    group.label = "Mixer";
    REQUIRE(harness.bus.executeGroup(std::move(commands), group).ok());

    const auto* bass = state.findTrack(session.bass);
    REQUIRE(bass->plugins.size() >= 2);
    CHECK(bass->plugins.front().id == hosted.id); // still first, untouched
    CHECK(bass->plugins.back().ref.format == PluginRef::internalFormat);

    // Mixed again on the state it left: nothing is stacked.
    const auto mixedAgain = compile(state, proposal, ids());
    for (const auto& command : mixedAgain)
        CHECK(std::string{command->type()} != "plugin.insert");
    const auto equalisers = std::count_if(bass->plugins.begin(),
                                          bass->plugins.end(),
                                          [](const PluginInstance& plugin)
                                          { return plugin.ref.identifier == internal::equaliser; });
    CHECK(equalisers == 1);

    // One Ctrl+Z: the project before, to the byte.
    REQUIRE(harness.bus.undo().ok());
    CHECK(json::write(state.toValue()) == before);
}

TEST_CASE("the axes move the decision without a new measure")
{
    Session session;
    const auto neutral = baseMix(session.brief());
    const auto front = baseMix(session.brief(Axes{0.0, -1.0, 0.0}));
    const auto back = baseMix(session.brief(Axes{0.0, 1.0, 0.0}));
    const auto voiceLevel = [&session](const Proposal& proposal)
    {
        const auto* change = find(proposal, session.voice, Change::Kind::volume);
        return change != nullptr ? change->value : 0.0;
    };
    CHECK(voiceLevel(front) > voiceLevel(neutral));
    CHECK(voiceLevel(back) < voiceLevel(neutral));

    const auto punchy = baseMix(session.brief(Axes{1.0, 0.0, 0.0}));
    const auto clean = baseMix(session.brief(Axes{-1.0, 0.0, 0.0}));
    const auto* hard = find(punchy, session.voice, Change::Kind::compressor);
    const auto* soft = find(clean, session.voice, Change::Kind::compressor);
    REQUIRE(hard != nullptr);
    if (soft != nullptr)
        CHECK(hard->parameters.at(std::string{internal::ratio}) >
              soft->parameters.at(std::string{internal::ratio}));

    const auto wide = baseMix(session.brief(Axes{0.0, 0.0, 1.0}));
    const auto tight = baseMix(session.brief(Axes{0.0, 0.0, -1.0}));
    CHECK(std::abs(find(wide, session.hats, Change::Kind::pan)->value) >
          std::abs(find(tight, session.hats, Change::Kind::pan)->value));
}

TEST_CASE("a reference becomes a target, and the axes lean towards it")
{
    Session session;
    const auto master = session.brief().master;
    auto reference = master;
    reference.crestDb = master.crestDb - 4.0;     // denser
    reference.sideShare = master.sideShare + 0.1; // wider
    const auto target = targetOf(reference, "reference.wav");
    CHECK(target.source == "reference.wav");
    const auto none = towards(target, master, Axes{}, 0.0);
    CHECK(none.punch == 0.0);
    const auto all = towards(target, master, Axes{}, 1.0);
    CHECK(all.punch > 0.5);
    CHECK(all.width > 0.5);
}

TEST_CASE("the master is trimmed under -1 dBTP, and only when it is over")
{
    ProjectState state;
    CHECK_FALSE(masterTrim(state, -1.5).has_value());
    const auto trim = masterTrim(state, 0.4);
    REQUIRE(trim.has_value());
    CHECK(*trim == doctest::Approx(-1.6));
}

TEST_CASE("a proposal survives its text, the way a model would send it")
{
    Session session;
    const auto proposal = baseMix(session.brief());
    const auto text = json::write(proposal.toValue());
    auto read = Proposal::fromValue(json::read(text).value());
    REQUIRE(read.ok());
    CHECK(json::write(read.value().toValue()) == text);
    CHECK(check(session.brief(), read.value()).empty());
}
