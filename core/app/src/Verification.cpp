#include "Verification.h"

#include "NativeWindow.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/engine/Export.h"
#include "daw/engine/MeterTap.h"
#include "daw/engine/Rendering.h"
#include "daw/ui/FrameTicker.h"
#include "daw/ui/panels/BrowserPanel.h"
#include "daw/ui/panels/ChannelRackPanel.h"
#include "daw/ui/panels/GenerationPanel.h"
#include "daw/ui/panels/MixerPanel.h"
#include "daw/ui/panels/PianoRollPanel.h"
#include "daw/ui/panels/PlaylistPanel.h"
#include "daw/ui/panels/TransportPanel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

namespace daw::app
{
namespace
{

constexpr int tickMs = 120;

// Ticks left after a step is ready, so the window has painted what the step
// changed before it is photographed.
constexpr int settleTicks = 3;

constexpr double stepBeats = 0.25; // the rack's default resolution: sixteenths
constexpr int beatsPerBar = 4;

[[nodiscard]] juce::String fileSafe(const std::string& text)
{
    return juce::File::createLegalFileName(juce::String::fromUTF8(text.c_str())).replaceCharacter(' ', '-');
}

} // namespace

Verification::Verification(Wiring wiring)
    : bus_(wiring.bus)
    , state_(wiring.state)
    , view_(wiring.view)
    , history_(wiring.history)
    , copilot_(wiring.copilot)
    , selection_(wiring.selection)
    , clock_(wiring.clock)
    , tokens_(wiring.tokens)
    , edit_(wiring.edit)
    , samples_(wiring.samples)
    , levels_(wiring.levels)
    , window_(wiring.window)
    , shell_(wiring.shell)
    , titleBar_(wiring.titleBar)
    , folder_(std::move(wiring.folder))
    , run_(wiring.run)
    , finished_(std::move(wiring.finished))
    , newProjectAt_(std::move(wiring.newProjectAt))
    , openProjectAt_(std::move(wiring.openProjectAt))
    , saveAsTo_(std::move(wiring.saveAsTo))
    , lastRefusal_(std::move(wiring.lastRefusal))
    , probe_(wiring.probe)
    , project_(wiring.project)
    , mix_(wiring.mix)
{
    exporter_ = wiring.exporter;
}

Verification::~Verification()
{
    ui::FrameTicker::holdStill(false);
    levels_.removeChangeListener(this);
    stopTimer();
}

void Verification::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &levels_ && recordingMaster_)
        masterSeen_.push_back(levelOf(engine::MeterTapPlugin::masterStrip.toStdString()).peakDb);
}

void Verification::start()
{
    static_cast<void>(folder_.createDirectory());

    // A step checks a zoom the moment the wheel turns: the screen is held
    // still, the glides and fades of the fluid mode wait (S18 bis). The
    // fluidity run lets them go for the step that proves them.
    ui::FrameTicker::holdStill(true);

    switch (run_)
    {
    case Run::list:
        buildList();
        break;
    case Run::reopen:
        buildReopen();
        break;
    case Run::legacy:
        buildLegacy();
        break;
    case Run::file:
        buildFile();
        break;
    case Run::canvas:
        buildCanvas();
        break;
    case Run::canvasLoad:
        buildCanvasLoad();
        break;
    case Run::fluidity:
        buildFluidity();
        break;
    case Run::mix:
        buildMix();
        break;
    }

    report_.add(juce::String::fromUTF8("# Vérification S11 — ") +
                juce::Time::getCurrentTime().toString(true, true));
    report_.add({});
    startTimer(tickMs);
}

void Verification::add(std::string title,
                       std::function<void()> act,
                       std::function<bool()> ready,
                       double timeoutMs)
{
    steps_.push_back(Step{std::move(title), std::move(act), std::move(ready), timeoutMs});
}

void Verification::timerCallback()
{
    // How long the message thread went without ticking, while a step is
    // watching for a freeze.
    if (watchTicks_)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        longestTickMs_ = std::max(longestTickMs_, now - lastTickMs_);
        lastTickMs_ = now;
    }

    if (current_ >= steps_.size())
    {
        stopTimer();
        finish();
        return;
    }

    auto& step = steps_[current_];

    if (!acted_)
    {
        report_.add({});
        report_.add("## " + juce::String(static_cast<int>(current_) + 1) + ". " +
                    juce::String::fromUTF8(step.title.c_str()));
        acted_ = true;
        probed_ = false;
        refusalsAtStart_ = probe_ != nullptr ? probe_->refusalCount() : 0;
        settle_ = settleTicks;
        startedAtMs_ = juce::Time::getMillisecondCounterHiRes();
        if (step.act)
            step.act();
        return;
    }

    const auto waited = juce::Time::getMillisecondCounterHiRes() - startedAtMs_;
    if (step.ready && !step.ready() && waited < step.timeoutMs)
        return;

    if (step.ready && !step.ready() && !timedOut_)
    {
        timedOut_ = true;
        check(false, "l'étape n'a pas abouti en " + std::to_string(static_cast<int>(step.timeoutMs)) + " ms");
    }

    if (--settle_ > 0)
        return;

    // Whatever the bus refused during the step, even when every check passed:
    // a refusal nobody asserted on is how an action gets lost quietly.
    if (probe_ != nullptr && probe_->refusalCount() > refusalsAtStart_)
    {
        const auto& recent = probe_->recentRefusals();
        const auto count = std::min(probe_->refusalCount() - refusalsAtStart_, recent.size());
        for (auto line = recent.end() - static_cast<std::ptrdiff_t>(count); line != recent.end(); ++line)
            note("refus du bus : " + *line);
    }

    snapshot(std::to_string(current_ + 1) + "-" + step.title);
    acted_ = false;
    timedOut_ = false;
    ++current_;
}

// --- recording --------------------------------------------------------------

void Verification::check(bool passed, const std::string& what)
{
    (passed ? passed_ : failed_)++;
    report_.add(juce::String::fromUTF8(passed ? "- OK : " : "- **ÉCHEC** : ") +
                juce::String::fromUTF8(what.c_str()));

    // The first failure of a step carries the state it happened in: the
    // intermittent failures of S12 and S13 left nothing else to go on.
    if (!passed && !probed_ && probe_ != nullptr)
    {
        probed_ = true;
        note("état au premier échec : " + probe_->describe());
    }
}

void Verification::note(const std::string& what)
{
    report_.add("- " + juce::String::fromUTF8(what.c_str()));
}

void Verification::snapshot(const std::string& name)
{
    const auto image = shell_.createComponentSnapshot(shell_.getLocalBounds(), true, 1.0f);
    const auto file = folder_.getChildFile(fileSafe(name) + ".png");
    static_cast<void>(file.deleteFile());

    juce::FileOutputStream stream{file};
    juce::PNGImageFormat png;
    if (stream.openedOk() && png.writeImageToStream(image, stream))
        report_.add("- capture : `" + file.getFileName() + "`");
}

Verification::Heard Verification::listen(const std::string& name, double beatsPerMinute, float floor)
{
    const auto file = folder_.getChildFile(fileSafe(name) + ".wav");
    static_cast<void>(file.deleteFile());

    Heard heard{};
    // As it plays: the freeze path of Tracktion's renderToFile unmutes
    // every track, and a muted track would be heard here.
    if (!engine::renderAsPlayed(edit_, file))
    {
        check(false, "le rendu hors ligne a échoué");
        return heard;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        check(false, "le rendu est vide");
        return heard;
    }

    juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples)};
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    heard.seconds = static_cast<double>(buffer.getNumSamples()) / reader->sampleRate;

    // One window per sixteenth, the rack's own grid. A note starts where a
    // window is loud and the one before it was much quieter.
    const auto samplesPerStep =
        static_cast<int>(std::lround(60.0 / beatsPerMinute * stepBeats * reader->sampleRate));
    const auto steps = buffer.getNumSamples() / std::max(1, samplesPerStep);

    std::vector<float> levels;
    for (int step = 0; step < steps; ++step)
        levels.push_back(buffer.getRMSLevel(0, step * samplesPerStep, samplesPerStep));

    const auto loudest = levels.empty() ? 0.0f : *std::max_element(levels.begin(), levels.end());
    for (std::size_t step = 0; step < levels.size(); ++step)
    {
        const auto before = step == 0 ? 0.0f : levels[step - 1];
        // A tail only decays, so any rise above the window before is a new
        // note: a hat six decibels under a kick's tail still rises above it.
        if (levels[step] > loudest * floor && levels[step] > before * 1.25f)
        {
            heard.onsets.push_back(static_cast<int>(step));
            heard.onsetLevels.push_back(levels[step]);
        }
    }

    report_.add(juce::String::fromUTF8("- écoute : `") + file.getFileName() + "`, " +
                juce::String(heard.seconds, 2) + " s, " +
                juce::String(static_cast<int>(heard.onsets.size())) + " attaques");
    return heard;
}

void Verification::finish()
{
    // The drumkit folder was given to this machine's browser for the run, and
    // is taken back: the next run, and the person after it, find the browser
    // as it was.
    if (kit_ != juce::File{})
        samples_.removeFolder(kit_);

    report_.add({});
    report_.add(juce::String::fromUTF8("## Résultat"));
    report_.add("- " + juce::String(passed_) + juce::String::fromUTF8(" vérifications passées, ") +
                juce::String(failed_) + juce::String::fromUTF8(" en échec"));

    static_cast<void>(folder_.getChildFile("rapport.md").replaceWithText(report_.joinIntoString("\n")));

    if (run_ == Run::list)
    {
        const auto kept = domain::Value::object(
            {{"state", state_.toValue()}, {"undoDepth", domain::Value{static_cast<std::int64_t>(depth())}}});
        static_cast<void>(folder_.getChildFile("etat.json")
                              .replaceWithText(juce::String::fromUTF8(domain::json::write(kept).c_str())));
    }

    if (finished_)
        finished_(failed_ == 0);
}

// --- the doors a person uses -------------------------------------------------

juce::Component* Verification::panel(const char* id) const
{
    return view_.panel(id);
}

juce::Button* Verification::button(juce::Component& root, const juce::String& text) const
{
    if (auto* candidate = dynamic_cast<juce::Button*>(&root);
        candidate != nullptr && candidate->getButtonText() == text)
        return candidate;

    for (auto* child : root.getChildren())
    {
        if (auto* found = button(*child, text); found != nullptr)
            return found;
    }
    return nullptr;
}

void Verification::press(const juce::String& text)
{
    auto* found = button(view_, text);
    if (found == nullptr)
    {
        check(false, "aucun bouton « " + text.toStdString() + " »");
        return;
    }

    // What a click on the button ends in, without the click's own detour
    // through the message queue.
    if (found->onClick)
        found->onClick();
}

void Verification::doubleClick(juce::Component& target, juce::Point<int> at)
{
    const auto now = juce::Time::getCurrentTime();
    const auto position = at.toFloat();
    const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(),
                                 position,
                                 juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier},
                                 juce::MouseInputSource::defaultPressure,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 &target,
                                 &target,
                                 now,
                                 position,
                                 now,
                                 2,
                                 false};
    target.mouseDoubleClick(event);
}

void Verification::click(juce::Component& target, juce::Point<int> at, bool right, bool shift, bool ctrl)
{
    // A button that cannot take the click drops it without a word: disabled,
    // hidden, or under a modal component. That is one way an action during
    // playback has "no effect", so it is said where it happens.
    if (auto* button = dynamic_cast<juce::Button*>(&target); button != nullptr)
    {
        std::string why;
        if (!button->isEnabled())
            why += " désactivé";
        // JUCE asks isVisible(), the component's own flag, not isShowing():
        // a button on a page behind another window still takes the click.
        if (!button->isVisible())
            why += " invisible";
        if (button->isCurrentlyBlockedByAnotherModalComponent())
        {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            why += " sous un composant modal « " +
                   (modal != nullptr ? modal->getName().toStdString() : std::string{}) + " »";
        }
        if (!why.empty())
            note("clic sur « " + button->getButtonText().toStdString() + " » perdu :" + why);
    }

    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();

    auto held = juce::ModifierKeys{right ? juce::ModifierKeys::rightButtonModifier
                                         : juce::ModifierKeys::leftButtonModifier};
    if (shift)
        held = held.withFlags(juce::ModifierKeys::shiftModifier);
    if (ctrl)
        held = held.withFlags(juce::ModifierKeys::ctrlModifier);

    const auto position = at.toFloat();
    const juce::MouseEvent down{source,
                                position,
                                held,
                                juce::MouseInputSource::defaultPressure,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                &target,
                                &target,
                                now,
                                position,
                                now,
                                1,
                                false};
    target.mouseDown(down);

    const juce::MouseEvent up{source,
                              position,
                              held.withoutMouseButtons(),
                              juce::MouseInputSource::defaultPressure,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.0f,
                              &target,
                              &target,
                              now,
                              position,
                              now,
                              1,
                              false};
    target.mouseUp(up);
}

void Verification::drag(juce::Component& target,
                        juce::Point<int> from,
                        juce::Point<int> to,
                        bool ctrl,
                        bool middle,
                        bool shift,
                        bool alt)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto held = juce::ModifierKeys{
        (middle ? juce::ModifierKeys::middleButtonModifier : juce::ModifierKeys::leftButtonModifier) |
        (ctrl ? juce::ModifierKeys::ctrlModifier : 0) | (shift ? juce::ModifierKeys::shiftModifier : 0) |
        (alt ? juce::ModifierKeys::altModifier : 0)};
    const auto start = from.toFloat();

    const juce::MouseEvent down{source,
                                start,
                                held,
                                juce::MouseInputSource::defaultPressure,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                &target,
                                &target,
                                now,
                                start,
                                now,
                                1,
                                false};
    target.mouseDown(down);

    // Through every pixel in between, like a hand: a drag that jumped would
    // not prove that the moves in the middle merge into one entry.
    const auto distance = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
    for (int index = 1; index <= distance; ++index)
    {
        const auto point = from.toFloat() +
                           (to - from).toFloat() * (static_cast<float>(index) / static_cast<float>(distance));
        const juce::MouseEvent moved{source,
                                     point,
                                     held,
                                     juce::MouseInputSource::defaultPressure,
                                     0.0f,
                                     0.0f,
                                     0.0f,
                                     0.0f,
                                     &target,
                                     &target,
                                     now,
                                     start,
                                     now,
                                     1,
                                     true};
        target.mouseDrag(moved);
    }

    const auto end = to.toFloat();
    const juce::MouseEvent up{source,
                              end,
                              held.withoutMouseButtons(),
                              juce::MouseInputSource::defaultPressure,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.0f,
                              &target,
                              &target,
                              now,
                              start,
                              now,
                              1,
                              true};
    target.mouseUp(up);
}

void Verification::writeHit(const juce::File& file, double seconds)
{
    constexpr double rate = 44100.0;
    juce::AudioBuffer<float> buffer{1, static_cast<int>(seconds * rate)};
    buffer.clear();
    for (int index = 0; index < static_cast<int>(0.05 * rate); ++index)
        buffer.setSample(0, index, 0.8f * std::sin(static_cast<float>(index) * 0.2f));

    static_cast<void>(file.deleteFile());
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(16));
    if (writer != nullptr)
        static_cast<void>(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}

void Verification::key(const juce::KeyPress& press)
{
    static_cast<void>(view_.keyPressed(press));
}

