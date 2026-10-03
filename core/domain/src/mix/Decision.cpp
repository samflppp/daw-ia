#include "daw/domain/mix/Decision.h"

#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/InternalEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>

namespace daw::domain::mix
{
namespace
{

using Kind = Change::Kind;

double clampTo(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}

// « -3,5 » : a decimal comma, one digit, the way the screen writes.
std::string french(double value, int decimals = 1)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    std::string out{text};
    std::replace(out.begin(), out.end(), '.', ',');
    if (out == "-0" || out == "-0,0")
        out = out.substr(1);
    return out;
}

std::string bandWord(std::size_t band)
{
    return bandName(band);
}

bool isDrum(MixRole role)
{
    return role == MixRole::kick || role == MixRole::snare || role == MixRole::hats ||
           role == MixRole::percussion;
}

bool staysCentred(MixRole role)
{
    return role == MixRole::kick || role == MixRole::bass || role == MixRole::vocal || role == MixRole::snare;
}

std::optional<MixRole> fromFamily(tidy::Family family)
{
    using tidy::Family;
    switch (family)
    {
    case Family::kick:
        return MixRole::kick;
    case Family::snare:
    case Family::clap:
        return MixRole::snare;
    case Family::hat:
        return MixRole::hats;
    case Family::percussion:
        return MixRole::percussion;
    case Family::bass:
        return MixRole::bass;
    case Family::chords:
    case Family::pad:
        return MixRole::chords;
    case Family::melody:
        return MixRole::melody;
    case Family::vocal:
        return MixRole::vocal;
    case Family::fx:
        return MixRole::fx;
    case Family::unknown:
        break;
    }
    return std::nullopt;
}

// Energy-weighted mean of the band indices: where a sound sits.
double centreOf(const StreamMeasure& measure)
{
    double weight = 0.0;
    double sum = 0.0;
    for (std::size_t band = 0; band < bandCount; ++band)
    {
        const auto power = std::pow(10.0, measure.bandsDb[band] / 10.0);
        weight += power;
        sum += power * static_cast<double>(band);
    }
    return weight > 0.0 ? sum / weight : 5.0;
}

double peakTenMsDb(const StreamMeasure& measure)
{
    return measure.loudestTenMs > 0.0 ? 10.0 * std::log10(measure.loudestTenMs) : silenceDb;
}

std::size_t loudestBand(const StreamMeasure& measure)
{
    return static_cast<std::size_t>(std::distance(
        measure.bandsDb.begin(), std::max_element(measure.bandsDb.begin(), measure.bandsDb.end())));
}

// The level a role is placed at in the mix, post-fader, LUFS. A balance and
// not a loudness target: the master is not mastered.
double roleLevel(MixRole role)
{
    switch (role)
    {
    case MixRole::kick:
        return -16.0;
    case MixRole::snare:
        return -18.0;
    case MixRole::hats:
        return -26.0;
    case MixRole::percussion:
        return -24.0;
    case MixRole::bass:
        return -17.0;
    case MixRole::chords:
        return -22.0;
    case MixRole::melody:
        return -19.0;
    case MixRole::vocal:
        return -16.0;
    case MixRole::fx:
        return -26.0;
    }
    return -22.0;
}

// The crest a role wants, dB, over 10 ms windows; nothing: not compressed.
std::optional<double> roleCrest(MixRole role)
{
    switch (role)
    {
    case MixRole::kick:
    case MixRole::snare:
    case MixRole::percussion:
        return 12.0;
    case MixRole::bass:
        return 8.0;
    case MixRole::chords:
    case MixRole::melody:
        return 9.0;
    case MixRole::vocal:
        return 7.0;
    case MixRole::hats:
    case MixRole::fx:
        break;
    }
    return std::nullopt;
}

double roleAttack(MixRole role)
{
    if (isDrum(role))
        return 15.0;
    if (role == MixRole::bass)
        return 5.0;
    if (role == MixRole::vocal)
        return 3.0;
    return 10.0;
}

double roleHighPass(MixRole role)
{
    switch (role)
    {
    case MixRole::kick:
    case MixRole::bass:
        return 30.0;
    case MixRole::snare:
        return 120.0;
    case MixRole::hats:
        return 300.0;
    case MixRole::vocal:
        return 90.0;
    case MixRole::percussion:
    case MixRole::chords:
    case MixRole::melody:
    case MixRole::fx:
        return 150.0;
    }
    return 100.0;
}

// The number a measure holds under a name of the evidence vocabulary.
std::optional<double> measured(const Brief& brief, const Brief::Strip& strip, const std::string& name)
{
    const auto& m = strip.measure;
    if (name == "lufs")
        return m.integratedLufs;
    if (name == "crest")
        return m.crestDb;
    if (name == "truePeak")
        return m.truePeakDb;
    if (name == "correlation")
        return m.correlation;
    if (name == "active")
        return m.activeShare * 100.0;
    if (name == "peak10ms")
        return peakTenMsDb(m);
    if (name.starts_with("bands."))
    {
        const auto band = static_cast<std::size_t>(std::stoul(name.substr(6)));
        return band < bandCount ? std::optional<double>{m.bandsDb[band]} : std::nullopt;
    }
    if (name.starts_with("overlap."))
    {
        // overlap.<band>.<track>: the share, in percent, of the time both play.
        const auto dot = name.find('.', 8);
        if (dot == std::string::npos)
            return std::nullopt;
        const auto band = static_cast<std::size_t>(std::stoul(name.substr(8, dot - 8)));
        const auto other = TrackId::parse(name.substr(dot + 1));
        if (!other)
            return std::nullopt;
        std::size_t self = brief.strips.size();
        std::size_t with = brief.strips.size();
        for (std::size_t index = 0; index < brief.strips.size(); ++index)
        {
            if (brief.strips[index].track == strip.track)
                self = index;
            if (brief.strips[index].track == other.value())
                with = index;
        }
        for (const auto& overlap : brief.overlaps)
        {
            const bool pair = (overlap.first == self && overlap.second == with) ||
                              (overlap.first == with && overlap.second == self);
            if (pair && overlap.band == band)
                return overlap.share * 100.0;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// Whether the sentence writes the number, the way a person reads it: rounded
// to a unit or to a tenth, comma or point.
bool mentions(const std::string& sentence, double value)
{
    for (const auto& written : {french(value, 0),
                                french(value, 1),
                                french(value, 2),
                                french(std::abs(value), 0),
                                french(std::abs(value), 1),
                                french(std::abs(value), 2)})
    {
        if (sentence.find(written) != std::string::npos)
            return true;
        auto pointed = written;
        std::replace(pointed.begin(), pointed.end(), ',', '.');
        if (sentence.find(pointed) != std::string::npos)
            return true;
    }
    return false;
}

Value parametersValue(const std::map<std::string, double>& parameters)
{
    Value::Object members;
    for (const auto& [name, value] : parameters)
        members.emplace_back(name, Value{std::round(value * 100.0) / 100.0});
    return Value::object(std::move(members));
}

std::string kindName(Kind kind)
{
    switch (kind)
    {
    case Kind::volume:
        return "volume";
    case Kind::pan:
        return "pan";
    case Kind::equaliser:
        return "equaliser";
    case Kind::compressor:
        return "compressor";
    }
    return "volume";
}

std::optional<Kind> kindOf(const std::string& name)
{
    for (const auto kind : {Kind::volume, Kind::pan, Kind::equaliser, Kind::compressor})
    {
        if (kindName(kind) == name)
            return kind;
    }
    return std::nullopt;
}

const PluginInstance* lastInternal(const Track& track, std::string_view identifier)
{
    const PluginInstance* found = nullptr;
    for (const auto& plugin : track.plugins)
    {
        if (plugin.ref.format == PluginRef::internalFormat && plugin.ref.identifier == identifier)
            found = &plugin;
    }
    return found;
}

std::map<std::string, double> internalParameters(const PluginInstance* plugin)
{
    std::map<std::string, double> values;
    if (plugin == nullptr)
        return values;
    if (const auto* effect = findInternalEffect(plugin->ref.identifier); effect != nullptr)
    {
        for (const auto& parameter : effect->parameters)
            values[std::string{parameter.id}] = internalValue(*plugin, parameter.id);
    }
    return values;
}

} // namespace

// --- roles ---------------------------------------------------------------------

std::string roleLabel(MixRole role)
{
    switch (role)
    {
    case MixRole::kick:
        return "kick";
    case MixRole::snare:
        return "caisse claire";
    case MixRole::hats:
        return "charleys";
    case MixRole::percussion:
        return "percussions";
    case MixRole::bass:
        return "basse";
    case MixRole::chords:
        return "accords";
    case MixRole::melody:
        return "mélodie";
    case MixRole::vocal:
        return "voix";
    case MixRole::fx:
        return "effets";
    }
    return "effets";
}

RoleGuess guessRole(const ProjectState& state,
                    TrackId track,
                    const StreamMeasure* measure,
                    const tidy::PresetNames& presets)
{
    RoleGuess guess;
    const auto* strip = state.findTrack(track);
    if (strip == nullptr)
        return guess;

    if (strip->role.has_value())
    {
        guess.role = *strip->role;
        guess.known = true;
        guess.chosen = true;
        guess.because = "choisi par toi";
        return guess;
    }

    const auto named = tidy::classifyTrack(state, track, presets);
    if (const auto role = fromFamily(named.family); role.has_value())
    {
        guess.role = *role;
        guess.known = true;
        guess.because = tidy::because(named);
        return guess;
    }

    if (measure != nullptr && measure->integratedLufs > -70.0)
    {
        const auto centre = centreOf(*measure);
        const auto hz = bandCentres[static_cast<std::size_t>(std::lround(clampTo(centre, 0.0, 9.0)))];
        guess.known = true;
        guess.because = "d'après le son : énergie vers " + french(hz, 0) + " Hz, facteur de crête " +
                        french(measure->crestDb) + " dB";
        if (centre < 1.8)
            guess.role = measure->crestDb > 12.0 ? MixRole::kick : MixRole::bass;
        else if (centre >= 7.0)
            guess.role = MixRole::hats;
        else if (measure->crestDb > 14.0)
            guess.role = MixRole::snare;
        else
        {
            guess.role = MixRole::melody;
            guess.known = false; // a mid-range sound: chords, lead or voice, the sound does not say
        }
        return guess;
    }

    guess.because = "rien ne le dit";
    return guess;
}

// --- axes and targets ----------------------------------------------------------

Value Axes::toValue() const
{
    return Value::object({{"punch", Value{std::round(punch * 100.0) / 100.0}},
                          {"focus", Value{std::round(focus * 100.0) / 100.0}},
                          {"width", Value{std::round(width * 100.0) / 100.0}}});
}

Value Target::toValue() const
{
    Value::Object members{{"source", Value{source}}};
    if (tilt.has_value())
    {
        Value::Array bands;
        for (const auto level : *tilt)
            bands.push_back(Value{std::round(level * 10.0) / 10.0});
        members.emplace_back("tilt", Value::array(std::move(bands)));
    }
    if (crestDb.has_value())
        members.emplace_back("crest", Value{std::round(*crestDb * 10.0) / 10.0});
    if (sideShare.has_value())
        members.emplace_back("side", Value{std::round(*sideShare * 100.0) / 100.0});
    return Value::object(std::move(members));
}

std::array<double, bandCount> tiltOf(const StreamMeasure& measure)
{
    std::array<double, bandCount> tilt{};
    double sum = 0.0;
    std::size_t counted = 0;
    for (const auto level : measure.bandsDb)
    {
        if (level > silenceDb + 1.0)
        {
            sum += level;
            ++counted;
        }
    }
    const auto mean = counted > 0 ? sum / static_cast<double>(counted) : 0.0;
    for (std::size_t band = 0; band < bandCount; ++band)
        tilt[band] = measure.bandsDb[band] - mean;
    return tilt;
}

Target targetOf(const StreamMeasure& reference, std::string name)
{
    Target target;
    target.tilt = tiltOf(reference);
    target.crestDb = reference.crestDb;
    target.sideShare = reference.sideShare;
    target.source = std::move(name);
    return target;
}

Axes towards(const Target& target, const StreamMeasure& master, Axes given, double amount)
{
    amount = clampTo(amount, 0.0, 1.0);
    Axes wanted = given;
    if (target.crestDb.has_value())
        wanted.punch = clampTo((master.crestDb - *target.crestDb) / 4.0, -1.0, 1.0);
    if (target.sideShare.has_value())
        wanted.width = clampTo((*target.sideShare - master.sideShare) * 8.0, -1.0, 1.0);
    if (target.tilt.has_value())
    {
        // Presence, 2 to 4 kHz: more in the reference, the voice comes forward.
        const auto tilt = tiltOf(master);
        const auto presence = ((*target.tilt)[6] + (*target.tilt)[7]) / 2.0 - (tilt[6] + tilt[7]) / 2.0;
        wanted.focus = clampTo(-presence / 4.0, -1.0, 1.0);
    }
    return {given.punch + (wanted.punch - given.punch) * amount,
            given.focus + (wanted.focus - given.focus) * amount,
            given.width + (wanted.width - given.width) * amount};
}

// --- serialisation -------------------------------------------------------------

Value Evidence::toValue() const
{
    return Value::object({{"measure", Value{measure}}, {"value", Value{std::round(value * 100.0) / 100.0}}});
}

Result<Evidence> Evidence::fromValue(const Value& value)
{
    auto measure = value.stringAt("measure");
    if (!measure)
        return measure.error();
    auto number = value.doubleAt("value");
    if (!number)
        return number.error();
    return Evidence{measure.value(), number.value()};
}

Value Change::toValue() const
{
    Value::Array cited;
    for (const auto& item : evidence)
        cited.push_back(item.toValue());
    Value::Object members{{"trackId", Value{track.toString()}}, {"kind", Value{kindName(kind)}}};
    if (kind == Kind::volume || kind == Kind::pan)
        members.emplace_back("value", Value{std::round(value * 100.0) / 100.0});
    else
        members.emplace_back("parameters", parametersValue(parameters));
    members.emplace_back("sentence", Value{sentence});
    members.emplace_back("evidence", Value::array(std::move(cited)));
    return Value::object(std::move(members));
}

Result<Change> Change::fromValue(const Value& value)
{
    Change change;
    auto track = value.stringAt("trackId");
    if (!track)
        return track.error();
    auto parsed = TrackId::parse(track.value());
    if (!parsed)
        return parsed.error();
    change.track = parsed.value();

    auto kind = value.stringAt("kind");
    if (!kind)
        return kind.error();
    const auto known = kindOf(kind.value());
    if (!known.has_value())
        return fail(ErrorCode::invalidArgument, "réglage inconnu : " + kind.value());
    change.kind = *known;

    if (change.kind == Kind::volume || change.kind == Kind::pan)
    {
        auto number = value.doubleAt("value");
        if (!number)
            return number.error();
        change.value = number.value();
    }
    else
    {
        const auto* parameters = value.find("parameters");
        const auto* object = parameters != nullptr ? parameters->asObject() : nullptr;
        if (object == nullptr)
            return fail(ErrorCode::invalidPayload, "parameters must be an object");
        for (const auto& [name, member] : *object)
        {
            const auto number = member.asDouble();
            if (!number)
                return fail(ErrorCode::invalidPayload, "parameter " + name + " must be a number");
            change.parameters[name] = number.value();
        }
    }

    auto sentence = value.stringAt("sentence");
    if (!sentence)
        return sentence.error();
    change.sentence = sentence.value();

    if (const auto* cited = value.find("evidence"); cited != nullptr && cited->asArray() != nullptr)
    {
        for (const auto& item : *cited->asArray())
        {
            auto evidence = Evidence::fromValue(item);
            if (!evidence)
                return evidence.error();
            change.evidence.push_back(evidence.value());
        }
    }
    return change;
}

Value Proposal::toValue() const
{
    Value::Array items;
    for (const auto& change : changes)
        items.push_back(change.toValue());
    return Value::object({{"decidedBy", Value{decidedBy}}, {"changes", Value::array(std::move(items))}});
}

Result<Proposal> Proposal::fromValue(const Value& value)
{
    Proposal proposal;
    if (const auto by = value.stringAt("decidedBy"); by)
        proposal.decidedBy = by.value();
    const auto* changes = value.find("changes");
    if (changes == nullptr || changes->asArray() == nullptr)
        return fail(ErrorCode::invalidPayload, "changes must be an array");
    for (const auto& item : *changes->asArray())
    {
        auto change = Change::fromValue(item);
        if (!change)
            return change.error();
        proposal.changes.push_back(std::move(change).value());
    }
    return proposal;
}

std::vector<const Change*> Proposal::of(TrackId track) const
{
    std::vector<const Change*> found;
    for (const auto& change : changes)
    {
        if (change.track == track)
            found.push_back(&change);
    }
    return found;
}

// --- the brief -----------------------------------------------------------------

const Brief::Strip* Brief::find(TrackId track) const
{
    for (const auto& strip : strips)
    {
        if (strip.track == track)
            return &strip;
    }
    return nullptr;
}

Value Brief::toValue() const
{
    Value::Array tracks;
    for (const auto& strip : strips)
    {
        Value::Array chain;
        for (const auto& name : strip.chain)
            chain.push_back(Value{name});
        Value::Object members{{"trackId", Value{strip.track.toString()}},
                              {"name", Value{strip.name}},
                              {"role", Value{std::string{mixRoleName(strip.role.role)}}},
                              {"roleKnown", Value{strip.role.known}},
                              {"volumeDb", Value{std::round(strip.volumeDb * 10.0) / 10.0}},
                              {"pan", Value{std::round(strip.pan * 100.0) / 100.0}},
                              {"chain", Value::array(std::move(chain))},
                              {"measure", strip.measure.toValue()}};
        if (strip.muted)
            members.emplace_back("muted", Value{true});
        if (!strip.equaliser.empty())
            members.emplace_back("equaliser", parametersValue(strip.equaliser));
        if (!strip.compressor.empty())
            members.emplace_back("compressor", parametersValue(strip.compressor));
        members.emplace_back("peak10ms", Value{std::round(peakTenMsDb(strip.measure) * 10.0) / 10.0});
        tracks.push_back(Value::object(std::move(members)));
    }

    Value::Array masking;
    for (const auto& overlap : overlaps)
    {
        if (masking.size() == 12)
            break;
        Value::Array spans;
        for (const auto& span : overlap.spans)
            spans.push_back(Value::array({Value{std::round(span.fromSeconds * 10.0) / 10.0},
                                          Value{std::round(span.toSeconds * 10.0) / 10.0}}));
        masking.push_back(Value::object({{"a", Value{strips[overlap.first].track.toString()}},
                                         {"b", Value{strips[overlap.second].track.toString()}},
                                         {"band", Value{static_cast<std::int64_t>(overlap.band)}},
                                         {"share", Value{std::round(overlap.share * 100.0)}},
                                         {"levelDb", Value{std::round(overlap.levelDb * 10.0) / 10.0}},
                                         {"seconds", Value::array(std::move(spans))}}));
    }

    Value::Array centres;
    for (const auto centre : bandCentres)
        centres.push_back(Value{centre});

    return Value::object({{"bandsHz", Value::array(std::move(centres))},
                          {"tracks", Value::array(std::move(tracks))},
                          {"master", master.toValue()},
                          {"masking", Value::array(std::move(masking))},
                          {"axes", axes.toValue()},
                          {"target", target.toValue()},
                          {"bounds", Bounds::toValue()}});
}

Brief briefOf(const ProjectState& state,
              const std::map<std::string, StreamMeasure>& tracks,
              const StreamMeasure& master,
              Axes axes,
              Target target,
              const tidy::PresetNames& presets)
{
    Brief brief;
    brief.master = master;
    brief.axes = axes;
    brief.target = std::move(target);
    std::vector<StreamMeasure> measures;
    for (const auto& track : state.tracks())
    {
        const auto found = tracks.find(track.id.toString());
        if (found == tracks.end())
            continue;
        Brief::Strip strip;
        strip.track = track.id;
        strip.name = track.name;
        strip.role = guessRole(state, track.id, &found->second, presets);
        strip.volumeDb = track.volumeDb;
        strip.pan = track.pan;
        strip.muted = track.muted;
        for (const auto& plugin : track.plugins)
            strip.chain.push_back(plugin.ref.format == PluginRef::internalFormat ? plugin.ref.identifier
                                                                                 : plugin.ref.name);
        strip.equaliser = internalParameters(lastInternal(track, internal::equaliser));
        strip.compressor = internalParameters(lastInternal(track, internal::compressor));
        strip.measure = found->second;
        measures.push_back(found->second);
        brief.strips.push_back(std::move(strip));
    }
    brief.overlaps = overlaps(measures);
    return brief;
}

// --- the guards ----------------------------------------------------------------

double Bounds::highPassLimit(MixRole role)
{
    switch (role)
    {
    case MixRole::kick:
    case MixRole::bass:
        return 40.0;
    case MixRole::hats:
        return 600.0;
    case MixRole::fx:
        return 400.0;
    case MixRole::snare:
    case MixRole::percussion:
    case MixRole::chords:
        return 200.0;
    case MixRole::melody:
    case MixRole::vocal:
        return 150.0;
    }
    return 150.0;
}

Value Bounds::toValue()
{
    return Value::object({{"equaliserCutDb", Value{cutDb}},
                          {"equaliserBoostDb", Value{boostDb}},
                          {"q", Value::array({Value{minQ}, Value{maxQ}})},
                          {"maxRatio", Value{maxRatio}},
                          {"minAttackMs", Value{minAttackMs}},
                          {"maxMakeupDb", Value{maxMakeupDb}},
                          {"maxGainReductionDb", Value{maxReductionDb}},
                          {"volumeFromCurrentDb", Value::array({Value{volumeDownDb}, Value{volumeUpDb}})},
                          {"volumeFloorDb", Value{floorDb}},
                          {"maxPan", Value{maxPan}},
                          {"maxPanKickBassVoiceSnare", Value{maxCentredPan}},
                          {"masterTruePeakCeilingDb", Value{masterCeilingDb}},
                          {"highPassLimitHz",
                           Value::object({{"kick", Value{highPassLimit(MixRole::kick)}},
                                          {"bass", Value{highPassLimit(MixRole::bass)}},
                                          {"snare", Value{highPassLimit(MixRole::snare)}},
                                          {"hats", Value{highPassLimit(MixRole::hats)}},
                                          {"percussion", Value{highPassLimit(MixRole::percussion)}},
                                          {"chords", Value{highPassLimit(MixRole::chords)}},
                                          {"melody", Value{highPassLimit(MixRole::melody)}},
                                          {"vocal", Value{highPassLimit(MixRole::vocal)}},
                                          {"fx", Value{highPassLimit(MixRole::fx)}}})}});
}

Value Refusal::toValue() const
{
    return Value::object({{"trackId", Value{track.toString()}},
                          {"change", Value{static_cast<std::int64_t>(change)}},
                          {"why", Value{why}}});
}

std::vector<Refusal> check(const Brief& brief, const Proposal& proposal)
{
    std::vector<Refusal> refused;
    for (std::size_t index = 0; index < proposal.changes.size(); ++index)
    {
        const auto& change = proposal.changes[index];
        const auto refuse = [&refused, &change, index](std::string why)
        { refused.push_back(Refusal{change.track, index, std::move(why)}); };

        const auto* strip = brief.find(change.track);
        if (strip == nullptr)
        {
            refuse("cette piste n'est pas mesurée : le mixage n'y touche pas");
            continue;
        }
        if (strip->muted)
        {
            refuse(strip->name + " est coupée par toi : le mixage n'y touche pas");
            continue;
        }
        const auto role = strip->role.role;

        if (change.sentence.empty())
            refuse("un réglage sans phrase");
        if (change.evidence.empty())
            refuse("la phrase ne cite aucune mesure");
        for (const auto& item : change.evidence)
        {
            const auto truth = measured(brief, *strip, item.measure);
            if (!truth.has_value())
                refuse("la mesure « " + item.measure + " » n'existe pas pour " + strip->name);
            else if (std::abs(*truth - item.value) > 0.15)
                refuse("la phrase cite " + item.measure + " = " + french(item.value) + ", la mesure dit " +
                       french(*truth));
            else if (!mentions(change.sentence, item.value))
                refuse("la phrase ne dit pas le nombre qu'elle cite (" + french(item.value) + ")");
        }

        switch (change.kind)
        {
        case Kind::volume:
        {
            const auto delta = change.value - strip->volumeDb;
            if (delta < Bounds::volumeDownDb || delta > Bounds::volumeUpDb)
                refuse("le volume de " + strip->name + " bougerait de " + french(delta) +
                       " dB, au-delà de -12 à +6 dB");
            if (change.value < Bounds::floorDb)
                refuse("le volume de " + strip->name + " descendrait sous -60 dB : ce serait la couper");
            break;
        }
        case Kind::pan:
        {
            const auto limit = staysCentred(role) ? Bounds::maxCentredPan : Bounds::maxPan;
            if (std::abs(change.value) > limit + 1e-9)
                refuse("le pan de " + strip->name + " (" + roleLabel(role) + ") irait à " +
                       french(change.value, 2) + ", au-delà de " + french(limit, 1));
            break;
        }
        case Kind::equaliser:
        case Kind::compressor:
        {
            const auto* effect = findInternalEffect(change.kind == Kind::equaliser ? internal::equaliser
                                                                                   : internal::compressor);
            for (const auto& [name, value] : change.parameters)
            {
                if (auto valid = validateInternalParameter(*effect, name, value); !valid)
                    refuse(valid.error().message);
            }
            const auto value = [&change](std::string_view name, double fallback)
            {
                const auto found = change.parameters.find(std::string{name});
                return found != change.parameters.end() ? found->second : fallback;
            };
            if (change.kind == Kind::equaliser)
            {
                for (const auto gain :
                     {internal::lowGain, internal::mid1Gain, internal::mid2Gain, internal::highGain})
                {
                    const auto level = value(gain, 0.0);
                    if (level < Bounds::cutDb || level > Bounds::boostDb)
                        refuse("l'égaliseur de " + strip->name + " irait à " + french(level) + " dB (" +
                               std::string{gain} + "), au-delà de -6 à +4 dB");
                }
                for (const auto q : {internal::lowQ, internal::mid1Q, internal::mid2Q, internal::highQ})
                {
                    const auto width = value(q, 1.0);
                    if (width < Bounds::minQ || width > Bounds::maxQ)
                        refuse("un Q de " + french(width, 2) + " sur " + strip->name + ", hors de 0,3 à 4");
                }
                const auto cut = value(internal::highPassFrequency, internal::highPassOff);
                if (cut > Bounds::highPassLimit(role) + 1e-9)
                    refuse("un coupe-bas à " + french(cut, 0) + " Hz sur " + strip->name + " (" +
                           roleLabel(role) + "), au-delà de " + french(Bounds::highPassLimit(role), 0) +
                           " Hz");
            }
            else
            {
                const auto ratio = value(internal::ratio, 2.0);
                const auto threshold = value(internal::threshold, -12.0);
                if (ratio > Bounds::maxRatio)
                    refuse("un ratio de " + french(ratio) + ":1 sur " + strip->name + ", au-delà de 6:1");
                if (value(internal::attack, 10.0) < Bounds::minAttackMs)
                    refuse("une attaque sous 1 ms sur " + strip->name);
                if (value(internal::makeup, 0.0) > Bounds::maxMakeupDb)
                    refuse("un gain de sortie au-delà de +6 dB sur " + strip->name);
                const auto reduction = std::max(0.0, peakTenMsDb(strip->measure) - threshold) *
                                       (1.0 - 1.0 / std::max(1.0, ratio));
                if (reduction > Bounds::maxReductionDb + 0.05)
                    refuse("environ " + french(reduction) + " dB de réduction de gain sur " + strip->name +
                           ", au-delà de 6 dB");
            }
            break;
        }
        }
    }
    return refused;
}

Proposal without(const Proposal& proposal, const std::vector<Refusal>& refused)
{
    std::set<std::size_t> out;
    for (const auto& refusal : refused)
        out.insert(refusal.change);
    Proposal kept;
    kept.decidedBy = proposal.decidedBy;
    for (std::size_t index = 0; index < proposal.changes.size(); ++index)
    {
        if (!out.contains(index))
            kept.changes.push_back(proposal.changes[index]);
    }
    return kept;
}

// --- the rules -----------------------------------------------------------------

Proposal baseMix(const Brief& brief)
{
    Proposal proposal;
    proposal.decidedBy = "règles";
    const auto& axes = brief.axes;

    // From a reference: the low end and the top of its tilt, against ours.
    double lowOffset = 0.0;
    double highOffset = 0.0;
    if (brief.target.tilt.has_value())
    {
        const auto ours = tiltOf(brief.master);
        const auto& theirs = *brief.target.tilt;
        lowOffset = clampTo(((theirs[1] + theirs[2]) - (ours[1] + ours[2])) / 2.0, -3.0, 3.0);
        highOffset = clampTo(((theirs[8] + theirs[9]) - (ours[8] + ours[9])) / 2.0, -3.0, 3.0);
    }

    std::map<MixRole, int> seen;
    for (const auto& strip : brief.strips)
    {
        const auto& m = strip.measure;
        if (strip.muted || m.integratedLufs < -70.0 || m.activeShare < 0.02)
            continue;
        const auto role = strip.role.role;
        const auto label = roleLabel(role);
        const auto name = strip.name.empty() ? label : strip.name;
        const auto rank = seen[role]++;

        // 1. Level, by role, post-fader.
        if (strip.role.known)
        {
            auto target = roleLevel(role);
            if (isDrum(role))
                target += 1.5 * axes.punch;
            if (role == MixRole::vocal || role == MixRole::melody)
                target -= 2.0 * axes.focus;
            if (role == MixRole::chords || role == MixRole::fx)
                target += 1.0 * axes.focus;
            if (role == MixRole::kick || role == MixRole::bass)
                target += lowOffset;
            if (role == MixRole::hats)
                target += highOffset;
            const auto post = m.integratedLufs + strip.volumeDb;
            auto volume = strip.volumeDb + (target - post);
            volume =
                clampTo(volume, strip.volumeDb + Bounds::volumeDownDb, strip.volumeDb + Bounds::volumeUpDb);
            volume = std::max(volume, Bounds::floorDb);
            volume = std::round(volume * 10.0) / 10.0;
            if (std::abs(volume - strip.volumeDb) >= 1.0)
            {
                Change change{strip.track, Kind::volume, volume, {}, {}, {}};
                const auto lufs = std::round(m.integratedLufs * 10.0) / 10.0;
                change.sentence = name + " jouait à " + french(lufs) + " LUFS avant son fader ; pour une " +
                                  label + " je vise " + french(target) + " LUFS dans le mix : fader " +
                                  (volume < strip.volumeDb ? "baissé" : "monté") + " de " +
                                  french(std::abs(volume - strip.volumeDb)) + " dB.";
                change.evidence = {{"lufs", lufs}};
                proposal.changes.push_back(std::move(change));
            }
        }

        std::map<std::string, double> eq;

        // 2. A high-pass where low energy serves nothing.
        const auto cutoff = std::min(roleHighPass(role), Bounds::highPassLimit(role));
        const auto loudest = loudestBand(m);
        if (strip.role.known && role != MixRole::kick && role != MixRole::bass && cutoff > 40.0)
        {
            std::size_t below = 0;
            while (below + 1 < bandCount && bandHigh(below) <= cutoff)
                ++below;
            // The loudest band entirely under the cut-off.
            std::optional<std::size_t> worst;
            for (std::size_t band = 0; band < below; ++band)
            {
                if (!worst.has_value() || m.bandsDb[band] > m.bandsDb[*worst])
                    worst = band;
            }
            if (worst.has_value() && loudest > *worst && m.bandsDb[*worst] > m.bandsDb[loudest] - 30.0)
            {
                const auto level = std::round(m.bandsDb[*worst] * 10.0) / 10.0;
                eq[std::string{internal::highPassFrequency}] = cutoff;
                Change change{strip.track, Kind::equaliser, 0.0, {}, {}, {}};
                change.sentence = name + " avait de l'énergie à " + bandWord(*worst) + " (" + french(level) +
                                  " dBFS) qui ne sert pas une " + label + " : coupe-bas à " +
                                  french(cutoff, 0) + " Hz.";
                change.evidence = {{"bands." + std::to_string(*worst), level}};
                change.parameters = eq;
                proposal.changes.push_back(std::move(change));
            }
        }

        // 3. Carving: the bass makes room for the kick, the chords for the
        // voice or the lead. Where they overlap the loudest first: two
        // octaves both shared, the one that carries the kick is carved.
        auto byLevel = brief.overlaps;
        std::stable_sort(byLevel.begin(),
                         byLevel.end(),
                         [](const Overlap& lhs, const Overlap& rhs) { return lhs.levelDb > rhs.levelDb; });
        for (const auto& overlap : byLevel)
        {
            const auto& a = brief.strips[overlap.first];
            const auto& b = brief.strips[overlap.second];
            const Brief::Strip* other = nullptr;
            if (a.track == strip.track)
                other = &b;
            else if (b.track == strip.track)
                other = &a;
            if (other == nullptr || !other->role.known || !strip.role.known)
                continue;
            const auto share = std::round(overlap.share * 100.0);

            if (role == MixRole::bass && other->role.role == MixRole::kick && overlap.band <= 2 &&
                overlap.share >= 0.25 && eq.find(std::string{internal::mid1Gain}) == eq.end())
            {
                const auto depth =
                    std::round(clampTo(-3.0 - 1.0 * axes.punch, Bounds::cutDb, -2.0) * 10.0) / 10.0;
                const auto frequency = bandCentres[overlap.band];
                eq[std::string{internal::mid1Frequency}] = frequency;
                eq[std::string{internal::mid1Gain}] = depth;
                eq[std::string{internal::mid1Q}] = 1.4;
                Change change{strip.track, Kind::equaliser, 0.0, {}, {}, {}};
                change.sentence = "La " + label + " et le kick se recouvrent à " + bandWord(overlap.band) +
                                  " " + french(share, 0) +
                                  " % du temps où ils jouent ensemble : j'ai creusé " + name + " de " +
                                  french(std::abs(depth)) + " dB à " + bandWord(overlap.band) +
                                  " pour laisser passer le kick.";
                change.evidence = {
                    {"overlap." + std::to_string(overlap.band) + "." + other->track.toString(), share}};
                change.parameters = eq;
                proposal.changes.push_back(std::move(change));
            }

            const bool lead = other->role.role == MixRole::vocal || other->role.role == MixRole::melody;
            if ((role == MixRole::chords || role == MixRole::fx) && lead && overlap.band >= 5 &&
                overlap.band <= 7 && overlap.share >= 0.25 &&
                eq.find(std::string{internal::mid2Gain}) == eq.end())
            {
                const auto depth =
                    std::round(clampTo(-2.5 + 1.5 * axes.focus, Bounds::cutDb, -1.0) * 10.0) / 10.0;
                const auto frequency = bandCentres[overlap.band];
                eq[std::string{internal::mid2Frequency}] = frequency;
                eq[std::string{internal::mid2Gain}] = depth;
                eq[std::string{internal::mid2Q}] = 1.0;
                Change change{strip.track, Kind::equaliser, 0.0, {}, {}, {}};
                change.sentence =
                    name + " et " + (other->name.empty() ? roleLabel(other->role.role) : other->name) +
                    " se recouvrent à " + bandWord(overlap.band) + " " + french(share, 0) +
                    " % du temps : j'ai creusé " + name + " de " + french(std::abs(depth)) + " dB à " +
                    bandWord(overlap.band) + " pour que " + roleLabel(other->role.role) + " passe devant.";
                change.evidence = {
                    {"overlap." + std::to_string(overlap.band) + "." + other->track.toString(), share}};
                change.parameters = eq;
                proposal.changes.push_back(std::move(change));
            }
        }

        // 4. Compression where the crest is far above what the role wants.
        if (const auto wanted = roleCrest(role); wanted.has_value() && strip.role.known)
        {
            const auto aim = *wanted - 2.0 * axes.punch;
            if (m.crestDb > aim + 2.0)
            {
                const auto reduction = clampTo(m.crestDb - aim, 2.0, 5.5);
                const auto ratio = std::round(clampTo(2.5 + 1.0 * axes.punch, 1.5, 4.0) * 10.0) / 10.0;
                const auto peak = peakTenMsDb(m);
                const auto threshold =
                    std::round(clampTo(peak - reduction / (1.0 - 1.0 / ratio), -40.0, 0.0) * 10.0) / 10.0;
                const auto estimated = std::max(0.0, peak - threshold) * (1.0 - 1.0 / ratio);
                const auto crest = std::round(m.crestDb * 10.0) / 10.0;
                Change change{strip.track, Kind::compressor, 0.0, {}, {}, {}};
                change.parameters = {
                    {std::string{internal::threshold}, threshold},
                    {std::string{internal::ratio}, ratio},
                    {std::string{internal::attack}, roleAttack(role)},
                    {std::string{internal::release}, 100.0},
                    {std::string{internal::makeup}, std::round(estimated * 0.5 * 10.0) / 10.0}};
                change.sentence = name + " avait un facteur de crête de " + french(crest) + " dB, une " +
                                  label + " en veut environ " + french(aim, 0) + " : compresseur " +
                                  french(ratio) + ":1, seuil " + french(threshold) + " dB, attaque " +
                                  french(roleAttack(role), 0) + " ms, environ " + french(estimated) +
                                  " dB de réduction sur les passages forts.";
                change.evidence = {{"crest", crest}};
                proposal.changes.push_back(std::move(change));
            }
        }

        // 5. Pans by role, for what sits in the middle and need not.
        if (strip.role.known && std::abs(strip.pan) < 0.05 && !staysCentred(role))
        {
            const auto spread = clampTo(1.0 + 0.6 * axes.width, 0.4, 1.6);
            double pan = 0.0;
            if (role == MixRole::hats)
                pan = 0.25 * spread;
            else if (role == MixRole::percussion)
                pan = (rank % 2 == 0 ? 0.35 : -0.35) * spread;
            else if (role == MixRole::fx)
                pan = (rank % 2 == 0 ? -0.5 : 0.5) * spread;
            else if ((role == MixRole::chords || role == MixRole::melody) && rank > 0)
                pan = (rank % 2 == 1 ? -0.3 : 0.3) * spread;
            pan = std::round(clampTo(pan, -Bounds::maxPan, Bounds::maxPan) * 100.0) / 100.0;
            if (std::abs(pan) >= 0.05)
            {
                const auto correlation = std::round(m.correlation * 100.0) / 100.0;
                Change change{strip.track, Kind::pan, pan, {}, {}, {}};
                change.sentence = name + " était au centre, avec une corrélation de " +
                                  french(correlation, 2) + " entre gauche et droite : placée à " +
                                  french(std::abs(pan) * 100.0, 0) + " % " +
                                  (pan > 0.0 ? "à droite" : "à gauche") + " pour ouvrir l'image.";
                change.evidence = {{"correlation", correlation}};
                proposal.changes.push_back(std::move(change));
            }
        }
    }
    return proposal;
}

// --- compile -------------------------------------------------------------------

std::vector<std::unique_ptr<Command>>
compile(const ProjectState& state, const Proposal& proposal, const std::function<PluginId()>& newPluginId)
{
    std::vector<std::unique_ptr<Command>> commands;

    // Per track and per effect, the parameters every change asks for, in
    // order: two changes on one equaliser (a high-pass, then a dip) are one
    // equaliser with both.
    struct Wanted
    {
        std::optional<double> volume;
        std::optional<double> pan;
        std::map<std::string, double> equaliser;
        std::map<std::string, double> compressor;
    };
    std::vector<std::pair<TrackId, Wanted>> tracks;
    const auto wantedFor = [&tracks](TrackId track) -> Wanted&
    {
        for (auto& [id, wanted] : tracks)
        {
            if (id == track)
                return wanted;
        }
        tracks.emplace_back(track, Wanted{});
        return tracks.back().second;
    };
    for (const auto& change : proposal.changes)
    {
        const auto* track = state.findTrack(change.track);
        if (track == nullptr || track->muted)
            continue;
        auto& wanted = wantedFor(change.track);
        switch (change.kind)
        {
        case Kind::volume:
            wanted.volume = change.value;
            break;
        case Kind::pan:
            wanted.pan = change.value;
            break;
        case Kind::equaliser:
            for (const auto& [name, value] : change.parameters)
                wanted.equaliser[name] = value;
            break;
        case Kind::compressor:
            for (const auto& [name, value] : change.parameters)
                wanted.compressor[name] = value;
            break;
        }
    }

    for (const auto& [id, wanted] : tracks)
    {
        const auto& track = *state.findTrack(id);
        if (wanted.volume.has_value() && std::abs(*wanted.volume - track.volumeDb) > 1e-9)
            commands.push_back(std::make_unique<SetTrackVolume>(id, *wanted.volume));
        if (wanted.pan.has_value() && std::abs(*wanted.pan - track.pan) > 1e-9)
            commands.push_back(std::make_unique<SetTrackPan>(id, *wanted.pan));

        // After the user's plugins, never before: the effects of the DAW
        // never bypass what the person chose. An existing one is set.
        auto end = track.plugins.size();
        const auto place = [&](std::string_view identifier, const std::map<std::string, double>& parameters)
        {
            if (parameters.empty())
                return;
            if (const auto* existing = lastInternal(track, identifier); existing != nullptr)
            {
                for (const auto& [name, value] : parameters)
                {
                    if (std::abs(internalValue(*existing, name) - value) > 1e-9)
                        commands.push_back(std::make_unique<SetPluginParameter>(existing->id, name, value));
                }
                return;
            }
            PluginInstance instance{};
            instance.id = newPluginId();
            const auto* effect = findInternalEffect(identifier);
            instance.ref = PluginRef{
                std::string{PluginRef::internalFormat}, std::string{identifier}, std::string{effect->name}};
            for (const auto& [name, value] : parameters)
                instance.params.push_back(PluginParam{name, value});
            std::sort(instance.params.begin(),
                      instance.params.end(),
                      [](const PluginParam& lhs, const PluginParam& rhs)
                      { return lhs.paramId < rhs.paramId; });
            commands.push_back(std::make_unique<InsertPlugin>(id, instance, end));
            ++end;
        };
        // An equaliser before a compressor already there: it is the order
        // a mixing engineer chains them in.
        if (lastInternal(track, internal::equaliser) == nullptr && !wanted.equaliser.empty())
        {
            if (const auto* compressor = lastInternal(track, internal::compressor); compressor != nullptr)
            {
                for (std::size_t index = 0; index < track.plugins.size(); ++index)
                {
                    if (track.plugins[index].id == compressor->id)
                        end = index;
                }
                place(internal::equaliser, wanted.equaliser);
                end = track.plugins.size() + 1;
                place(internal::compressor, wanted.compressor);
                continue;
            }
        }
        place(internal::equaliser, wanted.equaliser);
        place(internal::compressor, wanted.compressor);
    }
    return commands;
}

std::optional<double> masterTrim(const ProjectState& state, double renderedTruePeakDb)
{
    if (renderedTruePeakDb <= Bounds::masterCeilingDb)
        return std::nullopt;
    const auto current = state.master().volumeDb;
    return std::round((current - (renderedTruePeakDb - Bounds::masterCeilingDb) - 0.2) * 10.0) / 10.0;
}

} // namespace daw::domain::mix
