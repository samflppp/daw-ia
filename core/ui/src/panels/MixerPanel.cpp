#include "daw/ui/panels/MixerPanel.h"

#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/copilot/MixingReadiness.h"
#include "daw/ui/AutomatableSlider.h"
#include "daw/ui/model/AutomationEditing.h"
#include "daw/ui/model/Motion.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace daw::ui
{
namespace
{

// The meter's scale: sixty decibels under full scale, six over. A reading
// below is drawn empty.
constexpr float meterFloorDb = -60.0f;
constexpr float meterCeilingDb = 6.0f;

// A send made from the mixer starts here: audible, well under the dry signal.
constexpr double firstSendDb = -12.0;

enum ComboIds
{
    noneId = 1,
    firstBusId = 2
};

[[nodiscard]] float meterFraction(float db)
{
    return std::clamp((db - meterFloorDb) / (meterCeilingDb - meterFloorDb), 0.0f, 1.0f);
}

[[nodiscard]] juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

} // namespace

// --- the meter ------------------------------------------------------------------

class MixerPanel::Meter final : public juce::Component
{
public:
    explicit Meter(const Tokens& tokens)
        : tokens_(tokens)
    {
    }

    // A reading. On the light pace, shown as it is. On the fluid one, a level
    // above the one shown takes it up at once, a level under it lets it fall
    // at its rate, image by image (advance).
    void show(const StripMeter& meter)
    {
        read_ = meter;
        if (!FrameTicker::animates())
        {
            meter_ = meter;
            repaint();
            return;
        }
        advance(FrameTicker::nowMs());
    }

    // One image of the fall; repaints only when what is drawn moved.
    void advance(double nowMs)
    {
        if (!FrameTicker::animates())
            return;

        const auto rate = tokens_.number("motion.meter.fallDbPerSecond");
        const auto hold = static_cast<double>(tokens_.integer("motion.duration.peakHold"));
        auto shown = read_;
        shown.rmsLeftDb = rmsLeft_.advance(read_.rmsLeftDb, nowMs, rate, 0.0, meterFloorDb);
        shown.rmsRightDb = rmsRight_.advance(read_.rmsRightDb, nowMs, rate, 0.0, meterFloorDb);
        shown.peakLeftDb = peakLeft_.advance(read_.peakLeftDb, nowMs, rate, hold, meterFloorDb);
        shown.peakRightDb = peakRight_.advance(read_.peakRightDb, nowMs, rate, hold, meterFloorDb);

        const auto moved = [](float a, float b) { return std::abs(a - b) > 0.05f; };
        if (moved(shown.rmsLeftDb, meter_.rmsLeftDb) || moved(shown.rmsRightDb, meter_.rmsRightDb) ||
            moved(shown.peakLeftDb, meter_.peakLeftDb) || moved(shown.peakRightDb, meter_.peakRightDb) ||
            shown.over != meter_.over)
        {
            meter_ = shown;
            repaint();
        }
    }

    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        const auto over = area.removeFromTop(tokens_.integer("metric.mixer.overHeight"));
        g.setColour(tokens_.colour(meter_.over ? "color.meter.peak" : "color.meter.track"));
        g.fillRect(over.withTrimmedTop(tokens_.integer("stroke.hairline"))
                       .withTrimmedBottom(tokens_.integer("stroke.hairline")));

        const auto gap = tokens_.integer("stroke.hairline");
        const auto half = (area.getWidth() - gap) / 2;
        paintBar(g, area.removeFromLeft(half), meter_.rmsLeftDb, meter_.peakLeftDb);
        area.removeFromLeft(gap);
        paintBar(g, area, meter_.rmsRightDb, meter_.peakRightDb);
    }

private:
    void paintBar(juce::Graphics& g, juce::Rectangle<int> bar, float rmsDb, float peakDb) const
    {
        g.setColour(tokens_.colour("color.meter.track"));
        g.fillRect(bar);

        const auto height = static_cast<float>(bar.getHeight());
        const auto rms = static_cast<int>(height * meterFraction(rmsDb));
        g.setColour(tokens_.colour("color.meter.level"));
        g.fillRect(bar.withTop(bar.getBottom() - rms));

        if (peakDb > meterFloorDb)
        {
            const auto y = bar.getBottom() - static_cast<int>(height * meterFraction(peakDb));
            g.setColour(tokens_.colour(peakDb >= 0.0f ? "color.meter.peak" : "color.meter.levelActive"));
            g.fillRect(bar.getX(), y, bar.getWidth(), tokens_.integer("stroke.playhead"));
        }
    }

    const Tokens& tokens_;
    StripMeter meter_{}; // as drawn
    StripMeter read_{};  // as last read
    FallingLevel rmsLeft_;
    FallingLevel rmsRight_;
    FallingLevel peakLeft_;
    FallingLevel peakRight_;
};

