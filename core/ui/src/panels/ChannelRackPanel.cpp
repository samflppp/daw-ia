#include "daw/ui/panels/ChannelRackPanel.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/ui/model/PatternEditing.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace daw::ui
{
namespace
{

constexpr int playheadRefreshMs = 33;
constexpr int semitonesPerOctave = 12;

constexpr int defaultVelocity = 100;

// The length a pattern gets when the rack makes one: four bars.
constexpr double newPatternLengthBeats = 16.0;

// The resolutions the chooser offers, in beats. A screen setting: it decides
// how finely the pattern is cut on screen, never how long the pattern is.
struct Resolution
{
    double beats;
    const char* label;
};

constexpr Resolution resolutions[] = {
    {1.0, "noires"}, {0.5, "croches"}, {0.25, "doubles"}, {0.125, "triples"}};

[[nodiscard]] juce::String pitchName(int pitch)
{
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const auto index = ((pitch % semitonesPerOctave) + semitonesPerOctave) % semitonesPerOctave;
    return juce::String(names[index]) + juce::String(pitch / semitonesPerOctave - 1);
}

} // namespace

ChannelRackPanel::ChannelRackPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
    , clipboard_(context.clipboard)
    , samples_(context.samples)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);

    addAndMakeVisible(patternChooser_);

    // A chooser that kept the focus after a click would open its list on the
    // next press of Space instead of letting the transport have it.
    // The rack takes Ctrl+C, Ctrl+V and Ctrl+B; Space and Ctrl+Z go on up to
    // the view as before.
    setWantsKeyboardFocus(true);
    patternChooser_.setWantsKeyboardFocus(false);
    resolutionChooser_.setWantsKeyboardFocus(false);
    patternChooser_.setTextWhenNothingSelected("aucun pattern");

    // Choosing a pattern is not an edit: it goes to the Selection, never to
    // the bus. The piano roll listens to the same Selection, so the two move
    // together without either knowing about the other.
    patternChooser_.onChange = [this]
    {
        const auto index = patternChooser_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(state_.patterns().size()))
            return;

        const auto& chosen = state_.patterns()[static_cast<std::size_t>(index)];
        if (chosen.id != selection_.pattern())
        {
            selection_.selectPattern(chosen.id);
            patternEditing::follow(bus_, state_, chosen.id);
        }
    };

    addAndMakeVisible(resolutionChooser_);
    for (int index = 0; index < static_cast<int>(std::size(resolutions)); ++index)
        resolutionChooser_.addItem(resolutions[static_cast<std::size_t>(index)].label, index + 1);
    resolutionChooser_.setSelectedId(3, juce::dontSendNotification); // doubles-croches

    resolutionChooser_.onChange = [this]
    {
        const auto index = resolutionChooser_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(std::size(resolutions)))
            return;

        resolutionBeats_ = resolutions[static_cast<std::size_t>(index)].beats;
        repaint();
    };

    addAndMakeVisible(addPattern_);
    addPattern_.onClick = [this] { createPattern(); };

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
    startTimer(playheadRefreshMs);
    rebuildPatternChooser();
}

ChannelRackPanel::~ChannelRackPanel()
{
    stopTimer();
    cancelPendingUpdate();
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void ChannelRackPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // A selection is changed by a click, never by a command, so pattern mode
    // can follow it at once. A project change arrives from inside a bus
    // notification, where calling back into the bus is refused: the check is
    // put off until the notification is over.
    if (source == &selection_)
        followCurrentPattern();
    else
        triggerAsyncUpdate();

    rebuildPatternChooser();
    repaint();
}

void ChannelRackPanel::handleAsyncUpdate()
{
    followCurrentPattern();
}

void ChannelRackPanel::followCurrentPattern()
{
    // The pattern pattern mode plays is the one this rack shows. An undo that
    // removed it, or a pattern.remove, leaves the rack on another one, and the
    // transport has to follow it there rather than audition a name that finds
    // nothing.
    const auto* shown = pattern();
    patternEditing::follow(bus_, state_, shown != nullptr ? shown->id : domain::PatternId{});
}

