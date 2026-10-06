#pragma once

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/model/PluginHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>
#include <vector>

namespace daw::ui
{

// The effects of one strip of the mixer (S24): put, taken out, moved and
// bypassed without leaving the mixer, by the commands that exist.
//
//   one slot per effect, in the chain's order: a dot, lit when the effect
//   plays, hollow when it is bypassed (a click on it: plugin.set_bypassed);
//   its name; what can be said of it without opening it — the curve of
//   the DAW's equaliser, the threshold and ratio of its compressor, and of a
//   plugin of the person's only how many of its settings were touched, for
//   its settings mean nothing to the DAW;
//   dragged onto another slot: plugin.move, the same plugin at its new place;
//   a double-click opens its window; a right-click: Ouvrir, Contourner or
//   Rétablir, Retirer (plugin.remove);
//   the last slot, « ＋ effet »: the DAW's effects first, then the person's,
//   inserted at the end (plugin.insert, the identifier made here).
//
// It holds no plugin and no value the project does not: it reads the strip
// in the state at each refresh.
class InsertSlots final : public juce::Component
{
public:
    InsertSlots(const Tokens& tokens,
                DawLookAndFeel& lookAndFeel,
                domain::CommandBus& bus,
                const domain::ProjectState& state,
                PluginHost& plugins,
                domain::TrackId strip);

    void refresh();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // The gestures, as the person makes them on a slot: for the verification.
    void toggleBypass(std::size_t slot);
    void remove(std::size_t slot);
    void move(std::size_t from, std::size_t to);
    void insert(const domain::PluginRef& ref);
    void open(std::size_t slot);

    // What each slot shows, as text: « ● Égaliseur · coupe-bas 120 Hz »,
    // « ○ Réverb · 3 réglages », the last one « ＋ effet ».
    [[nodiscard]] std::vector<std::string> shown() const;

private:
    struct Slot
    {
        domain::PluginId id;
        std::string name;
        std::string summary; // what is said of it without opening it
        bool bypassed{false};
        bool installed{true};
        bool equaliser{false};
        domain::PluginInstance instance; // for the equaliser's curve
    };

    [[nodiscard]] int rowHeight() const;
    [[nodiscard]] int rows() const;
    // The slots drawn: all of them, or one row short of the room when the
    // chain is longer than the strip, the last row saying how many more.
    [[nodiscard]] int shownSlots() const;
    [[nodiscard]] std::optional<std::size_t> slotAt(juce::Point<int> position) const;
    [[nodiscard]] bool onDot(juce::Point<int> position) const;
    [[nodiscard]] bool onAdd(juce::Point<int> position) const;
    void showAddMenu();
    void showSlotMenu(std::size_t slot);
    void
    drawCurve(juce::Graphics& g, const domain::PluginInstance& instance, juce::Rectangle<int> area) const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    PluginHost& plugins_;
    domain::TrackId strip_;
    std::vector<Slot> slots_;

    std::optional<std::size_t> pressed_;
    std::optional<std::size_t> dropAt_;
    bool dragging_{false};
};

} // namespace daw::ui
