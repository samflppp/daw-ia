#include "daw/domain/buses/Shared.h"

#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/project/InternalEffects.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace daw::domain::buses
{
namespace
{

constexpr double closeParameter = 0.02;

bool sameRef(const PluginRef& a, const PluginRef& b)
{
    return a.format == b.format && a.identifier == b.identifier;
}

bool sameParameters(const PluginInstance& a, const PluginInstance& b, double tolerance)
{
    if (a.params.size() != b.params.size())
        return false;
    for (std::size_t index = 0; index < a.params.size(); ++index)
        if (a.params[index].paramId != b.params[index].paramId ||
            std::abs(a.params[index].value - b.params[index].value) > tolerance)
            return false;
    return true;
}

// The last plugin of a channel, when it can be shared: on, and not its
// instrument (an instrument leads a chain, an effect follows it).
const PluginInstance* lastEffect(const Track& track, const Recognise& recognise)
{
    if (track.plugins.empty() || track.plugins.back().bypassed)
        return nullptr;
    const auto& last = track.plugins.back();
    if (last.ref.format != PluginRef::internalFormat && recognise &&
        recognise(last.ref).kind == Kind::instrument)
        return nullptr;
    return &last;
}

// What a send's sentence calls the effect.
std::string called(std::optional<Kind> kind)
{
    return kind == Kind::delay ? "Le même écho" : "La même réverbération";
}

std::string names(const ProjectState& state, const std::vector<TrackId>& tracks)
{
    std::string said;
    for (std::size_t index = 0; index < tracks.size(); ++index)
    {
        const auto* track = state.findTrack(tracks[index]);
        said += (index == 0                   ? ""
                 : index + 1 == tracks.size() ? " et "
                                              : ", ") +
                (track != nullptr ? track->name : std::string{"?"});
    }
    return said;
}

} // namespace

std::vector<Shared> propose(const ProjectState& state, const Recognise& recognise)
{
    std::vector<Shared> proposals;
    std::vector<bool> taken(state.tracks().size(), false);
    const auto& tracks = state.tracks();

    for (std::size_t first = 0; first < tracks.size(); ++first)
    {
        if (taken[first])
            continue;
        const auto* effect = lastEffect(tracks[first], recognise);
        if (effect == nullptr)
            continue;

        const bool internal = effect->ref.format == PluginRef::internalFormat;
        std::optional<Way> way;
        Recognition recognised{};
        if (internal &&
            (effect->ref.identifier == internal::equaliser || effect->ref.identifier == internal::compressor))
            way = Way::group;
        else if (!internal && recognise)
        {
            recognised = recognise(effect->ref);
            if (recognised.kind == Kind::reverb || recognised.kind == Kind::delay ||
                recognised.by == KnownBy::unknown)
                way = Way::send;
        }
        if (!way)
            continue;
        // A group moves the tracks' way out: only tracks that send nowhere.
        if (*way == Way::group && !tracks[first].sends.empty())
            continue;

        Shared shared;
        shared.way = *way;
        shared.plugin = *effect;
        shared.tracks.push_back(tracks[first].id);
        shared.removed.push_back(effect->id);
        shared.destination = tracks[first].output;
        if (*way == Way::send)
        {
            shared.by = recognised.by;
            shared.suggested = recognised.suggested;
        }

        for (std::size_t other = first + 1; other < tracks.size(); ++other)
        {
            if (taken[other])
                continue;
            const auto* candidate = lastEffect(tracks[other], recognise);
            if (candidate == nullptr || !sameRef(candidate->ref, effect->ref))
                continue;
            if (*way == Way::group)
            {
                if (!sameParameters(*candidate, *effect, 0.0) || tracks[other].output != shared.destination ||
                    !tracks[other].sends.empty())
                    continue;
            }
            else
            {
                const bool sameState =
                    candidate->state == effect->state && sameParameters(*candidate, *effect, 0.0);
                if (!sameState)
                {
                    if (!sameParameters(*candidate, *effect, closeParameter))
                        continue;
                    shared.certainty = Certainty::close;
                }
            }
            shared.tracks.push_back(tracks[other].id);
            shared.removed.push_back(candidate->id);
            taken[other] = true;
        }
        if (shared.tracks.size() < 2)
            continue;
        taken[first] = true;

        const auto who = names(state, shared.tracks);
        const auto count = std::to_string(shared.tracks.size());
        if (shared.toAsk())
            shared.sentence =
                effect->ref.name + ", en dernier sur " + count + " pistes, " + who + " : " +
                (shared.suggested == Kind::reverb  ? std::string{"une réverbération, d'après son nom ?"}
                 : shared.suggested == Kind::delay ? std::string{"un écho, d'après son nom ?"}
                                                   : std::string{"une réverbération, un écho, ou ni l'un "
                                                                 "ni l'autre ?"}) +
                " Dites-le : s'il en est un, un bus d'envoi le portera une fois.";
        else if (shared.way == Way::send)
            shared.sentence =
                called(recognised.kind) + " (" + effect->ref.name + ") sur " + count + " pistes, " + who +
                " : un bus d'envoi " + (recognised.kind == Kind::delay ? "le" : "la") +
                " porte une fois, chaque piste y envoie à 0 dB." +
                (shared.by != KnownBy::name       ? ""
                 : recognised.kind == Kind::delay ? " Reconnu par son nom."
                                                  : " Reconnue par son nom.") +
                (shared.certainty == Certainty::close ? " Réglages proches, état interne différent : "
                                                        "l'écoute tranche."
                                                      : "");
        else if (effect->ref.identifier == internal::equaliser)
            shared.sentence =
                "Le même égaliseur, aux mêmes réglages, sur " + count + " pistes, " + who +
                " : un bus de groupe le porte une fois ; un égaliseur sur la somme est le même son.";
        else
            shared.sentence =
                "Le même compresseur, aux mêmes réglages, sur " + count + " pistes, " + who +
                " : compression de groupe, le son change — il compresse la somme, plus chaque piste.";
        proposals.push_back(std::move(shared));
    }
    return proposals;
}

std::vector<std::unique_ptr<Command>>
compile(const Shared& shared, TrackId bus, const std::string& busName, PluginId plugin)
{
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<AddBus>(bus, busName));
    auto instance = shared.plugin;
    instance.id = plugin;
    commands.push_back(std::make_unique<InsertPlugin>(bus, instance, 0));
    if (shared.way == Way::group && !shared.destination.isNil())
        commands.push_back(std::make_unique<SetTrackOutput>(bus, shared.destination));
    for (std::size_t index = 0; index < shared.tracks.size(); ++index)
    {
        if (shared.way == Way::send)
            commands.push_back(std::make_unique<SetTrackSend>(shared.tracks[index], bus, 0.0));
        else
            commands.push_back(std::make_unique<SetTrackOutput>(shared.tracks[index], bus));
        commands.push_back(std::make_unique<RemovePlugin>(shared.removed[index]));
    }
    return commands;
}

} // namespace daw::domain::buses
