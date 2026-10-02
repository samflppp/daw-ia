#include "DisplayMode.h"
#include "Verification.h"
#include "VerificationTiming.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/AutomationCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/ui/FrameTicker.h"
#include "daw/ui/PageWindow.h"
#include "daw/ui/panels/PianoRollPanel.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace daw::app
{
namespace
{

// The project the measure is made on: what a beat half-way through looks
// like, not an empty screen.
constexpr int trackCount = 16;
constexpr int patternCount = 8;
constexpr int layingsPerPattern = 5;
constexpr int openNotes = 200;
constexpr int otherNotes = 16;
constexpr double patternBeats = 16.0;
constexpr int automationPoints = 8;

// Repaints kept per measure, after the ones thrown away: the first paints of
// a surface fill the glyph and gradient caches, and they are not what a
// person feels on the hundredth.
constexpr int warmUps = 5;
constexpr int samples = 60;

// The internal window's drag: 60 images, half one way and half back, a few
// pixels each, the pace of a hand.
constexpr int moveFrames = 60;
constexpr int moveStepPx = 5;

// A drag of a block, a note or a band: twenty images, there and back.
constexpr int dragFrames = 20;

// The target of S18 bis: a whole repaint of the playlist fits in one image at
// 120 Hz.
constexpr double targetMs = 8.0;

// Every page of the beatmaker workspace.
constexpr const char* pageIds[] = {"browser",
                                   "playlist",
                                   "channel_rack",
                                   "piano_roll",
                                   "plugin_chain",
                                   "mixer",
                                   "tracks",
                                   "history",
                                   "copilot"};

using timing::describe;
using timing::paintMs;
using timing::Timing;

[[nodiscard]] Timing timingOf(std::vector<double> ms)
{
    return timing::of(std::move(ms));
}

[[nodiscard]] Timing measure(juce::Component& component, const juce::ImageType& type)
{
    return timing::measure(component, type, warmUps, samples);
}

// A drag, image by image, each image being the move of the hand and the
// repaint of what it invalidated. The window is on the software renderer for
// the time of the measure: its peer paints the invalidated region when asked,
// where Direct2D waits for the next vertical blank. The project's change
// message, if the move sent one, is handed over within the image, as the
// message loop would hand it over before the next move.
struct DragTiming
{
    Timing frame;
    Timing paint; // the part spent in the peer's repaint
};

[[nodiscard]] DragTiming dragTiming(juce::Component& target,
                                    juce::Point<int> from,
                                    juce::Point<int> step,
                                    juce::ModifierKeys held,
                                    ui::ProjectObserver* project)
{
    auto* peer = target.getPeer();
    if (peer == nullptr)
        return {};

    const auto engine = peer->getCurrentRenderingEngine();
    peer->setCurrentRenderingEngine(
        std::max(0, peer->getAvailableRenderingEngines().indexOf("Software Renderer")));

    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto event = [&](juce::Point<int> at, juce::ModifierKeys mods, bool dragged)
    {
        return juce::MouseEvent{source,
                                at.toFloat(),
                                mods,
                                juce::MouseInputSource::defaultPressure,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                &target,
                                &target,
                                now,
                                from.toFloat(),
                                now,
                                1,
                                dragged};
    };
    double painting = 0.0;
    const auto settle = [&]
    {
        if (project != nullptr)
            project->dispatchPendingMessages();
        const auto started = juce::Time::getMillisecondCounterHiRes();
        peer->performAnyPendingRepaintsNow();
        painting = juce::Time::getMillisecondCounterHiRes() - started;
    };

    settle();
    target.mouseDown(event(from, held, false));
    settle();

    std::vector<double> ms;
    std::vector<double> paints;
    auto at = from;
    for (int frame = 0; frame < dragFrames; ++frame)
    {
        at += frame < dragFrames / 2 ? step : -step;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        target.mouseDrag(event(at, held, true));
        settle();
        ms.push_back(juce::Time::getMillisecondCounterHiRes() - started);
        paints.push_back(painting);
    }

    target.mouseUp(event(at, held.withoutMouseButtons(), true));
    settle();
    peer->setCurrentRenderingEngine(engine);
    return {timingOf(std::move(ms)), timingOf(std::move(paints))};
}

[[nodiscard]] std::string describe(const DragTiming& timing)
{
    return describe(timing.frame) + " ; dont repeint : médiane " +
           juce::String(timing.paint.median, 2).toStdString() + " ms, 95e centile " +
           juce::String(timing.paint.p95, 2).toStdString() + " ms";
}

} // namespace

// S18 bis: how long the interface takes to paint, on a project with something
// in it. Repaints are timed, not counted: a repaint() that was asked for says
// nothing of what the frame cost. Each surface is painted whole, the way the
// peer paints it after an invalidation, and the internal window is dragged the
// way the hand drags it, each image repainting what it uncovered and covered.
void Verification::buildFluidity()
{
    struct Measures
    {
        domain::PatternId opened{};
        domain::TrackId openedTrack{};
        domain::ClipId openedClip{};
        std::vector<std::pair<std::string, Timing>> rows;

        // A line of automation on the master's volume, and the point the
        // hand drags (S19: not measured in S18 bis).
        domain::AutomationLineId line{};
        domain::AutomationPoint dragged{};

        // Which pages were open before: the machine remembers them, and a
        // measure must leave the person's screen as it found it.
        std::vector<std::pair<const char*, bool>> pagesBefore;

        // The images the display showed while the song played, and the
        // playhead's moves meanwhile.
        std::unique_ptr<juce::VBlankAttachment> vblank;
        std::size_t images{0};
        std::size_t movesAtStart{0};
        std::size_t imagesAtStart{0};
        double playedFromMs{0.0};
        double beatsAtPlay{0.0};

        // The machine's display setting before the run touched it.
        bool wasLight{false};

        // The zoom and the fade, at once and after.
        int barBefore{0};
        int barAtOnce{0};
        float alphaAtOnce{-1.0f};
    };
    auto measures = std::make_shared<Measures>();

    add("un projet rempli : 16 pistes, 8 patterns, 40 blocs, un pattern de 200 notes",
        [this, measures]
        {
            check(state_.tracks().empty() && state_.patterns().empty(),
                  "le projet de départ est vide : la mesure porte sur ce qu'elle a construit");

            // Through the bus, in one entry: the rack and the playlist are not
            // what is verified here, their repaints are.
            std::vector<std::unique_ptr<domain::Command>> commands;
            std::vector<domain::TrackId> tracks;
            for (int index = 0; index < trackCount; ++index)
            {
                tracks.push_back(domain::TrackId::generate());
                commands.push_back(std::make_unique<domain::AddTrack>(
                    tracks.back(), "Piste " + std::to_string(index + 1), 0.0));
            }

            for (int pattern = 0; pattern < patternCount; ++pattern)
            {
                const auto patternId = domain::PatternId::generate();
                commands.push_back(std::make_unique<domain::CreatePattern>(
                    patternId, "Motif " + std::to_string(pattern + 1), patternBeats));

                // Two channels a pattern; the first pattern's first channel
                // holds the notes the piano roll opens on.
                for (int row = 0; row < 2; ++row)
                {
                    const auto trackId = tracks[static_cast<std::size_t>((pattern * 2 + row) % trackCount)];
                    const auto clipId = domain::ClipId::generate();
                    commands.push_back(std::make_unique<domain::AddPatternTrack>(patternId, clipId, trackId));

                    const auto opened = pattern == 0 && row == 0;
                    const auto count = opened ? openNotes : otherNotes;
                    for (int index = 0; index < count; ++index)
                    {
                        domain::Note note{};
                        note.id = domain::NoteId::generate();
                        note.pitch = 48 + (index * 7) % 36;
                        note.velocity = 100;
                        note.startBeats =
                            patternBeats * static_cast<double>(index) / static_cast<double>(count);
                        note.lengthBeats = patternBeats / static_cast<double>(count);
                        commands.push_back(std::make_unique<domain::AddNote>(clipId, note));
                    }

                    if (opened)
                    {
                        measures->opened = patternId;
                        measures->openedTrack = trackId;
                        measures->openedClip = clipId;
                    }
                }

                for (int laying = 0; laying < layingsPerPattern; ++laying)
                    commands.push_back(std::make_unique<domain::PlacePattern>(
                        domain::PlacementId::generate(), patternId, patternBeats * laying));
            }

            // Eight points down the song on the master's volume, the shape
            // of a fade: the lane shows under the blocks' lines.
            measures->line = domain::AutomationLineId::generate();
            commands.push_back(std::make_unique<domain::CreateAutomationLine>(
                measures->line, domain::AutomationTarget::volumeOf(domain::ProjectState::masterTrackId())));
            for (int index = 0; index < automationPoints; ++index)
            {
                domain::AutomationPoint point{};
                point.id = domain::AutomationPointId::generate();
                point.beats = patternBeats * layingsPerPattern * static_cast<double>(index) /
                              static_cast<double>(automationPoints - 1);
                point.value = -1.5 * static_cast<double>(index);
                commands.push_back(std::make_unique<domain::AddAutomationPoint>(measures->line, point));
                if (index == automationPoints / 2)
                    measures->dragged = point;
            }

            const auto built =
                bus_.executeGroup(std::move(commands), domain::GroupOptions{"verif : fluidité", {}});
            check(static_cast<bool>(built), "le projet est construit");
            check(state_.tracks().size() == trackCount, "16 pistes");
            check(state_.arrangement().size() == patternCount * layingsPerPattern, "40 blocs posés");

            // The pattern and its channel are chosen as a click in the rack
            // chooses them; the pages as F-keys open them, the piano roll last
            // so it is in front.
            selection_.selectPattern(measures->opened);
            selection_.selectClip(measures->openedTrack, measures->openedClip);

            for (const auto* id : pageIds)
                measures->pagesBefore.emplace_back(id, panel(id) != nullptr && panel(id)->isShowing());

            for (const auto* id : {"browser", "channel_rack", "history", "copilot", "plugin_chain", "tracks"})
                static_cast<void>(view_.showPage(id, false));
            for (const auto* id : {"playlist", "mixer", "piano_roll"})
                static_cast<void>(view_.showPage(id, true));

            for (const auto* id : {"playlist", "mixer", "piano_roll"})
                check(panel(id) != nullptr && panel(id)->isShowing(),
                      std::string{"la page "} + id + " est ouverte");

            const auto* pattern = state_.findPattern(measures->opened);
            const auto* clip =
                pattern != nullptr ? pattern->findClipForTrack(measures->openedTrack) : nullptr;
            check(clip != nullptr && clip->notes.size() == openNotes, "200 notes dans le pattern ouvert");
        });

    const auto surface = [this, measures](std::string title, const char* id)
    {
        add(title,
            [this, measures, title, id]
            {
                auto* shown = panel(id);
                if (shown == nullptr || !shown->isShowing())
                {
                    check(false, std::string{"la page "} + id + " n'est pas à l'écran");
                    return;
                }

                const auto scale = juce::Component::getApproximateScaleFactorForComponent(shown);
                note(std::to_string(shown->getWidth()) + " × " + std::to_string(shown->getHeight()) +
                     " px, échelle " + juce::String(scale, 2).toStdString());

                const auto direct2d = measure(*shown, juce::NativeImageType{});
                const auto software = measure(*shown, juce::SoftwareImageType{});
                note("Direct2D : " + describe(direct2d));
                note("logiciel : " + describe(software));
                measures->rows.emplace_back(title + " (Direct2D)", direct2d);
                measures->rows.emplace_back(title + " (logiciel)", software);
            });
    };

    surface("repeint complet de la playlist", "playlist");
    add("un défilement de la playlist ne reconstruit rien, un changement du projet reconstruit une fois",
        [this, measures]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
            {
                check(false, "pas de playlist");
                return;
            }

            // The view moves the way a hand moves it: Ctrl + wheel zooms,
            // Shift + wheel scrolls. Each move is painted.
            const auto before = playlist->contentBuilds();
            const auto centre = playlist->getLocalBounds().getCentre();
            wheel(*playlist, centre, 0.5f, false, true);
            static_cast<void>(paintMs(*playlist, playlist->getLocalBounds(), juce::NativeImageType{}));
            wheel(*playlist, centre, -0.25f, true);
            static_cast<void>(paintMs(*playlist, playlist->getLocalBounds(), juce::NativeImageType{}));
            wheel(*playlist, centre, -0.25f, false, true);
            static_cast<void>(paintMs(*playlist, playlist->getLocalBounds(), juce::NativeImageType{}));
            check(playlist->contentBuilds() == before,
                  "zoom et défilement : " + std::to_string(playlist->contentBuilds() - before) +
                      " reconstruction du contenu");

            static_cast<void>(
                bus_.execute(std::make_unique<domain::RenamePattern>(measures->opened, "Motif ouvert")));
            static_cast<void>(paintMs(*playlist, playlist->getLocalBounds(), juce::NativeImageType{}));
            static_cast<void>(paintMs(*playlist, playlist->getLocalBounds(), juce::NativeImageType{}));
            check(playlist->contentBuilds() == before + 1,
                  "un pattern renommé : " + std::to_string(playlist->contentBuilds() - before) +
                      " reconstruction, au premier repeint qui suit");
        });

    surface("repeint complet du piano-roll", "piano_roll");
    surface("repeint complet du mixer", "mixer");

    add("60 images d'un déplacement de la fenêtre du piano-roll au-dessus de la playlist",
        [this, measures]
        {
            auto* roll = panel("piano_roll");
            auto* page = roll != nullptr ? roll->findParentComponentOfClass<ui::PageWindow>() : nullptr;
            if (page == nullptr)
            {
                check(false, "la fenêtre du piano-roll n'est pas trouvée");
                return;
            }

            // A hand on the window's title: the real pointer moves, since JUCE
            // drags after it.
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto pointerWas = juce::Desktop::getMousePosition();
            const auto now = juce::Time::getCurrentTime();
            const auto held = juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier};
            const auto grip =
                juce::Point<int>{page->getWidth() / 2, tokens_.integer("metric.page.titleHeight") / 2};
            const auto onScreen = page->localPointToGlobal(grip);

            const auto event = [&](juce::Point<int> screen, int clicks)
            {
                source.setScreenPosition(screen.toFloat());
                return juce::MouseEvent{source,
                                        page->getLocalPoint(nullptr, screen).toFloat(),
                                        held,
                                        juce::MouseInputSource::defaultPressure,
                                        0.0f,
                                        0.0f,
                                        0.0f,
                                        0.0f,
                                        page,
                                        page,
                                        now,
                                        grip.toFloat(),
                                        now,
                                        clicks,
                                        clicks == 0};
            };

            const auto startedAt = page->getPosition();
            page->mouseDown(event(onScreen, 1));

            std::vector<double> direct2d;
            juce::Point<int> offset;
            for (int frame = 0; frame < moveFrames; ++frame)
            {
                offset.x += frame < moveFrames / 2 ? moveStepPx : -moveStepPx;

                // One image: the event moves the window, and what it left and
                // what it now covers are painted, from the top of the window
                // down, as the peer would.
                const auto started = juce::Time::getMillisecondCounterHiRes();
                const auto before = shell_.getLocalArea(page, page->getLocalBounds());
                page->mouseDrag(event(onScreen + offset, 0));
                const auto after = shell_.getLocalArea(page, page->getLocalBounds());
                static_cast<void>(paintMs(shell_, before.getUnion(after), juce::NativeImageType{}));
                direct2d.push_back(juce::Time::getMillisecondCounterHiRes() - started);
            }

            page->mouseUp(event(onScreen + offset, 0));
            juce::Desktop::setMousePosition(pointerWas);

            check(page->getPosition() == startedAt, "la fenêtre est revenue à sa place");
            const auto timing = timingOf(std::move(direct2d));
            note("Direct2D, déplacement et repeint par image : " + describe(timing));
            measures->rows.emplace_back("déplacement de fenêtre interne, par image (Direct2D)", timing);
        });

    add("glisser un bloc, une note, une bande : chaque image, et ce qu'elle repeint",
        [this, measures]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            const auto* pattern = state_.findPattern(measures->opened);
            const auto* clip =
                pattern != nullptr ? pattern->findClipForTrack(measures->openedTrack) : nullptr;
            if (playlist == nullptr || roll == nullptr || clip == nullptr || clip->notes.empty())
            {
                check(false, "la playlist, le piano-roll ou les notes manquent");
                return;
            }

            // The playlist in front, then the piano roll: each is dragged
            // where the hand would see it.
            static_cast<void>(view_.showPage("playlist", true));
            const auto left = juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier};
            const auto bar = playlist->pointFor(0, state_.beatsPerBar()) - playlist->pointFor(0, 0.0);
            const auto lane = playlist->pointFor(1, 0.0) - playlist->pointFor(0, 0.0);

            const auto depthBefore = depth();
            const auto block =
                dragTiming(*playlist, playlistBeat(0, 0.5), juce::Point<int>{bar.x, 0}, left, project_);
            // Past the song, where no block is: Ctrl there draws a band.
            const auto band = dragTiming(*playlist,
                                         playlistBeat(2, patternBeats * layingsPerPattern + 2.0),
                                         juce::Point<int>{bar.x / 2, lane.y / 2},
                                         left.withFlags(juce::ModifierKeys::ctrlModifier),
                                         project_);

            static_cast<void>(view_.showPage("piano_roll", true));
            // The notes of the pattern are short: at the width that fits the
            // pattern, a note is narrower than its resizing grip, and grabbing
            // it would stretch it. The hand zooms in first, on the ruler.
            for (int notch = 0; notch < 4; ++notch)
                wheel(*roll, roll->ruler().getCentre(), 1.0f);

            // A note in the middle of what the piano roll shows. The notes
            // follow each other, one at a time: the key above a note, at its
            // start, is empty, and a band starts there.
            const auto middle = roll->getLocalBounds().reduced(roll->getWidth() / 4, roll->getHeight() / 4);
            const auto shown =
                std::find_if(clip->notes.begin(),
                             clip->notes.end(),
                             [roll, middle](const domain::Note& candidate)
                             { return middle.contains(roll->noteBounds(candidate).getCentre()); });
            if (shown == clip->notes.end())
            {
                check(false, "aucune note au milieu du piano-roll");
                return;
            }
            const auto grabbed = *shown;
            const auto key = tokens_.integer("metric.pianoRoll.keyHeight");
            const auto noteDrag = dragTiming(
                *roll, roll->pointFor(grabbed.startBeats + 0.01, grabbed.pitch), {0, -key}, left, project_);
            const auto noteBand = dragTiming(*roll,
                                             roll->pointFor(grabbed.startBeats + 0.01, grabbed.pitch + 1),
                                             juce::Point<int>{key, key / 2},
                                             left.withFlags(juce::ModifierKeys::ctrlModifier),
                                             project_);

            for (int notch = 0; notch < 4; ++notch)
                wheel(*roll, roll->ruler().getCentre(), -1.0f);

            note("bloc de la playlist : " + describe(block));
            note("bande de la playlist : " + describe(band));
            note("note du piano-roll : " + describe(noteDrag));
            note("bande du piano-roll : " + describe(noteBand));
            measures->rows.emplace_back("glisser un bloc, par image (logiciel)", block.frame);
            measures->rows.emplace_back("bande de sélection de la playlist, par image (logiciel)",
                                        band.frame);
            measures->rows.emplace_back("glisser une note, par image (logiciel)", noteDrag.frame);
            measures->rows.emplace_back("bande de sélection du piano-roll, par image (logiciel)",
                                        noteBand.frame);

            // There and back: the block where it was, the note at its pitch;
            // the note's drag is one entry, the block's none.
            check(depth() == depthBefore + 1, "le glissé de la note est une entrée, celui du bloc aucune");
            static_cast<void>(bus_.undo());

            // A point of the automation line, dragged up and back by the
            // hand: what it repaints is its lane (S18 bis E).
            static_cast<void>(view_.showPage("playlist", true));
            const auto automationLane = playlist->laneOfAutomation(measures->line);
            if (!automationLane.has_value())
            {
                check(false, "la ligne d'automation a sa bande dans la playlist");
                return;
            }
            static_cast<void>(playlistBeat(*automationLane, measures->dragged.beats));
            const auto handle = playlist->automationPointFor(
                measures->line, measures->dragged.beats, measures->dragged.value);
            if (!handle.has_value())
            {
                check(false, "le point d'automation est à l'écran");
                return;
            }
            const auto entriesBefore = depth();
            const auto pointDrag = dragTiming(*playlist, *handle, {0, -1}, left, project_);
            note("point d'automation de la playlist : " + describe(pointDrag));
            measures->rows.emplace_back("glisser un point d'automation, par image (logiciel)",
                                        pointDrag.frame);
            check(depth() == entriesBefore + 1, "le glissé du point est une entrée");
            static_cast<void>(bus_.undo());
        });

    // The playhead, zoomed in so it crosses pixels faster than the display
    // shows images: it should move once per image, whatever the display.
    add(
        "la lecture, deux secondes, la playlist zoomée",
        [this, measures]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;

            static_cast<void>(view_.showPage("playlist", true));
            for (int notch = 0; notch < 4; ++notch)
                wheel(*playlist, playlist->getLocalBounds().getCentre(), 1.0f, false, true);

            press("SONG");
            measures->beatsAtPlay = clock_.positionBeats();
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportPlay>()));

            measures->vblank =
                std::make_unique<juce::VBlankAttachment>(&shell_, [measures] { ++measures->images; });
            measures->playedFromMs = 0.0;
        },
        [this, measures]
        {
            // Counted from the moment the engine plays: before it, the
            // playhead has nowhere to go. Playing is not enough: the engine
            // says it plays ~200 ms before its device gives the first block,
            // and those images were counted as missed moves until S19.
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (measures->playedFromMs == 0.0)
            {
                if (playlist == nullptr || !clock_.isPlaying() ||
                    clock_.positionBeats() == measures->beatsAtPlay)
                    return false;
                measures->movesAtStart = playlist->playheadMoves();
                measures->imagesAtStart = measures->images;
                measures->playedFromMs = juce::Time::getMillisecondCounterHiRes();
            }
            return juce::Time::getMillisecondCounterHiRes() - measures->playedFromMs > 2000.0;
        },
        4000.0);

    add("la tête de lecture avance à chaque image de l'écran",
        [this, measures]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            const auto seconds = (juce::Time::getMillisecondCounterHiRes() - measures->playedFromMs) / 1000.0;
            const auto images = measures->images - measures->imagesAtStart;
            const auto moves = playlist != nullptr ? playlist->playheadMoves() - measures->movesAtStart : 0;
            measures->vblank.reset();
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));

            if (playlist != nullptr)
                for (int notch = 0; notch < 4; ++notch)
                    wheel(*playlist, playlist->getLocalBounds().getCentre(), -1.0f, false, true);

            const auto rate = [seconds](std::size_t count)
            { return juce::String(static_cast<double>(count) / std::max(seconds, 1e-3), 1).toStdString(); };
            note("en " + juce::String(seconds, 2).toStdString() + " s : " + std::to_string(images) +
                 " images de l'écran (" + rate(images) + " par seconde), " + std::to_string(moves) +
                 " déplacements de la tête de lecture (" + rate(moves) + " par seconde)");
            check(images > 0 && static_cast<double>(moves) >= 0.9 * static_cast<double>(images),
                  "un déplacement par image, à 10 % près");
        });

    // The fluid mode's movements, let go for this step: the zoom reaches where
    // the wheel aimed it over a few images, not at once; a page opened by the
    // hand fades in.
    add(
        "fluide : le zoom glisse, une fenêtre ouverte apparaît en fondu",
        [this, measures]
        {
            ui::FrameTicker::holdStill(false);
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;
            static_cast<void>(view_.showPage("playlist", true));
            static_cast<void>(view_.showPage("tracks", false));

            const auto bar = [this, playlist]
            { return playlist->pointFor(0, state_.beatsPerBar()).x - playlist->pointFor(0, 0.0).x; };
            measures->barBefore = bar();
            wheel(*playlist, playlist->getLocalBounds().getCentre(), 1.0f, false, true);
            measures->barAtOnce = bar();

            static_cast<void>(view_.showPage("tracks", true));
            auto* tracks = panel("tracks");
            auto* page = tracks != nullptr ? tracks->findParentComponentOfClass<ui::PageWindow>() : nullptr;
            measures->alphaAtOnce = page != nullptr ? page->getAlpha() : -1.0f;
            measures->playedFromMs = juce::Time::getMillisecondCounterHiRes();
        },
        [measures] { return juce::Time::getMillisecondCounterHiRes() - measures->playedFromMs > 500.0; },
        2000.0);

    add("fluide : ce que le zoom et le fondu sont devenus",
        [this, measures]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            auto* tracks = panel("tracks");
            auto* page = tracks != nullptr ? tracks->findParentComponentOfClass<ui::PageWindow>() : nullptr;
            const auto barAfter = playlist != nullptr ? playlist->pointFor(0, state_.beatsPerBar()).x -
                                                            playlist->pointFor(0, 0.0).x
                                                      : 0;

            note("largeur d'une mesure : " + std::to_string(measures->barBefore) + " px avant, " +
                 std::to_string(measures->barAtOnce) + " px au cran, " + std::to_string(barAfter) +
                 " px une demi-seconde après");
            check(measures->barAtOnce < barAfter && barAfter > measures->barBefore,
                  "le zoom n'est pas atteint au cran, il l'est ensuite");
            check(measures->alphaAtOnce >= 0.0f && measures->alphaAtOnce < 0.5f,
                  "la fenêtre ouverte commence transparente : " +
                      juce::String(measures->alphaAtOnce, 2).toStdString());
            check(page != nullptr && page->getAlpha() == 1.0f && page->isOpaque(),
                  "puis opaque, et marquée opaque de nouveau");

            if (playlist != nullptr)
                wheel(*playlist, playlist->getLocalBounds().getCentre(), -1.0f, false, true);
            static_cast<void>(view_.showPage("tracks", false));
            ui::FrameTicker::holdStill(true);
        });

    // Fichier > Affichage > Léger, through the menu's own item: the software
    // renderer, thirty images a second, applied without a restart. Then the
    // machine's setting as it was.
    add(
        "Affichage > Léger : moteur logiciel, et la lecture",
        [this, measures]
        {
            measures->wasLight = ui::FrameTicker::pace() == ui::FrameTicker::Pace::light;
            titleBar_.runMenuItem(ui::TitleBarView::lightDisplayItem);

            auto* peer = window_.getPeer();
            check(ui::FrameTicker::pace() == ui::FrameTicker::Pace::light, "le rythme léger est pris");
            check(peer != nullptr && display::rendererOf(*peer) != "Direct2D",
                  "la fenêtre dessine avec : " +
                      (peer != nullptr ? display::rendererOf(*peer).toStdString() : ""));

            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (playlist == nullptr)
                return;
            for (int notch = 0; notch < 4; ++notch)
                wheel(*playlist, playlist->getLocalBounds().getCentre(), 1.0f, false, true);
            measures->beatsAtPlay = clock_.positionBeats();
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportPlay>()));
            measures->playedFromMs = 0.0;
        },
        [this, measures]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            if (measures->playedFromMs == 0.0)
            {
                if (playlist == nullptr || !clock_.isPlaying() ||
                    clock_.positionBeats() == measures->beatsAtPlay)
                    return false;
                measures->movesAtStart = playlist->playheadMoves();
                measures->playedFromMs = juce::Time::getMillisecondCounterHiRes();
            }
            return juce::Time::getMillisecondCounterHiRes() - measures->playedFromMs > 2000.0;
        },
        4000.0);

    add("Affichage > Léger : trente images par seconde ; puis le réglage d'avant",
        [this, measures]
        {
            auto* playlist = dynamic_cast<ui::PlaylistPanel*>(panel("playlist"));
            const auto seconds = (juce::Time::getMillisecondCounterHiRes() - measures->playedFromMs) / 1000.0;
            const auto moves = playlist != nullptr ? playlist->playheadMoves() - measures->movesAtStart : 0;
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            if (playlist != nullptr)
                for (int notch = 0; notch < 4; ++notch)
                    wheel(*playlist, playlist->getLocalBounds().getCentre(), -1.0f, false, true);

            const auto wanted = static_cast<double>(tokens_.integer("motion.light.framesPerSecond"));
            const auto rate = static_cast<double>(moves) / std::max(seconds, 1e-3);
            check(rate > wanted * 0.8 && rate < wanted * 1.2,
                  "déplacements de la tête de lecture par seconde : " + juce::String(rate, 1).toStdString());

            titleBar_.runMenuItem(measures->wasLight ? ui::TitleBarView::lightDisplayItem
                                                     : ui::TitleBarView::fluidDisplayItem);
            auto* peer = window_.getPeer();
            if (!measures->wasLight)
                check(ui::FrameTicker::pace() == ui::FrameTicker::Pace::fluid && peer != nullptr &&
                          display::rendererOf(*peer) == "Direct2D",
                      "Fluide de nouveau, Direct2D de nouveau");
        });

    add("les pages comme elles étaient",
        [this, measures]
        {
            for (const auto& [id, open] : measures->pagesBefore)
                static_cast<void>(view_.showPage(id, open));
            for (const auto& [id, open] : measures->pagesBefore)
                check(panel(id) == nullptr || panel(id)->isShowing() == open,
                      std::string{"la page "} + id + (open ? " rouverte" : " refermée"));
        });

    add("les chiffres",
        [this, measures]
        {
#if JUCE_DEBUG
            note("build : Debug — les chiffres de référence sont ceux d'un build Release");
#else
            note("build : Release");
#endif
            report_.add({});
            report_.add(juce::String::fromUTF8("| mesure | médiane (ms) | 95e centile (ms) | pire (ms) |"));
            report_.add("|---|---|---|---|");
            for (const auto& [title, timing] : measures->rows)
                report_.add("| " + juce::String::fromUTF8(title.c_str()) + " | " +
                            juce::String(timing.median, 2) + " | " + juce::String(timing.p95, 2) + " | " +
                            juce::String(timing.worst, 2) + " |");
            report_.add({});

            for (const auto& [title, timing] : measures->rows)
            {
                if (title == "repeint complet de la playlist (Direct2D)")
                    check(timing.p95 < targetMs,
                          "playlist entière sous 8 ms au 95e centile : " +
                              juce::String(timing.p95, 2).toStdString() + " ms");
            }
        });
}

} // namespace daw::app