void Verification::prompt(ui::GenerationPanel& bar, const juce::String& words)
{
    bar.field().setText(words, false);
    static_cast<void>(bar.field().keyPressed(juce::KeyPress{juce::KeyPress::returnKey}));

    // The list's own timer is held while the messages run: a step must not
    // start inside the step that waits. Past the reader's patience the local
    // words have answered; the margin is for the message that brings them.
    stopTimer();
    const auto until = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (bar.isReading() && juce::Time::getMillisecondCounterHiRes() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    startTimer(tickMs);
    if (bar.isReading())
        check(false, "la lecture des mots n'a pas abouti en 10 s");
}

engine::StripLevel Verification::levelIn(const std::vector<engine::StripLevel>& levels,
                                         const std::string& strip)
{
    const auto found =
        std::find_if(levels.begin(),
                     levels.end(),
                     [&strip](const engine::StripLevel& level) { return level.strip == strip; });
    if (found != levels.end())
        return *found;

    engine::StripLevel silent{};
    silent.strip = strip;
    return silent;
}

engine::StripLevel Verification::levelOf(const std::string& strip) const
{
    return levelIn(levels_.levels(), strip);
}

void Verification::writeNotes(int row, const std::vector<double>& beats)
{
    // The way notes are written since S12: the channel chosen in the rack,
    // then a click per note in the piano roll, on the channel's own pitch.
    auto* rack = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"));
    if (rack == nullptr || row < 0 || row >= static_cast<int>(state_.tracks().size()))
    {
        check(false, "le rack montre le canal " + std::to_string(row + 1));
        return;
    }
    click(*rack, rackChannel(row));

    // A person moves the mouse to the piano roll after choosing the channel,
    // and the panels have long heard of the choice by then: a ChangeBroadcaster
    // speaks asynchronously. Here the next click comes at once, so the news is
    // delivered first.
    selection_.dispatchPendingMessages();

    const auto wasShowing = panel("piano_roll") != nullptr && panel("piano_roll")->isShowing();
    if (!wasShowing)
        key(juce::KeyPress{juce::KeyPress::F7Key});

    auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
    if (roll != nullptr)
    {
        const auto trackId = state_.tracks()[static_cast<std::size_t>(row)].id;
        const auto count = [this, trackId]
        {
            const auto* shown = state_.findPattern(selection_.pattern());
            const auto* clip = shown != nullptr ? shown->findClipForTrack(trackId) : nullptr;
            return clip != nullptr ? clip->notes.size() : std::size_t{0};
        };
        const auto before = count();
        const auto pitch = state_.tracks()[static_cast<std::size_t>(row)].channelPitch;
        for (const auto beat : beats)
            click(*roll, roll->pointFor(beat, pitch));
        if (count() != before + beats.size())
            note("écrire au piano-roll : " + std::to_string(count() - before) + " notes sur " +
                 std::to_string(beats.size()) + ", piano-roll " + roll->getBounds().toString().toStdString() +
                 ", premier clic " + roll->pointFor(beats.front(), pitch).toString().toStdString() +
                 ", piste choisie " + (selection_.track() == trackId ? "oui" : "non") + ", visible " +
                 (roll->isShowing() ? "oui" : "non"));
    }

    if (!wasShowing)
        key(juce::KeyPress{juce::KeyPress::F7Key});
}

void Verification::wheel(
    juce::Component& target, juce::Point<int> at, float deltaY, bool shift, bool ctrl, bool alt)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();

    auto held = juce::ModifierKeys{};
    if (shift)
        held = held.withFlags(juce::ModifierKeys::shiftModifier);
    if (ctrl)
        held = held.withFlags(juce::ModifierKeys::ctrlModifier);
    if (alt)
        held = held.withFlags(juce::ModifierKeys::altModifier);

    const auto position = at.toFloat();
    const juce::MouseEvent event{source,
                                 position,
                                 held,
                                 juce::MouseInputSource::defaultPressure,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 &target,
                                 &target,
                                 now,
                                 position,
                                 now,
                                 0,
                                 false};

    juce::MouseWheelDetails details{};
    details.deltaX = 0.0f;
    details.deltaY = deltaY;
    details.isReversed = false;
    details.isSmooth = false;
    details.isInertial = false;
    target.mouseWheelMove(event, details);
}

juce::Point<int> Verification::playlistBeat(int lane, double beats)
{
    auto* view = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
    if (view == nullptr)
        return {};

    // A notch at a time, as a hand turns it.
    constexpr float notch = 0.25f;
    for (int turn = 0; turn < 400; ++turn)
    {
        const auto point = view->pointFor(lane, beats);
        const auto area = view->timelineArea();
        const auto centre = area.getCentre();

        if (point.getX() < area.getX() + 2)
            wheel(*view, centre, notch, true);
        else if (point.getX() >= area.getRight() - 2)
            wheel(*view, centre, -notch, true);
        else if (point.getY() < area.getY())
            wheel(*view, centre, notch);
        else if (point.getY() >= area.getBottom())
            wheel(*view, centre, -notch);
        else
            return point;
    }
    return view->pointFor(lane, beats);
}

// --- the list ------------------------------------------------------------------

