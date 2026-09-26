#include "daw/ui/panels/TransportPanel.h"

#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/ui/model/PatternEditing.h"
#include "daw/ui/model/TempoEditing.h"

#include <cmath>

namespace daw::ui
{
namespace
{

// Thirty frames a second for a readout nobody reads faster than that. Sixty
// would cost twice the repaints to move digits the eye cannot follow.
constexpr int readoutRefreshMs = 33;

constexpr int sixteenthsPerBeat = 4;

// How long the wheel rests before its turn is over, and the next notch opens
// a new history entry.
constexpr juce::uint32 wheelRestMs = 500;

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
    if (wheelGesture_.has_value() && juce::Time::getMillisecondCounter() - lastWheelMs_ > wheelRestMs)
        closeWheelGesture();

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

    lastPosition_ = positionText();
}

juce::String TransportPanel::positionText() const
{
    // Bars from one, beats from one, sixteenths from one: what a musician
    // counts out loud. A playhead at zero reads 001.1.01, not 000.0.00.
    //
    // The beat is a quarter note whatever the signature, so a bar of 6/8
    // counts three of them, and one of 7/8 three and a half: its last beat is
    // an eighth long.
    const auto beats = std::max(0.0, clock_.positionBeats());
    const auto barBeats = state_.beatsPerBar();

    const auto barIndex = std::floor(beats / barBeats + 1e-9);
    const auto inBar = std::max(0.0, beats - barIndex * barBeats);
    const auto bar = static_cast<int>(barIndex) + 1;
    const auto beat = static_cast<int>(inBar) + 1;
    const auto sixteenth =
        static_cast<int>((beats - std::floor(beats)) * static_cast<double>(sixteenthsPerBeat)) + 1;

    return juce::String::formatted("%03d.%d.%02d", bar, beat, sixteenth);
}

juce::String TransportPanel::tempoText() const
{
    // The project's tempo, the one the wheel changes: a readout that showed
    // the tempo under the playhead would not move when turned, the moment the
    // playhead sits after an automation point.
    const auto bpm = tempoEditing::projectTempo(state_);
    return juce::String(bpm, std::abs(bpm - std::round(bpm)) < 1e-9 ? 0 : 1);
}

juce::String TransportPanel::signatureText() const
{
    const auto& signature = state_.timeSignature();
    return juce::String(signature.numerator) + "/" + juce::String(signature.denominator);
}

juce::Rectangle<int> TransportPanel::readoutArea(int index) const
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.lg"), tokens_.integer("space.sm"));

    // The buttons sit on the left; the readouts start after them. The panel
    // arranges its own children, so it knows where they are — it just does not
    // know where itself is.
    const auto buttonsWidth =
        (tokens_.integer("metric.transport.buttonSize") + tokens_.integer("space.xs")) * 5;
    area.removeFromLeft(buttonsWidth + tokens_.integer("space.xl"));

    const auto readoutWidth = tokens_.integer("metric.transport.buttonSize") * 4;
    area.removeFromLeft((readoutWidth + tokens_.integer("space.lg")) * index);
    return area.removeFromLeft(readoutWidth);
}

domain::ExecuteOptions TransportPanel::wheelOptions(const char* label)
{
    lastWheelMs_ = juce::Time::getMillisecondCounter();
    if (!wheelGesture_.has_value() || bus_.openGesture() != wheelGesture_)
        wheelGesture_ = bus_.beginGesture(label);

    domain::ExecuteOptions options;
    options.gesture = wheelGesture_;
    return options;
}

void TransportPanel::closeWheelGesture()
{
    if (wheelGesture_.has_value() && bus_.openGesture() == wheelGesture_)
        static_cast<void>(bus_.endGesture(*wheelGesture_));
    wheelGesture_.reset();
}