void ChannelRackPanel::rebuildPatternChooser()
{
    const auto* shown = pattern();

    patternChooser_.clear(juce::dontSendNotification);

    for (std::size_t index = 0; index < state_.patterns().size(); ++index)
    {
        const auto& candidate = state_.patterns()[index];
        patternChooser_.addItem(patternEditing::displayName(state_, candidate), static_cast<int>(index) + 1);

        if (shown != nullptr && candidate.id == shown->id)
            patternChooser_.setSelectedId(static_cast<int>(index) + 1, juce::dontSendNotification);
    }
}

void ChannelRackPanel::createPattern()
{
    auto created = patternEditing::newPattern(newPatternLengthBeats);

    // Created and not laid down: pattern mode plays it where it is, and the
    // playlist is where it goes onto the song. A group of one, so the history
    // names what the user did.
    domain::GroupOptions group{};
    group.label = "créer un pattern";

    if (!bus_.executeGroup(std::move(created.commands), group).ok())
        return;

    selection_.selectPattern(created.patternId);
}

const domain::Pattern* ChannelRackPanel::pattern() const
{
    return patternEditing::current(state_, selection_);
}

double ChannelRackPanel::stepBeats() const
{
    return resolutionBeats_;
}

int ChannelRackPanel::stepCount() const
{
    const auto* shown = pattern();
    if (shown == nullptr || stepBeats() <= 0.0)
        return 0;

    // The grid follows the pattern, never the reverse: sixteen steps of a
    // sixteenth fill four beats, and a pattern of eight beats shows
    // thirty-two of them.
    return std::max(1, static_cast<int>(std::llround(shown->lengthBeats / stepBeats())));
}

// --- geometry ---------------------------------------------------------------

juce::Rectangle<int> ChannelRackPanel::channelArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    return area.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth"));
}

juce::Rectangle<int> ChannelRackPanel::gridArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth"));
    return area;
}

int ChannelRackPanel::rowAtY(int y) const
{
    const auto area = gridArea();
    const auto rowHeight = tokens_.integer("metric.channelRack.rowHeight");
    if (y < area.getY() || rowHeight <= 0)
        return -1;

    const auto row = (y - area.getY()) / rowHeight;
    return row < static_cast<int>(state_.tracks().size()) ? row : -1;
}

int ChannelRackPanel::stepAtX(int x) const
{
    const auto area = gridArea();
    const auto steps = stepCount();
    if (steps <= 0 || area.getWidth() <= 0 || x < area.getX() || x >= area.getRight())
        return -1;

    const auto step = ((x - area.getX()) * steps) / area.getWidth();
    return std::clamp(step, 0, steps - 1);
}

juce::Rectangle<int> ChannelRackPanel::cellBounds(int row, int step) const
{
    const auto area = gridArea();
    const auto rowHeight = tokens_.integer("metric.channelRack.rowHeight");
    const auto steps = stepCount();
    if (steps <= 0)
        return {};

    // Computed from the edges rather than from a width, so rounding never
    // leaves a one-pixel gap between two cells.
    const auto left = area.getX() + (step * area.getWidth()) / steps;
    const auto right = area.getX() + ((step + 1) * area.getWidth()) / steps;

    return {left, area.getY() + row * rowHeight, right - left, rowHeight};
}

const domain::Note* ChannelRackPanel::noteAt(int row, int step) const
{
    const auto* shown = pattern();
    if (shown == nullptr || row < 0 || step < 0)
        return nullptr;

    if (row >= static_cast<int>(state_.tracks().size()))
        return nullptr;

    const auto& track = state_.tracks()[static_cast<std::size_t>(row)];
    const auto* clip = shown->findClipForTrack(track.id);
    if (clip == nullptr)
        return nullptr;

    // A note belongs to the cell its start falls in, whatever its length. A
    // rack draws where a sound begins; how long it rings is what the piano
    // roll is for.
    const auto from = static_cast<double>(step) * stepBeats();
    const auto to = from + stepBeats();

    for (const auto& note : clip->notes)
    {
        if (note.startBeats >= from - stepBeats() / 2.0 && note.startBeats < to - stepBeats() / 2.0)
            return &note;
    }

    return nullptr;
}

// --- painting ---------------------------------------------------------------