// --- a strip -------------------------------------------------------------------

class MixerPanel::Strip final : public juce::Component
{
public:
    enum class Kind
    {
        channel,
        bus,
        master
    };

    Strip(MixerPanel& owner, domain::TrackId id, Kind kind)
        : owner_(owner)
        , tokens_(owner.tokens_)
        , bus_(owner.bus_)
        , state_(owner.state_)
        , id_(id)
        , kind_(kind)
        , meter_(owner.tokens_)
    {
        setLookAndFeel(&owner.lookAndFeel_);
        setOpaque(true); // paint() fills the whole rectangle: what is behind is never painted

        addAndMakeVisible(name_);
        name_.setJustificationType(juce::Justification::centred);
        name_.setInterceptsMouseClicks(false, false);

        addAndMakeVisible(fader_);
        fader_.setSliderStyle(juce::Slider::LinearVertical);
        fader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader_.setRange(domain::ProjectState::minVolumeDb, domain::ProjectState::maxVolumeDb);
        fader_.setSkewFactorFromMidPoint(-12.0);
        fader_.setDoubleClickReturnValue(true, 0.0);
        fader_.onDragStart = [this] { gesture_ = bus_.beginGesture("fader"); };
        fader_.onValueChange = [this]
        { execute(std::make_unique<domain::SetTrackVolume>(id_, fader_.getValue())); };
        fader_.onDragEnd = [this] { endGesture(); };
        fader_.onAutomate = [this] { automate(domain::AutomationTarget::volumeOf(id_)); };

        addAndMakeVisible(pan_);
        pan_.setSliderStyle(juce::Slider::LinearHorizontal);
        pan_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan_.setRange(domain::ProjectState::minPan, domain::ProjectState::maxPan);
        pan_.setDoubleClickReturnValue(true, 0.0);
        pan_.onDragStart = [this] { gesture_ = bus_.beginGesture("panoramique"); };
        pan_.onValueChange = [this] { execute(std::make_unique<domain::SetTrackPan>(id_, pan_.getValue())); };
        pan_.onDragEnd = [this] { endGesture(); };
        pan_.onAutomate = [this] { automate(domain::AutomationTarget::panOf(id_)); };

        addAndMakeVisible(mute_);
        mute_.setButtonText("M");
        mute_.setMouseClickGrabsKeyboardFocus(false);
        mute_.onClick = [this]
        {
            if (const auto* strip = state_.findStrip(id_); strip != nullptr)
                static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackMuted>(id_, !strip->muted)));
        };

        if (kind_ != Kind::master)
        {
            addAndMakeVisible(solo_);
            solo_.setButtonText("S");
            solo_.setMouseClickGrabsKeyboardFocus(false);
            solo_.onClick = [this]
            {
                if (const auto* strip = state_.findStrip(id_); strip != nullptr)
                    static_cast<void>(
                        bus_.execute(std::make_unique<domain::SetTrackSolo>(id_, !strip->soloed)));
            };

            addAndMakeVisible(output_);
            output_.setTooltip(juce::String::fromUTF8("Sortie"));
            output_.onChange = [this] { chooseOutput(); };

            addAndMakeVisible(send_);
            send_.setTooltip(juce::String::fromUTF8("Envoi"));
            send_.onChange = [this] { chooseSend(); };

            addAndMakeVisible(sendLevel_);
            sendLevel_.setSliderStyle(juce::Slider::LinearHorizontal);
            sendLevel_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            sendLevel_.setRange(domain::ProjectState::minVolumeDb, domain::ProjectState::maxVolumeDb);
            sendLevel_.setSkewFactorFromMidPoint(-12.0);
            sendLevel_.onDragStart = [this] { gesture_ = bus_.beginGesture("envoi"); };
            sendLevel_.onValueChange = [this]
            {
                if (const auto target = sendBus(); target.has_value())
                    execute(std::make_unique<domain::SetTrackSend>(id_, *target, sendLevel_.getValue()));
            };
            sendLevel_.onDragEnd = [this] { endGesture(); };
        }

        addAndMakeVisible(meter_);
        meter_.onClick = [&owner] { owner.levels_.clearOvers(); };

        refresh();
    }

    ~Strip() override { setLookAndFeel(nullptr); }

    [[nodiscard]] domain::TrackId id() const noexcept { return id_; }

    void showMeter(const StripMeter& meter) { meter_.show(meter); }
    void advanceMeter(double nowMs) { meter_.advance(nowMs); }

    void mouseDown(const juce::MouseEvent& event) override
    {
        // The name selects a channel: the piano roll and the plugin page
        // follow the selected track.
        if (kind_ == Kind::channel && name_.getBounds().contains(event.getPosition()))
            owner_.selection_.selectTrack(id_);
    }

    // `force` after a refused choice: the combo shows what the hand chose, and
    // the project, which did not change, is what it must show again.
    void refresh(bool force = false)
    {
        const auto* strip = state_.findStrip(id_);
        if (strip == nullptr)
            return;

        // Every command reaches here, a note moved in the piano roll as well:
        // a strip whose name, state, inserts and routing are what it already
        // shows follows its fader and pan and stops there. Refilling its
        // combos and repainting it was a full mixer per step of a drag
        // (S18 bis).
        const auto shown = shownKey(*strip);
        if (!force && shown == shownKey_)
        {
            follow();
            return;
        }
        shownKey_ = shown;

        name_.setText(text(strip->name), juce::dontSendNotification);
        name_.setColour(juce::Label::textColourId,
                        tokens_.colour(state_.isAudible(id_) ? "color.text.primary" : "color.text.disabled"));

        follow();
        mute_.setToggleState(strip->muted, juce::dontSendNotification);

        inserts_.clear();
        for (const auto& plugin : strip->plugins)
            inserts_.add(text(plugin.ref.name));

        if (kind_ == Kind::master)
        {
            repaint();
            return;
        }

        solo_.setToggleState(strip->soloed, juce::dontSendNotification);

        // The buses, by rank: the combo ids are ranks, rebuilt every time,
        // because a bus removed or added moves every rank after it.
        output_.clear(juce::dontSendNotification);
        send_.clear(juce::dontSendNotification);
        output_.addItem("Master", noneId);
        send_.addItem(juce::String::fromUTF8("— aucun envoi"), noneId);
        for (std::size_t rank = 0; rank < state_.buses().size(); ++rank)
        {
            const auto& bus = state_.buses()[rank];
            if (bus.id == id_)
                continue;
            output_.addItem(text(bus.name), firstBusId + static_cast<int>(rank));
            send_.addItem(text(bus.name), firstBusId + static_cast<int>(rank));
        }

        const auto outputRank = strip->output.isNil() ? std::optional<std::size_t>{} : rankOf(strip->output);
        output_.setSelectedId(outputRank.has_value() ? firstBusId + static_cast<int>(*outputRank) : noneId,
                              juce::dontSendNotification);

        // One send is shown: the first. The domain holds as many as there are
        // buses; the copilot and a later screen reach the others.
        if (!strip->sends.empty())
        {
            const auto rank = rankOf(strip->sends.front().bus);
            send_.setSelectedId(rank.has_value() ? firstBusId + static_cast<int>(*rank) : noneId,
                                juce::dontSendNotification);
            sendLevel_.setValue(strip->sends.front().levelDb, juce::dontSendNotification);
            sendLevel_.setEnabled(true);
        }
        else
        {
            send_.setSelectedId(noneId, juce::dontSendNotification);
            sendLevel_.setValue(firstSendDb, juce::dontSendNotification);
            sendLevel_.setEnabled(false);
        }

        repaint();
    }

    // The fader and the pan as they should be shown now: the curve while the
    // song plays, the project otherwise, and the hand while it holds them.
    void follow()
    {
        const auto playing = owner_.clock_.isPlaying();
        const auto beats = owner_.clock_.positionBeats();
        const auto shown = [&](const domain::AutomationTarget& target)
        { return automationEditing::shownValue(state_, target, playing, beats); };
        const auto faderMoved = fader_.follow(shown(domain::AutomationTarget::volumeOf(id_)));
        pan_.follow(shown(domain::AutomationTarget::panOf(id_)));
        if (faderMoved)
            repaint(readoutArea_);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(tokens_.colour(kind_ == Kind::channel ? "color.surface.panel" : "color.surface.raised"));
        g.fillRect(getLocalBounds());

        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(owner_.lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
        auto area = insertsArea_;
        const auto line = tokens_.integer("metric.mixer.insertHeight");
        const auto lines = tokens_.integer("metric.mixer.insertLines");
        for (int index = 0; index < std::min(inserts_.size(), lines); ++index)
            g.drawText(inserts_[index], area.removeFromTop(line), juce::Justification::centredLeft, true);
        if (inserts_.isEmpty())
            g.drawText(juce::String::fromUTF8("aucun effet"),
                       area.removeFromTop(line),
                       juce::Justification::centredLeft,
                       true);

        g.setColour(tokens_.colour("color.text.secondary"));
        g.setFont(owner_.lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
        g.drawText(
            juce::String(fader_.getValue(), 1) + " dB", readoutArea_, juce::Justification::centred, false);

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(getWidth() - tokens_.integer("stroke.hairline"),
                   0,
                   tokens_.integer("stroke.hairline"),
                   getHeight());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(tokens_.integer("space.xs"));
        name_.setBounds(area.removeFromTop(tokens_.integer("metric.mixer.nameHeight")));
        insertsArea_ = area.removeFromTop(tokens_.integer("metric.mixer.insertHeight") *
                                          tokens_.integer("metric.mixer.insertLines"));

        const auto row = tokens_.integer("metric.mixer.rowHeight");
        if (kind_ != Kind::master)
        {
            send_.setBounds(area.removeFromTop(row));
            sendLevel_.setBounds(area.removeFromTop(row));
            output_.setBounds(area.removeFromTop(row));
        }
        area.removeFromTop(tokens_.integer("space.xs"));
        pan_.setBounds(area.removeFromTop(row));

        auto buttons = area.removeFromTop(row);
        if (kind_ != Kind::master)
        {
            mute_.setBounds(
                buttons.removeFromLeft(buttons.getWidth() / 2).reduced(tokens_.integer("stroke.hairline")));
            solo_.setBounds(buttons.reduced(tokens_.integer("stroke.hairline")));
        }
        else
        {
            mute_.setBounds(buttons.reduced(tokens_.integer("stroke.hairline")));
        }

        readoutArea_ = area.removeFromBottom(tokens_.integer("metric.mixer.readoutHeight"));
        area.removeFromTop(tokens_.integer("space.xs"));
        meter_.setBounds(area.removeFromRight(tokens_.integer("metric.mixer.meterWidth")));
        area.removeFromRight(tokens_.integer("space.xs"));
        fader_.setBounds(area);
    }

private:
    [[nodiscard]] std::optional<std::size_t> rankOf(domain::TrackId bus) const
    {
        const auto rank = state_.busIndex(bus);
        return rank ? std::optional<std::size_t>{rank.value()} : std::nullopt;
    }

    [[nodiscard]] std::optional<domain::TrackId> busAt(int comboId) const
    {
        const auto rank = comboId - firstBusId;
        if (rank < 0 || rank >= static_cast<int>(state_.buses().size()))
            return std::nullopt;
        return state_.buses()[static_cast<std::size_t>(rank)].id;
    }

    [[nodiscard]] std::optional<domain::TrackId> sendBus() const
    {
        const auto* strip = state_.findStrip(id_);
        if (strip == nullptr || strip->sends.empty())
            return std::nullopt;
        return strip->sends.front().bus;
    }

    void chooseOutput()
    {
        const auto chosen = busAt(output_.getSelectedId());
        if (!bus_.execute(std::make_unique<domain::SetTrackOutput>(id_, chosen.value_or(domain::TrackId{})))
                 .ok())
            refresh(true); // a loop refused: the combo goes back to what is true
    }

    void chooseSend()
    {
        const auto chosen = busAt(send_.getSelectedId());
        const auto current = sendBus();
        if (chosen == current)
            return;

        // Moving the send from one bus to another is one thing the user did:
        // one group, one Ctrl+Z.
        std::vector<std::unique_ptr<domain::Command>> commands;
        if (current.has_value())
            commands.push_back(std::make_unique<domain::RemoveTrackSend>(id_, *current));
        if (chosen.has_value())
            commands.push_back(std::make_unique<domain::SetTrackSend>(id_, *chosen, firstSendDb));

        domain::GroupOptions group{};
        group.label = chosen.has_value() ? "envoi" : "retirer l'envoi";
        if (!bus_.executeGroup(std::move(commands), group).ok())
            refresh(true);
    }

    void execute(std::unique_ptr<domain::Command> command)
    {
        domain::ExecuteOptions options{};
        options.gesture = gesture_;
        static_cast<void>(bus_.execute(std::move(command), options));
    }

    void endGesture()
    {
        if (gesture_.has_value())
            static_cast<void>(bus_.endGesture(*gesture_));
        gesture_.reset();
    }

    // The right-click on the fader or the pan: its line, created when it has
    // none, shown in the playlist.
    void automate(const domain::AutomationTarget& target)
    {
        const auto line = automationEditing::open(bus_, state_, target);
        if (!line.isNil())
            owner_.selection_.showAutomation(line);
    }

    // Everything refresh() shows but the fader and the pan, in one string.
    [[nodiscard]] juce::String shownKey(const domain::Track& strip) const
    {
        juce::String key = text(strip.name);
        key << '|' << (state_.isAudible(id_) ? 1 : 0) << (strip.muted ? 1 : 0) << (strip.soloed ? 1 : 0);
        for (const auto& plugin : strip.plugins)
            key << '|' << text(plugin.ref.name);
        key << '|' << juce::String{strip.output.toString()};
        for (const auto& bus : state_.buses())
            key << '|' << juce::String{bus.id.toString()} << ':' << text(bus.name);
        for (const auto& send : strip.sends)
            key << '|' << juce::String{send.bus.toString()} << ':' << send.levelDb;
        return key;
    }

    MixerPanel& owner_;
    const Tokens& tokens_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    domain::TrackId id_;
    Kind kind_;
    juce::String shownKey_;

    juce::Label name_;
    AutomatableSlider fader_;
    AutomatableSlider pan_;
    juce::TextButton mute_;
    juce::TextButton solo_;
    juce::ComboBox output_;
    juce::ComboBox send_;
    AutomatableSlider sendLevel_;
    Meter meter_;
    juce::StringArray inserts_;
    juce::Rectangle<int> insertsArea_;
    juce::Rectangle<int> readoutArea_;
    std::optional<domain::GestureId> gesture_;
};

// --- the scrolled row of strips --------------------------------------------------

class MixerPanel::Content final : public juce::Component
{
public:
    explicit Content(const Tokens& tokens)
        : tokens_(tokens)
    {
    }

    void place(const std::vector<std::unique_ptr<Strip>>& strips, int height)
    {
        const auto width = tokens_.integer("metric.mixer.stripWidth");
        setSize(std::max(1, width * static_cast<int>(strips.size())), height);
        for (std::size_t index = 0; index < strips.size(); ++index)
            strips[index]->setBounds(static_cast<int>(index) * width, 0, width, height);
    }

private:
    const Tokens& tokens_;
};

// --- the panel ------------------------------------------------------------------

MixerPanel::MixerPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
    , copilot_(context.copilot)
    , levels_(context.levels)
    , titled_(context.titled)
    , content_(std::make_unique<Content>(context.tokens))
{
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true); // paint() fills the whole rectangle: what is behind is never painted

    addAndMakeVisible(addBus_);
    addBus_.setButtonText("+ Bus");
    addBus_.onClick = [this]
    {
        // The caller names what it creates, here as everywhere.
        const auto name = "Bus " + std::to_string(state_.buses().size() + 1);
        static_cast<void>(bus_.execute(std::make_unique<domain::AddBus>(domain::TrackId::generate(), name)));
    };

    addAndMakeVisible(readiness_);
    readiness_.setButtonText(juce::String::fromUTF8("Mixer par l'IA"));
    // The report takes the place of the strips while it is shown; the same
    // button puts the strips back.
    readiness_.onClick = [this]
    {
        if (report_.isVisible())
        {
            report_.setVisible(false);
            resized();
            return;
        }
        runReadiness();
    };

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(false, true);
    viewport_.setScrollBarThickness(tokens_.integer("metric.scrollbar.thickness"));

    addChildComponent(report_);
    report_.setMultiLine(true);
    report_.setReadOnly(true);
    report_.setScrollbarsShown(true);

    project_.addChangeListener(this);
    levels_.addChangeListener(this);
    rebuild();
}