void Verification::buildList()
{
    add("disposition",
        [this]
        {
            // Everything opens where the manifest says, whatever a previous
            // session left behind.
            for (const auto* id : {"playlist", "channel_rack", "history", "copilot"})
                static_cast<void>(view_.showPage(id, true));
            // The canvas (S18) shares the playlist's place: closed here, the
            // steps before S18 see the playlist they were written for.
            for (const auto* id : {"piano_roll", "plugin_chain", "tracks", "canvas"})
                static_cast<void>(view_.showPage(id, false));

            check(panel("transport") != nullptr && panel("transport")->isShowing(),
                  "le transport est dans la barre");
            for (const auto* id : {"playlist", "channel_rack", "history", "copilot"})
                check(panel(id) != nullptr && panel(id)->isShowing(),
                      std::string{"la page "} + id + " est ouverte");
            check(panel("piano_roll") != nullptr && !panel("piano_roll")->isShowing(),
                  "le piano-roll est fermé");
            check(button(view_, "PAT") != nullptr && button(view_, "PAT")->getToggleState(),
                  "PAT est allumé");
        });

    add("F7 ouvre le piano-roll, F7 le referme",
        [this]
        {
            key(juce::KeyPress{juce::KeyPress::F7Key});
            check(panel("piano_roll")->isShowing(), "F7 ouvre le piano-roll");
            key(juce::KeyPress{juce::KeyPress::F7Key});
            check(!panel("piano_roll")->isShowing(), "F7 referme le piano-roll, qui était devant");
        });

    add(
        "deux pistes, un pattern qui ne se pose pas",
        [this]
        {
            // The tracks are made through the bus: the track list is not what
            // this list verifies.
            static_cast<void>(
                bus_.execute(std::make_unique<domain::AddTrack>(domain::TrackId::generate(), "Kick", 0.0)));
            static_cast<void>(
                bus_.execute(std::make_unique<domain::AddTrack>(domain::TrackId::generate(), "Hat", -6.0)));

            savedDepth_ = depth();
            press("+ Pattern");
        },
        [this] { return !state_.patterns().empty(); });

    add("le pattern existe, sans placement",
        [this]
        {
            check(state_.patterns().size() == 1, "un pattern");
            check(state_.arrangement().empty(), "aucune pose dans la playlist");
            check(depth() == savedDepth_ + 1, "une entrée d'historique");
        });

    add("le mode pattern joue le pattern sans pose",
        [this]
        {
            writeNotes(0, {0.0, 1.0, 2.0, 3.0});

            const auto heard = listen("3-mode-pattern", 120.0);
            check(std::abs(heard.seconds - 8.0) < 0.2,
                  "le rendu dure la longueur du pattern, 8 s (16 temps)");
            check(heard.onsets == std::vector<int>{0, 4, 8, 12}, "quatre kicks, sur les pas 1, 5, 9, 13");
        });

    add("poser le pattern huit fois",
        [this]
        {
            auto* playlist = panel("playlist");
            for (int index = 0; index < 8; ++index)
                click(*playlist, playlistBeat(0, 16.0 * index + 0.5));

            check(state_.arrangement().size() == 8, "huit poses");

            std::vector<double> starts;
            for (const auto& placement : state_.arrangement())
                starts.push_back(placement.startBeats);
            check(starts == std::vector<double>{0, 16, 32, 48, 64, 80, 96, 112}, "aux mesures 1, 5, 9 … 29");

            press("SONG");
            check(state_.transport().mode == domain::PlayMode::song, "SONG est pris");

            const auto heard = listen("4-chanson-huit-poses", 120.0);
            check(std::abs(heard.seconds - 64.0) < 0.3, "le morceau dure 32 mesures, 64 s");
            check(heard.onsets.size() == 32, "32 kicks : quatre par pose");
        });

    add(
        "en chanson, la lecture traverse la playlist",
        [this] { static_cast<void>(bus_.execute(std::make_unique<domain::TransportPlay>())); },
        [this] { return clock_.positionBeats() > 3.0; },
        8000.0);

    add("LA preuve : une édition du piano-roll, huit poses changées",
        [this]
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            savedDepth_ = depth();

            writeNotes(1, {0.5, 1.5, 2.5, 3.5});

            check(state_.patterns().front().clips.size() == 2, "la ligne du Hat est ouverte dans le pattern");
            check(state_.arrangement().size() == 8, "toujours huit poses, aucune copiée");

            const auto heard = listen("5-preuve-hat-partout", 120.0);
            check(heard.onsets.size() == 64, "64 attaques : le hat s'entend dans les huit poses");

            bool everyLaying = true;
            for (int laying = 0; laying < 8; ++laying)
            {
                for (const auto step : {2, 6, 10, 14})
                {
                    if (std::find(heard.onsets.begin(), heard.onsets.end(), laying * 64 + step) ==
                        heard.onsets.end())
                        everyLaying = false;
                }
            }
            check(everyLaying, "aux mêmes pas dans chacune des huit poses");
        });

    add("Ctrl+Z retire le hat des huit poses",
        [this]
        {
            const auto lit = depth() - savedDepth_;
            note("quatre clics séparés = " + std::to_string(lit) +
                 " entrées d'historique (le premier ouvre la ligne du Hat dans la même) ; autant de Ctrl+Z");
            for (std::size_t index = 0; index < lit; ++index)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});

            const auto heard = listen("5b-apres-ctrl-z", 120.0);
            check(heard.onsets.size() == 32, "de retour à 32 attaques, dans les huit poses à la fois");
        });

    add("glisser un bloc, en retirer un",
        [this]
        {
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            auto* playlist = panel("playlist");
            const auto fourth = state_.arrangement()[3].id;
            // Grabbed two beats into the block and let go eight beats further:
            // the move snaps to the bar as a whole, and eight beats is two bars
            // with nothing to round.
            drag(*playlist, playlistBeat(0, 50.0), playlistBeat(0, 58.0));

            check(state_.findPlacement(fourth)->startBeats == 56.0,
                  "le 4e bloc a suivi, à la mesure près (mesure 15)");
            check(depth() == savedDepth_ + 1, "un glissé = une seule entrée d'historique");

            bool othersStill = true;
            for (std::size_t index = 0; index < 8; ++index)
            {
                if (index != 3 && state_.arrangement()[index].startBeats != 16.0 * static_cast<double>(index))
                    othersStill = false;
            }
            check(othersStill, "les sept autres n'ont pas bougé");

            click(*playlist, playlistBeat(0, 16.0 * 6 + 2.0), true);
            check(state_.arrangement().size() == 7, "clic droit : le bloc disparaît");
            check(state_.patterns().size() == 1, "le pattern reste");
        });

    add("deux Ctrl+Z remettent tout en place",
        [this]
        {
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_,
                  "l'arrangement est identique, à l'octet près");
        });

    add("S17 : glisser un bloc vers le bas le range sur une nouvelle ligne, sans rien changer au son",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();
            const auto before = listen("5c-avant-ligne", 120.0);
            const auto lines = state_.lanes().size();
            const auto second = state_.arrangement()[1].id;

            // Straight down, from the line of pattern 1 to the empty one under
            // it: the block keeps its beat and changes line.
            drag(*playlist, playlistBeat(0, 18.0), playlistBeat(static_cast<int>(lines), 18.0));

            check(state_.lanes().size() == lines + 1, "une ligne de plus, créée par le glissé");
            const auto* moved = state_.findPlacement(second);
            check(moved != nullptr && moved->laneId == state_.lanes().back().id,
                  "le 2e bloc est sur la nouvelle ligne");
            check(moved != nullptr && std::abs(moved->startBeats - 16.0) < 1e-9,
                  "à son temps, 16 : un glissé vertical ne décale pas");
            check(depth() == savedDepth_ + 1, "la ligne et le glissé : une seule entrée d'historique");

            const auto after = listen("5d-apres-ligne", 120.0);
            check(after.onsets == before.onsets, "les mêmes attaques aux mêmes pas : une ligne ne sonne pas");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_,
                  "Ctrl+Z : la ligne et le bloc reviennent, à l'octet près");
        });

    add("un deuxième pattern pour le copilote",
        [this]
        {
            press("+ Pattern");
            writeNotes(1, {0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5});
            check(state_.patterns().size() == 2, "deux patterns");
            check(state_.arrangement().size() == 8, "le pattern 2 n'est pas posé");
        });

    add(
        "le copilote est prêt",
        {},
        [this] { return copilot_.status() == ui::CopilotHost::Status::ready; },
        30000.0);

    add(
        "« répète le pattern 1 huit fois puis ajoute le pattern 2 »",
        [this]
        {
            check(copilot_.status() == ui::CopilotHost::Status::ready, "le copilote répond");
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();
            transcriptBefore_ = copilot_.transcript().size();
            copilot_.ask("répète le pattern 1 huit fois puis ajoute le pattern 2");
        },
        [this]
        {
            return copilot_.transcript().size() > transcriptBefore_ + 1 &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        120000.0);

    add("ce que le copilote a fait",
        [this]
        {
            const auto& lines = copilot_.transcript();
            if (!lines.empty())
                note("réponse : « " + lines.back().text + " »");

            check(depth() == savedDepth_ + 1, "une seule entrée d'historique");

            const auto& entries = history_.entries();
            const auto last = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(last != nullptr && last->actor == domain::Actor::copilot, "marquée copilote");

            const auto& first = state_.patterns()[0];
            const auto& second = state_.patterns()[1];
            const auto ofFirst = state_.placementsOf(first.id).size();
            const auto ofSecond = state_.placementsOf(second.id);
            note("poses du pattern 1 : " + std::to_string(ofFirst) +
                 ", du pattern 2 : " + std::to_string(ofSecond.size()));
            check(ofFirst >= 8, "le pattern 1 est posé au moins huit fois");
            check(ofSecond.size() == 1, "le pattern 2 est posé une fois");
            if (!ofSecond.empty())
            {
                double end = 0.0;
                for (const auto* placement : state_.placementsOf(first.id))
                    end = std::max(end, placement->startBeats + first.lengthBeats);
                check(ofSecond.front()->startBeats >= end - 0.001, "le pattern 2 vient après le pattern 1");
            }
            check(state_.patterns().size() == 2, "aucun pattern créé pour répéter");
        });

    add("un Ctrl+Z défait tout ce que le copilote a fait",
        [this]
        {
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_,
                  "l'arrangement d'avant la demande, à l'octet près");
            key(juce::KeyPress{'y', juce::ModifierKeys::ctrlModifier, 0});
            check(depth() == savedDepth_ + 1, "Ctrl+Y le refait");
        });

    add("PAT isole le pattern 2",
        [this]
        {
            selection_.selectPattern(state_.patterns()[1].id);
            press("PAT");
            check(state_.transport().mode == domain::PlayMode::pattern, "PAT est pris");
            check(state_.transport().auditionedPattern == state_.patterns()[1].id, "sur le pattern 2");

            const auto heard = listen("8-pat-pattern-2", 120.0);
            check(std::abs(heard.seconds - 8.0) < 0.2, "le rendu ne dure que le pattern, 8 s");
            check(heard.onsets == std::vector<int>({0, 2, 4, 6, 8, 10, 12, 14}),
                  "seuls les huit hats du pattern 2");

            press("SONG");
            check(state_.transport().mode == domain::PlayMode::song, "SONG ramène l'arrangement");
        });

    add("le tempo à 90",
        [this]
        {
            const auto before = listen("9a-tempo-120", 120.0);
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTempoPointBpm>(
                domain::ProjectState::originTempoPointId(), 90.0)));
            const auto after = listen("9b-tempo-90", 90.0);

            check(std::abs(after.seconds - before.seconds * 120.0 / 90.0) < 0.5,
                  "le morceau s'allonge de 4/3 : " + std::to_string(before.seconds) + " s -> " +
                      std::to_string(after.seconds) + " s");
            check(after.onsets == before.onsets,
                  "chaque attaque reste sur son pas : aucun décalage entre les pistes");
        });

    add("renommer",
        [this]
        {
            static_cast<void>(
                bus_.execute(std::make_unique<domain::RenamePattern>(state_.patterns()[1].id, "Refrain")));
            note("le renommage passe par le bus : la boîte de dialogue de la playlist est modale, et une "
                 "vérification "
                 "automatique ne tape pas dedans");
        });

    add("le nouveau nom dans le rack, puis supprimer et rétablir",
        [this]
        {
            // Read a step later: the transport rebuilds its chooser when it
            // hears of the change, and it hears asynchronously.
            auto* transport = panel("transport");
            bool named = false;
            for (auto* child : transport->getChildren())
            {
                if (auto* chooser = dynamic_cast<juce::ComboBox*>(child); chooser != nullptr)
                {
                    for (int index = 0; index < chooser->getNumItems(); ++index)
                        named = named || chooser->getItemText(index) == "Refrain";
                }
            }
            check(named, "« Refrain » dans le sélecteur du transport");

            savedState_ = domain::json::write(state_.toValue());
            static_cast<void>(bus_.execute(std::make_unique<domain::RemovePattern>(state_.patterns()[1].id)));
            check(state_.patterns().size() == 1, "supprimé, avec ses poses");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_, "Ctrl+Z le remet au même endroit");
        });

    // --- the week's additions -------------------------------------------------

    add(
        "Espace lance la lecture",
        [this] { key(juce::KeyPress{juce::KeyPress::spaceKey}); },
        [this] { return clock_.isPlaying(); },
        8000.0);

    add(
        "Espace l'arrête",
        [this]
        {
            check(clock_.isPlaying(), "la lecture tournait");
            key(juce::KeyPress{juce::KeyPress::spaceKey});
        },
        [this] { return !clock_.isPlaying(); },
        8000.0);

    add("le navigateur montre un drumkit",
        [this]
        {
            // A drumkit of two samples, written here so the list needs nothing
            // from the machine it runs on.
            kit_ = folder_.getChildFile("drumkit");
            static_cast<void>(kit_.createDirectory());
            writeHit(kit_.getChildFile("Kick 808.wav"), 0.4);
            writeHit(kit_.getChildFile("Clap.wav"), 1.5);

            samples_.addFolder(kit_);
            auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
            check(browser != nullptr && browser->isShowing(), "la page Navigateur est ouverte");
            if (browser == nullptr)
                return;

            browser->refresh();

            juce::TreeView* tree = nullptr;
            for (auto* child : browser->getChildren())
                tree = tree != nullptr ? tree : dynamic_cast<juce::TreeView*>(child);

            // Found by its path, not by its rank: the browser shows every
            // folder this machine was given, and a run that stopped before
            // its end leaves its own behind.
            juce::TreeViewItem* kitItem = nullptr;
            for (int index = 0; tree != nullptr && tree->getRootItem() != nullptr &&
                                index < tree->getRootItem()->getNumSubItems();
                 ++index)
            {
                auto* item = tree->getRootItem()->getSubItem(index);
                if (item->getUniqueName() == kit_.getFullPathName())
                    kitItem = item;
            }

            check(kitItem != nullptr, "le dossier du drumkit est dans l'arbre");
            if (kitItem != nullptr)
            {
                kitItem->setOpen(true);
                check(kitItem->getNumSubItems() == 2, "ses deux samples y sont");
                check(kitItem->getNumSubItems() == 2 &&
                          kitItem->getSubItem(0)->getDragSourceDescription().toString().startsWith("sample:"),
                      "un sample se glisse par son chemin");
            }
        });

    add("déposer un sample sous les canaux du rack crée un canal sampler",
        [this]
        {
            auto* rack = dynamic_cast<juce::DragAndDropTarget*>(panel("channel_rack"));
            check(rack != nullptr, "le rack accepte un dépôt");
            if (rack == nullptr)
                return;

            const auto tracks = state_.tracks().size();
            savedDepth_ = depth();

            const auto below = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"))
                                   ->channelBounds(static_cast<int>(tracks) + 2)
                                   .getCentre();
            const juce::DragAndDropTarget::SourceDetails details{
                "sample:" + kit_.getChildFile("Kick 808.wav").getFullPathName(), panel("browser"), below};
            check(rack->isInterestedInDragSource(details), "un sample du navigateur l'intéresse");
            rack->itemDropped(details);

            check(state_.tracks().size() == tracks + 1, "un canal de plus");
            check(depth() == savedDepth_ + 1, "une seule entrée d'historique");
            const auto& added = state_.tracks().back();
            check(added.sample.has_value() && added.sample->name == "Kick 808.wav",
                  "le canal joue « Kick 808.wav »");
            check(added.name == "Kick 808", "nommé d'après le sample");
            if (added.sample.has_value())
                note("octets copiés dans le projet : " + std::to_string(added.sample->blob.byteCount) +
                     ", empreinte " + added.sample->blob.digest.substr(0, 12) + "…");
        });

    add("le canal sampler joue ses notes",
        [this]
        {
            // In pattern mode, on the pattern the rack shows.
            press("PAT");
            const auto row = static_cast<int>(state_.tracks().size()) - 1;

            const auto before = listen("20a-avant-sampler", 90.0);
            writeNotes(row, {0.25, 1.25, 2.25, 3.25});
            const auto after = listen("20b-canal-sampler", 90.0);

            bool heard = true;
            for (const auto step : {1, 5, 9, 13})
                heard =
                    heard && std::find(after.onsets.begin(), after.onsets.end(), step) != after.onsets.end();
            check(heard, "le sample s'entend sur les pas 2, 6, 10, 14 qu'on vient d'écrire");
            // The hats on the steps right after a sampler hit sit under its
            // tail and cannot be told apart by level; the ones that follow a
            // silent step can, and they must all still be there.
            bool hatsKept = true;
            for (const auto step : {0, 4, 8, 12})
                hatsKept = hatsKept &&
                           std::find(after.onsets.begin(), after.onsets.end(), step) != after.onsets.end();
            check(hatsKept,
                  "les hats du pattern sont toujours là : le sampler s'ajoute, il ne remplace rien");
            note("attaques avant : " + std::to_string(before.onsets.size()) +
                 ", après : " + std::to_string(after.onsets.size()) +
                 " ; un hat collé derrière un coup du sampler est sous sa queue");

            // Left in PAT on purpose: a sample dropped on the playlist must be
            // heard even when the rack was the last thing played.
        });

    add("déposer un sample sur la playlist pose un clip audio",
        [this]
        {
            auto* playlistPanel = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            check(playlistPanel != nullptr, "la playlist accepte un dépôt");
            if (playlistPanel == nullptr)
                return;

            double end = 0.0;
            for (const auto& placement : state_.arrangement())
                end = std::max(end,
                               placement.startBeats + state_.findPattern(placement.patternId)->lengthBeats);
            audioStart_ = end + 4.0;

            savedDepth_ = depth();
            const juce::DragAndDropTarget::SourceDetails details{
                "sample:" + kit_.getChildFile("Clap.wav").getFullPathName(),
                panel("browser"),
                playlistBeat(0, audioStart_ + 0.5)};
            playlistPanel->itemDropped(details);

            check(state_.audioClips().size() == 1, "un clip audio");
            check(depth() == savedDepth_ + 1, "une seule entrée d'historique : la piste et le clip ensemble");
            check(state_.transport().mode == domain::PlayMode::song,
                  "le dépôt passe en mode chanson : en PAT, le clip serait muet");
            if (!state_.audioClips().empty())
            {
                const auto& clip = state_.audioClips().front();
                check(clip.startBeats == audioStart_,
                      "posé à la mesure sous le pointeur, temps " + std::to_string(clip.startBeats));
                check(std::abs(clip.sample.seconds - 1.5) < 0.01, "il dure son sample : 1,5 s");
            }

            const auto heard = listen("21-clip-audio", 90.0);
            const auto step = static_cast<int>(std::lround(audioStart_ / stepBeats));
            check(std::find(heard.onsets.begin(), heard.onsets.end(), step) != heard.onsets.end(),
                  "le clap s'entend à son temps");
            check(heard.seconds >= audioStart_ * 60.0 / 90.0 + 1.4, "le rendu va jusqu'au bout du clap");
        });

    add("Ctrl + glisser sélectionne une zone",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            // From the empty space above the first lane? The lanes start right
            // under the ruler; the band starts in lane 0 before beat 0 is not
            // possible, so it starts on an empty spot of lane 1 and sweeps up
            // over the first two layings of pattern 1.
            // Both ends in sight before either is aimed at: bringing the
            // second into view must not move the first.
            static_cast<void>(playlistBeat(0, 1.0));
            const auto from = playlistBeat(1, 30.0);
            const auto to = playlistBeat(0, 1.0);
            drag(*playlist, from, to, true);

            check(playlist->selected().size() == 2,
                  "deux blocs pris dans la zone : " + std::to_string(playlist->selected().size()));
        });

    add("Ctrl + clic ajoute un bloc à la sélection, Ctrl + Maj + clic aussi",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            click(*playlist, playlistBeat(0, 32.0 + 2.0), false, false, true);
            check(playlist->selected().size() == 3, "Ctrl + clic : trois blocs sélectionnés");
            click(*playlist, playlistBeat(0, 32.0 + 2.0), false, false, true);
            check(playlist->selected().size() == 2, "un second Ctrl + clic le retire");
            click(*playlist, playlistBeat(0, 32.0 + 2.0), false, true, true);
            check(playlist->selected().size() == 3, "Ctrl + Maj + clic, le geste de la S10, l'ajoute aussi");
            click(*playlist, playlistBeat(0, 32.0 + 2.0), false, true, true);
            check(playlist->selected().size() == 2, "et le retire");
        });

    add("Ctrl+B duplique la sélection juste après elle",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            const auto placements = state_.arrangement().size();
            savedDepth_ = depth();
            static_cast<void>(playlist->keyPressed(juce::KeyPress{'b', juce::ModifierKeys::ctrlModifier, 0}));

            check(state_.arrangement().size() == placements + 2, "deux poses de plus");
            check(depth() == savedDepth_ + 1, "une seule entrée d'historique");

            // The selection covered beats 0 to 32; its copy starts at 32 and 48,
            // on top of the layings already there — a duplicate is a laying.
            int at32 = 0;
            int at48 = 0;
            for (const auto& placement : state_.arrangement())
            {
                at32 += placement.startBeats == 32.0 ? 1 : 0;
                at48 += placement.startBeats == 48.0 ? 1 : 0;
            }
            check(at32 >= 2 && at48 >= 2, "les copies commencent aux temps 32 et 48");
            check(playlist->selected().size() == 2,
                  "la sélection passe aux copies, prête pour un autre Ctrl+B");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(state_.arrangement().size() == placements, "un Ctrl+Z les retire toutes les deux");
        });

    add("Ctrl+C puis Ctrl+V colle à la tête de lecture",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            // The selection: the first laying only.
            click(*playlist, playlistBeat(0, 2.0));
            check(playlist->selected().size() == 1, "un bloc sélectionné au clic");

            static_cast<void>(playlist->keyPressed(juce::KeyPress{'c', juce::ModifierKeys::ctrlModifier, 0}));
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(100.0)));

            const auto placements = state_.arrangement().size();
            static_cast<void>(playlist->keyPressed(juce::KeyPress{'v', juce::ModifierKeys::ctrlModifier, 0}));

            check(state_.arrangement().size() == placements + 1, "une pose de plus");
            const auto pasted =
                std::any_of(state_.arrangement().begin(),
                            state_.arrangement().end(),
                            [](const domain::Placement& placement) { return placement.startBeats == 100.0; });
            check(pasted, "au temps 100, là où est la tête");
        });

    add("Suppr retire la sélection, Ctrl+Z la rend",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            savedState_ = domain::json::write(state_.toValue());
            const auto placements = state_.arrangement().size();

            static_cast<void>(playlist->keyPressed(juce::KeyPress{juce::KeyPress::deleteKey}));
            check(state_.arrangement().size() == placements - 1, "le bloc collé est retiré");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_, "Ctrl+Z le remet, à l'octet près");
        });

    addAuditionSteps();
    addPlaylistViewSteps();
    addPreviewSteps();
    addClipboardSteps();
    addMeterSteps();
    addMixerSteps();
    addTempoSteps();
    addSearchSteps();
    addVelocitySteps();
    addRackSteps();
    addWorkflowSteps();
    addNavigationSteps();
    addExportSteps();
    addAutomationSteps();
    addGenerationSteps();
    addFormSteps();
    addLearningSteps();
    addZoneSteps();
    addCanvasSteps();

    // --- the title bar -----------------------------------------------------------

    add("la barre de titre remplace celle du système",
        [this]
        {
            check(!window_.isUsingNativeTitleBar(), "plus de barre de titre Windows");
            check(titleBar_.isShowing(), "la barre de DAW IA est affichée");
            check(titleBar_.projectName().isNotEmpty(),
                  "elle porte le nom du projet : « " + titleBar_.projectName().toStdString() + " »");

            int buttons = 0;
            bool beatmakerLit = false;
            juce::StringArray labels;
            for (auto* child : titleBar_.getChildren())
            {
                if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr)
                {
                    ++buttons;
                    labels.add(button->getButtonText());
                    beatmakerLit =
                        beatmakerLit || (button->getButtonText() == "Beatmaker" && button->getToggleState());
                }
            }
            check(labels.contains("Fichier"), "le menu Fichier");
            check(beatmakerLit, "le bouton Beatmaker, allumé");
            check(buttons == 1 + 4 + 3, "Fichier, quatre workspaces, réduire, agrandir, fermer");

            bool transportSwitch = false;
            if (auto* transport = panel("transport"); transport != nullptr)
            {
                for (auto* child : transport->getChildren())
                {
                    if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr)
                        transportSwitch = transportSwitch || button->getButtonText() == "Beatmaker";
                }
            }
            check(!transportSwitch, "le transport ne porte plus les workspaces");
        });

    add("Ctrl+S enregistre",
        [this]
        {
            static_cast<void>(shell_.keyPressed(juce::KeyPress{'s', juce::ModifierKeys::ctrlModifier, 0}));
            check(titleBar_.status() == juce::String(u8"enregistré"), "la barre dit « enregistré »");
        });

    add("double-clic sur la barre : agrandir, puis rendre sa taille",
        [this]
        {
            // On the caption, as Windows delivers it (S18 bis): the bar no
            // longer handles the double-click, the system does.
            const auto before = window_.getBounds();
            doubleClickCaption();
            check(window_.isFullScreen(), "la fenêtre est agrandie");
            savedBounds_ = before;
        });

    add("le second double-clic rend la taille d'avant",
        [this]
        {
            doubleClickCaption();
            check(!window_.isFullScreen(), "elle ne l'est plus");
            check(window_.getBounds() == savedBounds_, "et elle a repris sa place");
        });

    add("les workspaces se changent depuis la barre",
        [this]
        {
            juce::TextButton* discovery = nullptr;
            juce::TextButton* beatmaker = nullptr;
            for (auto* child : titleBar_.getChildren())
            {
                if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr)
                {
                    if (button->getButtonText() == juce::String(u8"Découverte"))
                        discovery = button;
                    if (button->getButtonText() == "Beatmaker")
                        beatmaker = button;
                }
            }
            check(discovery != nullptr && beatmaker != nullptr, "les deux boutons existent");
            if (discovery == nullptr || beatmaker == nullptr)
                return;

            discovery->triggerClick();
        });

    add("Découverte est affiché, puis retour au beatmaker",
        [this]
        {
            juce::TextButton* discovery = nullptr;
            juce::TextButton* beatmaker = nullptr;
            for (auto* child : titleBar_.getChildren())
            {
                if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr)
                {
                    if (button->getButtonText() == juce::String(u8"Découverte"))
                        discovery = button;
                    if (button->getButtonText() == "Beatmaker")
                        beatmaker = button;
                }
            }
            if (discovery == nullptr || beatmaker == nullptr)
                return;

            check(discovery->getToggleState() && !beatmaker->getToggleState(), "Découverte est allumé");
            beatmaker->triggerClick();
        });
}

void Verification::buildReopen()
{
    add("réouverture",
        [this]
        {
            const auto kept =
                domain::json::read(folder_.getChildFile("etat.json").loadFileAsString().toStdString());
            check(kept.ok(), "l'état gardé à la fermeture est lisible");
            if (!kept.ok())
                return;

            check(*kept.value().find("state") == state_.toValue(),
                  "même état après réouverture par un autre processus");
            check(kept.value().intAt("undoDepth").value() == static_cast<std::int64_t>(depth()),
                  "même profondeur d'historique : " + std::to_string(depth()));
            check(state_.transport().mode == domain::PlayMode::pattern, "rouvert en mode pattern");
        });

    add("elle sonne pareil",
        [this]
        {
            press("SONG");
            const auto heard = listen("11-rouvert", 90.0);
            check(heard.onsets.size() > 0, "le morceau rouvert sonne");
        });
}

// --verify-canvas: the canvas alone, on whatever project is open.
void Verification::buildCanvas()
{
    addCanvasSteps();
}

// --verify-canvas-charge: the canvas on a loaded project, measured.
void Verification::buildCanvasLoad()
{
    addCanvasLoadSteps();
}

void Verification::buildLegacy()
{
    add("un projet des neuf premières semaines",
        [this]
        {
            check(state_.patterns().size() == 2,
                  "deux patterns : le clip de la S8, le pattern du rack de la S9");
            check(state_.arrangement().size() == 2, "deux poses");
            press("SONG");

            // The S8 clip starts at beat 8 with hits on its first four beats,
            // the S9 pattern at beat 12 with the same: beats 8 to 15.
            const auto heard = listen("12-ancien-projet", 120.0);
            check(std::abs(heard.seconds - 8.0) < 0.2, "il dure jusqu'au temps 16, 8 s");
            check(heard.onsets == std::vector<int>({32, 36, 40, 44, 48, 52, 56, 60}),
                  "une attaque sur chaque temps de 8 à 15 : là où les clips commençaient");
        });
}