void ChannelRackPanel::resized()
{
    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    header = header.reduced(tokens_.integer("space.md"), tokens_.integer("space.xs"));

    addPattern_.setBounds(header.removeFromRight(tokens_.integer("metric.channelRack.channelWidth") / 2));
    header.removeFromRight(tokens_.integer("space.sm"));
    resolutionChooser_.setBounds(
        header.removeFromRight(tokens_.integer("metric.channelRack.channelWidth") / 2));
    header.removeFromRight(tokens_.integer("space.sm"));
    patternChooser_.setBounds(header.removeFromRight(tokens_.integer("metric.channelRack.channelWidth")));
}

void ChannelRackPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(header);
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    if (!titled_)
    {
        g.drawText("CHANNEL RACK",
                   header.removeFromLeft(tokens_.integer("metric.channelRack.channelWidth")),
                   juce::Justification::centredLeft,
                   false);
    }

    if (pattern() == nullptr || state_.tracks().empty())
    {
        paintEmpty(g);
        return;
    }

    paintGrid(g, gridArea());
    paintChannels(g, channelArea());
    paintPlayhead(g, gridArea());
}

void ChannelRackPanel::paintEmpty(juce::Graphics& g) const
{
    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));

    const auto message =
        state_.tracks().empty() ? u8"ajoutez une piste" : u8"créez un pattern pour commencer à écrire";

    g.drawText(message, getLocalBounds(), juce::Justification::centred, false);
}

