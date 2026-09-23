#include "daw/ui/panels/TransportPanel.h"

#include "daw/domain/commands/TransportCommands.h"
#include "daw/ui/model/PatternEditing.h"

#include <cmath>

namespace daw::ui
{
namespace
{

// Thirty frames a second for a readout nobody reads faster than that. Sixty
// would cost twice the repaints to move digits the eye cannot follow.
constexpr int readoutRefreshMs = 33;

constexpr int beatsPerBar = 4;
constexpr int sixteenthsPerBeat = 4;

} // namespace

// A transport button. The shape is a path, built from the button's own size, so
// it stays sharp whatever the metric token says.
class TransportPanel::IconButton final : public juce::Button
{
public:
    IconButton(const Tokens& tokens, Icon icon, const juce::String& name)
        : juce::Button(name)
        , tokens_(tokens)
        , icon_(icon)
    {
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(tokens_.number("stroke.hairline") * 0.5f);
        const auto radius = tokens_.number("radius.sm");
        const auto on = getToggleState();

        if (on)
        {
            g.setColour(tokens_.colour("color.accent.primary"));
            g.fillRoundedRectangle(bounds, radius);
        }
        else
        {
            if (down)
                g.setColour(tokens_.colour("color.state.pressed"));
            else if (highlighted)
                g.setColour(tokens_.colour("color.state.hover"));
            else
                g.setColour(juce::Colour{});

            g.fillRoundedRectangle(bounds, radius);
            g.setColour(tokens_.colour("color.border.hairline"));
            g.drawRoundedRectangle(bounds, radius, tokens_.number("stroke.hairline"));
        }

        if (!isEnabled())
            g.setColour(tokens_.colour("color.text.disabled"));
        else if (on)
            g.setColour(tokens_.colour("color.accent.onPrimary"));
        else if (highlighted)
            g.setColour(tokens_.colour("color.text.primary"));
        else
            g.setColour(tokens_.colour("color.text.secondary"));

        g.fillPath(shape(bounds.reduced(tokens_.number("space.sm"))));

        if (hasKeyboardFocus(false))
        {
            g.setColour(tokens_.colour("color.state.focus"));
            g.drawRoundedRectangle(bounds, radius, tokens_.number("stroke.focus"));
        }
    }

private:
    [[nodiscard]] juce::Path shape(juce::Rectangle<float> area) const
    {
        juce::Path path;
        const auto stroke = tokens_.number("stroke.focus");

        switch (icon_)
        {
        case Icon::play:
            path.addTriangle(
                area.getX(), area.getY(), area.getX(), area.getBottom(), area.getRight(), area.getCentreY());
            break;

        case Icon::stop:
            path.addRectangle(area);
            break;

        case Icon::rewind:
            path.addRectangle(area.getX(), area.getY(), stroke, area.getHeight());
            path.addTriangle(area.getRight(),
                             area.getY(),
                             area.getRight(),
                             area.getBottom(),
                             area.getX() + stroke * 2.0f,
                             area.getCentreY());
            break;

        case Icon::undo:
        case Icon::redo:
        {
            // An arrow that turns back on itself: the arc says "again", the
            // head says which way.
            const auto mirrored = icon_ == Icon::redo;
            const auto inset = tokens_.number("metric.icon.arcInset");
            const auto lift = tokens_.number("metric.icon.arcLift");
            const auto head = tokens_.number("metric.icon.arrowHead");

            auto arc = area.withTrimmedTop(inset).withTrimmedBottom(inset);

            juce::Path curve;
            curve.startNewSubPath(arc.getX(), arc.getBottom());
            curve.quadraticTo(arc.getCentreX(), arc.getY() - lift, arc.getRight(), arc.getBottom());

            juce::PathStrokeType{stroke}.createStrokedPath(path, curve);

            const auto tipX = mirrored ? arc.getRight() : arc.getX();
            const auto direction = mirrored ? -1.0f : 1.0f;
            path.addTriangle(tipX,
                             arc.getBottom() + inset,
                             tipX + direction * head,
                             arc.getBottom() - inset,
                             tipX + direction * head,
                             arc.getBottom() + head);
            break;
        }
        }

        return path;
    }