// A double-click on the title bar's caption, just right of the File button,
// delivered as Windows delivers it. Outside Windows, the window is maximised
// and restored the way the system would.
void Verification::doubleClickCaption()
{
    auto* peer = window_.getPeer();
    auto* file = button(titleBar_, "Fichier");
    if (peer == nullptr || file == nullptr)
    {
        check(false, "la fenêtre ou le bouton Fichier manque");
        return;
    }

    const auto gap = titleBar_.localPointToGlobal(
        juce::Point<int>{file->getRight() + tokens_.integer("space.sm"), file->getBounds().getCentreY()});
    if (!native::doubleClickCaption(*peer, gap))
        window_.setFullScreen(!window_.isFullScreen());
}

// The File menu and the window, past the dialogs: Windows' file dialog is not
// something a script can fill, so each step hands the function behind it the
// folder a person would have picked. The run ends on a real "Enregistrer
// sous", which closes this process and starts another on the copy.
void Verification::buildFile()
{
    add("Ouvrir refuse un dossier qui n'est pas un projet",
        [this]
        {
            const auto notAProject = folder_.getChildFile("pas-un-projet");
            static_cast<void>(notAProject.createDirectory());
            check(openProjectAt_ && !openProjectAt_(notAProject), "refusé");
            check(lastRefusal_ && lastRefusal_().contains("n'est pas un projet"),
                  "le message : « " + (lastRefusal_ ? lastRefusal_().toStdString() : std::string{}) + " »");
        });

    add("Nouveau et Enregistrer sous refusent un projet qui existe",
        [this]
        {
            const auto existing = folder_.getChildFile("Existant.dawproj");
            static_cast<void>(existing.createDirectory());
            check(newProjectAt_ && !newProjectAt_(existing), "Nouveau refuse : le nom est pris");
            check(lastRefusal_ && lastRefusal_().contains(juce::String(u8"existe déjà")),
                  "le message : « " + (lastRefusal_ ? lastRefusal_().toStdString() : std::string{}) + " »");
            check(saveAsTo_ && !saveAsTo_(existing), "Enregistrer sous refuse : rien n'est écrasé");
            check(existing.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0,
                  "le dossier existant n'a pas été touché");
        });

    // S18 bis: the window is moved by Windows, not by the bar. What the bar
    // does is answer the system's question — what is under this point — and
    // the question is asked here the way Windows asks it.
    add("la barre de titre répond à Windows comme une barre de titre",
        [this]
        {
            auto* peer = window_.getPeer();
            auto* file = button(titleBar_, "Fichier");
            auto* minimise = button(titleBar_, juce::String::fromUTF8("\xe2\x80\x93"));
            auto* maximise = button(titleBar_, juce::String::fromUTF8("\xe2\x96\xa1"));
            auto* close = button(titleBar_, juce::String::fromUTF8("\xc3\x97"));
            if (peer == nullptr || file == nullptr || minimise == nullptr || maximise == nullptr ||
                close == nullptr)
            {
                check(false, "la fenêtre ou un bouton de la barre manque");
                return;
            }

            const auto at = [peer](juce::Component& where, juce::Point<int> local)
            { return native::hitTest(*peer, where.localPointToGlobal(local)); };
            if (at(titleBar_, {}) == "unsupported")
            {
                note("hors Windows : rien à demander au système");
                return;
            }

            // Just right of the File button: the gap before the name, which is
            // caption as the name is.
            const auto gap = juce::Point<int>{file->getRight() + tokens_.integer("space.sm"),
                                              file->getBounds().getCentreY()};
            const auto said = [](const juce::String& kind) { return " (" + kind.toStdString() + ")"; };

            check(at(titleBar_, gap) == "caption", "à côté du nom : la légende" + said(at(titleBar_, gap)));
            const auto centre = [](juce::Component& c) { return c.getLocalBounds().getCentre(); };
            check(at(*minimise, centre(*minimise)) == "minimise",
                  "le bouton réduire" + said(at(*minimise, centre(*minimise))));
            check(at(*maximise, centre(*maximise)) == "maximise",
                  "le bouton agrandir" + said(at(*maximise, centre(*maximise))));
            check(at(*close, centre(*close)) == "close",
                  "le bouton fermer" + said(at(*close, centre(*close))));
            check(at(*file, centre(*file)) == "client",
                  "le menu Fichier reste un bouton" + said(at(*file, centre(*file))));
            check(at(view_, centre(view_)) == "client",
                  "l'espace de travail reste à l'application" + said(at(view_, centre(view_))));
        });

    add("un double-clic sur la légende agrandit, un second rend la taille",
        [this]
        {
            auto* peer = window_.getPeer();
            auto* file = button(titleBar_, "Fichier");
            if (peer == nullptr || file == nullptr)
            {
                check(false, "la fenêtre ou le bouton Fichier manque");
                return;
            }

            const auto gap = [this, file]
            {
                return titleBar_.localPointToGlobal(juce::Point<int>{
                    file->getRight() + tokens_.integer("space.sm"), file->getBounds().getCentreY()});
            };
            savedBounds_ = window_.getBounds();

            if (!native::doubleClickCaption(*peer, gap()))
            {
                note("hors Windows : rien à demander au système");
                return;
            }
            check(window_.isFullScreen(), "agrandie");

            static_cast<void>(native::doubleClickCaption(*peer, gap()));
            check(!window_.isFullScreen(), "le second double-clic lui rend sa taille");
            check(window_.getBounds() == savedBounds_, "et sa place");
        });

    add("Enregistrer sous, la suite",
        [this]
        {
            static_cast<void>(folder_.getChildFile("Copie.dawproj").deleteRecursively());
            note("à la fin de ce rapport : copie vers `Copie.dawproj`, puis ce processus se ferme et un "
                 "autre s'ouvre sur la copie. Ce qu'elle contient se vérifie par --verify-reopen sur elle.");
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::addMeterSteps()
{
    // --- the meters ---------------------------------------------------------
    //
    // What a person does, in the order they do it: play before looking, look
    // while it plays, cut a track in the middle, ask the copilot, stop. No
    // figure below is read from a volume field: every one is measured.

    const auto master = engine::MeterTapPlugin::masterStrip.toStdString();

    // The first bar of the song, looped: a kick on every beat. The pattern
    // itself holds its hits in its first bar only and loops over four, so in
    // PAT a meter reads eight seconds of true silence out of eleven — right,
    // and useless to tell a copilot reading from a copilot guessing.
    add(
        "boucler la première mesure, Espace : les vu-mètres bougent",
        [this]
        {
            press("SONG");
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(true, 0.0, 4.0)));
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
            droppedBefore_ = levels_.meters().droppedBlocks();
            key(juce::KeyPress{juce::KeyPress::spaceKey});
        },
        [this, master] { return clock_.isPlaying() && levelOf(master).peakDb > -60.0f; },
        6000.0);

    add("chaque piste mesure ce qu'elle joue, et seulement ça",
        [this, master]
        {
            const auto levels = levels_.levels();
            const auto onMaster = levelIn(levels, master);
            note("master : crête " + juce::String(onMaster.peakDb, 1).toStdString() + " dBFS, RMS " +
                 juce::String(onMaster.rmsDb, 1).toStdString() + " dBFS");
            check(onMaster.peakDb > -60.0f, "le master mesure la lecture");

            float loudest = engine::StripLevel::floorDb;
            for (const auto& track : state_.tracks())
            {
                const auto level = levelIn(levels, track.id.toString());
                note(track.name + " : crête " + juce::String(level.peakDb, 1).toStdString() + " dBFS");
                if (level.peakDb > loudest)
                {
                    loudest = level.peakDb;
                    loudest_ = track.id.toString();
                }
            }
            check(loudest > -60.0f, "au moins une piste de la mesure bouclée mesure un signal");

            // The clap is laid after the end of the patterns, far outside the
            // loop: its meter must not move.
            if (!state_.audioClips().empty())
            {
                const auto clap = levelIn(levels, state_.audioClips().front().trackId.toString());
                check(clap.peakDb <= engine::StripLevel::floorDb + 1.0f,
                      "la piste du clap, hors de la boucle, reste à -100 dBFS");
            }

            check(onMaster.peakDb >= loudest - 0.1f, "le master n'est jamais sous sa piste la plus forte");
        });

    add(
        "couper la piste la plus forte pendant la lecture",
        [this]
        {
            const auto trackId = domain::TrackId::parse(loudest_);
            if (trackId)
                static_cast<void>(
                    bus_.execute(std::make_unique<domain::SetTrackMuted>(trackId.value(), true)));
        },
        [this] { return levelOf(loudest_).peakDb <= engine::StripLevel::floorDb + 1.0f; },
        2000.0);

    add(
        "la rendre : son vu-mètre repart au coup suivant",
        [this]
        {
            check(levelOf(loudest_).peakDb <= engine::StripLevel::floorDb + 1.0f,
                  "coupée, elle mesure -100 dBFS en moins de 2 s");
            const auto trackId = domain::TrackId::parse(loudest_);
            if (trackId)
                static_cast<void>(
                    bus_.execute(std::make_unique<domain::SetTrackMuted>(trackId.value(), false)));
        },
        [this] { return levelOf(loudest_).peakDb > -60.0f; },
        3000.0);

    add(
        "le copilote lit les vu-mètres",
        [this]
        {
            masterSeen_.clear();
            recordingMaster_ = true;
            levels_.addChangeListener(this);
            savedDepth_ = depth();
            transcriptBefore_ = copilot_.transcript().size();
            copilot_.ask(
                "Mesure le niveau crête du master maintenant et donne-le en dBFS, sans rien modifier.");
        },
        [this]
        {
            return copilot_.transcript().size() > transcriptBefore_ + 1 &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        120000.0);

    add("ce qu'il a lu est ce que le vu-mètre mesure",
        [this, master]
        {
            recordingMaster_ = false;
            levels_.removeChangeListener(this);

            const auto& lines = copilot_.transcript();
            const auto answer = lines.empty() ? std::string{} : lines.back().text;
            note("réponse : « " + answer + " »");
            note(std::to_string(masterSeen_.size()) + " lectures du vu-mètre du master pendant la demande");
            check(depth() == savedDepth_, "aucune entrée d'historique : une lecture ne modifie rien");

            // Every figure the answer gives in dB, against what the master
            // meter read while the copilot was working.
            const auto text = juce::String::fromUTF8(answer.c_str())
                                  .replaceCharacter(',', '.')
                                  .replaceCharacter(static_cast<juce::juce_wchar>(0x2212), '-');
            bool close = false;
            for (int at = text.indexOf("dB"); at > 0; at = text.indexOf(at + 2, "dB"))
            {
                auto start = at;
                while (start > 0 &&
                       (juce::CharacterFunctions::isDigit(text[start - 1]) || text[start - 1] == '.' ||
                        text[start - 1] == '-' || text[start - 1] == ' '))
                    --start;
                const auto figure = text.substring(start, at).trim();
                if (!figure.containsAnyOf("0123456789"))
                    continue;
                // The copilot is handed the meter rounded to a tenth of a dB.
                for (const auto seen : masterSeen_)
                    close = close || std::abs(figure.getFloatValue() - seen) <= 0.06f;
            }

            float quietest = 0.0f;
            float loudest = engine::StripLevel::floorDb;
            for (const auto seen : masterSeen_)
            {
                quietest = std::min(quietest, seen);
                loudest = std::max(loudest, seen);
            }
            check(loudest > -60.0f, "le master sonnait pendant la demande");
            check(close,
                  "la réponse donne, au dixième de dB, une crête que le vu-mètre du master a lue pendant la "
                  "demande (entre " +
                      juce::String(quietest, 1).toStdString() + " et " +
                      juce::String(loudest, 1).toStdString() + " dBFS)");
        });

    add(
        "Espace arrête : tout retombe à -100",
        [this]
        {
            check(levels_.meters().droppedBlocks() == droppedBefore_,
                  "aucun bloc perdu pendant la lecture en direct");
            key(juce::KeyPress{juce::KeyPress::spaceKey});
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(false, 0.0, 0.0)));
        },
        [this, master]
        { return !clock_.isPlaying() && levelOf(master).peakDb <= engine::StripLevel::floorDb + 1.0f; },
        2000.0);

    add("au rendu, le master mesure ce que le fichier contient",
        [this, master]
        {
            levels_.meters().resetTotals();
            const auto file = folder_.getChildFile("22-vu-metres.wav");
            if (!engine::renderAsPlayed(edit_, file))
            {
                check(false, "le rendu hors ligne a échoué");
                return;
            }

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
            if (reader == nullptr || reader->lengthInSamples <= 0)
            {
                check(false, "le rendu est vide");
                return;
            }

            juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels),
                                            static_cast<int>(reader->lengthInSamples)};
            reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);

            double peak = 0.0;
            double sum = 0.0;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto* samples = buffer.getReadPointer(channel);
                for (int index = 0; index < buffer.getNumSamples(); ++index)
                {
                    peak = std::max(peak, static_cast<double>(std::abs(samples[index])));
                    sum += static_cast<double>(samples[index]) * samples[index];
                }
            }
            const auto seconds = static_cast<double>(buffer.getNumSamples()) / reader->sampleRate;
            const auto filePeakDb = static_cast<float>(20.0 * std::log10(std::max(peak, 1e-9)));

            // An RMS is an energy over a duration, and the render processes a
            // few silent blocks past the end of the file: energies compare.
            const auto fileEnergyDb = static_cast<float>(
                10.0 * std::log10(std::max(sum / buffer.getNumChannels() / reader->sampleRate, 1e-18)));

            const auto totals = levels_.meters().totals();
            const auto onMaster = levelIn(totals, master);
            const auto meterEnergyDb =
                onMaster.rmsDb + static_cast<float>(10.0 * std::log10(std::max(onMaster.seconds, 1e-9)));

            note("fichier : " + juce::String(seconds, 2).toStdString() + " s, crête " +
                 juce::String(filePeakDb, 2).toStdString() +
                 " dBFS ; vu-mètre du master : " + juce::String(onMaster.seconds, 2).toStdString() +
                 " s, crête " + juce::String(onMaster.peakDb, 2).toStdString() + " dBFS");
            check(std::abs(onMaster.peakDb - filePeakDb) < 0.1f, "même crête, à 0,1 dB près");
            check(std::abs(meterEnergyDb - fileEnergyDb) < 0.1f, "même énergie, à 0,1 dB près");

            if (!state_.audioClips().empty())
            {
                const auto clap = levelIn(totals, state_.audioClips().front().trackId.toString());
                check(clap.peakDb > -60.0f,
                      "en SONG, la piste du clap mesure son clip : sa piste compagnon a sa prise");
            }
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::addPlaylistViewSteps()
{
    // --- the playlist, longer than the screen -----------------------------------
    //
    // The debt of S10: the whole timeline was squeezed into the width, and a
    // song past sixty bars became slivers. What a person does with a long
    // song, in their order: scroll to the end to lay something there, zoom in
    // to look, zoom out too far, play and expect the view to follow, change
    // their mind with Ctrl+Z while the view is at the end.

    constexpr float notch = 0.25f;

    add("un morceau de cent mesures : on défile pour poser au bout",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            farBeats_ = 99.0 * beatsPerBar; // bar 100
            const auto before = state_.arrangement().size();
            const auto at = playlistBeat(0, farBeats_ + 0.5);
            check(playlist->timelineArea().contains(at), "la mesure 100 est amenée à l'écran à la molette");
            click(*playlist, at);

            check(state_.arrangement().size() == before + 1, "une pose de plus");
            const auto laid = std::any_of(state_.arrangement().begin(),
                                          state_.arrangement().end(),
                                          [this](const domain::Placement& placement)
                                          { return placement.startBeats == farBeats_; });
            check(laid, "à la mesure 100");

            const auto floor = tokens_.integer("metric.playlist.beatWidthMin");
            note("largeur d'un temps : " + juce::String(playlist->beatWidth(), 1).toStdString() +
                 " px, premier temps visible : " + juce::String(playlist->firstBeat(), 1).toStdString());
            check(playlist->beatWidth() >= floor,
                  "un temps n'est jamais plus étroit que " + std::to_string(floor) +
                      " px : " + std::to_string(floor * beatsPerBar) + " px par mesure au moins");
        });

    add("Ctrl + molette zoome autour du pointeur",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            const auto area = playlist->timelineArea();
            const auto x = area.getX() + area.getWidth() / 3;
            const auto beatAt = [playlist, area](int px)
            { return playlist->firstBeat() + static_cast<double>(px - area.getX()) / playlist->beatWidth(); };

            const auto width = playlist->beatWidth();
            const auto anchored = beatAt(x);
            for (int turn = 0; turn < 4; ++turn)
                wheel(*playlist, {x, area.getCentreY()}, notch, false, true);

            note("zoom : " + juce::String(width, 1).toStdString() + " -> " +
                 juce::String(playlist->beatWidth(), 1).toStdString() + " px par temps");
            check(std::abs(playlist->beatWidth() - width * 2.0) < 0.01, "quatre crans doublent la largeur");
            check(std::abs(beatAt(x) - anchored) <= 1.0 / playlist->beatWidth(),
                  "le temps sous le pointeur y reste, au pixel près");
        });

    add("dézoomer trop loin s'arrête à la largeur lisible",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            const auto area = playlist->timelineArea();
            for (int turn = 0; turn < 60; ++turn)
                wheel(*playlist, area.getCentre(), -notch, false, true);

            const auto floor = static_cast<double>(tokens_.integer("metric.playlist.beatWidthMin"));
            check(std::abs(playlist->beatWidth() - floor) < 0.01,
                  "soixante crans en arrière : " + juce::String(playlist->beatWidth(), 1).toStdString() +
                      " px par temps, le plancher");
        });

    add(
        "zoomé au début, Espace en SONG : la vue suit la tête de lecture",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            const auto area = playlist->timelineArea();
            for (int turn = 0; turn < 12; ++turn)
                wheel(*playlist, {area.getX(), area.getCentreY()}, notch, false, true);
            for (int turn = 0; turn < 2000 && playlist->firstBeat() > 0.0; ++turn)
                wheel(*playlist, area.getCentre(), notch, true);

            check(playlist->firstBeat() == 0.0, "la vue est au début");

            press("SONG");
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
            key(juce::KeyPress{juce::KeyPress::spaceKey});
        },
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            return playlist != nullptr && playlist->firstBeat() > 0.0;
        },
        15000.0);

    add("la tête de lecture reste dans la vue",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            const auto beats = clock_.positionBeats();
            const auto first = playlist->firstBeat();
            const auto shown =
                static_cast<double>(playlist->timelineArea().getWidth()) / playlist->beatWidth();
            note("tête au temps " + juce::String(beats, 2).toStdString() + ", vue de " +
                 juce::String(first, 1).toStdString() + " à " + juce::String(first + shown, 1).toStdString());
            check(beats >= first && beats <= first + shown, "la page a tourné, la tête est à l'écran");
            check(std::fmod(first, static_cast<double>(beatsPerBar)) == 0.0,
                  "la page commence sur une mesure");

            key(juce::KeyPress{juce::KeyPress::spaceKey});
        });

    add("au bout de la chanson, Ctrl+Z retire la pose lointaine ; la molette ramène au début",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            static_cast<void>(playlistBeat(0, farBeats_ + 8.0));
            check(playlist->firstBeat() > farBeats_ - 64.0,
                  "la vue est au bout, sur la pose de la mesure 100");
            const auto viewed = playlist->firstBeat();

            // What the Ctrl+Z is about to undo, named in the report: if it is
            // not the far placement, the line says what slipped in between.
            if (history_.cursor() > 0)
                note("dernière entrée avant Ctrl+Z : « " +
                     std::string{history_.entries()[history_.cursor() - 1].label()} + " »");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            const auto gone = std::none_of(state_.arrangement().begin(),
                                           state_.arrangement().end(),
                                           [this](const domain::Placement& placement)
                                           { return placement.startBeats == farBeats_; });
            check(gone, "la pose de la mesure 100 est retirée");

            // The view stays where the hand left it, the way FL and Ableton
            // leave it: an undo does not scroll. The song is one wheel away.
            check(playlist->firstBeat() == viewed, "la vue reste où elle était");
            const auto back = playlistBeat(0, 2.0);
            check(playlist->timelineArea().contains(back), "la molette ramène au début de la chanson");
            check(playlist->firstBeat() <= 2.0, "la vue montre de nouveau le début");
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::writeSong(const juce::File& file, double seconds)
{
    // Long, stereo, and with a shape an eye can check: four swells of a tone,
    // louder each time, the right channel a little quieter than the left.
    constexpr double rate = 44100.0;
    const auto length = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer{2, length};
    for (int index = 0; index < length; ++index)
    {
        const auto t = static_cast<double>(index) / rate;
        const auto swell = static_cast<float>(std::abs(std::sin(t / seconds * 4.0 * 3.14159265358979)));
        const auto loudness = 0.4f + 0.2f * static_cast<float>(std::floor(t / seconds * 4.0));
        const auto tone = static_cast<float>(std::sin(t * 2.0 * 3.14159265358979 * 110.0));
        buffer.setSample(0, index, swell * loudness * tone);
        buffer.setSample(1, index, 0.7f * swell * loudness * tone);
    }

    static_cast<void>(file.deleteFile());
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(16));
    if (writer != nullptr)
        static_cast<void>(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}

void Verification::addPreviewSteps()
{
    // --- what the blocks show -----------------------------------------------------
    //
    // Measured, not only looked at: how many previews were built, and whether
    // the message thread kept ticking while a long sample was measured. The
    // project notifies the panels asynchronously, so every count is read one
    // step after the gesture that should have moved it.

    add("déplacer un bloc",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            static_cast<void>(playlistBeat(0, 0.5));
            previewBuilds_ = playlist->previewBuilds();
            note("aperçus construits jusqu'ici : " + std::to_string(previewBuilds_));

            // The last laying of pattern 1, a bar to the right.
            const auto from = playlistBeat(0, 112.5);
            drag(*playlist, from, from.translated(static_cast<int>(playlist->beatWidth() * beatsPerBar), 0));
        });

    add("aucun aperçu reconstruit ; Ctrl+Z",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            check(playlist->previewBuilds() == previewBuilds_, "un bloc déplacé : aucun aperçu reconstruit");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
        });

    add("toujours aucun ; une note de plus dans le pattern 1",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            check(playlist->previewBuilds() == previewBuilds_, "le Ctrl+Z du déplacement non plus");

            selection_.selectPattern(state_.patterns().front().id);
            savedDepth_ = depth();
            writeNotes(1, {1.75});
            check(depth() == savedDepth_ + 1, "la note est écrite : une entrée d'historique");
            static_cast<void>(playlistBeat(0, 0.5));
        });

    add("un aperçu reconstruit, pour les huit poses ; Ctrl+Z",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            note("aperçus construits : " + std::to_string(playlist->previewBuilds()));
            check(playlist->previewBuilds() == previewBuilds_ + 1,
                  "une note ajoutée au pattern 1 : exactement un aperçu reconstruit, pour ses huit poses");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
        });

    add("le Ctrl+Z reconstruit l'aperçu une fois",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            check(playlist->previewBuilds() == previewBuilds_ + 2, "une construction de plus, pas davantage");
        });

    add(
        "mesurer un sample de deux minutes : le thread message continue",
        [this]
        {
            // The measurement alone, before any drop: the engine laying a clip
            // of two minutes is another cost, noted in the next steps.
            const auto song = kit_.getChildFile("Morceau.wav");
            writeSong(song, 120.0);

            const auto started = juce::Time::getMillisecondCounterHiRes();
            const auto imported = samples_.import(song);
            note("copie de 21 Mo dans le projet : " +
                 juce::String(juce::Time::getMillisecondCounterHiRes() - started, 0).toStdString() + " ms");
            if (!imported)
            {
                check(false, "le sample est importé");
                return;
            }

            measuredBefore_ = samples_.waveformsMeasured();
            measuringSince_ = juce::Time::getMillisecondCounterHiRes();
            check(samples_.waveform(imported.value()) == nullptr,
                  "la première demande rend la main tout de suite, sans forme d'onde");
            note("durée de la demande : " +
                 juce::String(juce::Time::getMillisecondCounterHiRes() - measuringSince_, 1).toStdString() +
                 " ms");

            longestTickMs_ = 0.0;
            lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
            watchTicks_ = true;
        },
        [this]
        {
            // Watching stops the moment the waveform is back: what follows is
            // the verification taking its own snapshot, not the application.
            const auto done = samples_.waveformsMeasured() > measuredBefore_;
            if (done)
                watchTicks_ = false;
            return done;
        },
        30000.0);

    add(
        "aucun gel pendant la mesure ; déposer un autre long sample sur la playlist",
        [this]
        {
            watchTicks_ = false;
            note("mesure faite en " +
                 juce::String(juce::Time::getMillisecondCounterHiRes() - measuringSince_, 0).toStdString() +
                 " ms ; plus long écart entre deux tics : " + juce::String(longestTickMs_, 0).toStdString() +
                 " ms (un tic toutes les 120 ms)");
            check(longestTickMs_ < 400.0, "aucun gel : jamais plus de 400 ms sans tic");
            check(samples_.waveformsMeasured() == measuredBefore_ + 1, "une mesure");

            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            // Another two minutes, another digest: this one is dropped the way a
            // person drops it, and measured after the drop.
            const auto other = kit_.getChildFile("Morceau 2.wav");
            writeSong(other, 121.0);
            savedDepth_ = depth();
            measuredBefore_ = samples_.waveformsMeasured();

            const auto at = playlistBeat(0, audioStart_ + 16.5);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            playlist->itemDropped(juce::DragAndDropTarget::SourceDetails{
                "sample:" + other.getFullPathName(), panel("browser"), at});
            note("dépôt, copie et pose du clip par le moteur comprises : " +
                 juce::String(juce::Time::getMillisecondCounterHiRes() - started, 0).toStdString() + " ms");

            check(depth() == savedDepth_ + 1, "un dépôt, une entrée d'historique");
            if (!state_.audioClips().empty())
                check(samples_.waveform(state_.audioClips().back().sample) == nullptr,
                      "juste après le dépôt, la forme d'onde est en cours de mesure : le bloc est vide");
            playlist->repaint();

            longestTickMs_ = 0.0;
            lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
            watchTicks_ = true;
        },
        [this]
        {
            // Watching stops the moment the waveform is back: what follows is
            // the verification taking its own snapshot, not the application.
            const auto done = samples_.waveformsMeasured() > measuredBefore_;
            if (done)
                watchTicks_ = false;
            return done;
        },
        30000.0);

    add("le bloc s'est rempli",
        [this]
        {
            watchTicks_ = false;
            note("après le dépôt, plus long écart entre deux tics : " +
                 juce::String(longestTickMs_, 0).toStdString() +
                 " ms — le moteur reconstruit son graphe pour le nouveau clip sur le thread message, comme à "
                 "chaque dépôt depuis la S10 ; la mesure seule est à l'étape d'avant");
            check(samples_.waveformsMeasured() == measuredBefore_ + 1, "une mesure pour ce sample");

            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr || state_.audioClips().empty())
                return;

            const auto& clip = state_.audioClips().back();
            const auto peaks = samples_.waveform(clip.sample);
            check(peaks != nullptr && std::abs(peaks->seconds - 121.0) < 0.01,
                  "la forme d'onde couvre 121 s");

            // For the eye: zoomed out to the readable floor, the start of the
            // clip on the left, so that its first swells are in sight.
            const auto lane = static_cast<int>(state_.patterns().size()) + 1;
            const auto at = playlistBeat(lane, clip.startBeats + 1.0);
            for (int turn = 0; turn < 60; ++turn)
                wheel(*playlist, at, -0.25f, false, true);
            static_cast<void>(playlistBeat(lane, clip.startBeats + 1.0));
            for (int turn = 0; turn < 40 && playlist->firstBeat() + 1.0 < clip.startBeats; ++turn)
                wheel(*playlist, at, -0.25f, true);
        });

    add("neuf dépôts de plus du même sample",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            const auto other = kit_.getChildFile("Morceau 2.wav");
            for (int drop = 0; drop < 9; ++drop)
            {
                const auto at = playlistBeat(0, audioStart_ + 16.5);
                playlist->itemDropped(juce::DragAndDropTarget::SourceDetails{
                    "sample:" + other.getFullPathName(), panel("browser"), at});
            }
            playlist->repaint();
        });

    add("dix clips, une seule mesure ; dix Ctrl+Z",
        [this]
        {
            check(samples_.waveformsMeasured() == measuredBefore_ + 1,
                  "toujours une seule mesure pour dix clips du même sample (" +
                      std::to_string(samples_.waveformsMeasured() - measuredBefore_) + ")");

            for (int undo = 0; undo < 10; ++undo)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(depth() == savedDepth_, "les dix dépôts défaits");
            check(state_.audioClips().size() == 1, "il reste le clap");
        });
}

} // namespace daw::app

