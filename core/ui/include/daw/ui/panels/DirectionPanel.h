#pragma once

#include "daw/ui/PanelRegistry.h"
#include "daw/ui/model/DirectionHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace daw::ui
{

// The direction by references (S22): what was understood of the references,
// and every value correctable by hand.
//
//   the references, each with its weight and a way to take it out;
//   the tempo and the key the references settle, or the sentence that says
//   they disagree, or the candidates when the notes cannot tell; each one
//   corrected here, and given back to the references with « auto »;
//   the sections of the reference that counts most, as a strip;
//   the amount of direction, 0 to 1, the slider of S20.
//
// Not a preset: what the panel shows is read from the references, and what
// the person changes is written by direction.set, one Ctrl+Z each.
class DirectionPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit DirectionPanel(const PanelContext& context);
    ~DirectionPanel() override;

    DirectionPanel(const DirectionPanel&) = delete;
    DirectionPanel& operator=(const DirectionPanel&) = delete;
    DirectionPanel(DirectionPanel&&) = delete;
    DirectionPanel& operator=(DirectionPanel&&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // What the panel shows, as text, for the verification: the tempo line,
    // the key line, the contradictions, the sections' letters, and the
    // sections as they read on the project's grid ("A 1–8, B 9–24"), empty
    // when they were cut without a tempo.
    [[nodiscard]] juce::String tempoLine() const { return tempo_.getText(); }
    [[nodiscard]] juce::String keyLine() const { return key_.getText(); }
    [[nodiscard]] juce::String contradictionsLine() const { return contradictions_.getText(); }
    [[nodiscard]] std::string sectionLetters() const;
    [[nodiscard]] std::string sectionBars() const;
    [[nodiscard]] int referenceRows() const { return static_cast<int>(rows_.size()); }

    // Corrects the tempo or the key the way the person does, through the
    // panel's own controls.
    void typeTempo(const juce::String& text);
    void chooseKey(int itemId);

private:
    class Row;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refresh();
    void chooseReference();
    [[nodiscard]] juce::Rectangle<int> sectionsArea() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    DirectionHost& direction_;
    bool titled_{false};

    juce::TextButton add_;
    juce::TextButton cancel_;
    juce::TextButton clear_;
    juce::Label status_;
    double progress_{0.0};
    juce::ProgressBar progressBar_{progress_};

    std::vector<std::unique_ptr<Row>> rows_;

    juce::Label tempo_;
    juce::TextEditor tempoEdit_;
    juce::TextButton tempoAuto_;
    juce::Label key_;
    juce::ComboBox keyChoice_;
    juce::Label contradictions_;
    juce::Label amountLabel_;
    juce::Slider amount_;

    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace daw::ui
