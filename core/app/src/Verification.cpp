#include "Verification.h"

#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/engine/MeterTap.h"
#include "daw/engine/Rendering.h"
#include "daw/ui/panels/BrowserPanel.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
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
{
}

Verification::~Verification()
{
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

Verification::Heard Verification::listen(const std::string& name, double beatsPerMinute)
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
        if (levels[step] > loudest * 0.25f && levels[step] > before * 1.25f)
            heard.onsets.push_back(static_cast<int>(step));
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

void Verification::drag(juce::Component& target, juce::Point<int> from, juce::Point<int> to, bool ctrl)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto held = juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier |
                                         (ctrl ? juce::ModifierKeys::ctrlModifier : 0)};
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

juce::Point<int> Verification::rackCell(int row, int step) const
{
    auto* rack = panel("channel_rack");
    const auto* pattern = state_.patterns().empty() ? nullptr : &state_.patterns().front();
    if (rack == nullptr || pattern == nullptr)
        return {};

    if (const auto* current = state_.findPattern(selection_.pattern()); current != nullptr)
        pattern = current;

    const auto top = tokens_.integer("metric.panel.headerHeight");
    const auto left = tokens_.integer("metric.channelRack.channelWidth");
    const auto rowHeight = tokens_.integer("metric.channelRack.rowHeight");
    const auto width = rack->getWidth() - left;
    const auto steps = std::max(1, static_cast<int>(std::lround(pattern->lengthBeats / stepBeats)));

    const auto x = left + (step * width) / steps + width / (2 * steps);
    return {x, top + row * rowHeight + rowHeight / 2};
}