namespace daw::app
{

namespace
{

template <typename Type>
Type* childOfType(juce::Component& root, int rank = 0)
{
    for (auto* child : root.getChildren())
    {
        if (auto* found = dynamic_cast<Type*>(child); found != nullptr)
        {
            if (rank == 0)
                return found;
            --rank;
        }
    }
    return nullptr;
}

} // namespace

juce::Component* Verification::mixerStrip(const domain::TrackId& id) const
{
    auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
    if (mixer == nullptr)
        return nullptr;

    // Channels, then buses, in the project's order; the master last.
    std::vector<domain::TrackId> ids;
    for (const auto& track : state_.tracks())
        ids.push_back(track.id);
    for (const auto& bus : state_.buses())
        ids.push_back(bus.id);
    ids.push_back(domain::ProjectState::masterTrackId());

    const auto strips = mixer->strips();
    const auto found = std::find(ids.begin(), ids.end(), id);
    const auto rank = static_cast<std::size_t>(std::distance(ids.begin(), found));
    return found != ids.end() && rank < strips.size() ? strips[rank] : nullptr;
}

double Verification::renderedMasterPeakDb(const std::string& name)
{
    const auto file = folder_.getChildFile(name + ".wav");
    if (!engine::renderAsPlayed(edit_, file))
        return engine::StripLevel::floorDb;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return engine::StripLevel::floorDb;

    juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples)};
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    const auto peak = buffer.getMagnitude(0, buffer.getNumSamples());
    return peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak)) : engine::StripLevel::floorDb;
}

