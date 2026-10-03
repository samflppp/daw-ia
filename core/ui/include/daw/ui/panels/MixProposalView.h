#pragma once

#include "daw/domain/project/ProjectState.h"
#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/model/MixHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui
{

// The mix by the AI, in the mixer (S20): what it is doing, then what it
// proposes, strip by strip, each change with its sentence.
//
// It takes the place of the strips while a mix is running or proposed, the
// way the readiness report did. Nothing here is written to the project: the
// person listens before and after, at equal loudness, unticks the strips
// whose changes they do not want, moves an axis (which decides again without
// measuring again), and keeps or refuses. All of it goes through MixHost.
class MixProposalView final : public juce::Component, private juce::ChangeListener
{
public:
    MixProposalView(MixHost& mix,
                    const domain::ProjectState& state,
                    const Tokens& tokens,
                    DawLookAndFeel& lookAndFeel);
    ~MixProposalView() override;

    MixProposalView(const MixProposalView&) = delete;
    MixProposalView& operator=(const MixProposalView&) = delete;
    MixProposalView(MixProposalView&&) = delete;
    MixProposalView& operator=(MixProposalView&&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // For the verification, which clicks what a person clicks.
    [[nodiscard]] int rowCount() const;
    [[nodiscard]] juce::Button* rowToggle(domain::TrackId track) const;
    [[nodiscard]] juce::String shownSentences() const;

private:
    class Rows;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refresh();
    void rebuildRows();
    void chooseReference();
    [[nodiscard]] domain::mix::Axes axesShown() const;

    MixHost& mix_;
    const domain::ProjectState& state_;
    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;

    juce::Label status_;
    double progress_{0.0};
    juce::ProgressBar progressBar_{progress_};

    juce::TextButton before_;
    juce::TextButton after_;
    juce::TextButton keep_;
    juce::TextButton reject_;

    juce::Label punchLabel_;
    juce::Slider punch_;
    juce::Label focusLabel_;
    juce::Slider focus_;
    juce::Label widthLabel_;
    juce::Slider width_;

    juce::TextButton reference_;
    juce::Label referenceName_;
    juce::Slider referenceAmount_;
    std::unique_ptr<juce::FileChooser> chooser_;

    juce::Viewport viewport_;
    std::unique_ptr<Rows> rows_;
    const domain::mix::Proposal* shownProposal_{nullptr};
    std::size_t shownChanges_{0};
};

} // namespace daw::ui
