#pragma once

#include "daw/domain/flux/Graph.h"
#include "daw/ui/FrameTicker.h"
#include "daw/ui/PanelRegistry.h"
#include "daw/ui/model/FluxHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daw::ui
{

// The audio flux (S24): the way the sound goes through the whole song, drawn
// from the project — states of the sound joined by effects, the outputs and
// the sends to the buses and the master. Each state shows the sound there,
// live: its waveform over the last second or so, and its level. A click on
// an effect opens, under the graph, its before and its after: the two
// waveforms one over the other, and the two spectra.
//
// The graph is computed from ProjectState on every change (flux::graphOf);
// where it is drawn, the zoom and the view, are this window's and nobody
// else's. The navigation is the canvas's (S18): Ctrl+wheel zooms around the
// mouse, the wheel scrolls, the middle button drags, F frames everything.
//
// Only the states on the screen are armed; a hidden window arms nothing.
//
// What a person does in it goes through the commands that exist (S24):
//   - an effect dropped on a link of a chain: plugin.move in its own chain,
//     plugin.remove then plugin.insert, in one group, into another;
//   - a right click on a link of a chain: an effect inserted there;
//   - a line drawn from a strip's way out to a bus: track.set_send, -12 dB;
//   - the end of an output dragged onto a bus or the master: track.set_output;
//   - the dot of an effect: plugin.set_bypassed; Del: plugin.remove;
//   - a right click in the empty: a new bus (bus.add).
class FluxPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit FluxPanel(const PanelContext& context);
    ~FluxPanel() override;

    FluxPanel(const FluxPanel&) = delete;
    FluxPanel& operator=(const FluxPanel&) = delete;
    FluxPanel(FluxPanel&&) = delete;
    FluxPanel& operator=(FluxPanel&&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // --- what the window shows, for the verification

    [[nodiscard]] const domain::flux::Graph& graph() const noexcept { return graph_; }
    [[nodiscard]] const std::vector<FluxHost::Place>& armed() const noexcept { return armed_; }
    [[nodiscard]] const std::string& selected() const noexcept { return selected_; }
    [[nodiscard]] float zoom() const noexcept { return zoom_; }

    // Where a node is drawn, in this component; empty when it is not. An
    // effect's dot, inside it.
    [[nodiscard]] juce::Rectangle<float> boundsOf(const std::string& node) const;
    [[nodiscard]] juce::Rectangle<float> dotOf(const std::string& effect) const
    {
        return dotOf(boundsOf(effect));
    }

    // The place a state node reads, if it is a state.
    [[nodiscard]] std::optional<FluxHost::Place> placeOf(const domain::flux::Node& node) const;

    // The samples a state node shows now (the last read), and its level.
    [[nodiscard]] std::vector<float> shownAt(const std::string& node) const;
    [[nodiscard]] double levelAt(const std::string& node) const;

    // The before and the after of the selected effect, as the detail draws
    // them: the two spectra, in dB per band.
    [[nodiscard]] const std::vector<double>& spectrumBefore() const noexcept { return spectrumBefore_; }
    [[nodiscard]] const std::vector<double>& spectrumAfter() const noexcept { return spectrumAfter_; }

    void select(const std::string& node);

    // The gestures, as the mouse ends them; each says whether a command ran.
    // A link is named by its two ends.
    bool insertOn(const std::string& from, const std::string& to, const domain::PluginRef& ref);
    bool dropEffect(const std::string& effect, const std::string& from, const std::string& to);
    bool sendTo(const std::string& wayOut, const std::string& sum);
    bool outputTo(const std::string& wayOut, const std::string& sum);
    bool toggleBypass(const std::string& effect);
    bool removeSelected();
    bool addBus();

    void frameAll();
    void zoomAround(juce::Point<float> at, float factor);
    void frame(); // one image: what is armed, read, repainted

private:
    struct Shown
    {
        std::string node;
        std::vector<float> samples;
        double levelDb{-100.0};
    };

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void rebuild();
    void armVisible();
    void disarm();
    [[nodiscard]] juce::Rectangle<int> graphArea() const;
    [[nodiscard]] juce::Rectangle<int> detailArea() const;
    [[nodiscard]] juce::Point<float> toScreen(juce::Point<float> graphPoint) const;
    [[nodiscard]] juce::Rectangle<float> rawBoundsOf(const domain::flux::Node& node) const;
    [[nodiscard]] const domain::flux::Node* nodeAt(juce::Point<float> at) const;
    [[nodiscard]] juce::Rectangle<float> contentBounds() const;
    [[nodiscard]] const Shown* shownFor(const std::string& node) const;
    [[nodiscard]] std::string neighbour(const std::string& effect, bool before) const;
    [[nodiscard]] juce::Path pathOf(const domain::flux::Link& link) const;
    [[nodiscard]] const domain::flux::Link* linkAt(juce::Point<float> at) const;
    [[nodiscard]] juce::Rectangle<float> dotOf(const juce::Rectangle<float>& bounds) const;
    [[nodiscard]] const domain::flux::Node* stripNode(const std::string& id) const;
    void showLinkMenu(const domain::flux::Link& link);
    void showEmptyMenu();

    [[nodiscard]] juce::Font scaled(juce::Font font) const;
    void paintLinks(juce::Graphics& g) const;
    void paintNode(juce::Graphics& g, const domain::flux::Node& node) const;
    void paintWave(juce::Graphics& g,
                   juce::Rectangle<float> area,
                   const std::vector<float>& samples,
                   juce::Colour colour) const;
    void paintDetail(juce::Graphics& g) const;
    void paintSpectrum(juce::Graphics& g,
                       juce::Rectangle<float> area,
                       const std::vector<double>& levels,
                       juce::Colour colour) const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    PluginHost& plugins_;
    FluxHost& flux_;
    bool titled_{false};

    domain::flux::Graph graph_;
    std::vector<FluxHost::Place> armed_;
    std::vector<Shown> shown_;
    std::string selected_;
    std::vector<double> spectrumBefore_;
    std::vector<double> spectrumAfter_;

    float zoom_{1.0f};
    juce::Point<float> origin_; // the graph point at the top left of the graph area
    bool framed_{false};
    bool dragging_{false};
    juce::Point<float> dragStart_;
    juce::Point<float> originAtDrag_;

    // A gesture of the left button: what was pressed, and where the mouse is.
    enum class Pulling
    {
        nothing,
        effect, // an effect node, to a link
        send,   // a strip's way out, to a bus
        output  // the end of an output, to a bus or the master
    };
    Pulling pulling_{Pulling::nothing};
    std::string pulled_; // the effect node, or the strip's way out
    bool pullMoved_{false};
    juce::Point<float> pullAt_;

    FrameTicker frames_{*this, [this] { frame(); }};
};

} // namespace daw::ui