void Verification::addMixerSteps()
{
    // --- the mixer ------------------------------------------------------------------
    //
    // A person opens it with F10, makes a bus, sends the kick into it, pulls
    // the bus down, solos the hat while the song plays, asks the AI to mix,
    // then takes everything back with Ctrl+Z. Every sound claim is measured on
    // a render or on the meters.

    add("F10 ouvre le mixer : une tranche par piste, puis le master",
        [this]
        {
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();
            key(juce::KeyPress{juce::KeyPress::F10Key});
        });

    add("la page Mixer est ouverte",
        [this]
        {
            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            check(mixer != nullptr && mixer->isShowing(), "la page Mixer est à l'écran");
            if (mixer == nullptr)
                return;
            check(mixer->strips().size() == state_.tracks().size() + state_.buses().size() + 1,
                  std::to_string(mixer->strips().size()) +
                      " tranches : " + std::to_string(state_.tracks().size()) + " pistes, " +
                      std::to_string(state_.buses().size()) + " bus, le master");
        });

    add("« + Bus » crée un bus, une entrée d'historique",
        [this]
        {
            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            auto* add = mixer != nullptr ? button(*mixer, "+ Bus") : nullptr;
            if (add == nullptr)
            {
                check(false, "le bouton « + Bus »");
                return;
            }
            click(*add, add->getLocalBounds().getCentre());
            check(state_.buses().size() == 1, "un bus");
            check(depth() == savedDepth_ + 1, "une entrée d'historique");
        });

    add("envoyer le Kick dans le bus, baisser le bus : le rendu baisse de ce qu'il faut",
        [this]
        {
            if (state_.buses().empty())
                return;
            const auto busId = state_.buses().front().id;
            const auto kickId = state_.tracks().front().id;

            // Everything else muted, so that the file holds the kick alone.
            std::vector<std::unique_ptr<domain::Command>> silence;
            for (const auto& track : state_.tracks())
            {
                if (track.id != kickId)
                    silence.push_back(std::make_unique<domain::SetTrackMuted>(track.id, true));
            }
            static_cast<void>(
                bus_.executeGroup(std::move(silence), domain::GroupOptions{"verif : kick seul", {}}));
            press("PAT");
            const auto direct = renderedMasterPeakDb("50-kick-direct");

            auto* kickStrip = mixerStrip(kickId);
            auto* output = kickStrip != nullptr ? childOfType<juce::ComboBox>(*kickStrip, 0) : nullptr;
            check(output != nullptr, "la tranche du Kick a une sortie");
            if (output == nullptr)
                return;
            output->setSelectedItemIndex(1, juce::sendNotificationSync);
            check(state_.findTrack(kickId)->output == busId, "le Kick sort dans le bus");

            // The bus's fader, dragged down by the hand.
            auto* busStrip = mixerStrip(busId);
            auto* fader = busStrip != nullptr ? childOfType<juce::Slider>(*busStrip, 0) : nullptr;
            if (fader == nullptr)
            {
                check(false, "la tranche du bus a un fader");
                return;
            }
            const auto before = depth();
            const auto top = fader->getLocalBounds().getCentre();
            drag(*fader, top, top.translated(0, fader->getHeight() / 5));
            const auto busDb = state_.findStrip(busId)->volumeDb;
            check(depth() == before + 1, "un glissé du fader, une entrée d'historique");

            const auto routed = renderedMasterPeakDb("51-kick-par-le-bus");
            note("Kick direct : " + juce::String(direct, 2).toStdString() + " dBFS ; par le bus à " +
                 juce::String(busDb, 2).toStdString() + " dB : " + juce::String(routed, 2).toStdString() +
                 " dBFS");
            check(busDb < -0.5, "le fader du bus est descendu");
            check(std::abs((routed - direct) - busDb) < 0.1,
                  "le rendu baisse du niveau du fader du bus, à 0,1 dB près");
        });

    add(
        "en lecture, S sur le Hat : seul le Hat s'entend",
        [this]
        {
            // Everything back, then the song plays, then the solo.
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0}); // fader
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0}); // output
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0}); // the kick alone

            press("SONG");
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(true, 0.0, 4.0)));
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
            key(juce::KeyPress{juce::KeyPress::spaceKey});

            const auto hatId = state_.tracks()[1].id;
            auto* hatStrip = mixerStrip(hatId);
            auto* solo = hatStrip != nullptr ? button(*hatStrip, "S") : nullptr;
            if (solo == nullptr)
            {
                check(false, "la tranche du Hat a un bouton S");
                return;
            }
            click(*solo, solo->getLocalBounds().getCentre());
            check(state_.findTrack(hatId)->soloed, "le Hat est en solo, dans le projet");
            check(!state_.isAudible(state_.tracks().front().id), "le Kick n'est plus entendu");
        },
        [this] {
            return levelOf(state_.tracks().front().id.toString()).peakDb <=
                   engine::StripLevel::floorDb + 1.0f;
        },
        3000.0);

    add(
        "le vu-mètre du Kick est tombé ; S de nouveau, il repart",
        [this]
        {
            check(levelOf(state_.tracks().front().id.toString()).peakDb <= engine::StripLevel::floorDb + 1.0f,
                  "le Kick mesure -100 dBFS pendant le solo du Hat");
            auto* hatStrip = mixerStrip(state_.tracks()[1].id);
            if (auto* solo = hatStrip != nullptr ? button(*hatStrip, "S") : nullptr; solo != nullptr)
                click(*solo, solo->getLocalBounds().getCentre());
        },
        [this] { return levelOf(state_.tracks().front().id.toString()).peakDb > -60.0f; },
        3000.0);

    add("« Mixer par l'IA » : aucun modèle appelé, la liste de ce qui manque",
        [this]
        {
            key(juce::KeyPress{juce::KeyPress::spaceKey});
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(false, 0.0, 0.0)));

            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            auto* ask = mixer != nullptr ? button(*mixer, juce::String::fromUTF8("Mixer par l'IA")) : nullptr;
            if (ask == nullptr)
            {
                check(false, "le bouton « Mixer par l'IA »");
                return;
            }

            transcriptBefore_ = copilot_.transcript().size();
            click(*ask, ask->getLocalBounds().getCentre());
            const auto report = mixer->readinessReport();
            note("rapport :\n\n```\n" + report.toStdString() + "```");
            check(copilot_.transcript().size() == transcriptBefore_, "le copilote n'a rien reçu");
            check(!mixer->strips().empty() && !mixer->strips().back()->isVisible(),
                  "le rapport prend la place des tranches");
            check(report.contains("besoins sur"), "un compte des besoins couverts");
            check(report.contains("mix.measure") && report.contains("plugin.parameters"),
                  "les manques y sont, nommés : mesure sur un passage, paramètres des effets");
        });

    add("tout défaire au Ctrl+Z, dans l'ordre inverse",
        [this]
        {
            // The same button puts the strips back.
            if (auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer")); mixer != nullptr)
            {
                if (auto* ask = button(*mixer, juce::String::fromUTF8("Mixer par l'IA")); ask != nullptr)
                    click(*ask, ask->getLocalBounds().getCentre());
                check(!mixer->strips().empty() && mixer->strips().back()->isVisible(),
                      "le même bouton rend les tranches");
            }

            // The solo toggled twice, then the bus: three entries.
            for (int undo = 0; undo < 3; ++undo)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(depth() == savedDepth_, "l'historique est revenu à son point de départ");
            check(domain::json::write(state_.toValue()) == savedState_,
                  "le projet d'avant le mixer, à l'octet près");
            key(juce::KeyPress{juce::KeyPress::F10Key});
        });
}

} // namespace daw::app

namespace daw::app
{

juce::Point<int> Verification::rackChannel(int row) const
{
    auto* rack = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"));
    return rack != nullptr ? rack->channelBounds(row).getCentre() : juce::Point<int>{};
}

void Verification::addClipboardSteps()
{
    // --- the clipboard, in the piano roll and in the rack ---------------------------
    //
    // Ctrl + click to pick, Ctrl+C, change of pattern, Ctrl+V; what was copied
    // in the piano roll pasted in the rack; Ctrl+B again and again until the
    // pattern has to grow; and all of it undone. A paste of N notes is one
    // entry and one Ctrl+Z.

    add("dans le piano-roll, Ctrl + clic prend deux kicks, puis en rend un, puis le reprend",
        [this]
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            press("PAT");
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            selection_.selectPattern(state_.patterns().front().id);
            auto* rack = panel("channel_rack");
            if (rack != nullptr)
                click(*rack, rackChannel(0));

            key(juce::KeyPress{juce::KeyPress::F7Key});
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            const auto* row = state_.patterns().front().findClipForTrack(state_.tracks().front().id);
            check(roll != nullptr && row != nullptr && row->notes.size() >= 3,
                  "le piano-roll montre les kicks");
            if (roll == nullptr || row == nullptr || row->notes.size() < 3)
                return;

            const auto first = roll->noteBounds(row->notes[0]).getCentre();
            const auto third = roll->noteBounds(row->notes[2]).getCentre();
            click(*roll, first, false, false, true);
            click(*roll, third, false, false, true);
            check(roll->picked().size() == 2, "deux notes prises");
            click(*roll, first, false, false, true);
            check(roll->picked().size() == 1, "un second Ctrl + clic en rend une");
            click(*roll, first, false, false, true);
            check(roll->picked().size() == 2, "et un troisième la reprend");

            static_cast<void>(roll->keyPressed(juce::KeyPress{'c', juce::ModifierKeys::ctrlModifier, 0}));
            check(depth() == savedDepth_, "copier n'écrit rien");
        });

    add("autre pattern, Ctrl+V : les deux kicks à la tête de lecture, une entrée",
        [this]
        {
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            if (roll == nullptr || state_.patterns().size() < 2)
                return;

            const auto second = state_.patterns()[1].id;
            selection_.selectPattern(second);
            const auto kickId = state_.tracks().front().id;
            const auto before = state_.findPattern(second)->findClipForTrack(kickId);
            const auto had = before != nullptr ? before->notes.size() : 0;

            static_cast<void>(roll->keyPressed(juce::KeyPress{'v', juce::ModifierKeys::ctrlModifier, 0}));
            const auto* after = state_.findPattern(second)->findClipForTrack(kickId);
            check(after != nullptr && after->notes.size() == had + 2,
                  "deux notes dans le pattern 2, sur la ligne du Kick ouverte au besoin");
            check(depth() == savedDepth_ + 1, "un seul Ctrl+V, une seule entrée d'historique");
            if (after != nullptr && after->notes.size() >= 2)
                note("collées aux temps " +
                     juce::String(after->notes[after->notes.size() - 2].startBeats, 2).toStdString() +
                     " et " + juce::String(after->notes.back().startBeats, 2).toStdString() +
                     " : la tête est au temps 0, l'écart d'origine est gardé");
        });

    add("Ctrl+B quatre fois : les copies s'enchaînent, le pattern s'allonge à la cinquième mesure",
        [this]
        {
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            if (roll == nullptr)
                return;

            const auto second = state_.patterns()[1].id;
            const auto length = state_.findPattern(second)->lengthBeats;
            for (int press = 0; press < 4; ++press)
                static_cast<void>(roll->keyPressed(juce::KeyPress{'b', juce::ModifierKeys::ctrlModifier, 0}));

            const auto* pattern = state_.findPattern(second);
            note("longueur du pattern 2 : " + juce::String(length, 0).toStdString() + " -> " +
                 juce::String(pattern->lengthBeats, 0).toStdString() + " temps");
            check(depth() == savedDepth_ + 5, "quatre Ctrl+B, quatre entrées");
            check(pattern->lengthBeats > length,
                  "le dernier Ctrl+B a allongé le pattern pour tenir sa copie");
            check(std::fmod(pattern->lengthBeats, 4.0) == 0.0, "à la mesure");
            check(roll->picked().size() == 2, "la sélection suit les copies");

            for (int undo = 0; undo < 4; ++undo)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(state_.findPattern(second)->lengthBeats == length, "quatre Ctrl+Z : la longueur revient");
        });

    add("tout défaire : le projet d'avant le presse-papiers, à l'octet près",
        [this]
        {
            for (int undo = 0; undo < 20 && depth() > savedDepth_; ++undo)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_, "à l'octet près");
            key(juce::KeyPress{juce::KeyPress::F7Key});
            // Back on the pattern the clipboard started from, which the steps
            // after this one play in pattern mode.
            selection_.selectPattern(state_.patterns().front().id);
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::clickBrowserSample(const juce::String& name)
{
    auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
    if (browser == nullptr)
        return;

    juce::TreeView* tree = nullptr;
    for (auto* child : browser->getChildren())
        tree = tree != nullptr ? tree : dynamic_cast<juce::TreeView*>(child);
    if (tree == nullptr || tree->getRootItem() == nullptr)
        return;

    for (int folder = 0; folder < tree->getRootItem()->getNumSubItems(); ++folder)
    {
        auto* kit = tree->getRootItem()->getSubItem(folder);
        if (kit->getUniqueName() != kit_.getFullPathName())
            continue;

        kit->setOpen(true);
        for (int index = 0; index < kit->getNumSubItems(); ++index)
        {
            auto* item = kit->getSubItem(index);
            if (!item->getUniqueName().endsWith(name))
                continue;

            // The click a tree item receives, left button, on the item.
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent event{source,
                                         {},
                                         juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier},
                                         juce::MouseInputSource::defaultPressure,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         tree,
                                         tree,
                                         now,
                                         {},
                                         now,
                                         1,
                                         false};
            item->setSelected(true, true);
            item->itemClicked(event);
            return;
        }
    }
}

void Verification::addAuditionSteps()
{
    // --- listening in the browser ---------------------------------------------------
    //
    // A click on a sample plays it, nothing enters the project. What is heard
    // is measured where it leaves for the speakers.

    add(
        "un clic sur « Kick 808.wav » dans le navigateur le fait entendre",
        [this]
        {
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();
            clickBrowserSample("Kick 808.wav");
            check(samples_.auditioned().getFileName() == "Kick 808.wav", "le sample est en écoute");
        },
        [this] { return samples_.auditionPeakDb() > -60.0f; },
        3000.0);

    add("le projet n'a pas bougé ; un clic sur « Clap.wav » remplace le premier",
        [this]
        {
            check(samples_.auditionPeakDb() > -60.0f,
                  "la sortie de l'écoute mesure " + juce::String(samples_.auditionPeakDb(), 1).toStdString() +
                      " dBFS");
            check(depth() == savedDepth_, "aucune entrée d'historique");
            check(domain::json::write(state_.toValue()) == savedState_,
                  "le projet est identique, à l'octet près");
            clickBrowserSample("Clap.wav");
            check(samples_.auditioned().getFileName() == "Clap.wav", "le Clap remplace le Kick");
        });

    add(
        "pendant que la chanson joue, l'écoute s'y ajoute",
        [this]
        {
            press("SONG");
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
            key(juce::KeyPress{juce::KeyPress::spaceKey});
            clickBrowserSample("Kick 808.wav");
        },
        [this] { return clock_.isPlaying() && samples_.auditionPeakDb() > -60.0f; },
        3000.0);

    add("arrêter : la lecture et l'écoute",
        [this]
        {
            check(clock_.isPlaying() && samples_.auditionPeakDb() > -60.0f,
                  "la chanson et l'écoute sonnent ensemble");
            key(juce::KeyPress{juce::KeyPress::spaceKey});
            samples_.stopAudition();
            check(samples_.auditioned() == juce::File{}, "plus rien en écoute");
            check(depth() == savedDepth_, "toujours aucune entrée d'historique");
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::chooseMenuItem(int position)
{
    // The menu is a window of its own, modal while it is open. A person picks
    // with the mouse; the keyboard reaches the same item and does not depend
    // on where the window landed on the screen.
    auto* menu = juce::Component::getCurrentlyModalComponent();
    if (menu == nullptr)
    {
        check(false, "un menu est ouvert");
        return;
    }
    for (int index = 0; index < position; ++index)
        static_cast<void>(menu->keyPressed(juce::KeyPress{juce::KeyPress::downKey}));
    static_cast<void>(menu->keyPressed(juce::KeyPress{juce::KeyPress::returnKey}));
}

void Verification::answerDialog(const juce::String& field, const juce::String& typed)
{
    auto* dialog = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
    if (dialog == nullptr || dialog->getTextEditor(field) == nullptr)
    {
        check(false, "une boîte de saisie est ouverte");
        return;
    }
    dialog->getTextEditor(field)->setText(typed);
    dialog->exitModalState(1);
}

void Verification::addTempoSteps()
{
    // --- the tempo and the signature -----------------------------------------------
    //
    // FL's readouts, in the order a person discovers them: the wheel over the
    // tempo, a click that offers to type it, the signature the same way, then
    // "Automatiser le tempo" and the lane it opens in the playlist. What
    // changes the sound is measured on a render; what must not change it too.

    constexpr float notch = 0.25f;

    add("le morceau tel qu'il est, au rendu, avant de toucher au tempo",
        [this]
        {
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();
            tempoBefore_ = state_.tempoPoints().front().beatsPerMinute;
            heardBefore_ = listen("s12-tempo-avant", tempoBefore_);
        });

    add(
        "trois crans de molette sur le tempo : +3 BPM, une seule entrée d'historique",
        [this]
        {
            auto* transport = dynamic_cast<ui::TransportPanel*>(panel("transport"));
            if (transport == nullptr)
                return;

            for (int turn = 0; turn < 3; ++turn)
                wheel(*transport, transport->tempoArea().getCentre(), notch);
            check(state_.tempoPoints().front().beatsPerMinute == std::floor(tempoBefore_) + 3.0,
                  "le tempo du projet passe de " + juce::String(tempoBefore_, 1).toStdString() + " à " +
                      juce::String(state_.tempoPoints().front().beatsPerMinute, 1).toStdString());
            wheelAt_ = juce::Time::getMillisecondCounterHiRes();
        },
        // The turn is over when the wheel has rested: then the entry is closed.
        [this] { return juce::Time::getMillisecondCounterHiRes() - wheelAt_ > 800.0; },
        3000.0);

    const auto dialogOpen = []
    { return dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent()) != nullptr; };

    add(
        "un clic sur le tempo ouvre le menu ; « Saisir le tempo… »",
        [this]
        {
            check(depth() == savedDepth_ + 1, "le tour de molette est une seule entrée");

            auto* transport = dynamic_cast<ui::TransportPanel*>(panel("transport"));
            if (transport == nullptr)
                return;
            click(*transport, transport->tempoArea().getCentre());
            chooseMenuItem(ui::TransportPanel::typeTempoItem);
        },
        dialogOpen,
        3000.0);

    add(
        "la boîte de saisie : 97,5 avec une virgule",
        [this] { answerDialog("tempo", "97,5"); },
        [this] { return state_.tempoPoints().front().beatsPerMinute == 97.5; },
        3000.0);

    add("97,5 BPM, lu au rendu",
        [this]
        {
            check(state_.tempoPoints().front().beatsPerMinute == 97.5, "le tempo du projet est 97,5");
            // Two renders: 90 before this group, 97.5 now. The song is shorter
            // by the ratio of the tempos.
            const auto heard = listen("s12-tempo-97-5", 97.5);
            const auto expected = heardBefore_.seconds * tempoBefore_ / 97.5;
            check(std::abs(heard.seconds - expected) < 0.5,
                  "le morceau dure " + juce::String(heard.seconds, 2).toStdString() + " s, " +
                      juce::String(expected, 2).toStdString() + " attendues");
            heardBefore_ = heard;
        });

    add(
        "la molette sur la signature : 5/4, puis un clic pour taper 6/8",
        [this]
        {
            auto* transport = dynamic_cast<ui::TransportPanel*>(panel("transport"));
            if (transport == nullptr)
                return;

            wheel(*transport, transport->signatureArea().getCentre(), notch);
            check(state_.timeSignature() == domain::TimeSignature{5, 4}, "un cran : 5/4");

            click(*transport, transport->signatureArea().getCentre());
        },
        dialogOpen,
        3000.0);

    add(
        "la boîte de saisie : 6/8",
        [this] { answerDialog("signature", "6/8"); },
        [this] { return state_.timeSignature() == domain::TimeSignature{6, 8}; },
        3000.0);

    add("en 6/8 rien ne bouge au rendu ; un clic dans la playlist se cale sur la mesure de trois temps",
        [this]
        {
            check(state_.timeSignature() == domain::TimeSignature{6, 8}, "la signature est 6/8");

            const auto heard = listen("s12-signature-6-8", 97.5);
            check(std::abs(heard.seconds - heardBefore_.seconds) < 0.05,
                  "même durée qu'en 4/4 : " + juce::String(heard.seconds, 2).toStdString() + " s");
            check(heard.onsets == heardBefore_.onsets, "chaque attaque reste à sa place");

            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            // Past the song, on the first lane, a beat and a half into the
            // third bar of 6/8 (beat 6): the pattern lands on beat 6, where
            // 4/4 would have put it on beat 4.
            double songEnd = 0.0;
            for (const auto& placement : state_.arrangement())
            {
                if (const auto* pattern = state_.findPattern(placement.patternId); pattern != nullptr)
                    songEnd = std::max(songEnd, placement.startBeats + pattern->lengthBeats);
            }
            const auto before = state_.arrangement().size();
            const auto far = std::ceil((songEnd + 1.0) / 12.0) * 12.0 + 6.0;
            const auto at = playlistBeat(0, far + 1.5);
            click(*playlist, at);
            const auto laid = std::any_of(state_.arrangement().begin(),
                                          state_.arrangement().end(),
                                          [far](const domain::Placement& placement)
                                          { return placement.startBeats == far; });
            check(state_.arrangement().size() == before + 1 && laid,
                  "posé au temps " + juce::String(far, 0).toStdString() + ", début d'une mesure de 6/8");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(state_.arrangement().size() == before, "Ctrl+Z retire la pose");
        });

    add(
        "« Automatiser le tempo », depuis le menu du tempo",
        [this]
        {
            auto* transport = dynamic_cast<ui::TransportPanel*>(panel("transport"));
            if (transport == nullptr)
                return;

            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
            click(*transport, transport->tempoArea().getCentre());
            chooseMenuItem(ui::TransportPanel::automateTempoItem);
        },
        [this] { return state_.tempoPoints().size() == 2; },
        3000.0);

    add("un premier changement de tempo, sur la deuxième mesure",
        [this]
        {
            check(state_.tempoPoints().size() == 2, "un changement de tempo de plus");
            if (state_.tempoPoints().size() == 2)
            {
                const auto& added = state_.tempoPoints().back();
                check(added.startBeats == 3.0, "sur la deuxième mesure de 6/8, au temps 3");
                check(added.beatsPerMinute == 97.5, "au tempo qui joue déjà : rien ne s'entend encore");
            }
        });

    add("la ligne de tempo s'affiche ; on tire le changement vers le haut",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr || state_.tempoPoints().size() != 2)
                return;

            check(!playlist->tempoLane().isEmpty(), "la ligne TEMPO est sous la règle");
            snapshot("s12-ligne-de-tempo");

            // Out of sight to the left? Bring the start of the song back.
            static_cast<void>(playlistBeat(0, 0.0));
            const auto& added = state_.tempoPoints().back();
            const auto from = playlist->tempoPointFor(added.startBeats, added.beatsPerMinute);
            const auto depthBefore = depth();
            drag(*playlist, from, from.translated(0, -40));

            check(state_.tempoPoints().back().beatsPerMinute > 97.5 + 20.0,
                  "le changement monte à " +
                      juce::String(state_.tempoPoints().back().beatsPerMinute, 1).toStdString() + " BPM");
            check(state_.tempoPoints().back().startBeats == 3.0, "sans bouger dans le temps");
            check(depth() == depthBefore + 1, "le geste est une seule entrée");
        });

    add("l'accélération s'entend : le morceau raccourcit",
        [this]
        {
            if (state_.tempoPoints().size() != 2)
                return;
            const auto faster = state_.tempoPoints().back().beatsPerMinute;
            const auto heard = listen("s12-automation-tempo", 97.5);

            // Three beats at 97.5, the rest at the new tempo.
            const auto total = heardBefore_.seconds * 97.5 / 60.0;
            const auto expected = 3.0 * 60.0 / 97.5 + (total - 3.0) * 60.0 / faster;
            check(std::abs(heard.seconds - expected) < 0.5,
                  "le morceau dure " + juce::String(heard.seconds, 2).toStdString() + " s au lieu de " +
                      juce::String(heardBefore_.seconds, 2).toStdString() + ", " +
                      juce::String(expected, 2).toStdString() + " attendues");
        });

    add("clic droit sur le changement : il part, et la ligne avec",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr || state_.tempoPoints().size() != 2)
                return;

            const auto& added = state_.tempoPoints().back();
            click(*playlist, playlist->tempoPointFor(added.startBeats, added.beatsPerMinute), true);
            check(state_.tempoPoints().size() == 1, "il ne reste que le tempo du projet");
        });

    add("la ligne est partie ; tout défaire au Ctrl+Z",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            check(playlist != nullptr && playlist->tempoLane().isEmpty(), "plus de ligne de tempo");

            while (depth() > savedDepth_)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_,
                  "le projet d'avant, à l'octet près : tempo " +
                      juce::String(state_.tempoPoints().front().beatsPerMinute, 1).toStdString() +
                      ", signature " + std::to_string(state_.timeSignature().numerator) + "/" +
                      std::to_string(state_.timeSignature().denominator));
        });
}

} // namespace daw::app

