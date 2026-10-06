#include "daw/domain/live/AudioAdvice.h"

#include "daw/domain/live/Timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace daw::domain::live
{
namespace
{

// A number of milliseconds the French way: one decimal, a comma.
std::string milliseconds(double seconds)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", seconds * 1000.0);
    std::string out{text};
    std::replace(out.begin(), out.end(), '.', ',');
    return out + " ms";
}

const AudioDriver* find(const std::vector<AudioDriver>& drivers, std::string_view type)
{
    for (const auto& driver : drivers)
    {
        if (driver.type == type)
            return &driver;
    }
    return nullptr;
}

// Less wait at worst; on a tie, the driver then the buffer, so the advice
// is the same for the same trials.
bool waitsLess(const CardTrial& lhs, const CardTrial& rhs)
{
    const auto a = worstKeyToEar(lhs);
    const auto b = worstKeyToEar(rhs);
    if (a != b)
        return a < b;
    if (lhs.type != rhs.type)
        return lhs.type < rhs.type;
    return lhs.buffer < rhs.buffer;
}

} // namespace

std::vector<TrialSetup> trialSetups(const std::vector<AudioDriver>& drivers)
{
    std::vector<TrialSetup> setups;
    if (const auto* shared = find(drivers, sharedDriver); shared != nullptr && !shared->buffers.empty())
        setups.push_back({std::string{sharedDriver}, shared->buffers.front()});
    if (const auto* low = find(drivers, lowLatencyDriver); low != nullptr)
    {
        for (const auto size : low->buffers)
        {
            if (size > 0 && size <= 480)
                setups.push_back({std::string{lowLatencyDriver}, size});
        }
    }
    if (const auto* exclusive = find(drivers, exclusiveDriver);
        exclusive != nullptr && !exclusive->buffers.empty())
    {
        bool any = false;
        for (const auto wanted : exclusiveTrials)
        {
            if (std::find(exclusive->buffers.begin(), exclusive->buffers.end(), wanted) !=
                exclusive->buffers.end())
            {
                setups.push_back({std::string{exclusiveDriver}, wanted});
                any = true;
            }
        }
        if (!any)
            setups.push_back({std::string{exclusiveDriver},
                              *std::min_element(exclusive->buffers.begin(), exclusive->buffers.end())});
    }
    return setups;
}

bool held(const CardTrial& trial) noexcept
{
    return trial.timing.blocks >= trialMinBlocks && trial.timing.late == 0;
}

double worstKeyToEar(const CardTrial& trial) noexcept
{
    if (trial.sampleRate <= 0.0 || trial.buffer <= 0)
        return 0.0;
    const auto block = trial.buffer / trial.sampleRate;
    const auto margin = std::max(Timeline::marginSeconds, Timeline::marginShare * block);
    const auto overshoot = std::max(0.0, trial.timing.worstSeconds - block - margin);
    return block + margin + overshoot + trial.outputSeconds;
}

int closestBuffer(const std::vector<int>& buffers, int wanted)
{
    int best = 0;
    for (const auto size : buffers)
    {
        if (size <= 0)
            continue;
        if (best == 0 || std::abs(size - wanted) < std::abs(best - wanted) ||
            (std::abs(size - wanted) == std::abs(best - wanted) && size < best))
            best = size;
    }
    return best;
}