MixerPanel::~MixerPanel()
{
    levels_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

std::vector<juce::Component*> MixerPanel::strips() const
{
    std::vector<juce::Component*> all;
    for (const auto& strip : strips_)
        all.push_back(strip.get());
    if (master_ != nullptr)
        all.push_back(master_.get());
    return all;
}

void MixerPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &levels_)
    {
        const auto meters = levels_.meters();
        const auto show = [&meters](Strip& strip)
        {
            const auto wanted = strip.id() == domain::ProjectState::masterTrackId()
                                    ? std::string{LevelSource::masterStrip}
                                    : strip.id().toString();
            const auto found =
                std::find_if(meters.begin(),
                             meters.end(),
                             [&wanted](const StripMeter& meter) { return meter.strip == wanted; });
            strip.showMeter(found != meters.end() ? *found : StripMeter{wanted});
        };
        for (auto& strip : strips_)
            show(*strip);
        if (master_ != nullptr)
            show(*master_);
        return;
    }

    // The same strips in the same order are refreshed in place; any other
    // change rebuilds the row.
    std::vector<domain::TrackId> ids;
    for (const auto& track : state_.tracks())
        ids.push_back(track.id);
    for (const auto& bus : state_.buses())
        ids.push_back(bus.id);

    if (ids == shownIds_)
        refresh();
    else
        rebuild();
}

