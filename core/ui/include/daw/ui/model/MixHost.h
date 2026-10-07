#pragma once

#include "daw/domain/BlobRef.h"
#include "daw/domain/Ids.h"
#include "daw/domain/mix/Decision.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_events/juce_events.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace daw::ui
{

// What the mixer is allowed to know of the mix by the AI (S20).
//
// Measuring renders a copy of the session on a thread of its own, deciding may
// ask a model over a socket, verifying renders again: none of that belongs in
// a panel. The panel shows a stage, a progress, a proposal strip by strip with
// its sentences, and asks: start, cancel, keep, refuse, listen before or
// after, move an axis. The same shape as CopilotHost, for the same reason.
class MixHost : public juce::ChangeBroadcaster
{
public:
    enum class Stage
    {
        idle,
        measuring, // the render of the session: progress() moves
        deciding,  // the rules, or the model
        verifying, // the render of the proposal on a copy
        ready,     // a proposal is shown; nothing is written yet
        failed     // why in status()
    };

    MixHost() = default;
    ~MixHost() override = default;
    MixHost(const MixHost&) = delete;
    MixHost& operator=(const MixHost&) = delete;
    MixHost(MixHost&&) = delete;
    MixHost& operator=(MixHost&&) = delete;

    [[nodiscard]] virtual Stage stage() const = 0;
    [[nodiscard]] virtual double progress() const = 0;    // 0..1 while measuring or verifying
    [[nodiscard]] virtual std::string status() const = 0; // French, one line

    // « Mixer »: measures (or reuses the measure if nothing that sounds has
    // changed), decides, verifies. Returns at once.
    virtual void start() = 0;
    virtual void cancel() = 0;

    // The proposal, once ready: what passed the guards, and the master trim
    // the verification asked for, as a last change of the master.
    [[nodiscard]] virtual const domain::mix::Proposal* proposal() const = 0;
    [[nodiscard]] virtual const domain::mix::Brief* brief() const = 0;

    // The proposal in the audio flux (S24): the project as it would leave it,
    // and what the taps of the proposal's copy heard at one place — a strip's
    // TrackId text, the master's included, and a slot as FluxHost names it —
    // over the window rendered from where the playhead stood. Measured, never
    // simulated. Nothing before a proposal is ready.
    [[nodiscard]] virtual const domain::ProjectState* proposedState() const { return nullptr; }
    [[nodiscard]] virtual std::vector<float> proposedSound(const std::string& strip,
                                                           const std::string& slot) const
    {
        static_cast<void>(strip);
        static_cast<void>(slot);
        return {};
    }

    // What the verification did to the master, said: empty when it did not
    // have to touch it. Refusing the master's strip leaves it out.
    [[nodiscard]] virtual std::string masterSentence() const = 0;

    // The sentences of a mix that was kept, read back from what the history
    // line holds by digest: empty when the context is not a mix, or is gone.
    [[nodiscard]] virtual std::vector<std::string> sentencesOf(const domain::BlobRef& context) const = 0;

    // A strip whose changes the person refuses: left out at acceptance.
    virtual void refuseTrack(domain::TrackId track, bool refused) = 0;
    [[nodiscard]] virtual bool isRefused(domain::TrackId track) const = 0;

    // Writes what is kept, in one group of the history, by the copilot.
    virtual void accept() = 0;
    // Drops the proposal; nothing was written.
    virtual void reject() = 0;

    // Before / after, at equal loudness: the two renders the session made,
    // played side by side in step, the louder one turned down by what the
    // measure says separates them. The project is never touched.
    virtual void listen(bool after) = 0;
    [[nodiscard]] virtual bool listening() const = 0; // either side, playing
    [[nodiscard]] virtual bool listeningAfter() const = 0;

    // The axes: moving one decides again, without measuring again.
    [[nodiscard]] virtual domain::mix::Axes axes() const = 0;
    virtual void setAxes(domain::mix::Axes axes) = 0;

    // « Mixer comme ce morceau »: a recording measured like the master, as
    // the target, and how far towards it (0 to 1).
    virtual void setReference(const std::string& path) = 0;
    virtual void clearReference() = 0;
    [[nodiscard]] virtual std::string reference() const = 0; // its name, empty without
    virtual void setReferenceAmount(double amount) = 0;
    [[nodiscard]] virtual double referenceAmount() const = 0;
};

} // namespace daw::ui