void TransportPanel::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto point = event.getPosition();
    const auto notches = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (notches == 0)
        return;

    if (tempoArea().contains(point))
    {
        const auto wanted = tempoEditing::stepTempo(tempoEditing::projectTempo(state_), notches);
        if (wanted != tempoEditing::projectTempo(state_))
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTempoPointBpm>(
                                               domain::ProjectState::originTempoPointId(), wanted),
                                           wheelOptions("molette sur le tempo")));
        }
        return;
    }

    if (signatureArea().contains(point))
    {
        const auto wanted = tempoEditing::stepSignature(state_.timeSignature(), notches);
        if (!(wanted == state_.timeSignature()))
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTimeSignature>(wanted),
                                           wheelOptions("molette sur la signature")));
        }
        return;
    }

    Component::mouseWheelMove(event, wheel);
}

void TransportPanel::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    if (tempoArea().contains(point))
        showTempoMenu();
    else if (signatureArea().contains(point))
        typeSignature();
}

void TransportPanel::showTempoMenu()
{
    closeWheelGesture();

    juce::PopupMenu menu;
    menu.addItem(typeTempoItem, u8"Saisir le tempo…");
    menu.addItem(automateTempoItem,
                 tempoEditing::isAutomated(state_) ? u8"Ajouter un changement de tempo"
                                                   : u8"Automatiser le tempo");

    juce::Component::SafePointer<TransportPanel> safe{this};
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this).withTargetScreenArea(
                           localAreaToGlobal(tempoArea())),
                       [safe](int chosen)
                       {
                           if (safe == nullptr)
                               return;
                           if (chosen == typeTempoItem)
                               safe->typeTempo();
                           else if (chosen == automateTempoItem)
                               safe->automateTempo();
                       });
}

void TransportPanel::typeTempo()
{
    auto* window = new juce::AlertWindow(
        u8"Tempo du projet", u8"En BPM, de 20 à 300.", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("tempo", tempoText());
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TransportPanel> safe{this};
    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe, window](int result)
            {
                if (safe == nullptr || result != 1)
                    return;

                // A text that is not a tempo changes nothing: the readout
                // still shows the old one, which says so.
                const auto typed =
                    tempoEditing::parseTempo(window->getTextEditorContents("tempo").toStdString());
                if (typed.has_value())
                {
                    static_cast<void>(safe->bus_.execute(std::make_unique<domain::SetTempoPointBpm>(
                        domain::ProjectState::originTempoPointId(), *typed)));
                }
            }),
        true);
}

void TransportPanel::typeSignature()
{
    closeWheelGesture();

    auto* window = new juce::AlertWindow(
        u8"Signature rythmique", u8"Par exemple 4/4, 3/4 ou 6/8.", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("signature", signatureText());
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TransportPanel> safe{this};
    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe, window](int result)
            {
                if (safe == nullptr || result != 1)
                    return;

                const auto typed =
                    tempoEditing::parseSignature(window->getTextEditorContents("signature").toStdString());
                if (typed.has_value())
                    static_cast<void>(safe->bus_.execute(std::make_unique<domain::SetTimeSignature>(*typed)));
            }),
        true);
}

void TransportPanel::automateTempo()
{
    // FL's "create automation clip": the tempo lane appears in the playlist
    // with a first change to drag, at the tempo already playing — so nothing
    // is heard to change until the user moves it.
    const auto start = tempoEditing::automationStart(state_, clock_.positionBeats());
    static_cast<void>(bus_.execute(std::make_unique<domain::InsertTempoPoint>(
        domain::TempoPointId::generate(), start, state_.tempoAt(start))));
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

    paintReadout(g, readoutArea(0), lastPosition_, "mesure", true);
    paintReadout(g,
                 readoutArea(1),
                 tempoText(),
                 tempoEditing::isAutomated(state_) ? juce::String{u8"tempo · auto"} : juce::String{"tempo"},
                 false);
    paintReadout(g, readoutArea(2), signatureText(), "signature", false);
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
}

} // namespace daw::ui
