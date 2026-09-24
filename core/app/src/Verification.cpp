#include "Verification.h"

#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/serialization/Json.h"

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
    , folder_(std::move(wiring.folder))
    , run_(wiring.run)
    , finished_(std::move(wiring.finished))
{
}

Verification::~Verification()
{
    stopTimer();
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
    }

    report_.add(juce::String::fromUTF8("# Vérification S10 — ") +
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
    const auto image = view_.createComponentSnapshot(view_.getLocalBounds(), true, 1.0f);
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
    if (!tracktion::Renderer::renderToFile(edit_, file, false))
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

void Verification::click(juce::Component& target, juce::Point<int> at, bool right, bool shift)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();

    auto held = juce::ModifierKeys{right ? juce::ModifierKeys::rightButtonModifier
                                         : juce::ModifierKeys::leftButtonModifier};
    if (shift)
        held = held.withFlags(juce::ModifierKeys::shiftModifier);

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

void Verification::drag(juce::Component& target, juce::Point<int> from, juce::Point<int> to)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto held = juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier};
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

void Verification::key(const juce::KeyPress& press)
{
    static_cast<void>(view_.keyPressed(press));
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
            // Grabbed two beats into the block and let go ten beats further:
            // its start lands on beat 58, which the bar snap brings to 56. Far
            // from a bar line on both sides, so a pixel of rounding cannot
            // change the bar.
            drag(*playlist, playlistBeat(0, 50.0), playlistBeat(0, 60.0));

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

} // namespace daw::app
