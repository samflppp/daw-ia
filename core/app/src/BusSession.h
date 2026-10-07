#pragma once

#include "MixComparison.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/MixRender.h"
#include "daw/engine/PluginCatalogue.h"
#include "daw/ui/model/BusHost.h"
#include "daw/ui/model/TransportClock.h"

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

namespace daw::app
{

// The smart buses in the application (S24). A proposal tried on copies of
// the session, never on the one that plays: the song before, the song after
// (its flux captured from the playhead), and for a send, the effect alone
// on a known noise with and without it — what it lets through dry is the
// correlation at zero delay. Kept, it is one group of the history.
class BusSession final : public ui::BusHost, private juce::Timer
{
public:
    struct Wiring
    {
        domain::CommandBus& bus;
        const domain::ProjectState& state;
        tracktion::Edit& edit;
        engine::PluginCatalogue* catalogue{nullptr};
        std::function<engine::ContentStore*()> store;
        const ui::TransportClock* clock{nullptr};
        juce::AudioDeviceManager* device{nullptr};
    };

    explicit BusSession(Wiring wiring);
    ~BusSession() override;

    BusSession(const BusSession&) = delete;
    BusSession& operator=(const BusSession&) = delete;
    BusSession(BusSession&&) = delete;
    BusSession& operator=(BusSession&&) = delete;

    [[nodiscard]] Stage stage() const override { return stage_; }
    [[nodiscard]] double progress() const override { return progress_.load(); }
    [[nodiscard]] std::string status() const override { return status_; }

    void propose() override;
    [[nodiscard]] const std::vector<domain::buses::Shared>& proposals() const override { return proposals_; }

    void tryOut(std::size_t proposal) override;
    [[nodiscard]] std::optional<std::size_t> tried() const override { return tried_; }
    [[nodiscard]] const Tried* result() const override { return result_ ? &*result_ : nullptr; }

    [[nodiscard]] const domain::ProjectState* proposedState() const override { return proposedState_.get(); }
    [[nodiscard]] std::vector<float> proposedSound(const ui::FluxHost::Place& place) const override;

    void listen(bool after) override;
    void stopListening() override;
    bool keep() override;
    void refuse() override;

    // What the catalogue says a plugin is: « reverb », « delay »,
    // « instrument », or something else.
    [[nodiscard]] std::string categoryOf(const domain::PluginRef& ref) const;

    // The renders of the last try, for the verification.
    [[nodiscard]] const juce::File& beforeFile() const noexcept { return beforeFile_; }
    [[nodiscard]] const juce::File& afterFile() const noexcept { return afterFile_; }

private:
    void timerCallback() override;
    void setStage(Stage stage, std::string status);
    void clearTry();
    void join();

    Wiring wiring_;
    Stage stage_{Stage::idle};
    std::string status_;
    std::atomic<double> progress_{0.0};
    std::atomic<bool> cancelled_{false};
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    std::vector<domain::buses::Shared> proposals_;
    std::optional<std::size_t> tried_;
    std::optional<Tried> result_;
    std::shared_ptr<domain::ProjectState> proposedState_;
    domain::TrackId proposedBus_;
    domain::PluginId proposedPlugin_;
    std::map<std::tuple<std::string, std::string, bool>, std::vector<float>> proposedFlux_;
    std::unique_ptr<engine::MixRender> before_;
    std::unique_ptr<engine::MixRender> after_;
    std::unique_ptr<engine::MixRender> probeWith_;
    std::unique_ptr<engine::MixRender> probeWithout_;
    juce::File beforeFile_;
    juce::File afterFile_;
    std::unique_ptr<MixComparison> comparison_;
};

} // namespace daw::app