AudioAdvice advise(const std::vector<CardTrial>& trials, const std::string& currentType, int currentBuffer)
{
    AudioAdvice advice;
    advice.type = currentType;
    advice.buffer = currentBuffer;

    if (trials.empty())
    {
        advice.already = true;
        advice.sentence = "Pas encore mesurée : « Tester ma carte » essaie chaque réglage quelques secondes, "
                          "le son coupé, et conseille celui qui tient.";
        return advice;
    }
    advice.measured = true;

    std::optional<CardTrial> best;
    std::optional<CardTrial> bestSmall;
    for (const auto& trial : trials)
    {
        if (!held(trial))
            continue;
        if (!best.has_value() || waitsLess(trial, *best))
            best = trial;
        if (trial.buffer <= advisedMaxBuffer && (!bestSmall.has_value() || waitsLess(trial, *bestSmall)))
            bestSmall = trial;
    }
    if (bestSmall.has_value())
        best = bestSmall;

    const auto dropped =
        std::count_if(trials.begin(), trials.end(), [](const CardTrial& trial) { return !held(trial); });
    const auto said = std::to_string(trials.size()) + " réglages essayés, " + std::to_string(dropped) +
                      (dropped > 1 ? " décrochent" : " décroche");

    if (!best.has_value())
    {
        advice.already = true;
        advice.sentence = "Aucun réglage n'a tenu sans décrocher (" + said + ") : on garde " + currentType +
                          ", " + std::to_string(currentBuffer) + " échantillons.";
        return advice;
    }

    advice.type = best->type;
    advice.buffer = best->buffer;
    advice.silencesOthers = best->type == exclusiveDriver;
    advice.already = advice.type == currentType && advice.buffer == currentBuffer;
    advice.sentence = best->type + ", " + std::to_string(best->buffer) + " échantillons : au pire " +
                      milliseconds(worstKeyToEar(*best)) + " de la touche à l'oreille, sans un décrochage (" +
                      said + ")." +
                      (advice.silencesOthers ? " Les autres logiciels se taisent tant que DAW IA joue." : "");
    if (advice.already)
        advice.sentence = "Réglage conseillé, déjà en place. " + advice.sentence;
    return advice;
}

std::string describeLatency(double playSeconds, bool measured, double outputSeconds)
{
    return "de la touche au premier échantillon rendu : " + milliseconds(playSeconds) +
           (measured ? " (mesuré)" : " (attente du jeu)") + " ; la carte déclare " +
           milliseconds(outputSeconds) +
           " ; de la touche à l'oreille : " + milliseconds(playSeconds + outputSeconds);
}

Value toValue(const std::vector<CardTrial>& trials)
{
    Value::Array items;
    for (const auto& trial : trials)
    {
        items.push_back(Value::object({{"type", Value{trial.type}},
                                       {"buffer", Value{trial.buffer}},
                                       {"rate", Value{trial.sampleRate}},
                                       {"output", Value{trial.outputSeconds}},
                                       {"blocks", Value{trial.timing.blocks}},
                                       {"size", Value{trial.timing.lastSize}},
                                       {"mean", Value{trial.timing.meanSeconds}},
                                       {"worst", Value{trial.timing.worstSeconds}},
                                       {"late", Value{trial.timing.late}}}));
    }
    return Value::array(std::move(items));
}

std::vector<CardTrial> trialsFromValue(const Value& value)
{
    std::vector<CardTrial> trials;
    const auto* items = value.asArray();
    if (items == nullptr)
        return trials;
    for (const auto& item : *items)
    {
        const auto type = item.stringAt("type");
        const auto buffer = item.intAt("buffer");
        const auto rate = item.doubleAt("rate");
        const auto output = item.doubleAt("output");
        const auto blocks = item.intAt("blocks");
        const auto size = item.intAt("size");
        const auto mean = item.doubleAt("mean");
        const auto worst = item.doubleAt("worst");
        const auto late = item.intAt("late");
        // A trial written by another version, or damaged: left out whole.
        if (!type || !buffer || !rate || !output || !blocks || !size || !mean || !worst || !late)
            continue;
        CardTrial trial;
        trial.type = type.value();
        trial.buffer = static_cast<int>(buffer.value());
        trial.sampleRate = rate.value();
        trial.outputSeconds = output.value();
        trial.timing.blocks = blocks.value();
        trial.timing.lastSize = static_cast<int>(size.value());
        trial.timing.meanSeconds = mean.value();
        trial.timing.worstSeconds = worst.value();
        trial.timing.late = late.value();
        trials.push_back(std::move(trial));
    }
    return trials;
}

} // namespace daw::domain::live