void MixerPanel::frame()
{
    // A fader following its line moves once per image, so a fade reads as a
    // movement and not a series of jumps. Stopped with no line to follow,
    // every strip already shows the project: follow() finds nothing to move
    // and repaints nothing.
    //
    // And the meters fall, on the fluid pace, between two readings.
    const auto now = FrameTicker::nowMs();
    for (auto& strip : strips_)
    {
        strip->follow();
        strip->advanceMeter(now);
    }
    if (master_ != nullptr)
    {
        master_->follow();
        master_->advanceMeter(now);
    }
}

void MixerPanel::rebuild()
{
    strips_.clear();
    shownIds_.clear();

    for (const auto& track : state_.tracks())
    {
        strips_.push_back(std::make_unique<Strip>(*this, track.id, Strip::Kind::channel));
        shownIds_.push_back(track.id);
    }
    for (const auto& bus : state_.buses())
    {
        strips_.push_back(std::make_unique<Strip>(*this, bus.id, Strip::Kind::bus));
        shownIds_.push_back(bus.id);
    }
    for (auto& strip : strips_)
        content_->addAndMakeVisible(*strip);

    if (master_ == nullptr)
    {
        master_ = std::make_unique<Strip>(*this, domain::ProjectState::masterTrackId(), Strip::Kind::master);
        addAndMakeVisible(*master_);
    }

    resized();
}

