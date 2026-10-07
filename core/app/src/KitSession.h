#pragma once

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/SampleIndex.h"
#include "daw/ui/model/KitHost.h"
#include "daw/ui/model/SampleHost.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace daw::app
{

// The kit in the application (S24): the index built on a thread of its own
// from the browser's folders, the kit chosen on the message thread (rules,
// in microseconds), the preview mixed in memory and played by the browser's
// preview, the kit laid down in one group.
class KitSession final : public ui::KitHost, private juce::Timer
{
public:
    KitSession(domain::CommandBus& bus,
               const domain::ProjectState& state,
               ui::SampleHost& samples,
               juce::File indexStore);
    ~KitSession() override;

    KitSession(const KitSession&) = delete;
    KitSession& operator=(const KitSession&) = delete;
    KitSession(KitSession&&) = delete;
    KitSession& operator=(KitSession&&) = delete;

    [[nodiscard]] Stage stage() const override { return stage_; }
    [[nodiscard]] double progress() const override { return progress_.load(); }
    [[nodiscard]] std::string status() const override { return status_; }

    void index() override;
    void cancel() override;
    [[nodiscard]] std::size_t librarySize() const override { return library_.size(); }

    [[nodiscard]] domain::kit::Axes directionAxes() const override;
    void choose(const domain::kit::Axes& axes) override;
    [[nodiscard]] const domain::kit::Kit* kit() const override { return kit_ ? &*kit_ : nullptr; }

    void preview() override;
    void listenTo(std::size_t pick) override;
    void stop() override;
    bool pose() override;

    // What the last index cost, for the verification.
    [[nodiscard]] const engine::SampleIndex::Built* lastBuild() const { return built_.get(); }
    // The file the preview plays, mixed.
    [[nodiscard]] const juce::File& previewFile() const noexcept { return previewFile_; }

private:
    void timerCallback() override;
    void setStage(Stage stage, std::string status);
    void join();

    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ui::SampleHost& samples_;
    engine::SampleIndex index_;

    Stage stage_{Stage::idle};
    std::string status_{"Aucun index de tes samples."};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> cancelled_{false};
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    std::unique_ptr<engine::SampleIndex::Built> built_;
    std::vector<domain::kit::Sample> library_;
    std::optional<domain::kit::Kit> kit_;
    juce::File previewFile_;
};

} // namespace daw::app