juce::Point<int> Verification::playlistBeat(int lane, double beats) const
{
    auto* playlist = panel("playlist");
    if (playlist == nullptr)
        return {};

    // The playlist's own reading of how much timeline it shows.
    double end = 0.0;
    for (const auto& placement : state_.arrangement())
    {
        if (const auto* pattern = state_.findPattern(placement.patternId); pattern != nullptr)
            end = std::max(end, placement.startBeats + pattern->lengthBeats);
    }
    const auto visible = std::ceil(std::max(64.0, end + 16.0) / beatsPerBar) * beatsPerBar;

    const auto top =
        tokens_.integer("metric.panel.headerHeight") + tokens_.integer("metric.playlist.rulerHeight");
    const auto left = tokens_.integer("metric.playlist.headerWidth");
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    const auto width = playlist->getWidth() - left;

    const auto x = left + static_cast<int>(std::lround(beats * width / visible));
    return {x, top + lane * laneHeight + laneHeight / 2};
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
            for (const auto* id : {"piano_roll", "plugin_chain", "tracks"})
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
            auto* rack = panel("channel_rack");
            for (const auto step : {0, 4, 8, 12})
                click(*rack, rackCell(0, step));

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

    add("LA preuve : une édition du rack, huit poses changées",
        [this]
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            savedDepth_ = depth();

            auto* rack = panel("channel_rack");
            for (const auto step : {2, 6, 10, 14})
                click(*rack, rackCell(1, step));

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
                 " entrées d'historique ; autant de Ctrl+Z");
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

    add("un deuxième pattern pour le copilote",
        [this]
        {
            press("+ Pattern");
            auto* rack = panel("channel_rack");
            for (const auto step : {0, 2, 4, 6, 8, 10, 12, 14})
                click(*rack, rackCell(1, step));
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
            // Read a step later: the rack rebuilds its chooser when it hears of
            // the change, and it hears asynchronously.
            auto* rack = panel("channel_rack");
            bool named = false;
            for (auto* child : rack->getChildren())
            {
                if (auto* chooser = dynamic_cast<juce::ComboBox*>(child); chooser != nullptr)
                {
                    for (int index = 0; index < chooser->getNumItems(); ++index)
                        named = named || chooser->getItemText(index) == "Refrain";
                }
            }
            check(named, "« Refrain » dans le sélecteur du rack");

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

            const auto below = rackCell(static_cast<int>(tracks) + 2, 0);
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

    add("le canal sampler joue ses cases",
        [this]
        {
            // In pattern mode, on the pattern the rack shows.
            press("PAT");
            const auto row = static_cast<int>(state_.tracks().size()) - 1;
            auto* rack = panel("channel_rack");

            const auto before = listen("20a-avant-sampler", 90.0);
            for (const auto step : {1, 5, 9, 13})
                click(*rack, rackCell(row, step));
            const auto after = listen("20b-canal-sampler", 90.0);

            bool heard = true;
            for (const auto step : {1, 5, 9, 13})
                heard =
                    heard && std::find(after.onsets.begin(), after.onsets.end(), step) != after.onsets.end();
            check(heard, "le sample s'entend sur les pas 2, 6, 10, 14 qu'on vient d'allumer");
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
            const auto from = playlistBeat(1, 30.0);
            const auto to = playlistBeat(0, 1.0);
            drag(*playlist, from, to, true);

            check(playlist->selected().size() == 2,
                  "deux blocs pris dans la zone : " + std::to_string(playlist->selected().size()));
        });

    add("Ctrl + Maj + clic ajoute un bloc à la sélection",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            click(*playlist, playlistBeat(0, 32.0 + 2.0), false, true, true);
            check(playlist->selected().size() == 3, "trois blocs sélectionnés");
            click(*playlist, playlistBeat(0, 32.0 + 2.0), false, true, true);
            check(playlist->selected().size() == 2, "un second Ctrl + Maj + clic le retire");
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

    addMeterSteps();

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
            const auto before = window_.getBounds();
            doubleClick(titleBar_, titleBar_.getLocalBounds().getCentre());
            check(window_.isFullScreen(), "la fenêtre est agrandie");
            savedBounds_ = before;
        });

    add("le second double-clic rend la taille d'avant",
        [this]
        {
            doubleClick(titleBar_, titleBar_.getLocalBounds().getCentre());
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

// A hand on the title bar: the pointer moves across the screen, and each event
// is expressed where the bar is at that moment, since the bar moves with the
// window it drags. A fixed point in the bar's coordinates would not be a hand.
void Verification::dragWindow(juce::Point<int> by)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto held = juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier};

    const auto grip = juce::Point<int>{titleBar_.getWidth() / 2, titleBar_.getHeight() / 2};
    const auto onScreen = titleBar_.localPointToGlobal(grip);

    // JUCE moves a window after the real pointer, not after the event it is
    // handed: an event queued behind a move would carry a stale position. So
    // the real pointer is moved, and put back where it was afterwards.
    const auto pointerWas = juce::Desktop::getMousePosition();

    const auto event = [&](juce::Point<int> screen, int clicks)
    {
        source.setScreenPosition(screen.toFloat());
        const auto local = titleBar_.getLocalPoint(nullptr, screen).toFloat();
        return juce::MouseEvent{source,
                                local,
                                held,
                                juce::MouseInputSource::defaultPressure,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                &titleBar_,
                                &titleBar_,
                                now,
                                grip.toFloat(),
                                now,
                                clicks,
                                clicks == 0};
    };

    titleBar_.mouseDown(event(onScreen, 1));

    const auto steps = std::max(std::abs(by.x), std::abs(by.y));
    for (int index = 1; index <= steps; ++index)
    {
        const auto offset =
            (by.toFloat() * (static_cast<float>(index) / static_cast<float>(steps))).roundToInt();
        titleBar_.mouseDrag(event(onScreen + offset, 0));
    }

    titleBar_.mouseUp(event(onScreen + by, 0));
    juce::Desktop::setMousePosition(pointerWas);
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

    add("glisser la barre déplace la fenêtre",
        [this]
        {
            const auto before = window_.getPosition();
            dragWindow({60, 40});
            check(window_.getPosition() == before + juce::Point<int>{60, 40},
                  "de 60 px à droite et 40 px vers le bas, comme la souris");
            dragWindow({-60, -40});
            check(window_.getPosition() == before, "et revient à sa place");
        });

    add("agrandie, la fenêtre ne se glisse pas",
        [this]
        {
            doubleClick(titleBar_, titleBar_.getLocalBounds().getCentre());
            check(window_.isFullScreen(), "agrandie");
            savedBounds_ = window_.getBounds();

            dragWindow({60, 40});
            check(window_.getBounds() == savedBounds_, "elle reste à sa place, plein écran");

            doubleClick(titleBar_, titleBar_.getLocalBounds().getCentre());
            check(!window_.isFullScreen(), "le double-clic lui rend sa taille");
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
            check(levelOf(loudest_).peakDb > -60.0f, "rendue, elle mesure de nouveau");
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