void MixerPanel::refresh()
{
    for (auto& strip : strips_)
        strip->refresh();
    if (master_ != nullptr)
        master_->refresh();
}

void MixerPanel::runReadiness()
{
    const auto offered = copilot_.capabilities();
    const auto gaps = domain::copilot::mixingGaps(offered);
    const auto total = domain::copilot::mixingNeeds().size();

    std::string report =
        "Aucun modèle appelé : ceci vérifie ce que le copilote peut atteindre pour mixer.\n" +
        std::to_string(total - gaps.size()) + " besoins sur " + std::to_string(total) +
        " sont couverts. Ce qui manque encore :\n";
    for (const auto& gap : gaps)
        report += "• " + gap.what + " (" + gap.satisfiedBy.front() + ") — " + gap.why + "\n";

    const auto text = juce::String::fromUTF8(report.c_str());

    report_.setText(text, false);
    report_.setVisible(true);
    resized();
}

void MixerPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(header);
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    if (!titled_)
    {
        header.removeFromLeft(tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
        g.drawText("MIXER", header, juce::Justification::centredLeft, false);
    }
}

void MixerPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    header = header.reduced(tokens_.integer("space.xs"));
    readiness_.setBounds(header.removeFromRight(tokens_.integer("metric.mixer.headerButtonWidth")));
    header.removeFromRight(tokens_.integer("space.xs"));
    addBus_.setBounds(header.removeFromRight(tokens_.integer("metric.mixer.headerButtonWidth")));

    if (report_.isVisible())
        report_.setBounds(area);
    viewport_.setVisible(!report_.isVisible());
    if (master_ != nullptr)
        master_->setVisible(!report_.isVisible());

    if (master_ != nullptr)
        master_->setBounds(area.removeFromRight(tokens_.integer("metric.mixer.stripWidth")));

    viewport_.setBounds(area);
    content_->place(strips_, std::max(1, area.getHeight() - tokens_.integer("metric.scrollbar.thickness")));
}

} // namespace daw::ui
