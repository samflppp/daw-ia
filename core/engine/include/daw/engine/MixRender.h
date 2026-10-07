#pragma once

#include "daw/domain/mix/Measurement.h"
#include "daw/domain/project/ProjectState.h"

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace daw::engine
{

class PluginCatalogue;
class ContentStore;

// The render the mix listens to (S20): every track after its inserts and
// before its fader, and the master as it leaves, measured in one pass.
//
// Never on the Edit that plays. The live Edit is copied — its tree, plugin
// states flushed first, so the copy sounds as the session does now, including
// what a plugin holds between two captures — and the copy is rendered. The
// person keeps playing, the interface keeps drawing: only the copy's
// construction runs on the message thread (Tracktion loads plugins there),
// the render and the analysis run on the caller's thread, which is never the
// message thread nor the audio one.
//
// A proposal is measured the same way: the copy is given a projector of its
// own over the proposed state, which reconciles only what differs. The live
// Edit and the project are not touched.
class MixRender
{
public:
    struct Measured
    {
        // One per domain track that sounds: its pre-fader measure. A track
        // with recordings and notes has two Tracktion tracks; their measures
        // are combined (see domain::mix::combine).
        std::map<std::string, domain::mix::StreamMeasure> tracks;
        domain::mix::StreamMeasure master;
        double renderSeconds{0.0}; // the wall time of render and analysis
    };

    // Message thread. `proposed`, when given, is projected on the copy; the
    // catalogue and the store are the live projector's, so a plugin of the
    // proposal is found and a sample is read.
    // `state` is what the live Edit projects: its tracks are the ones
    // measured.
    [[nodiscard]] static std::unique_ptr<MixRender> prepare(tracktion::Edit& live,
                                                            const domain::ProjectState& state,
                                                            const domain::ProjectState* proposed,
                                                            PluginCatalogue* catalogue,
                                                            ContentStore* store);
    ~MixRender();

    MixRender(const MixRender&) = delete;
    MixRender& operator=(const MixRender&) = delete;
    MixRender(MixRender&&) = delete;
    MixRender& operator=(MixRender&&) = delete;

    // Any thread but the message and audio ones. `progress` is called with 0
    // to 1 from that thread. Returns nothing when cancelled or when the render
    // could not start.
    [[nodiscard]] std::unique_ptr<Measured> run(const std::atomic<bool>& cancelled,
                                                const std::function<void(double)>& progress);

    // How long the copy took to build on the message thread, in ms: the only
    // moment the interface waits.
    [[nodiscard]] double prepareMs() const noexcept { return prepareMs_; }

    // The master as rendered, a 32-bit WAV, handed over: the caller deletes
    // it. Empty before run() has finished. Before and after are listened to
    // from these, never from the Edit that plays.
    [[nodiscard]] juce::File releaseFile();

    // The flux of what is rendered (S24): `seconds` of every flux tap of the
    // copy from `fromSeconds`, captured while run() renders. Message thread,
    // before run(). Afterwards, per place — the strip as the domain's TrackId
    // text, the master's included, and the slot —, the taps of the place
    // added; and the rate of their samples.
    void captureFlux(double fromSeconds, double seconds);
    [[nodiscard]] std::map<std::pair<std::string, std::string>, std::vector<float>> fluxCaptured() const;
    [[nodiscard]] double fluxRate() const noexcept { return fluxRate_; }

    // Must be destroyed on the message thread: the copy unloads plugins.
private:
    MixRender() = default;

    struct Slots;
    std::unique_ptr<tracktion::Edit> copy_;
    std::unique_ptr<domain::ProjectState> proposed_;
    std::unique_ptr<class ProjectProjector> projector_;
    std::unique_ptr<Slots> slots_;
    double prepareMs_{0.0};
    double fluxRate_{48000.0};
    std::atomic<bool> finished_{false};
};

} // namespace daw::engine