    const Tokens& tokens_;
    Icon icon_;
};

TransportPanel::TransportPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , clock_(context.clock)
    , workspaces_(context.workspaces)
    , selection_(context.selection)
{
    setLookAndFeel(&lookAndFeel_);

    rewind_ = std::make_unique<IconButton>(tokens_, Icon::rewind, u8"Retour au début");
    play_ = std::make_unique<IconButton>(tokens_, Icon::play, "Lecture");
    stop_ = std::make_unique<IconButton>(tokens_, Icon::stop, u8"Arrêt");
    undo_ = std::make_unique<IconButton>(tokens_, Icon::undo, "Annuler");
    redo_ = std::make_unique<IconButton>(tokens_, Icon::redo, u8"Rétablir");

    for (auto* button : {rewind_.get(), play_.get(), stop_.get(), undo_.get(), redo_.get()})
        addAndMakeVisible(*button);

    rewind_->onClick = [this]
    { static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0))); };

    play_->onClick = [this] { static_cast<void>(bus_.execute(std::make_unique<domain::TransportPlay>())); };
    stop_->onClick = [this] { static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>())); };

    // Undo and redo are bus calls, not panel logic. The panel never decides
    // what can be undone; it asks, and it asks again after every change.
    undo_->onClick = [this] { static_cast<void>(bus_.undo()); };
    redo_->onClick = [this] { static_cast<void>(bus_.redo()); };

    // Pattern mode plays the pattern the rack and the piano roll show, and
    // names it in the command: the transport never reads a screen on its own.
    for (auto* button : {&patternMode_, &songMode_})
    {
        button->setClickingTogglesState(false);
        addAndMakeVisible(*button);
    }

    patternMode_.setTooltip(u8"Joue le pattern en cours d'édition, seul, en boucle");
    songMode_.setTooltip(u8"Joue toute la playlist");

    patternMode_.onClick = [this]
    {
        const auto* shown = patternEditing::current(state_, selection_);
        static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetMode>(
            domain::PlayMode::pattern, shown != nullptr ? shown->id : domain::PatternId{})));
    };

    songMode_.onClick = [this]
    {
        static_cast<void>(bus_.execute(
            std::make_unique<domain::TransportSetMode>(domain::PlayMode::song, domain::PatternId{})));
    };

    for (const auto& entry : workspaces_.available())
    {
        auto button = std::make_unique<juce::TextButton>(juce::String(entry.label));
        const auto id = entry.id;

        button->setClickingTogglesState(false);
        button->onClick = [this, id] { workspaces_.request(id); };

        addAndMakeVisible(*button);
        workspaceButtons_.push_back(std::move(button));
    }

    project_.addChangeListener(this);
    refresh();
    startTimer(readoutRefreshMs);
}

TransportPanel::~TransportPanel()
{
    stopTimer();
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void TransportPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    refresh();
    repaint();
}

void TransportPanel::timerCallback()
{
    const auto position = positionText();
    if (position == lastPosition_)
        return;

    lastPosition_ = position;
    play_->setToggleState(clock_.isPlaying(), juce::dontSendNotification);
    repaint();
}

void TransportPanel::refresh()
{
    undo_->setEnabled(bus_.canUndo());
    redo_->setEnabled(bus_.canRedo());

    // From the clock, never from ProjectState. The domain remembers the last
    // transport command; the engine knows whether sound is coming out. They
    // disagree the moment playback runs off the end of the material, and a lit
    // play button over silence is the interface lying about the one thing the
    // user can hear.
    play_->setToggleState(clock_.isPlaying(), juce::dontSendNotification);

    const auto patternMode = state_.transport().mode == domain::PlayMode::pattern;
    patternMode_.setToggleState(patternMode, juce::dontSendNotification);
    songMode_.setToggleState(!patternMode, juce::dontSendNotification);

    const auto current = workspaces_.current();
    const auto entries = workspaces_.available();

    for (std::size_t index = 0; index < workspaceButtons_.size() && index < entries.size(); ++index)
        workspaceButtons_[index]->setToggleState(entries[index].id == current, juce::dontSendNotification);

    lastPosition_ = positionText();
}

