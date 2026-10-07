#include "daw/domain/kit/Choice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <numeric>

namespace daw::domain::kit
{
namespace
{

std::string french(double value, int decimals)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.*f", decimals, value);
    std::string said{text.data()};
    std::replace(said.begin(), said.end(), '.', ',');
    return said;
}

std::string signedCents(double cents)
{
    return (cents >= 0.0 ? "+" : "") + french(cents, 0) + " cents";
}

// The z-score of each value among its own, 0 where they do not vary.
std::vector<double> scores(const std::vector<double>& values)
{
    if (values.empty())
        return {};
    const auto mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double spread = 0.0;
    for (const auto value : values)
        spread += (value - mean) * (value - mean);
    spread = std::sqrt(spread / static_cast<double>(values.size()));
    std::vector<double> z;
    z.reserve(values.size());
    for (const auto value : values)
        z.push_back(spread > 1e-12 ? (value - mean) / spread : 0.0);
    return z;
}

double distance(const Axes& a, const Axes& b)
{
    return (a.bright - b.bright) * (a.bright - b.bright) + (a.ample - b.ample) * (a.ample - b.ample) +
           (a.dirty - b.dirty) * (a.dirty - b.dirty);
}

bool closeEnough(const Axes& a, const Axes& b)
{
    return std::abs(a.bright - b.bright) <= colourApart && std::abs(a.ample - b.ample) <= colourApart &&
           std::abs(a.dirty - b.dirty) <= colourApart;
}

std::string axesSaid(const Axes& axes)
{
    return "brillance " + french(axes.bright, 1) + ", ampleur " + french(axes.ample, 1) + ", saturation " +
           french(axes.dirty, 1);
}

std::string fileOf(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

std::string noteName(int pitchClass)
{
    static constexpr std::array<const char*, 12> names{
        "do", "do#", "ré", "ré#", "mi", "fa", "fa#", "sol", "sol#", "la", "la#", "si"};
    return names[static_cast<std::size_t>(((pitchClass % 12) + 12) % 12)];
}

double lowCorrelation(const Features& first, const Features& second)
{
    const auto n = static_cast<double>(lowBands);
    const auto meanOf = [&](const auto& profile)
    { return std::accumulate(profile.begin(), profile.end(), 0.0) / n; };
    const auto a = meanOf(first.lowProfileDb);
    const auto b = meanOf(second.lowProfileDb);
    double cross = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t band = 0; band < lowBands; ++band)
    {
        const auto x = first.lowProfileDb[band] - a;
        const auto y = second.lowProfileDb[band] - b;
        cross += x * y;
        aa += x * x;
        bb += y * y;
    }
    return aa > 0.0 && bb > 0.0 ? cross / std::sqrt(aa * bb) : 0.0;
}

std::vector<Axes> axesOf(const std::vector<Sample>& library)
{
    std::vector<Axes> axes(library.size());
    std::map<Role, std::vector<std::size_t>> byRole;
    for (std::size_t index = 0; index < library.size(); ++index)
        byRole[library[index].role].push_back(index);

    for (const auto& [role, members] : byRole)
    {
        const auto field = [&](const std::function<double(const Features&)>& of)
        {
            std::vector<double> values;
            for (const auto index : members)
                values.push_back(of(library[index].features));
            return scores(values);
        };
        const auto centroid = field([](const Features& f) { return std::log2(std::max(f.centroidHz, 1.0)); });
        const auto high = field([](const Features& f) { return f.highShare; });
        const auto tail = field([](const Features& f) { return f.tailSeconds; });
        const auto width = field([](const Features& f) { return f.width; });
        const auto crest = field([](const Features& f) { return -f.crestDb; });
        for (std::size_t at = 0; at < members.size(); ++at)
        {
            auto& each = axes[members[at]];
            each.bright = std::clamp((centroid[at] + high[at]) / 2.0, -2.0, 2.0);
            each.ample = std::clamp((tail[at] + width[at]) / 2.0, -2.0, 2.0);
            each.dirty = std::clamp((crest[at] + high[at]) / 2.0, -2.0, 2.0);
        }
    }
    return axes;
}

Kit choose(const std::vector<Sample>& library, std::optional<int> tonic, const Axes& wanted)
{
    Kit kit;
    const auto axes = axesOf(library);

    // The samples of a role, nearest to the axes wanted first, a tie to the
    // first path.
    const auto candidates = [&](Role role)
    {
        std::vector<std::size_t> found;
        for (std::size_t index = 0; index < library.size(); ++index)
            if (library[index].role == role)
                found.push_back(index);
        std::sort(found.begin(),
                  found.end(),
                  [&](std::size_t a, std::size_t b)
                  {
                      const auto da = distance(axes[a], wanted);
                      const auto db = distance(axes[b], wanted);
                      return da != db ? da < db : library[a].path < library[b].path;
                  });
        return found;
    };
    const auto withChosen = [&](std::size_t index)
    {
        return std::all_of(kit.picks.begin(),
                           kit.picks.end(),
                           [&](const Pick& pick)
                           { return pick.outOfColour || closeEnough(axes[index], pick.axes); });
    };
    const auto take = [&](std::size_t index, std::vector<std::string> reasons)
    {
        reasons.push_back("couleur : " + axesSaid(axes[index]));
        kit.picks.push_back(Pick{library[index].role, library[index].path, axes[index], std::move(reasons)});
    };
    // The widest gap on an axis between a sample and the elements chosen in
    // colour: one taken out of colour does not move the colour of the kit.
    const auto gapOf = [&](std::size_t index)
    {
        double gap = 0.0;
        for (const auto& pick : kit.picks)
            if (!pick.outOfColour)
                gap = std::max({gap,
                                std::abs(axes[index].bright - pick.axes.bright),
                                std::abs(axes[index].ample - pick.axes.ample),
                                std::abs(axes[index].dirty - pick.axes.dirty)});
        return gap;
    };
    // A small role, none of it in colour: its nearest in colour, the gap said.
    const auto takeOutOfColour =
        [&](const std::vector<std::size_t>& among, std::size_t roleSize, std::vector<std::string> reasons)
    {
        const auto nearest = *std::min_element(
            among.begin(), among.end(), [&](std::size_t a, std::size_t b) { return gapOf(a) < gapOf(b); });
        const auto gap = gapOf(nearest);
        reasons.push_back("hors couleur : écart de " + french(gap, 2) + " sur un axe, au-delà de " +
                          french(colourApart, 2) + " ; ta bibliothèque n'en compte que " +
                          std::to_string(roleSize));
        take(nearest, std::move(reasons));
        kit.picks.back().outOfColour = true;
        kit.picks.back().colourGap = gap;
    };

    // --- the 808: on the tonic, or its fifth, within 15 cents
    std::optional<std::size_t> eightOhEight;
    {
        const auto all = candidates(Role::bass808);
        const auto tunedTo = [&](std::size_t index, int pitchClass)
        {
            const auto& f = library[index].features;
            return f.pitched && f.pitchClass == pitchClass && std::abs(f.cents) <= tunedCents;
        };
        if (all.empty())
            kit.missing.emplace_back("aucune 808 dans ta bibliothèque");
        else if (!tonic.has_value())
        {
            eightOhEight = all.front();
            const auto& f = library[all.front()].features;
            take(all.front(),
                 {"pas de tonalité dans le projet : la 808 n'est pas accordée (" + noteName(f.pitchClass) +
                  " " + signedCents(f.cents) + ")"});
        }
        else
        {
            for (const auto degree : {0, 7})
            {
                const auto pitchClass = (*tonic + degree) % 12;
                for (const auto index : all)
                    if (tunedTo(index, pitchClass))
                    {
                        eightOhEight = index;
                        const auto& f = library[index].features;
                        take(index,
                             {"808 en " + noteName(f.pitchClass) + " (" + signedCents(f.cents) + "), " +
                              (degree == 0 ? "la tonique" : "la quinte") + " de " + noteName(*tonic) +
                              (std::abs(f.glideSemitones) >= 0.5
                                   ? ", une glissade de " + french(f.glideSemitones, 1) + " demi-tons"
                                   : std::string{})});
                        break;
                    }
                if (eightOhEight)
                    break;
            }
            if (!eightOhEight)
            {
                // Nothing is made up: the nearest one, said.
                const auto away = [&](std::size_t index)
                {
                    const auto& f = library[index].features;
                    const auto steps = std::abs(f.pitchClass - *tonic) % 12;
                    return std::min(steps, 12 - steps) * 100.0 + std::abs(f.cents);
                };
                const auto nearest = *std::min_element(
                    all.begin(), all.end(), [&](std::size_t a, std::size_t b) { return away(a) < away(b); });
                const auto& f = library[nearest].features;
                kit.missing.push_back(
                    "aucune 808 en " + noteName(*tonic) +
                    " ni sur sa quinte ; la plus proche : " + fileOf(library[nearest].path) + " en " +
                    noteName(f.pitchClass) + " (" + signedCents(f.cents) + ")");
            }
        }
    }

    // --- the kick: apart from the 808 in the low end
    {
        bool found = false;
        std::string refused;
        const auto kicks = candidates(Role::kick);
        // Why a kick leaves the 808 its low end, or nothing when it does not.
        const auto lowEndKept = [&](std::size_t index) -> std::optional<std::vector<std::string>>
        {
            std::vector<std::string> reasons;
            if (eightOhEight)
            {
                const auto& kick = library[index].features;
                const auto& bass = library[*eightOhEight].features;
                const auto correlation = lowCorrelation(kick, bass);
                const auto ratio = bass.pitchHz > 0.0 ? kick.lowPeakHz / bass.pitchHz : 0.0;
                if (correlation >= overlapCorrelation || (ratio > 1.0 / lowApart && ratio < lowApart))
                    return std::nullopt;
                reasons.push_back("grave du kick à " + french(kick.lowPeakHz, 0) + " Hz, la 808 à " +
                                  french(bass.pitchHz, 0) + " Hz ; corrélation de leurs graves " +
                                  french(correlation, 2));
            }
            return reasons;
        };
        std::vector<std::size_t> outOfColourOnly; // the low end kept, the colour not
        for (const auto index : kicks)
        {
            auto reasons = lowEndKept(index);
            if (!reasons)
            {
                refused = "aucun kick qui laisse la place à la 808 dans le grave";
                continue;
            }
            if (!withChosen(index))
            {
                refused = "aucun kick assez proche de la couleur du kit";
                outOfColourOnly.push_back(index);
                continue;
            }
            take(index, std::move(*reasons));
            found = true;
            break;
        }
        if (!found && !outOfColourOnly.empty() && kicks.size() < smallRole)
        {
            const auto nearest =
                *std::min_element(outOfColourOnly.begin(),
                                  outOfColourOnly.end(),
                                  [&](std::size_t a, std::size_t b) { return gapOf(a) < gapOf(b); });
            takeOutOfColour({nearest}, kicks.size(), std::move(*lowEndKept(nearest)));
            found = true;
        }
        if (!found)
            kit.missing.push_back(refused.empty() ? "aucun kick dans ta bibliothèque" : refused);
    }

    // --- the others, the colour of the kit kept
    const auto inColour = [&](Role role)
    {
        for (const auto index : candidates(role))
            if (withChosen(index))
            {
                take(index, {});
                return true;
            }
        return false;
    };
    const auto outOfColour = [&](Role role)
    {
        const auto all = candidates(role);
        if (all.empty() || all.size() >= smallRole)
            return false;
        takeOutOfColour(all, all.size(), {});
        return true;
    };
    const auto choosePlain = [&](Role role, const std::string& none, const std::string& apart)
    {
        if (!inColour(role) && !outOfColour(role))
            kit.missing.push_back(candidates(role).empty() ? none : apart);
    };
    // A snare, else a clap, in colour; only then one of them out of colour.
    if (!inColour(Role::snare) && !inColour(Role::clap) && !outOfColour(Role::snare) &&
        !outOfColour(Role::clap))
        kit.missing.emplace_back(
            candidates(Role::snare).empty() && candidates(Role::clap).empty()
                ? "ni caisse claire ni clap dans ta bibliothèque"
                : "aucune caisse claire ni aucun clap assez proche de la couleur du kit");
    choosePlain(Role::closedHat,
                "pas de charley fermé dans ta bibliothèque",
                "aucun charley fermé assez proche de la couleur du kit");
    choosePlain(Role::openHat,
                "pas de charley ouvert dans ta bibliothèque",
                "aucun charley ouvert assez proche de la couleur du kit");
    choosePlain(Role::percussion,
                "pas de percussion dans ta bibliothèque",
                "aucune percussion assez proche de la couleur du kit");
    return kit;
}

} // namespace daw::domain::kit