void ChannelRackPanel::paintChannels(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto rowHeight = tokens_.integer("metric.channelRack.rowHeight");
    const auto hairline = tokens_.integer("stroke.hairline");
    const auto pitchWidth = tokens_.integer("metric.channelRack.pitchWidth");

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(area);

    for (std::size_t index = 0; index < state_.tracks().size(); ++index)
    {
        const auto& track = state_.tracks()[index];
        auto row = area.withY(area.getY() + static_cast<int>(index) * rowHeight).withHeight(rowHeight);

        if (row.getY() >= area.getBottom())
            break;

        if (track.id == selection_.track() ||
            std::find(picked_.begin(), picked_.end(), track.id) != picked_.end())
        {
            g.setColour(tokens_.colour("color.state.selected"));
            g.fillRect(row);
        }

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(row.getX(), row.getBottom() - hairline, row.getWidth(), hairline);

        auto content = row.reduced(tokens_.integer("space.sm"), 0);

        // The channel's pitch, on the right of its name. It is the note a lit
        // cell plays, so it belongs next to the cells and not in a dialog.
        auto pitch = content.removeFromRight(pitchWidth);
        g.setColour(tokens_.colour(track.muted ? "color.text.disabled" : "color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
        g.drawText(pitchName(track.channelPitch), pitch, juce::Justification::centredRight, false);

        g.setColour(tokens_.colour(track.muted ? "color.text.disabled" : "color.text.primary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
        g.drawText(
            juce::String::fromUTF8(track.name.c_str()), content, juce::Justification::centredLeft, true);

        // A sampler channel is marked, so a dropped kick is told apart from a
        // synth channel at a glance.
        if (track.sample.has_value())
        {
            g.setColour(tokens_.colour("color.actor.copilot"));
            g.fillRect(row.getX(), row.getY(), tokens_.integer("stroke.hairline") * 2, row.getHeight());
        }
    }

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(area.getRight() - hairline, area.getY(), hairline, area.getHeight());
}

void ChannelRackPanel::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto steps = stepCount();
    if (steps <= 0)
        return;

    const auto rowHeight = tokens_.integer("metric.channelRack.rowHeight");
    const auto gap = tokens_.integer("metric.channelRack.stepGap");
    const auto radius = tokens_.number("radius.sm");

    const auto stepsPerBeat = std::max(1, static_cast<int>(std::llround(1.0 / stepBeats())));
    // Steps of a bar: twelve for 6/8, fourteen for 7/8, whatever the grid.
    const auto stepsPerBar =
        std::max(1, static_cast<int>(std::llround(state_.beatsPerBar() * static_cast<double>(stepsPerBeat))));

    const auto soft = tokens_.colour("color.note.fillSoft");
    const auto full = tokens_.colour("color.note.fill");

    for (std::size_t index = 0; index < state_.tracks().size(); ++index)
    {
        const auto row = static_cast<int>(index);
        if (area.getY() + row * rowHeight >= area.getBottom())
            break;

        for (int step = 0; step < steps; ++step)
        {
            const auto cell = cellBounds(row, step).reduced(gap);

            // The bar the step falls in decides the empty shade, so sixteen
            // identical squares do not become a wall a beatmaker has to count.
            const auto onBar = (step % stepsPerBar) == 0;
            const auto onBeat = (step % stepsPerBeat) == 0;

            g.setColour(onBar ? tokens_.colour("color.grid.bar")
                              : (onBeat ? tokens_.colour("color.grid.beat")
                                        : tokens_.colour("color.grid.subdivision")));
            g.fillRoundedRectangle(cell.toFloat(), radius);

            const auto* note = noteAt(row, step);
            if (note == nullptr)
                continue;

            // Velocity is the shade, exactly as in the piano roll: the same
            // value read the same way in the two views of the same note.
            const auto amount =
                static_cast<float>(note->velocity - domain::Note::lowestVelocity) /
                static_cast<float>(domain::Note::highestVelocity - domain::Note::lowestVelocity);

            g.setColour(soft.interpolatedWith(full, amount));
            g.fillRoundedRectangle(cell.toFloat(), radius);
        }
    }
}

std::optional<int> ChannelRackPanel::playheadStep() const
{
    const auto* shown = pattern();
    if (shown == nullptr || stepBeats() <= 0.0)
        return {};

    const auto local = patternEditing::localBeats(state_, shown->id, clock_.positionBeats());
    if (!local)
        return {};

    return std::clamp(static_cast<int>(*local / stepBeats()), 0, std::max(0, stepCount() - 1));
}

void ChannelRackPanel::paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto step = playheadStep();
    if (!step.has_value())
        return;

    const auto column = cellBounds(0, *step);

    g.setColour(tokens_.colour("color.accent.live").withAlpha(0.12f));
    g.fillRect(column.getX(), area.getY(), column.getWidth(), area.getHeight());
}

void ChannelRackPanel::timerCallback()
{
    // Only the column under the playhead is repainted, and only the one it is
    // leaving with it: a full repaint thirty times a second would redraw a
    // grid that has not changed since the pattern was opened.
    const auto wanted = playheadStep();
    if (wanted == paintedPlayheadStep_)
        return;

    const auto area = gridArea();
    const auto column = [this, area](int step)
    {
        const auto bounds = cellBounds(0, step);
        repaint(bounds.getX(), area.getY(), bounds.getWidth(), area.getHeight());
    };

    if (paintedPlayheadStep_.has_value())
        column(*paintedPlayheadStep_);

    if (wanted.has_value())
        column(*wanted);

    paintedPlayheadStep_ = wanted;
}

// --- editing ----------------------------------------------------------------

void ChannelRackPanel::lightCell(int row, int step)
{
    const auto* shown = pattern();
    if (shown == nullptr || row < 0 || row >= static_cast<int>(state_.tracks().size()))
        return;

    const auto& track = state_.tracks()[static_cast<std::size_t>(row)];

    domain::Note note{};
    note.id = domain::NoteId::generate();
    note.pitch = track.channelPitch;
    note.velocity = defaultVelocity;
    note.startBeats = static_cast<double>(step) * stepBeats();
    note.lengthBeats = stepBeats();

    auto openRow = patternEditing::rowFor(state_, shown->id, track.id);
    if (openRow.clipId.isNil())
        return;

    domain::ExecuteOptions options{};
    if (drag_.has_value())
        options.gesture = drag_->gesture;

    // A channel that has no row in this pattern gets one, and the note goes in
    // with it: one group, one Ctrl+Z. A row left behind by an undone note is a
    // row nobody asked for.
    if (!openRow.opening.empty())
    {
        std::vector<std::unique_ptr<domain::Command>> commands;
        for (auto& command : openRow.opening)
            commands.push_back(std::move(command));
        commands.push_back(std::make_unique<domain::AddNote>(openRow.clipId, note));

        domain::GroupOptions group{};
        group.label = "allumer un pas";

        static_cast<void>(bus_.executeGroup(std::move(commands), group));
        return;
    }

    static_cast<void>(bus_.execute(std::make_unique<domain::AddNote>(openRow.clipId, note), options));
}

void ChannelRackPanel::clearCell(int row, int step)
{
    const auto* note = noteAt(row, step);
    if (note == nullptr)
        return;

    const auto* shown = pattern();
    const auto& track = state_.tracks()[static_cast<std::size_t>(row)];
    const auto* clip = shown != nullptr ? shown->findClipForTrack(track.id) : nullptr;
    if (clip == nullptr)
        return;

    domain::ExecuteOptions options{};
    if (drag_.has_value())
        options.gesture = drag_->gesture;

    static_cast<void>(bus_.execute(std::make_unique<domain::RemoveNote>(clip->id, note->id), options));
}

void ChannelRackPanel::applyStrokeAt(int row, int step)
{
    if (!drag_.has_value() || row < 0 || step < 0)
        return;

    // Crossing the same cell twice in one stroke writes once. Without this a
    // slow hand would send a command per mouse move, and the gesture would
    // hold a hundred of them for sixteen cells.
    if (drag_->row == row && drag_->step == step)
        return;

    drag_->row = row;
    drag_->step = step;

    if (drag_->stroke == Stroke::light)
    {
        if (noteAt(row, step) == nullptr)
            lightCell(row, step);
    }
    else
    {
        clearCell(row, step);
    }

    repaint();
}

void ChannelRackPanel::editChannelPitch(int row)
{
    if (row < 0 || row >= static_cast<int>(state_.tracks().size()))
        return;

    const auto& track = state_.tracks()[static_cast<std::size_t>(row)];

    juce::PopupMenu menu;
    // Two octaves around the current pitch: enough to reach a kick from a
    // snare without a list of a hundred and twenty-eight lines.
    const auto lowest = std::max(domain::ProjectState::lowestChannelPitch, track.channelPitch - 12);
    const auto highest = std::min(domain::ProjectState::highestChannelPitch, track.channelPitch + 12);

    for (int pitch = highest; pitch >= lowest; --pitch)
        menu.addItem(pitch + 1, pitchName(pitch), true, pitch == track.channelPitch);

    const auto trackId = track.id;
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                       [this, trackId](int chosen)
                       {
                           if (chosen <= 0)
                               return;

                           static_cast<void>(bus_.execute(
                               std::make_unique<domain::SetTrackChannelPitch>(trackId, chosen - 1)));
                       });
}

void ChannelRackPanel::mouseDown(const juce::MouseEvent& event)
{
    const auto row = rowAtY(event.getPosition().getY());
    if (row < 0)
        return;

    // The channel column selects the track, and a right-click there sets the
    // pitch it plays. Selecting is not an edit and writes no command.
    if (channelArea().contains(event.getPosition()))
    {
        const auto& track = state_.tracks()[static_cast<std::size_t>(row)];

        if (event.mods.isRightButtonDown())
        {
            editChannelPitch(row);
            return;
        }

        grabKeyboardFocus();

        // Ctrl + click adds the channel to the picked ones or takes it out,
        // the grammar of the playlist and the piano roll.
        if (event.mods.isCtrlDown())
        {
            const auto found = std::find(picked_.begin(), picked_.end(), track.id);
            if (found != picked_.end())
                picked_.erase(found);
            else
                picked_.push_back(track.id);
            repaint();
            return;
        }

        picked_ = {track.id};
        selection_.selectTrack(track.id);
        repaint();
        return;
    }

    const auto step = stepAtX(event.getPosition().getX());
    if (step < 0)
        return;

    // The first cell decides what the whole stroke does. A drag that flipped
    // each cell it touched would erase what it had just drawn on the way back.
    const auto lit = noteAt(row, step) != nullptr;

    Drag drag{};
    drag.stroke = lit ? Stroke::clear : Stroke::light;
    drag.gesture = bus_.beginGesture(lit ? "effacer des pas" : "peindre des pas");
    drag_ = drag;

    // The row under the hand becomes the selected track, so the piano roll
    // shows what the rack is writing.
    selection_.selectTrack(state_.tracks()[static_cast<std::size_t>(row)].id);

    applyStrokeAt(row, step);
}

void ChannelRackPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (!drag_.has_value())
        return;

    applyStrokeAt(rowAtY(event.getPosition().getY()), stepAtX(event.getPosition().getX()));
}

