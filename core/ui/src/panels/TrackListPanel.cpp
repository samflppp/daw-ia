#include "daw/ui/panels/TrackListPanel.h"

#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/ui/AutomatableSlider.h"
#include "daw/ui/model/AutomationEditing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace daw::ui
{

// One track. It knows its own identifier and nothing about the rows around it.
class TrackListPanel::Row final : public juce::Component
{
public:
    Row(const Tokens& tokens,
        DawLookAndFeel& lookAndFeel,
        domain::CommandBus& bus,
        const domain::ProjectState& state,
        Selection& selection,
        const TransportClock& clock,
        domain::TrackId trackId,
        int position)
        : tokens_(tokens)
        , lookAndFeel_(lookAndFeel)
        , bus_(bus)
        , state_(state)
        , selection_(selection)
        , clock_(clock)
        , trackId_(trackId)
        , position_(position)
    {
        setLookAndFeel(&lookAndFeel_);

        addAndMakeVisible(volume_);
        volume_.setSliderStyle(juce::Slider::LinearHorizontal);
        volume_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volume_.setRange(domain::ProjectState::minVolumeDb, domain::ProjectState::maxVolumeDb);

        // A drag is one gesture, so it is one history entry. beginGesture is
        // what says "the sixty commands about to arrive are one movement";
        // without it a fader sweep would need sixty undos to come back.
        volume_.onDragStart = [this] { gesture_ = bus_.beginGesture("volume de piste"); };

        volume_.onValueChange = [this]
        {
            domain::ExecuteOptions options{};
            options.gesture = gesture_;
            static_cast<void>(bus_.execute(
                std::make_unique<domain::SetTrackVolume>(trackId_, volume_.getValue()), options));
        };

        volume_.onDragEnd = [this]
        {
            if (gesture_.has_value())
                static_cast<void>(bus_.endGesture(*gesture_));
            gesture_.reset();
        };

        // The right-click opens the line, as it does on the mixer.
        volume_.onAutomate = [this] { automate(domain::AutomationTarget::volumeOf(trackId_)); };
        pan_.onAutomate = [this] { automate(domain::AutomationTarget::panOf(trackId_)); };

        // The name is a label and no longer a painted string: double-clicking it
        // renames the track, which is the gesture every list in every DAW uses.
        addAndMakeVisible(name_);
        name_.setEditable(false, true, false);

        // The label keeps the double-click that starts an edit, and forwards
        // everything else to the row: clicking a track's name has always
        // selected that track, and a rename must not cost that.
        name_.addMouseListener(this, false);
        name_.onTextChange = [this]
        {
            const auto* track = state_.findTrack(trackId_);
            if (track == nullptr)
                return;

            const auto wanted = name_.getText().trim();

            // An empty name would leave a row nothing can be said about, and a
            // name that did not change is not an edit: neither reaches the bus.
            if (wanted.isEmpty() || wanted.toStdString() == track->name)
            {
                name_.setText(juce::String(track->name), juce::dontSendNotification);
                return;
            }

            static_cast<void>(
                bus_.execute(std::make_unique<domain::RenameTrack>(trackId_, wanted.toStdString())));
        };

        addAndMakeVisible(pan_);
        pan_.setSliderStyle(juce::Slider::LinearHorizontal);
        pan_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan_.setRange(domain::ProjectState::minPan, domain::ProjectState::maxPan);

        // The centre is the one position a user needs to reach exactly, and no
        // drag lands on it reliably. A double click does.
        pan_.setDoubleClickReturnValue(true, 0.0);

        // Its own gesture, and its own command: a pan sweep is one history
        // entry, and undoing it must not move the fader too.
        pan_.onDragStart = [this] { panGesture_ = bus_.beginGesture("panoramique de piste"); };

        pan_.onValueChange = [this]
        {
            domain::ExecuteOptions options{};
            options.gesture = panGesture_;
            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetTrackPan>(trackId_, pan_.getValue()), options));
        };

        pan_.onDragEnd = [this]
        {
            if (panGesture_.has_value())
                static_cast<void>(bus_.endGesture(*panGesture_));
            panGesture_.reset();
        };

        addAndMakeVisible(mute_);
        mute_.setButtonText("M");

        // Clicking the row must not put the focus ring on the mute chip: an
        // outlined M next to a filled M reads as a second kind of mute. Tab
        // still reaches it.
        mute_.setMouseClickGrabsKeyboardFocus(false);
        mute_.onClick = [this]
        {
            const auto* track = state_.findTrack(trackId_);
            if (track == nullptr)
                return;
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackMuted>(trackId_, !track->muted)));
        };

        addAndMakeVisible(bypass_);
        bypass_.setButtonText("B");
        bypass_.setMouseClickGrabsKeyboardFocus(false);
        bypass_.onClick = [this] { toggleChainBypass(); };

        // track.remove has existed since S5 and nothing called it. A removal
        // takes clips, notes and plugins with it, so it is one undo away and
        // not one confirmation dialog away: the history is the safety net.
        addAndMakeVisible(remove_);
        remove_.setButtonText("x");
        remove_.setMouseClickGrabsKeyboardFocus(false);
        remove_.onClick = [bus = &bus_, trackId = trackId_]
        {
            // Asynchronous on purpose: removing the track rebuilds the list,
            // which deletes this very row, and a callback must not return into
            // an object its own command has destroyed.
            juce::MessageManager::callAsync(
                [bus, trackId]
                { static_cast<void>(bus->execute(std::make_unique<domain::RemoveTrack>(trackId))); });
        };

        refresh();
    }

    ~Row() override { setLookAndFeel(nullptr); }

    [[nodiscard]] domain::TrackId trackId() const noexcept { return trackId_; }

    // The number painted on the left. It is a place in the list, so it changes
    // when the list is reordered and the row itself does not.
    void setPosition(int position)
    {
        if (position == position_)
            return;

        position_ = position;
        repaint();
    }

    // The volume and the pan as they should be shown now: the curve while the
    // song plays, the project otherwise, and the hand while it holds them.
    void follow()
    {
        const auto playing = clock_.isPlaying();
        const auto beats = clock_.positionBeats();
        volume_.follow(automationEditing::shownValue(
            state_, domain::AutomationTarget::volumeOf(trackId_), playing, beats));
        pan_.follow(
            automationEditing::shownValue(state_, domain::AutomationTarget::panOf(trackId_), playing, beats));
    }

    // Reads the project again. Called after every change the bus reports, so a
    // volume set by a copilot moves this fader exactly like a drag would.
    void refresh()
    {
        const auto* track = state_.findTrack(trackId_);
        if (track == nullptr)
            return;

        // Not while it is being typed into: writing the state back under the
        // cursor would fight the user's own keystrokes.
        if (!name_.isBeingEdited())
            name_.setText(juce::String(track->name), juce::dontSendNotification);

        name_.setColour(juce::Label::textColourId,
                        track->muted ? tokens_.colour("color.text.disabled")
                                     : tokens_.colour("color.text.primary"));

        // Setting the slider from the state must not send a command back, or a
        // projection would fight the user's own movement.
        follow();
        mute_.setToggleState(track->muted, juce::dontSendNotification);
        bypass_.setToggleState(chainIsBypassed(*track), juce::dontSendNotification);
        bypass_.setEnabled(!track->plugins.empty());

        selected_ = selection_.track() == trackId_;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto* track = state_.findTrack(trackId_);
        if (track == nullptr)
            return;

        if (selected_)
        {
            g.fillAll(tokens_.colour("color.state.selected"));
            g.setColour(tokens_.colour("color.accent.primary"));
            g.fillRect(0, 0, tokens_.integer("stroke.focus"), getHeight());
        }
        else if (isMouseOver(true))
        {
            g.fillAll(tokens_.colour("color.state.hover"));
        }

        auto area = getLocalBounds().reduced(tokens_.integer("space.md"), 0);
        auto bottom = getLocalBounds().removeFromBottom(tokens_.integer("stroke.hairline"));
        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(bottom);

        auto index = area.removeFromLeft(tokens_.integer("space.lg"));
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
        g.drawText(juce::String(position_ + 1), index, juce::Justification::centredLeft, false);

        // The switches and the reading sit on the right; the name takes what is
        // left, and the fader lives under it.
        area.removeFromRight(tokens_.integer("metric.track.chipWidth") * 3 + tokens_.integer("space.sm") * 3);

        auto decibels = area.removeFromRight(tokens_.integer("metric.transport.buttonSize") * 2);
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
        g.drawText(juce::String(track->volumeDb, 1) + " dB",
                   decibels.withTrimmedBottom(tokens_.integer("space.md")),
                   juce::Justification::centredRight,
                   false);

        // Read in the vocabulary of a mixing desk, not as a number between
        // -1 and 1: "G31" is a position, "-0.31" is an implementation detail.
        g.drawText(panText(track->pan),
                   decibels.removeFromBottom(tokens_.integer("space.lg"))
                       .withTrimmedBottom(tokens_.integer("space.xs")),
                   juce::Justification::centredRight,
                   false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(tokens_.integer("space.md"), 0);
        area.removeFromLeft(tokens_.integer("space.lg"));

        auto switches = area.removeFromRight(tokens_.integer("metric.track.chipWidth") * 3 +
                                             tokens_.integer("space.sm") * 3);
        switches =
            switches.withSizeKeepingCentre(switches.getWidth(), tokens_.integer("metric.track.chipHeight"));
        switches.removeFromLeft(tokens_.integer("space.sm"));
        mute_.setBounds(switches.removeFromLeft(tokens_.integer("metric.track.chipWidth")));
        switches.removeFromLeft(tokens_.integer("space.sm"));
        bypass_.setBounds(switches.removeFromLeft(tokens_.integer("metric.track.chipWidth")));
        switches.removeFromLeft(tokens_.integer("space.sm"));
        remove_.setBounds(switches.removeFromLeft(tokens_.integer("metric.track.chipWidth")));

        area.removeFromRight(tokens_.integer("metric.transport.buttonSize") * 2);

        name_.setBounds(area.withTrimmedBottom(tokens_.integer("space.md")));

        auto fader = area.removeFromBottom(tokens_.integer("space.lg"));
        fader = fader.withTrimmedBottom(tokens_.integer("space.xs"));

        // The pan takes the right third of the strip the fader used to fill
        // whole: two continuous controls on one line, the loudest one widest.
        auto panArea = fader.removeFromRight(fader.getWidth() / 3);
        fader.removeFromRight(tokens_.integer("space.sm"));

        volume_.setBounds(fader);
        pan_.setBounds(panArea);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
        selection_.selectTrack(trackId_);

        // Where the drag starts from, read once: the list moves while the row
        // is held, so the place it left is the only fixed reference.
        if (const auto index = state_.trackIndex(trackId_); index)
            reorderFrom_ = index.value();
    }

    // Dragging a row up or down changes the order of the tracks.
    //
    // The command goes out as soon as the row crosses a place, not at the end:
    // the list is rebuilt from the project, so the row the user is holding
    // moves under the cursor and the drag shows what it does. The whole drag is
    // one gesture, therefore one history entry.
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (getHeight() <= 0 || !reorderFrom_.has_value())
            return;

        if (!reorder_.has_value())
            reorder_ = bus_.beginGesture("ordre des pistes");

        // How far the hand has travelled, in rows. The row itself is not asked
        // where it sits: it started at a known place and the distance says the
        // rest, which is also the only reading that survives the list moving
        // under the cursor.
        const auto rows = std::llround(static_cast<double>(event.getDistanceFromDragStartY()) /
                                       static_cast<double>(getHeight()));

        const auto last = static_cast<int>(state_.tracks().size()) - 1;
        const auto wanted =
            std::clamp(static_cast<int>(*reorderFrom_) + static_cast<int>(rows), 0, std::max(0, last));

        const auto current = state_.trackIndex(trackId_);
        if (!current || wanted == static_cast<int>(current.value()))
            return;

        domain::ExecuteOptions options{};
        options.gesture = reorder_;
        static_cast<void>(bus_.execute(
            std::make_unique<domain::ReorderTrack>(trackId_, static_cast<std::size_t>(wanted)), options));
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);

        reorderFrom_.reset();

        if (!reorder_.has_value())
            return;

        static_cast<void>(bus_.endGesture(*reorder_));
        reorder_.reset();
    }

    void mouseEnter(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
        repaint();
    }

    void mouseExit(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
        repaint();
    }

