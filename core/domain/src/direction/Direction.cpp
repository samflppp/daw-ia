#include "daw/domain/direction/Direction.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace daw::domain::direction
{
namespace
{

Value keyValue(const generation::Key& key)
{
    return Value::object(
        {{"tonic", Value{key.tonic}},
         {"mode", Value{std::string{key.mode == generation::Mode::major ? "major" : "minor"}}}});
}

Result<generation::Key> keyFrom(const Value& value)
{
    const auto tonic = value.intAt("tonic");
    const auto mode = value.stringAt("mode");
    if (!tonic || !mode || tonic.value() < 0 || tonic.value() > 11 ||
        (mode.value() != "major" && mode.value() != "minor"))
        return fail(ErrorCode::invalidPayload, "a key is a tonic 0-11 and a mode, major or minor");
    return generation::Key{static_cast<int>(tonic.value()),
                           mode.value() == "major" ? generation::Mode::major : generation::Mode::minor};
}

std::string bpmText(double bpm)
{
    std::ostringstream out;
    out << std::lround(bpm);
    return out.str();
}

} // namespace

Value Direction::toValue() const
{
    Value::Array referenceValues;
    for (const auto& reference : references)
        referenceValues.push_back(
            Value::object({{"reading", reference.reading.toValue()}, {"weight", Value{reference.weight}}}));

    Value::Object correctionMembers;
    if (corrections.bpm)
        correctionMembers.emplace_back("bpm", Value{*corrections.bpm});
    if (corrections.key)
        correctionMembers.emplace_back("key", keyValue(*corrections.key));

    return Value::object({{"references", Value::array(std::move(referenceValues))},
                          {"corrections", Value::object(std::move(correctionMembers))},
                          {"amount", Value{amount}}});
}

Result<Direction> Direction::fromValue(const Value& value)
{
    Direction direction;
    if (const auto amount = value.doubleAt("amount"); amount)
    {
        if (amount.value() < 0.0 || amount.value() > 1.0)
            return fail(ErrorCode::invalidArgument, "a direction's amount is between 0 and 1");
        direction.amount = amount.value();
    }

    if (const auto* references = value.find("references"); references != nullptr)
    {
        if (references->asArray() == nullptr)
            return fail(ErrorCode::invalidPayload, "references must be an array");
        for (const auto& item : *references->asArray())
        {
            const auto* reading = item.find("reading");
            if (reading == nullptr)
                return fail(ErrorCode::invalidPayload, "a reference holds its reading");
            auto read = Reading::fromValue(*reading);
            if (!read)
                return read.error();
            Reference reference;
            reference.reading = std::move(read).value();
            if (const auto weight = item.doubleAt("weight"); weight)
            {
                if (!(weight.value() > 0.0))
                    return fail(ErrorCode::invalidArgument, "a reference's weight is above 0");
                reference.weight = weight.value();
            }
            direction.references.push_back(std::move(reference));
        }
    }

    if (const auto* corrections = value.find("corrections"); corrections != nullptr && !corrections->isNull())
    {
        if (const auto bpm = corrections->doubleAt("bpm"); bpm)
        {
            if (bpm.value() < 20.0 || bpm.value() > 400.0)
                return fail(ErrorCode::invalidArgument, "a tempo is between 20 and 400 BPM");
            direction.corrections.bpm = bpm.value();
        }
        if (const auto* key = corrections->find("key"); key != nullptr && !key->isNull())
        {
            auto read = keyFrom(*key);
            if (!read)
                return read.error();
            direction.corrections.key = read.value();
        }
    }
    return direction;
}

Combined combine(const Direction& direction)
{
    Combined combined;
    combined.amount = direction.amount;
    const auto& references = direction.references;

    // --- the tempo: one pulse, or a contradiction said.
    std::vector<const Reference*> timed;
    for (const auto& reference : references)
        if (reference.reading.bpm)
            timed.push_back(&reference);
    if (direction.corrections.bpm)
    {
        combined.bpm = direction.corrections.bpm;
        combined.bpmCorrected = true;
    }
    else if (!timed.empty())
    {
        const auto first = *timed.front()->reading.bpm;
        const auto agree =
            std::all_of(timed.begin(),
                        timed.end(),
                        [first](const Reference* reference)
                        { return std::abs(*reference->reading.bpm / first - 1.0) <= sameTempo; });
        if (agree)
        {
            double sum = 0.0;
            double weights = 0.0;
            for (const auto* reference : timed)
            {
                sum += reference->weight * *reference->reading.bpm;
                weights += reference->weight;
            }
            combined.bpm = sum / weights;
        }
        else
        {
            std::string said = "Tempos différents :";
            for (const auto* reference : timed)
                said += " " + bpmText(*reference->reading.bpm) + " BPM (" + reference->reading.name + ")";
            combined.contradictions.push_back(said + ". Choisis-en un.");
        }
    }

    // --- the key: one, or a contradiction said; candidates when none settles.
    if (direction.corrections.key)
    {
        combined.key = direction.corrections.key;
        combined.keyCorrected = true;
    }
    else
    {
        std::vector<const Reference*> keyed;
        for (const auto& reference : references)
            if (reference.reading.key)
                keyed.push_back(&reference);
        if (!keyed.empty())
        {
            const auto first = *keyed.front()->reading.key;
            const auto agree = std::all_of(keyed.begin(),
                                           keyed.end(),
                                           [&first](const Reference* reference)
                                           { return *reference->reading.key == first; });
            if (agree)
                combined.key = first;
            else
            {
                std::string said = "Tonalités différentes :";
                for (const auto* reference : keyed)
                    said += " " + generation::describe(*reference->reading.key) + " (" +
                            reference->reading.name + ")";
                combined.contradictions.push_back(said + ". Choisis-en une.");
            }
        }
        else
        {
            for (const auto& reference : references)
                for (const auto& candidate : reference.reading.keyCandidates)
                    if (std::find(combined.keyCandidates.begin(), combined.keyCandidates.end(), candidate) ==
                        combined.keyCandidates.end())
                        combined.keyCandidates.push_back(candidate);
        }
    }

    if (references.empty())
        return combined;

    // --- the continuous values: weighted means.
    double weights = 0.0;
    std::array<double, mix::bandCount> tilt{};
    double crest = 0.0;
    double side = 0.0;
    for (const auto& reference : references)
    {
        weights += reference.weight;
        for (std::size_t band = 0; band < tilt.size(); ++band)
            tilt[band] += reference.weight * reference.reading.tilt[band];
        crest += reference.weight * reference.reading.crestDb;
        side += reference.weight * reference.reading.sideShare;
        for (const auto& [stem, read] : reference.reading.stems)
        {
            combined.balanceDb[stem] += reference.weight * read.balanceDb;
            combined.activeShare[stem] += reference.weight * read.activeShare;
        }
    }
    for (auto& value : tilt)
        value /= weights;
    combined.tilt = tilt;
    combined.crestDb = crest / weights;
    combined.sideShare = side / weights;
    for (auto& [stem, value] : combined.balanceDb)
        value /= weights;
    for (auto& [stem, value] : combined.activeShare)
        value /= weights;

    const auto heaviest =
        std::max_element(references.begin(),
                         references.end(),
                         [](const Reference& a, const Reference& b) { return a.weight < b.weight; });
    combined.sections = heaviest->reading.sections;
    combined.sectionsBpm = heaviest->reading.bpm;
    return combined;
}

std::vector<GridSection> onGrid(const Combined& combined)
{
    std::vector<GridSection> out;
    if (!combined.sectionsBpm || *combined.sectionsBpm <= 0.0)
        return out;

    // Reading.cpp: a bar of the reference is four beats at its tempo.
    constexpr double beatsPerReferenceBar = 4.0;
    const auto barSeconds = beatsPerReferenceBar * 60.0 / *combined.sectionsBpm;
    const auto toBar = [barSeconds](double seconds)
    { return static_cast<double>(std::lround(seconds / barSeconds)); };

    for (const auto& section : combined.sections)
    {
        GridSection placed;
        placed.fromBeats = toBar(section.fromSeconds) * beatsPerReferenceBar;
        placed.toBeats = std::max(placed.fromBeats + beatsPerReferenceBar,
                                  toBar(section.toSeconds) * beatsPerReferenceBar);
        placed.label = section.label;
        out.push_back(placed);
    }
    return out;
}

int firstBar(const GridSection& section, double beatsPerBar)
{
    return 1 + static_cast<int>(std::floor(section.fromBeats / beatsPerBar + 1e-9));
}

int lastBar(const GridSection& section, double beatsPerBar)
{
    return std::max(firstBar(section, beatsPerBar),
                    static_cast<int>(std::ceil(section.toBeats / beatsPerBar - 1e-9)));
}

} // namespace daw::domain::direction