void ChannelRackPanel::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (!drag_.has_value())
        return;

    static_cast<void>(bus_.endGesture(drag_->gesture));
    drag_.reset();
}

// --- the clipboard ------------------------------------------------------------

bool ChannelRackPanel::keyPressed(const juce::KeyPress& key)
{
    const auto ctrl = juce::ModifierKeys::ctrlModifier;
    if (key == juce::KeyPress{'c', ctrl, 0})
    {
        copyChannels();
        return true;
    }
    if (key == juce::KeyPress{'v', ctrl, 0})
    {
        pasteChannels(false);
        return true;
    }
    if (key == juce::KeyPress{'b', ctrl, 0})
    {
        pasteChannels(true);
        return true;
    }
    return false;
}

void ChannelRackPanel::copyChannels()
{
    const auto* shown = patternEditing::current(state_, selection_);
    if (shown == nullptr)
        return;

    auto tracks = picked_;
    if (tracks.empty() && !selection_.track().isNil())
        tracks.push_back(selection_.track());
    if (!tracks.empty())
        clipboard_.notes = copyRows(*shown, tracks);
}

void ChannelRackPanel::pasteChannels(bool duplicate)
{
    const auto* shown = patternEditing::current(state_, selection_);
    if (shown == nullptr)
        return;

    // Ctrl+B copies what is picked and lays it right after itself; Ctrl+V
    // lays what was copied at the playhead, on the step under it, or at the
    // pattern's start when the playhead is elsewhere.
    if (duplicate)
        copyChannels();
    if (!clipboard_.notes.has_value())
        return;

    const auto& copied = *clipboard_.notes;
    double at = 0.0;
    if (duplicate)
    {
        at = duplicateAt(copied, copied.originBeats, state_.beatsPerBar());
    }
    else if (const auto local = patternEditing::localBeats(state_, shown->id, clock_.positionBeats());
             local.has_value())
    {
        at = std::floor(*local / stepBeats()) * stepBeats();
    }

    auto plan = planPaste(
        state_, shown->id, copied, duplicate ? std::vector<domain::TrackId>{} : picked_, at, duplicate);
    if (plan.commands.empty())
        return;

    domain::GroupOptions group{};
    group.label =
        duplicate ? "dupliquer les canaux" : "coller " + std::to_string(plan.pasted.size()) + " notes";
    static_cast<void>(bus_.executeGroup(std::move(plan.commands), group));
}