namespace daw::app
{

juce::TreeViewItem* Verification::browserItem(const juce::File& file) const
{
    auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
    if (browser == nullptr)
        return nullptr;

    juce::TreeView* tree = nullptr;
    for (auto* child : browser->getChildren())
        tree = tree != nullptr ? tree : dynamic_cast<juce::TreeView*>(child);
    if (tree == nullptr || tree->getRootItem() == nullptr)
        return nullptr;

    // Down the tree the way the eye goes: only through what is open.
    std::function<juce::TreeViewItem*(juce::TreeViewItem*)> find =
        [&](juce::TreeViewItem* item) -> juce::TreeViewItem*
    {
        for (int index = 0; index < item->getNumSubItems(); ++index)
        {
            auto* sub = item->getSubItem(index);
            if (sub->getUniqueName() == file.getFullPathName())
                return sub;
            if (sub->isOpen())
            {
                if (auto* deeper = find(sub); deeper != nullptr)
                    return deeper;
            }
        }
        return nullptr;
    };
    return find(tree->getRootItem());
}

void Verification::clickBrowserItem(juce::TreeViewItem& item)
{
    auto* browser = panel("browser");
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event{source,
                                 {},
                                 juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier},
                                 juce::MouseInputSource::defaultPressure,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 browser,
                                 browser,
                                 now,
                                 {},
                                 now,
                                 1,
                                 false};
    item.setSelected(true, true);
    item.itemClicked(event);
}

juce::TextEditor* Verification::browserSearch() const
{
    auto* browser = panel("browser");
    if (browser == nullptr)
        return nullptr;
    for (auto* child : browser->getChildren())
    {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child); editor != nullptr)
            return editor;
    }
    return nullptr;
}

void Verification::addSearchSteps()
{
    // --- the browser: one click on a folder, and the search ---------------------------
    //
    // A pack with a folder of its own, the way sample packs come: "Kicks",
    // three kicks in it, and a house snare next to it. The person opens it with one click each, then types
    // "kick house" and expects the house kick first and the club and techno
    // ones right after — the example they gave.

    add("un dossier « Kicks » dans le kit ; un clic ouvre le kit, un clic ouvre « Kicks »",
        [this]
        {
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            const auto kicks = kit_.getChildFile("Kicks");
            static_cast<void>(kicks.createDirectory());
            for (const auto* name : {"Kick House.wav", "Kick Club 01.wav", "Kick Techno.wav"})
                writeHit(kicks.getChildFile(name), 0.3);
            writeHit(kit_.getChildFile("Snare House.wav"), 0.3);

            auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
            if (browser == nullptr)
                return;
            browser->refresh();

            auto* kit = browserItem(kit_);
            if (kit == nullptr)
            {
                check(false, "le kit est dans le navigateur");
                return;
            }
            kit->setOpen(false);
            clickBrowserItem(*kit);
            check(kit->isOpen(), "un clic ouvre le kit");

            auto* folder = browserItem(kicks);
            if (folder == nullptr)
            {
                check(false, "« Kicks » est dans le kit");
                return;
            }
            clickBrowserItem(*folder);
            check(folder->isOpen() && folder->getNumSubItems() == 3, "un clic ouvre « Kicks » : trois kicks");
            snapshot("s12-dossier-ouvert-d-un-clic");

            clickBrowserItem(*folder);
            check(!folder->isOpen(), "un second clic le referme");
        });

    add(
        "on tape « kick house » dans la recherche",
        [this]
        {
            auto* search = browserSearch();
            if (search == nullptr)
            {
                check(false, "une barre de recherche en haut du navigateur");
                return;
            }
            search->setText("kick house", true);
        },
        [this]
        {
            auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
            return browser != nullptr && !browser->results().empty();
        },
        5000.0);

    add(
        "le kick house d'abord, puis le club et le techno, puis ce qui ne répond qu'à moitié",
        [this]
        {
            auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
            if (browser == nullptr)
                return;

            juce::StringArray names;
            for (const auto& file : browser->results())
                names.add(file.getFileName());
            note("résultats : " + names.joinIntoString(", ").toStdString());

            check(names.size() >= 5, "au moins cinq résultats");
            if (names.size() < 5)
                return;
            check(names[0] == "Kick House.wav", "premier : Kick House");
            check(juce::StringArray{names[1], names[2]}.contains("Kick Club 01.wav") &&
                      juce::StringArray{names[1], names[2]}.contains("Kick Techno.wav"),
                  "puis Kick Club 01 et Kick Techno : house, club et techno sont de la même famille");
            check(names.indexOf("Snare House.wav") > 2 && names.indexOf("Kick 808.wav") > 2,
                  "Snare House et Kick 808, qui n'ont qu'un mot sur deux, après");
            check(!names.contains("Clap.wav"), "le Clap n'y est pas");
            snapshot("s12-recherche-kick-house");

            // One click on the second result plays it, as in the tree.
            if (auto* row = browser->resultRow(1); row != nullptr)
                click(*row, row->getLocalBounds().getCentre());
            check(samples_.auditioned() == browser->results()[1],
                  "un clic sur le deuxième résultat le fait entendre : « " +
                      samples_.auditioned().getFileName().toStdString() + " »");
        },
        [this] { return samples_.auditionPeakDb() > -60.0f; },
        3000.0);

    add(
        "deux fautes de frappe : « kcik clbu »",
        [this]
        {
            check(samples_.auditionPeakDb() > -60.0f,
                  "l'écoute sort à " + juce::String(samples_.auditionPeakDb(), 1).toStdString() + " dBFS");
            samples_.stopAudition();
            if (auto* search = browserSearch(); search != nullptr)
                search->setText("kcik clbu", true);
        },
        [this]
        {
            auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
            return browser != nullptr && !browser->results().empty() &&
                   browser->results().front().getFileName() == "Kick Club 01.wav";
        },
        3000.0);

    add("Kick Club 01 en tête ; Échap ramène l'arbre",
        [this]
        {
            auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
            if (browser == nullptr)
                return;
            check(browser->results().front().getFileName() == "Kick Club 01.wav", "Kick Club 01 en tête");

            if (auto* search = browserSearch(); search != nullptr)
                static_cast<void>(search->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
        });

    add("l'arbre est revenu ; le projet n'a pas bougé",
        [this]
        {
            auto* browser = dynamic_cast<ui::BrowserPanel*>(panel("browser"));
            auto* search = browserSearch();
            if (browser == nullptr || search == nullptr)
                return;

            check(search->isEmpty() && browser->results().empty(), "la recherche est vide");
            bool treeShown = false;
            for (auto* child : browser->getChildren())
                treeShown =
                    treeShown || (dynamic_cast<juce::TreeView*>(child) != nullptr && child->isVisible());
            check(treeShown, "l'arbre est de nouveau affiché");

            check(depth() == savedDepth_, "aucune entrée d'historique");
            check(domain::json::write(state_.toValue()) == savedState_,
                  "le projet est identique, à l'octet près");

            static_cast<void>(kit_.getChildFile("Kicks").deleteRecursively());
            static_cast<void>(kit_.getChildFile("Snare House.wav").deleteFile());
            browser->refresh();
        });
}

} // namespace daw::app

namespace daw::app
{

std::vector<int> Verification::kickVelocities() const
{
    std::vector<int> velocities;
    if (state_.patterns().empty() || state_.tracks().empty())
        return velocities;
    if (const auto* row = state_.patterns().front().findClipForTrack(state_.tracks().front().id);
        row != nullptr)
    {
        for (const auto& note : row->notes)
            velocities.push_back(note.velocity);
    }
    return velocities;
}

void Verification::addVelocitySteps()
{
    // --- the velocity lane, under the piano roll --------------------------------------
    //
    // FL's event editor: a click sets the note under it, a stroke across the
    // stems draws a crescendo, a selection limits the stroke to itself. Each
    // stroke is one Ctrl+Z. The crescendo is measured on a render, kick alone.

    add("F7 sur les kicks : la zone de vélocité est sous les notes ; un clic met le premier à 40",
        [this]
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            press("PAT");
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            selection_.selectPattern(state_.patterns().front().id);
            if (auto* rack = panel("channel_rack"); rack != nullptr)
                click(*rack, rackChannel(0));
            key(juce::KeyPress{juce::KeyPress::F7Key});

            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            const auto* row = state_.patterns().front().findClipForTrack(state_.tracks().front().id);
            check(roll != nullptr && row != nullptr && row->notes.size() >= 4,
                  "le piano-roll montre les kicks");
            if (roll == nullptr || row == nullptr || row->notes.size() < 4)
                return;

            check(!roll->velocityLane().isEmpty() &&
                      roll->velocityLane().getY() > roll->noteBounds(row->notes[0]).getY(),
                  "la zone VÉLOCITÉ est sous la grille");
            juce::StringArray before;
            for (const auto velocity : kickVelocities())
                before.add(juce::String(velocity));
            note("vélocités avant : " + before.joinIntoString(", ").toStdString());

            click(*roll, roll->velocityPointFor(row->notes[0], 40));
            const auto after = kickVelocities();
            check(std::abs(after[0] - 40) <= 1, "le premier kick passe à " + std::to_string(after[0]));
            check(depth() == savedDepth_ + 1, "une entrée d'historique");
            snapshot("s12-velocite-un-clic");
        });

    add("un trait de gauche à droite, de 60 à 127 : un crescendo, une seule entrée",
        [this]
        {
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            const auto* row = state_.patterns().front().findClipForTrack(state_.tracks().front().id);
            if (roll == nullptr || row == nullptr || row->notes.size() < 4)
                return;

            const auto from = roll->velocityPointFor(row->notes.front(), 60);
            const auto to = roll->velocityPointFor(row->notes.back(), 127);
            const auto depthBefore = depth();
            drag(*roll, from, to);

            const auto after = kickVelocities();
            juce::StringArray values;
            for (const auto v : after)
                values.add(juce::String(v));
            note("vélocités après le trait : " + values.joinIntoString(", ").toStdString());

            check(std::abs(after.front() - 60) <= 2 && std::abs(after.back() - 127) <= 2,
                  "de 60 au premier à 127 au dernier");
            check(std::is_sorted(after.begin(), after.end()) && after.front() < after.back(),
                  "chaque kick plus fort que le précédent");
            check(depth() == depthBefore + 1,
                  "un seul Ctrl+Z pour les " + std::to_string(after.size()) + " notes du trait");
            snapshot("s12-velocite-crescendo");
        });

    add("le crescendo s'entend : le Kick seul, chaque coup plus fort que le précédent",
        [this]
        {
            const auto kick = state_.tracks().front().id;
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackSolo>(kick, true)));
            // The kick alone, so a quiet first hit is still an onset: the
            // floor is 34 dB under the loudest rather than 12.
            const auto heard = listen("s12-crescendo", state_.tempoPoints().front().beatsPerMinute, 0.02f);
            static_cast<void>(bus_.undo());

            juce::StringArray levels;
            for (const auto level : heard.onsetLevels)
                levels.add(juce::String(juce::Decibels::gainToDecibels(level), 1));
            note("niveau de chaque attaque, en dBFS : " + levels.joinIntoString(", ").toStdString());

