#include "Verification.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TrackCommands.h"
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

struct Timing
{
    double median{0.0};
    double p95{0.0};
    double worst{0.0};
    int count{0};
};

[[nodiscard]] Timing timingOf(std::vector<double> ms)
{
    Timing timing{};
    if (ms.empty())
        return timing;

    std::sort(ms.begin(), ms.end());
    const auto n = ms.size();
    timing.count = static_cast<int>(n);
    timing.median = ms[n / 2];
    timing.p95 = ms[std::min(n - 1, static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n))) - 1)];
    timing.worst = ms.back();
    return timing;
}

// One real repaint of a component and everything in it, the way its window's
// peer paints it: at the display's scale, clipped to `area`, into an image of
// the renderer asked for. NativeImageType is Direct2D on Windows in JUCE 8,
// the window's own renderer; SoftwareImageType is the one the light mode would
// use. What is timed is the painting up to the context being flushed, not the
// allocation of the image.
[[nodiscard]] double
paintMs(juce::Component& component, juce::Rectangle<int> area, const juce::ImageType& type)
{
    const auto scale = juce::Component::getApproximateScaleFactorForComponent(&component);
    juce::Image image{juce::Image::ARGB,
                      std::max(1, juce::roundToInt(static_cast<float>(area.getWidth()) * scale)),
                      std::max(1, juce::roundToInt(static_cast<float>(area.getHeight()) * scale)),
                      false,
                      type};

    const auto started = juce::Time::getMillisecondCounterHiRes();
    {
        juce::Graphics g{image};
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setOrigin(-area.getPosition());
        g.reduceClipRegion(area);
        component.paintEntireComponent(g, true);
    }
    return juce::Time::getMillisecondCounterHiRes() - started;
}

[[nodiscard]] Timing measure(juce::Component& component, const juce::ImageType& type)
{
    for (int index = 0; index < warmUps; ++index)
        static_cast<void>(paintMs(component, component.getLocalBounds(), type));

    std::vector<double> ms;
    for (int index = 0; index < samples; ++index)
        ms.push_back(paintMs(component, component.getLocalBounds(), type));
    return timingOf(std::move(ms));
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

[[nodiscard]] std::string describe(const DragTiming& timing);

[[nodiscard]] std::string describe(const Timing& timing)
{
    return "médiane " + juce::String(timing.median, 2).toStdString() + " ms, 95e centile " +
           juce::String(timing.p95, 2).toStdString() + " ms, pire " +
           juce::String(timing.worst, 2).toStdString() + " ms (" + std::to_string(timing.count) +
           " repeints)";
}

std::string describe(const DragTiming& timing)
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

        // Which pages were open before: the machine remembers them, and a
        // measure must leave the person's screen as it found it.
        std::vector<std::pair<const char*, bool>> pagesBefore;
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
