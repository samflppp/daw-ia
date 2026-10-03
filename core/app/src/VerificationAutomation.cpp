#include "Verification.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/engine/Rendering.h"
#include "daw/ui/model/AutomationEditing.h"
#include "daw/ui/panels/MixerPanel.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace daw::app
{
namespace
{

constexpr double floorDb = -120.0;

// Past it, a window is silence in the render without automation: nothing
// there to compare.
constexpr double audibleDb = -80.0;

[[nodiscard]] juce::String fileSafe(const std::string& text)
{
    return juce::File::createLegalFileName(juce::String::fromUTF8(text.c_str())).replaceCharacter(' ', '-');
}

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

[[nodiscard]] double rmsDb(const juce::AudioBuffer<float>& audio, int channel, int start, int length)
{
    const auto level = audio.getRMSLevel(std::min(channel, audio.getNumChannels() - 1), start, length);
    return level > 0.0f ? std::max(floorDb, 20.0 * std::log10(static_cast<double>(level))) : floorDb;
}

[[nodiscard]] std::string dB(double value)
{
    return juce::String(value, 1).toStdString() + " dB";
}

[[nodiscard]] std::string listOf(const std::vector<double>& values)
{
    std::string text;
    for (const auto value : values)
        text += (text.empty() ? "" : ", ") + juce::String(value, 1).toStdString();
    return text;
}

} // namespace

double Verification::Window::meanDb() const
{
    const auto power = (std::pow(10.0, leftDb / 10.0) + std::pow(10.0, rightDb / 10.0)) / 2.0;
    return power > 0.0 ? std::max(floorDb, 10.0 * std::log10(power)) : floorDb;
}

Verification::Rendered Verification::render(const std::string& name)
{
    const auto file = folder_.getChildFile(fileSafe(name) + ".wav");
    static_cast<void>(file.deleteFile());

    Rendered rendered{};
    if (!engine::renderAsPlayed(edit_, file))
    {
        check(false, "le rendu hors ligne a échoué");
        return rendered;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        check(false, "le rendu est vide");
        return rendered;
    }

    rendered.audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&rendered.audio, 0, rendered.audio.getNumSamples(), 0, true, true);
    rendered.sampleRate = reader->sampleRate;
    report_.add(juce::String::fromUTF8("- écoute : `") + file.getFileName() + "`, " +
                juce::String(rendered.audio.getNumSamples() / rendered.sampleRate, 2) + " s");
    return rendered;
}

std::vector<Verification::Window>
Verification::windowsOf(const Rendered& rendered, double fromBeats, double toBeats, double windowBeats) const
{
    std::vector<Window> windows;
    if (rendered.sampleRate <= 0.0 || windowBeats <= 0.0)
        return windows;

    // The windows are beats of the song, turned into samples by the tempo the
    // Edit plays: after a tempo change, the same bar lands elsewhere in time.
    const auto sampleAt = [this, &rendered](double beats)
    {
        const auto seconds =
            edit_.tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats)).inSeconds();
        return static_cast<int>(std::lround(seconds * rendered.sampleRate));
    };

    for (auto from = fromBeats; from + windowBeats <= toBeats + 1.0e-6; from += windowBeats)
    {
        const auto start = sampleAt(from);
        const auto end = std::min(sampleAt(from + windowBeats), rendered.audio.getNumSamples());
        if (start < 0 || end <= start)
            break;
        windows.push_back(Window{from,
                                 rmsDb(rendered.audio, 0, start, end - start),
                                 rmsDb(rendered.audio, 1, start, end - start)});
    }
    return windows;
}

double Verification::songEndBeats() const
{
    double end = 0.0;
    for (const auto& placement : state_.arrangement())
    {
        if (const auto* pattern = state_.findPattern(placement.patternId); pattern != nullptr)
            end = std::max(end, placement.startBeats + pattern->lengthBeats);
    }
    return end;
}