private:
    // The right-click on a slider: its line, shown in the playlist.
    void automate(const domain::AutomationTarget& target)
    {
        const auto line = automationEditing::open(bus_, state_, target);
        if (!line.isNil())
            selection_.showAutomation(line);
    }

    // G for gauche, D for droite, C for centre: the panel is in French, and a
    // mixing desk says which side, not which sign.
    [[nodiscard]] static juce::String panText(double pan)
    {
        const auto percent = juce::roundToInt(std::abs(pan) * 100.0);
        if (percent == 0)
            return "C";

        return (pan < 0.0 ? "G" : "D") + juce::String(percent);
    }

    [[nodiscard]] static bool chainIsBypassed(const domain::Track& track)
    {
        if (track.plugins.empty())
            return false;

        return std::all_of(track.plugins.begin(),
                           track.plugins.end(),
                           [](const domain::PluginInstance& plugin) { return plugin.bypassed; });
    }

    // Bypassing "the track's chain" is bypassing each plugin in it: the domain
    // has no such thing as a chain switch, and inventing one would put a second
    // truth next to the instances. The commands are grouped in one gesture so
    // that one click is one undo.
    void toggleChainBypass()
    {
        const auto* track = state_.findTrack(trackId_);
        if (track == nullptr || track->plugins.empty())
            return;

        const auto wanted = !chainIsBypassed(*track);

        const auto gesture = bus_.beginGesture("bypass de la chaine");
        domain::ExecuteOptions options{};
        options.gesture = gesture;

        for (const auto& plugin : track->plugins)
        {
            if (plugin.bypassed == wanted)
                continue;

            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetPluginBypassed>(plugin.id, wanted), options));
        }

        static_cast<void>(bus_.endGesture(gesture));
    }

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    Selection& selection_;
    const TransportClock& clock_;
    domain::TrackId trackId_;
    int position_;

    juce::Label name_;
    bool selected_{false};

    AutomatableSlider volume_;
    AutomatableSlider pan_;
    juce::ToggleButton mute_;
    juce::ToggleButton bypass_;
    juce::TextButton remove_;
    std::optional<domain::GestureId> gesture_;
    std::optional<domain::GestureId> panGesture_;
    std::optional<domain::GestureId> reorder_;
    std::optional<std::size_t> reorderFrom_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Row)
};

