#pragma once

#include "CopilotBridge.h"
#include "MixComparison.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/copilot/Usage.h"
#include "daw/domain/mix/Decision.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/MixRender.h"
#include "daw/engine/PluginCatalogue.h"
#include "daw/ui/model/DirectionHost.h"
#include "daw/ui/model/MixHost.h"
#include "daw/ui/model/TransportClock.h"

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <thread>
#include <vector>

namespace daw::app
{

// The mix by the AI, in the application (S20): measure, decide, guard,
// verify, show; nothing written until accepted, then one group by the copilot.
//
//   measure   MixRender on a copy of the Edit, on a thread of its own; reused
//             while nothing that sounds has changed (the project without its
//             transport: a view gesture or a play never measures again)
//   decide    the model through the copilot when its process is there; the
//             rules otherwise, or when the model fails — said in the status
//   guard     mix::check; a model is shown what was refused, once; what is
//             still refused is left out
//   verify    the proposal rendered on another copy: the master's true peak,
//             trimmed under -1 dBTP if needed, and the « after » to listen to
//
// Every step after the copy runs off the message thread; the interface stays
// usable, playback included.
class MixSession final : public ui::MixHost, private juce::Timer
{
public:
    struct Wiring
    {
        domain::CommandBus& bus;
        const domain::ProjectState& state;
        tracktion::Edit& edit;
        engine::PluginCatalogue* catalogue{nullptr};
        engine::ContentStore* store{nullptr};
        CopilotBridge* copilot{nullptr};
        const ui::TransportClock* clock{nullptr};
        juce::AudioDeviceManager* device{nullptr};

        // The direction by references (S22): the mix's target comes from the
        // project's direction, and « Référence… » adds to it. Asked for when
        // needed, built after this session.
        std::function<ui::DirectionHost*()> direction;
    };

    explicit MixSession(Wiring wiring);
    ~MixSession() override;

    MixSession(const MixSession&) = delete;
    MixSession& operator=(const MixSession&) = delete;
    MixSession(MixSession&&) = delete;
    MixSession& operator=(MixSession&&) = delete;

    // --- ui::MixHost
    [[nodiscard]] Stage stage() const override { return stage_; }
    [[nodiscard]] double progress() const override { return progress_.load(); }
    [[nodiscard]] std::string status() const override { return status_; }
    void start() override;
    void cancel() override;
    [[nodiscard]] const domain::mix::Proposal* proposal() const override;
    [[nodiscard]] const domain::mix::Brief* brief() const override;
    [[nodiscard]] std::string masterSentence() const override { return masterSentence_; }
    [[nodiscard]] std::vector<std::string> sentencesOf(const domain::BlobRef& context) const override;
    void refuseTrack(domain::TrackId track, bool refused) override;
    [[nodiscard]] bool isRefused(domain::TrackId track) const override;
    void accept() override;
    void reject() override;
    void listen(bool after) override;
    [[nodiscard]] bool listening() const override { return comparison_ != nullptr && comparison_->playing(); }
    [[nodiscard]] bool listeningAfter() const override;
    [[nodiscard]] domain::mix::Axes axes() const override { return axes_; }
    void setAxes(domain::mix::Axes axes) override;
    // The direction changed: a proposal on screen is decided again, towards it.
    void directionChanged();

    void setReference(const std::string& path) override;
    void clearReference() override;
    [[nodiscard]] std::string reference() const override;
    void setReferenceAmount(double amount) override;
    [[nodiscard]] double referenceAmount() const override { return wiring_.state.direction().amount; }

    // --- what the verification and the copilot read
    [[nodiscard]] const engine::MixRender::Measured* before() const { return before_.get(); }
    [[nodiscard]] const engine::MixRender::Measured* after() const { return after_.get(); }
    [[nodiscard]] std::optional<double> masterTrimDb() const { return masterTrim_; }

    [[nodiscard]] double measureSeconds() const { return measureSeconds_; }
    [[nodiscard]] double prepareMs() const { return prepareMs_; }
    [[nodiscard]] bool measureReused() const { return measureReused_; }
    [[nodiscard]] const std::vector<domain::mix::Refusal>& refused() const { return refusals_; }
    [[nodiscard]] const domain::copilot::Usage& usage() const { return usage_; }
    [[nodiscard]] MixComparison* comparison() { return comparison_.get(); }

    // The rules only, never the model: what --verify-mix runs, without a key.
    void setUseModel(bool use) { useModel_ = use; }

    // For the copilot's mix.start: starts, and says what it does.
    [[nodiscard]] domain::Value startFromCopilot(const domain::Value& arguments);

private:
    void timerCallback() override;
    void setStage(Stage stage, std::string status);
    [[nodiscard]] std::string keyOf() const;

    void measure();
    void decide();
    void decideWithModel(int round, domain::Value previous, domain::Value refusals);
    void decided(domain::mix::Proposal proposal);
    void verify();
    void clearProposal();

    // Runs `work` on a thread; `then` on the message thread if the session is
    // still there and was not cancelled meanwhile.
    //
    // The message thread never waits for a worker that is still running: a
    // render being set up asks the message thread for things, and waiting
    // for it there was a deadlock (found by --verify-mix, at a cancel). A
    // worker is joined once it has said it is done; a cancelled render is
    // kept alive by its worker until then.
    void offThread(std::function<void()> work, std::function<void()> then);
    void reap();

    [[nodiscard]] domain::mix::Axes effectiveAxes() const;
    [[nodiscard]] domain::mix::Target target() const;

    Wiring wiring_;
    Stage stage_{Stage::idle};
    std::string status_;
    std::atomic<double> progress_{0.0};
    std::atomic<bool> cancelled_{false};
    std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};
    struct Worker
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
        std::unique_ptr<engine::MixRender> retired; // a cancelled render, freed once done
    };
    std::vector<Worker> workers_;
    std::uint64_t generation_{0};

    std::unique_ptr<engine::MixRender> render_;
    std::unique_ptr<engine::MixRender::Measured> before_;
    std::unique_ptr<engine::MixRender::Measured> after_;
    juce::File beforeFile_;
    juce::File afterFile_;
    std::string measuredKey_;
    double measureSeconds_{0.0};
    double prepareMs_{0.0};
    bool measureReused_{false};

    std::unique_ptr<domain::mix::Brief> brief_;
    std::unique_ptr<domain::mix::Proposal> proposal_;
    std::vector<domain::mix::Refusal> refusals_;
    std::set<std::string> refusedTracks_;
    std::optional<double> masterTrim_;
    std::string masterSentence_;
    domain::copilot::Usage usage_;
    std::string decisionNote_;

    domain::mix::Axes axes_;
    bool useModel_{true};

    std::unique_ptr<MixComparison> comparison_;
};

} // namespace daw::app