void Verification::addAutomationSteps()
{
    // --- S13: the automation --------------------------------------------------------
    //
    // The copilot asked for a fade-out, then the same kind of line drawn by
    // hand: a right-click on the master fader opens it in the playlist, clicks
    // lay its points, a drag moves one. Every claim about the sound is a
    // render cut into windows of the song and compared, window by window, to
    // the render without automation. Then the disorder: the tempo halved under
    // the points, undo and redo while the song plays, everything undone.

    const auto barBeats = [this]
    { return state_.timeSignature().numerator * 4.0 / state_.timeSignature().denominator; };
    const auto masterVolume = domain::AutomationTarget::volumeOf(domain::ProjectState::masterTrackId());
    const auto undo = [this] { key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0}); };
    const auto redo = [this] { key(juce::KeyPress{'y', juce::ModifierKeys::ctrlModifier, 0}); };
    const auto rested = [this] { return juce::Time::getMillisecondCounterHiRes() - stepStartedMs_ > 700.0; };

    add("le morceau en SONG, rendu sans automation",
        [this, barBeats]
        {
            if (clock_.isPlaying())
                static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            press("SONG");

            // The song of the list sounds in the first bar of each pattern
            // only: a fade over its last four bars would fade silence. Each
            // clip's first bar is copied over the bars after it, in one
            // group undone at the end, so that every bar has something to
            // measure.
            untouchedState_ = domain::json::write(state_.toValue());
            std::vector<std::unique_ptr<domain::Command>> fill;
            for (const auto& pattern : state_.patterns())
            {
                for (const auto& clip : pattern.clips)
                {
                    for (auto bar = barBeats(); bar + 1.0e-6 < pattern.lengthBeats; bar += barBeats())
                    {
                        for (const auto& first : clip.notes)
                        {
                            if (first.startBeats >= barBeats())
                                continue;
                            auto copy = first;
                            copy.id = domain::NoteId::generate();
                            copy.startBeats += bar;
                            fill.push_back(std::make_unique<domain::AddNote>(clip.id, copy));
                        }
                    }
                }
            }
            check(bus_.executeGroup(std::move(fill), domain::GroupOptions{"verif : chaque mesure sonne", {}})
                      .ok(),
                  "chaque mesure du morceau sonne");

            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();
            songEnd_ = songEndBeats();
            check(state_.automation().empty(), "aucune ligne d'automation au départ");
            check(songEnd_ >= 4.0 * barBeats(),
                  "le morceau dure au moins quatre mesures : " + juce::String(songEnd_, 1).toStdString() +
                      " temps");
            plain_ = render("s13-automation-sans");
        });

    // --- the copilot's fade-out

    add(
        "« fais un fade-out du master sur les quatre dernières mesures »",
        [this]
        {
            check(copilot_.status() == ui::CopilotHost::Status::ready, "le copilote répond");
            transcriptBefore_ = copilot_.transcript().size();
            copilot_.ask("fais un fade-out du master sur les quatre dernières mesures");
        },
        [this]
        {
            return copilot_.transcript().size() > transcriptBefore_ + 1 &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        120000.0);

    add("la ligne du copilote : cohérente, une entrée, et entendue",
        [this, barBeats, masterVolume]
        {
            const auto& lines = copilot_.transcript();
            if (!lines.empty())
                note("réponse : « " + lines.back().text + " »");

            check(depth() == savedDepth_ + 1, "une seule entrée d'historique");
            const auto& entries = history_.entries();
            const auto* last = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(last != nullptr && last->actor == domain::Actor::copilot, "marquée copilote");

            const auto* line = state_.findAutomationLineFor(masterVolume);
            check(line != nullptr, "une ligne sur le volume du master");
            if (line == nullptr)
                return;
            note("points : " + domain::json::write(line->toValue()));

            const auto fadeFrom = songEnd_ - 4.0 * barBeats();
            const auto resting = ui::automationEditing::staticValue(state_, masterVolume);
            check(std::abs(line->valueAt(fadeFrom) - resting) <= 1.5,
                  "au début des quatre dernières mesures, le volume du master : " +
                      dB(line->valueAt(fadeFrom)) + " pour " + dB(resting));
            check(std::abs(line->valueAt(fadeFrom - barBeats()) - resting) <= 0.5,
                  "rien ne bouge avant : " + dB(line->valueAt(fadeFrom - barBeats())));
            check(line->valueAt(songEnd_) <= -40.0, "à la fin du morceau : " + dB(line->valueAt(songEnd_)));

            auto falling = true;
            for (auto beats = fadeFrom; beats < songEnd_; beats += 0.5)
                falling = falling && line->valueAt(beats + 0.5) <= line->valueAt(beats) + 0.01;
            check(falling, "la ligne ne remonte jamais pendant le fondu");

            // Bar by bar, from the bar before the fade to the end: the level
            // against the render without automation.
            const auto heard = render("s13-automation-fade-copilote");
            const auto withFade = windowsOf(heard, fadeFrom - barBeats(), songEnd_, barBeats());
            const auto without = windowsOf(plain_, fadeFrom - barBeats(), songEnd_, barBeats());
            std::vector<double> drop;
            for (std::size_t index = 0; index < std::min(withFade.size(), without.size()); ++index)
            {
                if (without[index].meanDb() > audibleDb)
                    drop.push_back(withFade[index].meanDb() - without[index].meanDb());
            }
            note("écart au rendu sans automation, mesure par mesure (dB) : " + listOf(drop));
            check(drop.size() >= 4, "au moins quatre mesures audibles à comparer");
            if (drop.size() < 4)
                return;
            check(std::abs(drop.front()) <= 1.0, "la mesure avant le fondu est intacte");
            auto decreasing = true;
            for (std::size_t index = 1; index < drop.size(); ++index)
                decreasing = decreasing && drop[index] <= drop[index - 1] + 0.5;
            check(decreasing, "chaque mesure du fondu est plus basse que la précédente");
            check(drop.back() <= -10.0, "la dernière mesure est bien plus basse : " + dB(drop.back()));
        });

    add("un Ctrl+Z défait le fondu du copilote",
        [this, undo]
        {
            // Without the copilot nothing was written, and a Ctrl+Z here would
            // undo the bars filled above: the steps by hand would then measure
            // a song that is silent three bars out of four. The copilot's own
            // failure is already reported by the step before.
            if (depth() == savedDepth_)
            {
                note("le copilote n'a rien écrit : rien à défaire, Ctrl+Z non pressé");
                return;
            }
            undo();
            check(depth() == savedDepth_, "l'entrée est défaite");
            check(domain::json::write(state_.toValue()) == savedState_, "le projet d'avant, à l'octet près");
        });

    // --- the same kind of line, by hand

    add(
        "F10 : le mixer",
        [this]
        {
            auto* mixer = panel("mixer");
            if (mixer == nullptr || !mixer->isShowing())
                key(juce::KeyPress{juce::KeyPress::F10Key});
        },
        [this]
        {
            auto* mixer = panel("mixer");
            return mixer != nullptr && mixer->isShowing();
        },
        3000.0);

    add(
        "clic droit sur le fader du master : sa ligne, créée et montrée dans la playlist",
        [this, masterVolume]
        {
            auto* strip = mixerStrip(domain::ProjectState::masterTrackId());
            auto* fader = strip != nullptr ? childOfType<juce::Slider>(*strip, 0) : nullptr;
            check(fader != nullptr, "la tranche du master a un fader");
            if (fader == nullptr)
                return;

            const auto before = fader->getValue();
            click(*fader, fader->getLocalBounds().getCentre(), true);
            check(fader->getValue() == before, "le clic droit ne bouge pas le fader");

            const auto* line = state_.findAutomationLineFor(masterVolume);
            check(line != nullptr && line->points.empty(), "une ligne vide sur le volume du master");
            check(depth() == savedDepth_ + 1, "une entrée d'historique");
            masterLine_ = line != nullptr ? line->id : domain::AutomationLineId{};
            check(selection_.automationLine() == masterLine_, "la ligne demandée est celle-là");
        },
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            return playlist != nullptr && playlist->isShowing() && playlist->shownAutomation() == masterLine_;
        },
        3000.0);

    add("deux clics dans la ligne : 0 dB au début, -40 dB à la fin ; une entrée par clic",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            const auto lane = playlist != nullptr ? playlist->laneOfAutomation(masterLine_) : std::nullopt;
            check(lane.has_value(), "la ligne a sa piste dans la playlist, « Master · Volume »");
            if (!lane.has_value())
                return;
            snapshot("s13-ligne-master");

            const auto lay = [this, playlist, &lane](double beats, double value)
            {
                static_cast<void>(playlistBeat(*lane, beats));
                const auto at = playlist->automationPointFor(masterLine_, beats, value);
                if (at.has_value())
                    click(*playlist, *at);
            };

            lay(0.0, 0.0);
            lay(songEnd_, -40.0);

            const auto* line = state_.findAutomationLine(masterLine_);
            check(line != nullptr && line->points.size() == 2, "deux points");
            check(depth() == savedDepth_ + 3, "deux clics, deux entrées");
            if (line == nullptr || line->points.size() != 2)
                return;
            note("points : " + domain::json::write(line->toValue()));
            check(line->points.front().beats == 0.0 && std::abs(line->points.front().value) <= 1.5,
                  "le premier au temps 0, vers 0 dB : " + dB(line->points.front().value));
            check(std::abs(line->points.back().beats - songEnd_) <= 0.5 &&
                      std::abs(line->points.back().value + 40.0) <= 4.0,
                  "le second à la fin, vers -40 dB : " + dB(line->points.back().value));
        });

    add("un point au milieu, retiré au clic droit ; un autre, retiré au double-clic",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            const auto lane = playlist != nullptr ? playlist->laneOfAutomation(masterLine_) : std::nullopt;
            const auto* line = state_.findAutomationLine(masterLine_);
            if (!lane.has_value() || line == nullptr)
                return;

            const auto before = domain::json::write(line->toValue());
            const auto middle = std::round(songEnd_ / 2.0);
            for (const auto right : {true, false})
            {
                static_cast<void>(playlistBeat(*lane, middle));
                const auto at = playlist->automationPointFor(masterLine_, middle, -10.0);
                if (!at.has_value())
                    return;
                click(*playlist, *at);
                const auto* now = state_.findAutomationLine(masterLine_);
                check(now != nullptr && now->points.size() == 3, "un troisième point");

                // Aimed at where it was laid, not at where the click was.
                const auto& laid = now->points[1];
                const auto handle = playlist->automationPointFor(masterLine_, laid.beats, laid.value);
                if (!handle.has_value())
                    return;
                if (right)
                    click(*playlist, *handle, true);
                else
                    doubleClick(*playlist, *handle);
                check(state_.findAutomationLine(masterLine_)->points.size() == 2,
                      right ? "le clic droit le retire" : "le double-clic le retire");
            }
            check(domain::json::write(state_.findAutomationLine(masterLine_)->toValue()) == before,
                  "la ligne est celle d'avant");
            check(depth() == savedDepth_ + 7, "quatre entrées de plus : poser, retirer, poser, retirer");
        });

    add("tirer le dernier point vers le bas : une entrée",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            const auto lane = playlist != nullptr ? playlist->laneOfAutomation(masterLine_) : std::nullopt;
            const auto* line = state_.findAutomationLine(masterLine_);
            if (!lane.has_value() || line == nullptr || line->points.size() != 2)
                return;

            const auto end = line->points.back();
            static_cast<void>(playlistBeat(*lane, end.beats));
            const auto from = playlist->automationPointFor(masterLine_, end.beats, end.value);
            if (!from.has_value())
                return;
            const auto depthBefore = depth();
            drag(*playlist, *from, from->translated(0, 6));

            const auto& moved = state_.findAutomationLine(masterLine_)->points.back();
            check(moved.value < end.value - 1.0,
                  "le point descend de " + dB(end.value) + " à " + dB(moved.value));
            check(moved.beats == end.beats, "sans bouger dans le temps");
            check(depth() == depthBefore + 1, "le geste est une seule entrée");
        });

    add("le volume du master descend tout au long du morceau, au rendu",
        [this, barBeats]
        {
            const auto heard = render("s13-automation-master-a-la-main");
            const auto with = windowsOf(heard, 0.0, songEnd_, barBeats());
            const auto without = windowsOf(plain_, 0.0, songEnd_, barBeats());
            std::vector<double> drop;
            for (std::size_t index = 0; index < std::min(with.size(), without.size()); ++index)
            {
                if (without[index].meanDb() > audibleDb)
                    drop.push_back(with[index].meanDb() - without[index].meanDb());
            }
            note("écart au rendu sans automation, mesure par mesure (dB) : " + listOf(drop));
            if (drop.size() < 2)
            {
                check(false, "au moins deux mesures audibles à comparer");
                return;
            }
            auto decreasing = true;
            for (std::size_t index = 1; index < drop.size(); ++index)
                decreasing = decreasing && drop[index] <= drop[index - 1] + 0.75;
            check(decreasing, "chaque mesure est plus basse que la précédente, à 0,75 dB près");
            check(drop.front() >= -3.0, "la première mesure presque intacte : " + dB(drop.front()));
            check(drop.back() <= drop.front() - 15.0, "la dernière bien plus basse : " + dB(drop.back()));
        });

    // --- the pan of the Kick, from the left to the right

    add(
        "clic droit sur le pan du Kick : sa ligne",
        [this]
        {
            if (state_.tracks().empty())
                return;
            const auto kick = state_.tracks().front().id;
            auto* strip = mixerStrip(kick);
            auto* pan = strip != nullptr ? childOfType<juce::Slider>(*strip, 1) : nullptr;
            check(pan != nullptr, "la tranche du Kick a un pan");
            if (pan == nullptr)
                return;

            click(*pan, pan->getLocalBounds().getCentre(), true);
            const auto* line = state_.findAutomationLineFor(domain::AutomationTarget::panOf(kick));
            check(line != nullptr, "une ligne sur le pan du Kick");
            panLine_ = line != nullptr ? line->id : domain::AutomationLineId{};
        },
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            return playlist != nullptr && playlist->isShowing() && !panLine_.isNil() &&
                   playlist->shownAutomation() == panLine_;
        },
        3000.0);

    add("à gauche au début, à droite à la fin",
        [this]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            const auto lane = playlist != nullptr ? playlist->laneOfAutomation(panLine_) : std::nullopt;
            if (!lane.has_value())
            {
                check(false, "la ligne du pan a sa piste");
                return;
            }
            for (const auto& [beats, value] : {std::pair{0.0, -0.9}, std::pair{songEnd_, 0.9}})
            {
                static_cast<void>(playlistBeat(*lane, beats));
                if (const auto at = playlist->automationPointFor(panLine_, beats, value); at.has_value())
                    click(*playlist, *at);
            }
            const auto* line = state_.findAutomationLine(panLine_);
            check(line != nullptr && line->points.size() == 2, "deux points");
            if (line == nullptr || line->points.size() != 2)
                return;
            note("points : " + domain::json::write(line->toValue()));
            check(line->points.front().value < -0.5 && line->points.back().value > 0.5,
                  "de la gauche vers la droite");
            snapshot("s13-lignes-master-et-pan");
        });

    // Where the Kick crosses from left to right, in beats of the song: the
    // first window where the right is louder than the left, the render
    // without automation taken off.
    const auto crossing = [this](const Rendered& heard, const std::string& what) -> std::optional<double>
    {
        constexpr double windowBeats = 4.0;
        const auto with = windowsOf(heard, 0.0, songEnd_, windowBeats);
        std::vector<double> leaning;
        for (const auto& window : with)
            leaning.push_back(window.leftDb - window.rightDb);
        note(what + " : gauche moins droite, toutes les quatre temps (dB) : " + listOf(leaning));

        // Only the windows that lean: where the Kick does not play, the
        // other channels sit in the middle and say nothing of its pan.
        std::vector<std::pair<double, double>> leaningOnes;
        for (std::size_t index = 0; index < with.size(); ++index)
        {
            if (std::abs(leaning[index]) > 0.3)
                leaningOnes.emplace_back(with[index].fromBeats + windowBeats / 2.0, leaning[index]);
        }
        check(!leaningOnes.empty() && leaningOnes.front().second > 1.0, "le début penche à gauche");
        check(!leaningOnes.empty() && leaningOnes.back().second < -1.0, "la fin penche à droite");

        // Between the last window on the left and the first on the right,
        // where the straight line between them crosses zero.
        for (std::size_t index = 1; index < leaningOnes.size(); ++index)
        {
            const auto [beforeBeats, beforeLean] = leaningOnes[index - 1];
            const auto [afterBeats, afterLean] = leaningOnes[index];
            if (beforeLean > 0.0 && afterLean < 0.0)
                return beforeBeats + (afterBeats - beforeBeats) * beforeLean / (beforeLean - afterLean);
        }
        return std::nullopt;
    };

    add("au rendu, la gauche et la droite se croisent au milieu du morceau",
        [this, crossing]
        {
            const auto crossed = crossing(render("s13-automation-pan"), "pan du Kick");
            check(crossed.has_value() && std::abs(*crossed - songEnd_ / 2.0) <= 4.0,
                  "croisement au temps " +
                      (crossed.has_value() ? juce::String(*crossed, 1).toStdString() : "—") +
                      ", milieu au temps " + juce::String(songEnd_ / 2.0, 1).toStdString());
        });

    // --- the disorder

    add("le tempo divisé par deux sous les points : le croisement reste au même temps",
        [this, crossing]
        {
            const auto& first = state_.tempoPoints().front();
            tempoBefore_ = first.beatsPerMinute;
            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetTempoPointBpm>(first.id, tempoBefore_ / 2.0)));
            check(state_.tempoPoints().front().beatsPerMinute == tempoBefore_ / 2.0,
                  "le tempo passe à " + juce::String(tempoBefore_ / 2.0, 1).toStdString());

            const auto crossed = crossing(render("s13-automation-pan-tempo-moitie"), "à la moitié du tempo");
            check(crossed.has_value() && std::abs(*crossed - songEnd_ / 2.0) <= 4.0,
                  "croisement au temps " +
                      (crossed.has_value() ? juce::String(*crossed, 1).toStdString() : "—") +
                      ": les points ont suivi le temps, pas la seconde");
        });

    add(
        "la lecture, avec toutes ces lignes",
        [this]
        {
            playedState_ = domain::json::write(state_.toValue());
            playedDepth_ = depth();
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportPlay>()));
        },
        [this] { return clock_.isPlaying(); },
        8000.0);

    for (int turn = 1; turn <= 3; ++turn)
    {
        add(
            "Ctrl+Z pendant la lecture (" + std::to_string(turn) + "/3)",
            [this, undo, turn]
            {
                stepStartedMs_ = juce::Time::getMillisecondCounterHiRes();
                const auto before = domain::json::write(state_.toValue());
                undo();
                check(depth() == playedDepth_ - static_cast<std::size_t>(turn), "une entrée défaite");
                check(domain::json::write(state_.toValue()) != before, "le projet a changé");
                check(clock_.isPlaying(), "la lecture continue");
            },
            rested,
            3000.0);
    }
    for (int turn = 1; turn <= 3; ++turn)
    {
        add(
            "Ctrl+Y pendant la lecture (" + std::to_string(turn) + "/3)",
            [this, redo, turn]
            {
                stepStartedMs_ = juce::Time::getMillisecondCounterHiRes();
                redo();
                check(depth() == playedDepth_ - 3 + static_cast<std::size_t>(turn), "une entrée refaite");
                check(clock_.isPlaying(), "la lecture continue");
            },
            rested,
            3000.0);
    }

    add(
        "le projet est celui d'avant les Ctrl+Z ; arrêt",
        [this]
        {
            check(domain::json::write(state_.toValue()) == playedState_, "à l'octet près");
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
        },
        [this] { return !clock_.isPlaying(); },
        3000.0);

    add("tout défaire au Ctrl+Z : plus une ligne",
        [this, undo]
        {
            while (depth() > savedDepth_)
                undo();
            check(state_.automation().empty(), "aucune ligne");
            check(domain::json::write(state_.toValue()) == savedState_, "le projet d'avant, à l'octet près");

            undo();
            check(domain::json::write(state_.toValue()) == untouchedState_,
                  "et un Ctrl+Z de plus retire les notes ajoutées pour mesurer");
        });
}

} // namespace daw::app