TrackListPanel::TrackListPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);

    rowHolder_ = std::make_unique<juce::Component>();
    viewport_.setViewedComponent(rowHolder_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    addAndMakeVisible(add_);
    add_.onClick = [this] { addTrack(); };

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
    rebuild();

    // Thirty times a second, like the mixer: a fade reads as a movement.
    startTimerHz(30);
}

TrackListPanel::~TrackListPanel()
{
    stopTimer();
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void TrackListPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);

    // A row per track and in the same order: anything else means the list was
    // added to or taken from, and the rows are built again. Otherwise each row
    // reads the project again, which is cheaper than building components.
    const auto& tracks = state_.tracks();

    // The same tracks in another order is a reorder, not a new list: the rows
    // are moved rather than built again. Building them again would delete the
    // row the user is dragging, in the middle of their drag, and would throw
    // away a name being typed into.
    if (tracks.size() == rows_.size())
    {
        std::vector<Row*> ordered;
        ordered.reserve(rows_.size());

        for (const auto& track : tracks)
        {
            const auto found = std::find_if(
                rows_.begin(), rows_.end(), [&track](const Row* row) { return row->trackId() == track.id; });
            if (found == rows_.end())
                break;

            ordered.push_back(*found);
        }

        if (ordered.size() == rows_.size())
        {
            const bool moved = ordered != rows_;
            rows_ = std::move(ordered);

            for (int position = 0; position < static_cast<int>(rows_.size()); ++position)
                rows_[static_cast<std::size_t>(position)]->setPosition(position);

            for (auto* row : rows_)
                row->refresh();

            if (moved)
                resized();

            return;
        }
    }

    rebuild();
}

