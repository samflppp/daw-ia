#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <string>

namespace daw::ui
{

// The channel rack: the instruments of the project. The pattern being edited
// is chosen in the transport since S13.
//
// It writes no note. Since S12 notes are written in the piano roll only, and
// the rack is where the sounds that play them come in and go out:
//   + Sample          a new channel playing a sample of the machine
//   + Instrument      a new channel on a VST or CLAP instrument, or on the
//                     synth Tracktion ships with
//   drop a sample     on a channel: that channel plays it from now on;
//                     below the channels: a new one
//   click             the channel the piano roll edits
//   double-click      rename it
//   right-click       rename it, or remove it
//   click a grey name accept the name the channel's preset, sample or notes
//                     suggest (S17), shown next to a name nobody chose
// Each is one command or one group: one Ctrl+Z.
//
// The step grid it held until S11 is gone, on purpose: two places to write
// the same notes was one too many, and the piano roll is the one that can
// write a melody.
class ChannelRackPanel final : public juce::Component,
                               public juce::DragAndDropTarget,
                               public juce::FileDragAndDropTarget,
                               private juce::ChangeListener
{
public:
    explicit ChannelRackPanel(const PanelContext& context);
    ~ChannelRackPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // A sample dropped on a channel makes it a sampler channel on that sample;
    // dropped below the channels, it makes a new one. From the browser or from
    // the system, the way FL takes both.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Where a channel's row is drawn, and the row under the channels where a
    // drop makes a new one. The verification aims with them.
    [[nodiscard]] juce::Rectangle<int> channelBounds(int row) const;

    // What a channel plays, as its row says it: the sample, the instrument,
    // or the built-in synth.
    [[nodiscard]] juce::String instrumentName(const domain::Track& track) const;

    // The menu "+ Instrument" opens, in its order: the built-in synth first,
    // then the instruments installed here.
    enum InstrumentMenu
    {
        builtInSynthItem = 1,
        rescanItem = 2,
        firstPluginItem = 100
    };

    enum ChannelMenu
    {
        renameItem = 1,
        removeItem = 2
    };

    // The name a channel's own clues suggest, when its name is one nobody
    // chose (« Piste 3 », the plugin's name): what the preset, the sample or
    // the notes say it plays. Empty when there is nothing to suggest.
    // Computed, never stored: the project keeps only the name accepted.
    [[nodiscard]] std::string suggestedName(const domain::Track& track) const;

    // Where that suggestion is drawn, for the row: a click there accepts it.
    [[nodiscard]] juce::Rectangle<int> suggestionBounds(int row) const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    [[nodiscard]] juce::Rectangle<int> channelArea() const;
    [[nodiscard]] int rowAtY(int y) const; // -1 outside any row

    void paintChannels(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintEmpty(juce::Graphics& g) const;

    void chooseSample();
    void showInstrumentMenu();
    void addInstrument(const std::optional<domain::PluginRef>& ref);
    void showChannelMenu(int row);
    void renameChannel(int row);
    void dropSample(const juce::File& file, int y);

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    SampleHost& samples_;
    PluginHost& plugins_;

    juce::TextButton addSample_{"+ Sample"};
    juce::TextButton addInstrument_{"+ Instrument"};
    std::unique_ptr<juce::FileChooser> chooser_;

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelRackPanel)
};

} // namespace daw::ui