juce::String TransportPanel::positionText() const
{
    // Bars from one, beats from one, sixteenths from one: what a musician
    // counts out loud. A playhead at zero reads 001.1.01, not 000.0.00.
    const auto beats = std::max(0.0, clock_.positionBeats());

    const auto bar = static_cast<int>(beats) / beatsPerBar + 1;
    const auto beat = static_cast<int>(beats) % beatsPerBar + 1;
    const auto sixteenth =
        static_cast<int>((beats - std::floor(beats)) * static_cast<double>(sixteenthsPerBeat)) + 1;

    return juce::String::formatted("%03d.%d.%02d", bar, beat, sixteenth);
}

juce::String TransportPanel::tempoText() const
{
    // The tempo where the playhead is, not the tempo of the project: with a
    // sequence there is no single project tempo any more.
    return juce::String(state_.tempoAt(std::max(0.0, clock_.positionBeats())), 1);
}

void TransportPanel::paintReadout(juce::Graphics& g,
                                  juce::Rectangle<int> area,
                                  const juce::String& value,
                                  const juce::String& label,
                                  bool strong) const
{
    auto bounds = area;
    auto caption = bounds.removeFromBottom(tokens_.integer("font.size.micro") + tokens_.integer("space.xs"));

    g.setColour(strong ? tokens_.colour("color.text.primary") : tokens_.colour("color.text.secondary"));
    g.setFont(strong ? lookAndFeel_.typography().mono("font.size.display", "font.weight.medium")
                     : lookAndFeel_.typography().mono("font.size.title", "font.weight.regular"));
    g.drawText(value, bounds, juce::Justification::centredLeft, false);

    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText(label.toUpperCase(), caption, juce::Justification::centredLeft, false);
}

void TransportPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto area = getLocalBounds().reduced(tokens_.integer("space.lg"), tokens_.integer("space.sm"));

    // The buttons sit on the left; the readouts start after them. The panel
    // arranges its own children, so it knows where they are — it just does not
    // know where itself is.
    const auto buttonsWidth =
        (tokens_.integer("metric.transport.buttonSize") + tokens_.integer("space.xs")) * 5;
    area.removeFromLeft(buttonsWidth + tokens_.integer("space.xl"));

    const auto readoutWidth = tokens_.integer("metric.transport.buttonSize") * 4;

    paintReadout(g, area.removeFromLeft(readoutWidth), lastPosition_, "mesure", true);
    area.removeFromLeft(tokens_.integer("space.lg"));
    paintReadout(g, area.removeFromLeft(readoutWidth), tempoText(), "tempo", false);
    area.removeFromLeft(tokens_.integer("space.lg"));
    paintReadout(g, area.removeFromLeft(readoutWidth), "4/4", "signature", false);
}

void TransportPanel::resized()
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.lg"), tokens_.integer("space.sm"));

    const auto size = tokens_.integer("metric.transport.buttonSize");
    const auto gap = tokens_.integer("space.xs");

    auto controls = area.removeFromLeft((size + gap) * 5);
    controls = controls.withSizeKeepingCentre(controls.getWidth(), size);

    for (auto* button : {rewind_.get(), play_.get(), stop_.get()})
    {
        button->setBounds(controls.removeFromLeft(size));
        controls.removeFromLeft(gap);
    }

    controls.removeFromLeft(gap);
    undo_->setBounds(controls.removeFromLeft(size));
    controls.removeFromLeft(gap);
    redo_->setBounds(controls.removeFromLeft(size));

    // PAT and SONG after the three readouts, where paint() leaves them room.
    {
        auto modes = area;
        modes.removeFromLeft(tokens_.integer("space.xl"));
        const auto readoutWidth = size * 4;
        modes.removeFromLeft((readoutWidth + tokens_.integer("space.lg")) * 3);
        modes = modes.withSizeKeepingCentre(modes.getWidth(), size);

        patternMode_.setBounds(modes.removeFromLeft(size * 2));
        modes.removeFromLeft(gap);
        songMode_.setBounds(modes.removeFromLeft(size * 2));
    }

    // The workspace switch is pushed to the far right: it is the one control
    // here that does not act on the music.
    auto right = area.withSizeKeepingCentre(area.getWidth(), size);
    const auto switchWidth = size * 3;

    for (auto button = workspaceButtons_.rbegin(); button != workspaceButtons_.rend(); ++button)
    {
        (*button)->setBounds(right.removeFromRight(switchWidth));
        right.removeFromRight(gap);
    }
}

} // namespace daw::ui
