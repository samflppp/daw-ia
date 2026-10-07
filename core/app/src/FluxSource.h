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
            wanted.push_back({place.strip, place.slot});
        taps_.arm(wanted);
    }

    [[nodiscard]] std::int64_t latest() const override { return taps_.latest(); }
    [[nodiscard]] double sampleRate() const override { return taps_.sampleRate(); }

    void read(const Place& place, std::int64_t from, int count, float* out) const override
    {
        taps_.read({place.strip, place.slot}, from, count, out);
    }

private:
    engine::FluxTaps taps_;
};

} // namespace daw::app
