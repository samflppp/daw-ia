#pragma once

#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace daw::ui
{

// The generation window (S16): where a prompt is written, and where what came
// of it is said. A strip docked under the panel that owns it, never over the
// grid: the notes one wants to judge stay in sight.
//
// It holds no proposal and reads no project. The panel owns the proposal,
// and tells this bar what to show; the bar tells the panel what was asked.
//
//   first row    the prompt, then Valider and Fermer
//   second row   what is going on, in a musician's words: nothing before a
//                prompt is written, "Je lis ta demande…" while it is read, the
//                short sentence once the grey notes are there. The variants
//                and the "Détails" toggle sit at its right.
//   third row    the technical line of S14-S15, only when "Détails" is open
//
// Enter, Tab and Escape typed in the field go to the panel first: Tab would
// otherwise move the focus, and Escape would do nothing at all.
class GenerationPanel final : public juce::Component, private juce::Timer
{
public:
    GenerationPanel(const Tokens& tokens, DawLookAndFeel& lookAndFeel);
    ~GenerationPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // --- what the panel is told
    std::function<bool(const juce::KeyPress&)> onKey;
    std::function<void(int delta)> onVariant;
    std::function<void()> onAccept;
    std::function<void()> onClose;

    // Called when the bar needs another height: the details opened or closed.
    std::function<void()> onHeightChanged;

    // --- what the panel tells it

    // Opens empty of any proposal, with an example fitting the zone as the
    // field's placeholder. The field keeps what was typed last.
    void open(const juce::String& example);

    // Between a prompt sent and its answer. The row stays alive.
    void showReading();

    struct Shown
    {
        juce::String sentence; // "quatre mesures de mélodie en la mineur"
        juce::String notice;   // "Hors ligne : …", empty when nothing to say
        juce::String unused;   // "Je n'ai pas utilisé : …", empty when all was used
        juce::String details;  // the technical line
        int rank{0};
        int drawn{1};
    };
    void showProposal(const Shown& shown);

    // No proposal, and one sentence saying why.
    void showMessage(const juce::String& message);

    [[nodiscard]] juce::TextEditor& field() noexcept { return field_; }
    [[nodiscard]] int preferredHeight() const;

    // --- read by the verification
    [[nodiscard]] const juce::String& sentence() const noexcept { return shown_.sentence; }
    [[nodiscard]] const juce::String& notice() const noexcept { return shown_.notice; }
    [[nodiscard]] const juce::String& message() const noexcept { return message_; }
    [[nodiscard]] bool isReading() const noexcept { return state_ == State::reading; }
    [[nodiscard]] bool detailsOpen() const noexcept { return detailsOpen_; }
    void setDetailsOpen(bool open);

private:
    void timerCallback() override;
    void refreshButtons();

    class Field final : public juce::TextEditor
    {
    public:
        std::function<bool(const juce::KeyPress&)> onKey;
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (onKey && onKey(key))
                return true;
            return juce::TextEditor::keyPressed(key);
        }
    };

    enum class State
    {
        empty,
        reading,
        proposed,
        message
    };

    [[nodiscard]] juce::Rectangle<int> statusRow() const;
    [[nodiscard]] juce::Rectangle<int> detailsRow() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;

    Field field_;
    juce::TextButton previous_;
    juce::TextButton next_;
    juce::TextButton details_;
    juce::TextButton accept_;
    juce::TextButton close_;

    State state_{State::empty};
    Shown shown_;
    juce::String message_;
    bool detailsOpen_{false};
    int tick_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GenerationPanel)
};

} // namespace daw::ui
