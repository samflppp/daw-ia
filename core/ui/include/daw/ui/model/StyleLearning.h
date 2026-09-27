#pragma once

#include "daw/domain/Value.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/generation/Learning.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace daw::ui
{

// The generator learning from the person, on this machine and nowhere else.
//
// What each project taught is kept in one file per project, under
// %APPDATA%\DAW IA\generation\appris\<project id>.json, never in the project
// and never sent anywhere: the copilot does not see it, no request carries it.
// The style a proposal is drawn with is built at each Ctrl+G from three things:
//
//   the base (the hand-written fallback, StyleSource.h)
//   + every other project, as last saved
//   + the project open, counted there and then, three times
//
// so the bar written thirty seconds ago already weighs on the next proposal,
// and nothing waits for a save. Counting after a save, for the other projects
// to read later, runs on a thread of its own from a copy of the state: the
// message thread only copies, and the audio thread never hears of any of it.
//
// The person can turn it off, leave the open project out, or forget all of it.
class StyleLearning final : public domain::BusObserver
{
public:
    // `folder` holds the files; the default is the one above.
    explicit StyleLearning(juce::File folder = defaultFolder());
    ~StyleLearning() override;

    StyleLearning(const StyleLearning&) = delete;
    StyleLearning& operator=(const StyleLearning&) = delete;
    StyleLearning(StyleLearning&&) = delete;
    StyleLearning& operator=(StyleLearning&&) = delete;

    [[nodiscard]] static juce::File defaultFolder();

    // The project just opened, with the journal it was rebuilt from: the
    // notes the machine wrote are read from there, then from every receipt.
    void attach(const std::string& projectId,
                const std::string& projectName,
                const std::vector<domain::Value>& journal);

    // After a save: the project is counted again on the background thread and
    // written, when something changed since the last time.
    void saved(const domain::ProjectState& state);

    // Waits for a count in flight. Tests and the verification only.
    void settle();

    // Reads the other projects again, on the background thread.
    void reload();

    [[nodiscard]] const juce::File& folder() const noexcept { return folder_; }

    // The style for a proposal in this project.
    [[nodiscard]] std::shared_ptr<const domain::generation::StyleModel>
    model(const domain::ProjectState& state, const domain::generation::StyleModel& base);

    // What the zone says after "style :", for a role, about the last model.
    [[nodiscard]] std::string describe(domain::generation::Role role) const;

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled);

    [[nodiscard]] bool projectExcluded() const noexcept { return excluded_; }
    void setProjectExcluded(bool excluded);

    // Deletes every file of what was learned. The project open still teaches
    // from its notes at the next Ctrl+G, since they are there.
    void forget();

    // How many other projects the next model reads, and their notes.
    [[nodiscard]] int otherProjects() const;

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt&) override { dirty_ = true; }
    void onRedone(const domain::Receipt&) override { dirty_ = true; }

private:
    [[nodiscard]] juce::File fileFor(const std::string& projectId) const;
    void readOthers();
    void writeSettings() const;
    void writeProject(const domain::generation::Learned* learned) const;

    juce::File folder_;
    std::string projectId_;
    std::string projectName_;
    bool enabled_{true};
    bool excluded_{false};
    bool dirty_{true};

    domain::generation::MachineNotes machine_;

    mutable std::mutex mutex_; // guards the two below, written by the background thread
    domain::generation::Learned others_;
    int otherCount_{0};

    // The last model built, and what it was built from.
    domain::generation::Learned current_;
    domain::generation::Learned total_;
    std::shared_ptr<const domain::generation::StyleModel> model_;
    const domain::generation::StyleModel* modelBase_{nullptr};
    std::string baseOrigin_{"repli"};

    juce::ThreadPool pool_{1};
};

// The one the application made, or nothing: tests and the gallery have none,
// and the piano roll then draws with the base alone.
void setStyleLearning(StyleLearning* learning) noexcept;
[[nodiscard]] StyleLearning* styleLearning() noexcept;

} // namespace daw::ui