// --- dropping samples ---------------------------------------------------------

void ChannelRackPanel::dropSample(const juce::File& file, int y)
{
    auto sample = samples_.import(file);
    if (!sample)
        return;

    // On a channel: that channel plays the sample from now on.
    if (const auto row = rowAtY(y); row >= 0)
    {
        const auto& track = state_.tracks()[static_cast<std::size_t>(row)];
        static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackSample>(track.id, sample.value())));
        selection_.selectTrack(track.id);
        return;
    }

    // Below the channels: a new one, named after the sample. Two commands, one
    // thing the user did, one Ctrl+Z.
    const auto trackId = domain::TrackId::generate();
    std::vector<std::unique_ptr<domain::Command>> commands;
    commands.push_back(
        std::make_unique<domain::AddTrack>(trackId, file.getFileNameWithoutExtension().toStdString(), 0.0));
    commands.push_back(std::make_unique<domain::SetTrackSample>(trackId, sample.value()));

    domain::GroupOptions group{};
    group.label = "canal sampler : " + sample.value().name;

    if (bus_.executeGroup(std::move(commands), group).ok())
        selection_.selectTrack(trackId);
}

bool ChannelRackPanel::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith("sample:");
}

void ChannelRackPanel::itemDropped(const SourceDetails& details)
{
    const auto description = details.description.toString();
    if (description.startsWith("sample:"))
        dropSample(juce::File{description.fromFirstOccurrenceOf("sample:", false, false)},
                   details.localPosition.y);
}

bool ChannelRackPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(),
                       files.end(),
                       [](const juce::String& path) { return SampleHost::isSampleFile(juce::File{path}); });
}

void ChannelRackPanel::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x);
    for (const auto& path : files)
    {
        if (SampleHost::isSampleFile(juce::File{path}))
        {
            dropSample(juce::File{path}, y);
            return;
        }
    }
}

} // namespace daw::ui