            const auto count = kickVelocities().size();
            check(heard.onsetLevels.size() == count, std::to_string(count) + " attaques, une par kick");
            check(heard.onsetLevels.size() >= 2 &&
                      std::is_sorted(heard.onsetLevels.begin(), heard.onsetLevels.end()) &&
                      heard.onsetLevels.back() > heard.onsetLevels.front() * 1.5f,
                  "chaque attaque plus forte que la précédente, la dernière nettement");
        });

    add("deux kicks pris au Ctrl + clic : le trait ne touche qu'eux",
        [this]
        {
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            const auto* row = state_.patterns().front().findClipForTrack(state_.tracks().front().id);
            if (roll == nullptr || row == nullptr || row->notes.size() < 4)
                return;

            click(*roll, roll->noteBounds(row->notes[1]).getCentre(), false, false, true);
            click(*roll, roll->noteBounds(row->notes[2]).getCentre(), false, false, true);
            check(roll->picked().size() == 2, "deux kicks pris");

            const auto before = kickVelocities();
            const auto lane = roll->velocityLane();
            const auto y = roll->velocityPointFor(row->notes[0], 64).getY();
            drag(*roll, {lane.getX() + 1, y}, {lane.getRight() - 2, y});

            const auto after = kickVelocities();
            check(after[0] == before[0] && after.back() == before.back(),
                  "le premier et le dernier n'ont pas bougé");
            check(std::abs(after[1] - 64) <= 1 && std::abs(after[2] - 64) <= 1, "les deux pris passent à 64");
        });

    add("tout défaire au Ctrl+Z",
        [this]
        {
            while (depth() > savedDepth_)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_, "le projet d'avant, à l'octet près");
            key(juce::KeyPress{juce::KeyPress::F7Key});
            press("SONG");
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::addRackSteps()
{
    // --- the rack, without its grid ---------------------------------------------------
    //
    // Since S12 the rack brings sounds in and takes them out; notes are
    // written in the piano roll. "+ Instrument" makes a channel on the built-in
    // synth, a double click renames it, the right-click menu removes it, and a
    // click where the steps used to be writes nothing.

    add("un clic là où étaient les pas n'écrit plus rien",
        [this]
        {
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            auto* rack = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"));
            if (rack == nullptr)
                return;

            const auto row = rack->channelBounds(0);
            click(*rack, {row.getRight() - row.getWidth() / 4, row.getCentreY()});
            check(depth() == savedDepth_, "aucune entrée d'historique");
            check(domain::json::write(state_.toValue()) == savedState_, "aucune note écrite");
            check(selection_.track() == state_.tracks().front().id, "le clic choisit le canal, rien d'autre");
            note("le canal dit ce qui le joue : « " +
                 rack->instrumentName(state_.tracks().front()).toStdString() + " »");
            snapshot("s12-rack-sans-grille");
        });

    // A menu closes itself when the application is not in front, which a
    // verification running behind other windows often is: it is opened and
    // answered in the same step.
    add(
        "« + Instrument » : le synthé intégré",
        [this]
        {
            tracksBefore_ = state_.tracks().size();
            press("+ Instrument");
            chooseMenuItem(ui::ChannelRackPanel::builtInSynthItem);
        },
        [this] { return state_.tracks().size() == tracksBefore_ + 1; },
        3000.0);

    add(
        "un canal « Synth » de plus, une entrée ; double-clic pour le renommer",
        [this]
        {
            check(state_.tracks().size() == tracksBefore_ + 1, "un canal de plus");
            check(depth() == savedDepth_ + 1, "une entrée d'historique");
            check(state_.tracks().back().name == "Synth" && state_.tracks().back().plugins.empty() &&
                      !state_.tracks().back().sample.has_value(),
                  "« Synth », sans plugin ni sample : le synthé intégré le joue");
            check(selection_.track() == state_.tracks().back().id, "il est choisi : le piano-roll l'édite");

            auto* rack = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"));
            if (rack != nullptr)
                doubleClick(*rack, rack->channelBounds(static_cast<int>(tracksBefore_)).getCentre());
        },
        [] {
            return dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent()) != nullptr;
        },
        3000.0);

    add(
        "on tape « Basse »",
        [this] { answerDialog("name", "Basse"); },
        [this] { return !state_.tracks().empty() && state_.tracks().back().name == "Basse"; },
        3000.0);

    add("une note au piano-roll sur la Basse",
        [this]
        {
            check(state_.tracks().back().name == "Basse", "renommé « Basse »");
            writeNotes(static_cast<int>(tracksBefore_), {0.0});
            const auto* row = state_.patterns().front().findClipForTrack(state_.tracks().back().id);
            check(row != nullptr && row->notes.size() == 1, "le piano-roll écrit sur le nouveau canal");
        });

    add(
        "clic droit sur la Basse : « Retirer le canal »",
        [this]
        {
            auto* rack = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"));
            if (rack == nullptr)
                return;
            click(*rack, rack->channelBounds(static_cast<int>(tracksBefore_)).getCentre(), true);
            chooseMenuItem(ui::ChannelRackPanel::removeItem);
        },
        [this] { return state_.tracks().size() == tracksBefore_; },
        3000.0);

    add("le canal est parti avec ses notes ; tout défaire au Ctrl+Z",
        [this]
        {
            check(state_.tracks().size() == tracksBefore_, "le canal est retiré");
            const auto kept = std::none_of(state_.patterns().front().clips.begin(),
                                           state_.patterns().front().clips.end(),
                                           [this](const domain::Clip& clip)
                                           {
                                               return std::none_of(state_.tracks().begin(),
                                                                   state_.tracks().end(),
                                                                   [&clip](const domain::Track& track)
                                                                   { return track.id == clip.trackId; });
                                           });
            check(kept, "aucune ligne de pattern ne reste sans canal");

            while (depth() > savedDepth_)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_, "le projet d'avant, à l'octet près");
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::addWorkflowSteps()
{
    // --- S13: one place for each choice ---------------------------------------------
    //
    // The pattern is chosen in the transport, next to PAT; the channel the
    // piano roll writes for is chosen in the piano roll's header, and that
    // menu writes the same Selection a click in the rack does.

    add("le pattern se choisit dans le transport, plus dans le rack",
        [this]
        {
            auto* rack = panel("channel_rack");
            auto* transport = panel("transport");
            if (rack == nullptr || transport == nullptr)
                return;

            check(childOfType<juce::ComboBox>(*rack) == nullptr, "le rack n'a plus de sélecteur de pattern");
            check(button(*rack, "+ Pattern") == nullptr, "ni de « + Pattern »");
            check(childOfType<juce::ComboBox>(*transport) != nullptr, "le transport a le sélecteur");
            check(button(*transport, "+ Pattern") != nullptr, "et « + Pattern »");
            check(button(view_, "+ Ligne") == nullptr, "le « + Ligne » du piano-roll est retiré");
        });

    add("le piano-roll choisit son canal ; le rack suit, et l'inverse",
        [this]
        {
            auto* rack = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"));
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            if (rack == nullptr || roll == nullptr || state_.tracks().size() < 2)
            {
                check(false, "deux canaux, le rack et le piano-roll");
                return;
            }

            savedDepth_ = depth();
            auto& chooser = roll->channelChooser();
            check(chooser.getNumItems() == static_cast<int>(state_.tracks().size()),
                  "le menu liste les " + std::to_string(state_.tracks().size()) + " canaux du rack");

            chooser.setSelectedId(2, juce::sendNotificationSync);
            selection_.dispatchPendingMessages();
            check(selection_.track() == state_.tracks()[1].id,
                  "choisir « " + state_.tracks()[1].name + " » dans le menu le choisit");
            check(depth() == savedDepth_, "choisir n'est pas une édition : aucune entrée d'historique");

            click(*rack, rackChannel(0));
            selection_.dispatchPendingMessages();
            check(chooser.getSelectedId() == 1, "un clic sur le premier canal du rack remet le menu dessus");
            snapshot("s13-canal-du-piano-roll");
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::addNavigationSteps()
{
    // --- S13: zoom on the ruler, the middle button drags the view -----------------
    //
    // Four notches over the ruler double the width of a beat, and the beat
    // under the pointer stays under it. The middle button held down moves
    // the paper, not the notes. None of it is an edit.

    add("piano-roll : la molette sur la règle zoome autour du pointeur",
        [this]
        {
            if (panel("piano_roll") == nullptr || !panel("piano_roll")->isShowing())
                key(juce::KeyPress{juce::KeyPress::F7Key});
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            if (roll == nullptr || !roll->isShowing())
            {
                check(false, "le piano-roll est ouvert");
                return;
            }

            selection_.selectPattern(state_.patterns().front().id);
            selection_.selectTrack(state_.tracks().front().id);
            selection_.dispatchPendingMessages();

            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            const auto ruler = roll->ruler();
            const auto fit = roll->beatWidth();
            const auto at = juce::Point<int>{ruler.getX() + ruler.getWidth() / 3, ruler.getCentreY()};
            const auto beatBefore = roll->firstBeat() + (at.x - ruler.getX()) / roll->beatWidth();

            wheel(*roll, at, 1.0f);
            const auto beatAfter = roll->firstBeat() + (at.x - ruler.getX()) / roll->beatWidth();
            check(std::abs(roll->beatWidth() - fit * 2.0) < 0.01,
                  "quatre crans : un temps passe de " + std::to_string(fit) + " à " +
                      std::to_string(roll->beatWidth()) + " px");
            check(std::abs(beatAfter - beatBefore) < 1.0 / roll->beatWidth(),
                  "le temps sous le pointeur y reste");
            check(roll->firstBeat() > 0.0, "la vue part de plus loin que le début");
            check(depth() == savedDepth_, "zoomer n'est pas une édition");
            snapshot("s13-piano-roll-zoom");
        });

    add("piano-roll : le clic molette déplace la vue, pas les notes",
        [this]
        {
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            if (roll == nullptr)
                return;

            const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
            const auto ruler = roll->ruler();
            const auto from = juce::Point<int>{ruler.getCentreX(), ruler.getBottom() + keyHeight * 10};
            const auto first = roll->firstBeat();
            const auto top = roll->topPitch();
            const auto width = roll->beatWidth();

            drag(*roll, from, from + juce::Point<int>{-60, keyHeight * 3}, false, true);
            check(std::abs(roll->firstBeat() - (first + 60.0 / width)) < 0.01,
                  "60 px vers la gauche : la vue avance de " + std::to_string(60.0 / width) + " temps");
            check(roll->topPitch() == top + 3, "trois touches vers le bas : trois demi-tons plus haut");
            check(depth() == savedDepth_ && domain::json::write(state_.toValue()) == savedState_,
                  "aucune note posée ni bougée, aucune entrée d'historique");

            // Back to the whole pattern: zooming out stops at the width that fits.
            for (int notch = 0; notch < 4; ++notch)
                wheel(*roll, ruler.getCentre(), -1.0f);
            check(roll->firstBeat() == 0.0 && std::abs(roll->beatWidth() - width / 2.0) < 0.01,
                  "dézoomer revient au pattern entier");

            key(juce::KeyPress{juce::KeyPress::F7Key});
        });

    add("playlist : la molette sur la règle zoome, le clic molette déplace",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            const auto ruler = playlist->ruler();
            const auto fit = playlist->beatWidth();
            const auto at = juce::Point<int>{ruler.getX() + ruler.getWidth() / 3, ruler.getCentreY()};
            const auto beatBefore = playlist->firstBeat() + (at.x - ruler.getX()) / playlist->beatWidth();

            wheel(*playlist, at, 1.0f);
            const auto beatAfter = playlist->firstBeat() + (at.x - ruler.getX()) / playlist->beatWidth();
            check(std::abs(playlist->beatWidth() - fit * 2.0) < 0.01,
                  "quatre crans sur la règle : deux fois plus large");
            check(std::abs(beatAfter - beatBefore) < 1.0 / playlist->beatWidth(),
                  "le temps sous le pointeur y reste");

            // Started on the ruler, where a left click moves the playhead: the
            // middle button must not.
            const auto position = state_.transport().positionBeats;
            const auto first = playlist->firstBeat();
            const auto width = playlist->beatWidth();
            drag(*playlist, at, at + juce::Point<int>{-80, 0}, false, true);
            check(std::abs(playlist->firstBeat() - (first + 80.0 / width)) < 0.01,
                  "80 px vers la gauche : la vue avance de " + std::to_string(80.0 / width) + " temps");
            check(state_.transport().positionBeats == position, "la tête de lecture n'a pas bougé");
            check(depth() == savedDepth_ && domain::json::write(state_.toValue()) == savedState_,
                  "aucun bloc posé ni bougé, aucune entrée d'historique");
            snapshot("s13-playlist-zoom");

            for (int notch = 0; notch < 4; ++notch)
                wheel(*playlist, ruler.getCentre(), -1.0f);
            check(std::abs(playlist->beatWidth() - fit) < 0.01, "dézoomer revient à la vue entière");
        });
}

} // namespace daw::app

namespace daw::app
{

void Verification::openExportDialog()
{
    titleBar_.runMenuItem(ui::TitleBarView::exportItem);
}

void Verification::addExportSteps()
{
    // --- S13: Fichier > Exporter... -------------------------------------------------
    //
    // The four formats through the same dialog a person answers: the format,
    // the quality it offers, "Exporter". Each file is read back and measured
    // against the first, a WAV: same length (a lossy codec pads a few
    // milliseconds), same level (within half a decibel for MP3 and AAC).

    if (exporter_ == nullptr)
        return;

    struct Case
    {
        int formatId;
        const char* name;
        const char* extension;
        double levelTolerance;
    };
    static constexpr Case cases[] = {
        {1, "WAV", ".wav", 0.0},
        {2, "FLAC", ".flac", 0.01},
        {3, "MP3", ".mp3", 0.5},
        {4, "AAC", ".m4a", 0.5},
    };

    for (const auto& format : cases)
    {
        add(
            std::string{"Fichier > Exporter... : "} + format.name,
            [this] { openExportDialog(); },
            [] {
                return dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent()) !=
                       nullptr;
            },
            3000.0);

        add(
            std::string{"format "} + format.name + ", qualité proposée, « Exporter »",
            [this, format]
            {
                auto* dialog =
                    dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
                auto* formats =
                    dialog != nullptr ? dialog->getComboBoxComponent(SongExporter::formatField) : nullptr;
                auto* quality =
                    dialog != nullptr ? dialog->getComboBoxComponent(SongExporter::qualityField) : nullptr;
                if (formats == nullptr || quality == nullptr)
                {
                    check(false, "la boîte d'export a un format et une qualité");
                    return;
                }

                formats->setSelectedId(format.formatId, juce::sendNotificationSync);
                note("qualités proposées : " +
                     [quality]
                     {
                         juce::StringArray items;
                         for (int index = 0; index < quality->getNumItems(); ++index)
                             items.add(quality->getItemText(index));
                         return items.joinIntoString(", ").toStdString();
                     }() +
                     " ; choisie : " + quality->getText().toStdString());

                exportsBefore_ = exporter_->lastExport();
                dialog->exitModalState(SongExporter::exportButton);
            },
            [this, format] {
                return exporter_->lastExport().hasFileExtension(format.extension) ||
                       exporter_->lastError().isNotEmpty();
            },
            120000.0);

        add(std::string{"le fichier "} + format.name + " relu et mesuré",
            [this, format]
            {
                check(exporter_->lastError().isEmpty(),
                      "aucune erreur" + (exporter_->lastError().isEmpty()
                                             ? std::string{}
                                             : " (" + exporter_->lastError().toStdString() + ")"));
                const auto file = exporter_->lastExport();
                check(file.existsAsFile(), file.getFileName().toStdString() + " écrit");
                check(titleBar_.status() == juce::String(u8"exporté : ") + file.getFileName(),
                      "la barre dit « exporté : " + file.getFileName().toStdString() + " »");

                juce::AudioBuffer<float> audio;
                double rate = 0.0;
                if (!engine::readExport(file, audio, rate) || audio.getNumSamples() == 0)
                {
                    check(false, "le fichier se relit");
                    return;
                }

                const auto seconds = audio.getNumSamples() / rate;
                auto sum = 0.0;
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                {
                    const auto level = audio.getRMSLevel(channel, 0, audio.getNumSamples());
                    sum += static_cast<double>(level) * level;
                }
                const auto rmsDb = 10.0 * std::log10(sum / audio.getNumChannels());

                note(file.getFileName().toStdString() + " : " + std::to_string(file.getSize() / 1024) +
                     " Kio, " + juce::String(seconds, 2).toStdString() + " s, " +
                     juce::String(rate, 0).toStdString() + " Hz, " + juce::String(rmsDb, 2).toStdString() +
                     " dBFS RMS");

                if (format.formatId == 1)
                {
                    exportSeconds_ = seconds;
                    exportRmsDb_ = rmsDb;
                    check(seconds > 1.0, "le morceau entier, pas un fragment");
                    return;
                }

                check(seconds >= exportSeconds_ - 0.01 && seconds <= exportSeconds_ + 0.15,
                      "même durée que le WAV, à quelques millisecondes de rembourrage près");
                check(std::abs(rmsDb - exportRmsDb_) <= std::max(format.levelTolerance, 0.01),
                      "même niveau que le WAV (écart " + juce::String(rmsDb - exportRmsDb_, 3).toStdString() +
                          " dB)");
            });
    }
}

} // namespace daw::app