void TrackListPanel::timerCallback()
{
    for (auto* row : rows_)
        row->follow();
}

void TrackListPanel::rebuild()
{
    rows_.clear();
    rowHolder_->removeAllChildren();

    int position = 0;
    for (const auto& track : state_.tracks())
    {
        auto row = std::make_unique<Row>(
            tokens_, lookAndFeel_, bus_, state_, selection_, clock_, track.id, position++);

        rows_.push_back(row.get());
        rowHolder_->addAndMakeVisible(row.release());
    }

    resized();
    repaint();
}

void TrackListPanel::addTrack()
{
    const auto trackId = domain::TrackId::generate();
    const auto name = "Piste " + std::to_string(state_.tracks().size() + 1);

    // The identifier is generated here and travels in the payload: a replay
    // has to rebuild the same track, not a new one.
    if (bus_.execute(std::make_unique<domain::AddTrack>(trackId, name)).ok())
        selection_.selectTrack(trackId);
}

void TrackListPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    auto count = header.removeFromRight(tokens_.integer("space.xl"));

    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    if (!titled_)
    {
        g.drawText("PISTES", header, juce::Justification::centredLeft, false);
    }

    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
    g.drawText(juce::String(static_cast<int>(rows_.size())), count, juce::Justification::centredLeft, false);
}

void TrackListPanel::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));

    add_.setBounds(area.removeFromBottom(tokens_.integer("metric.plugin.slotHeight")));
    viewport_.setBounds(area);

    const auto rowHeight = tokens_.integer("metric.track.rowHeight");
    rowHolder_->setSize(viewport_.getMaximumVisibleWidth(), rowHeight * static_cast<int>(rows_.size()));

    int y = 0;
    for (auto* row : rows_)
    {
        row->setBounds(0, y, rowHolder_->getWidth(), rowHeight);
        y += rowHeight;
    }
}

} // namespace daw::ui
