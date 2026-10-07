#pragma once

#include "daw/engine/FluxTaps.h"
#include "daw/ui/model/FluxHost.h"

#include <tracktion_engine/tracktion_engine.h>

namespace daw::app
{

// The application's answer to FluxHost (S24): the taps of the live Edit,
// read by place.
class FluxSource final : public ui::FluxHost
{
public:
    explicit FluxSource(tracktion::Edit& edit)
        : taps_{edit}
    {
    }

    void arm(const std::vector<Place>& places) override
    {
        std::vector<engine::FluxTaps::Place> wanted;
        wanted.reserve(places.size());
        for (const auto& place : places)
            wanted.push_back(toEngine(place));
        taps_.arm(wanted);
    }

    // The engine's name for a way: the track, or its recordings' companion.
    [[nodiscard]] static engine::FluxTaps::Place toEngine(const Place& place)
    {
        engine::FluxTaps::Place engine{place.strip, place.slot, std::nullopt};
        if (place.way != Way::both)
            engine.companion = place.way == Way::recordings;
        return engine;
    }

    [[nodiscard]] std::int64_t latest() const override { return taps_.latest(); }
    [[nodiscard]] double sampleRate() const override { return taps_.sampleRate(); }

    void read(const Place& place, std::int64_t from, int count, float* out) const override
    {
        taps_.read(toEngine(place), from, count, out);
    }

    void listen(const std::optional<Place>& place) override
    {
        if (place)
            taps_.listen(toEngine(*place));
        else
            taps_.listen(std::nullopt);
    }

    [[nodiscard]] std::optional<Place> listening() const override
    {
        if (const auto& place = taps_.listening(); place)
        {
            Place said{place->strip, place->slot};
            if (place->companion)
                said.way = *place->companion ? Way::recordings : Way::instrument;
            return said;
        }
        return std::nullopt;
    }

private:
    engine::FluxTaps taps_;
};

} // namespace daw::app
