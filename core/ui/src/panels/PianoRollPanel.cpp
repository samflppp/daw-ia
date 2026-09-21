#include "daw/ui/panels/PianoRollPanel.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/domain/commands/TransportCommands.h"

#include <algorithm>
#include <cmath>

namespace daw::ui
{
namespace
{

constexpr int playheadRefreshMs = 33;

constexpr int semitonesPerOctave = 12;
constexpr int beatsPerBar = 4;

// One sixteenth. The grid the beatmaker workspace draws is the grid it snaps
// to: a note that lands between two lines it can see is a note the user has to
// fight.
constexpr double gridStepBeats = 0.25;

constexpr int defaultVelocity = 100;
constexpr int lowestVisiblePitch = 0;
constexpr int highestVisiblePitch = 127;

// The clip a new track gets when the user draws in an empty piano roll: four
// bars, from the start.
constexpr double newClipStartBeats = 0.0;
constexpr double newClipLengthBeats = 16.0;

// How far the hand travels for the whole velocity range. Three pixels per step
// would make 1 to 127 a four-hundred-pixel drag; a panel is not that tall.
constexpr double pixelsPerVelocityStep = 1.5;

[[nodiscard]] bool isBlackKey(int pitch)
{
    switch (((pitch % semitonesPerOctave) + semitonesPerOctave) % semitonesPerOctave)
    {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] juce::String pitchName(int pitch)
{
    return juce::String("C") + juce::String(pitch / semitonesPerOctave - 1);
}

} // namespace

PianoRollPanel::PianoRollPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
{
    setLookAndFeel(&lookAndFeel_);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(clipChooser_);
    clipChooser_.setTextWhenNothingSelected("aucun clip");

    // Selecting a clip is not an edit: it goes to the Selection, never to the
    // bus, and undoing a note must not undo a click.
    clipChooser_.onChange = [this]
    {
        const auto* owner = track();
        const auto index = clipChooser_.getSelectedId() - 1;
        if (owner == nullptr || index < 0 || index >= static_cast<int>(owner->clips.size()))
            return;

        const auto& chosen = owner->clips[static_cast<std::size_t>(index)];
        if (chosen.id != selection_.clip())
        {
            selection_.selectClip(owner->id, chosen.id);
            loopOverEditedClip();
        }
    };

    addAndMakeVisible(addClip_);
    addClip_.onClick = [this] { addClip(); };

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
    startTimer(playheadRefreshMs);
    rebuildClipChooser();

    // The pattern on screen loops from the first frame, without waiting for
    // the user to pick a clip they have already got.
    loopOverEditedClip();
}

PianoRollPanel::~PianoRollPanel()
{
    stopTimer();
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void PianoRollPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    rebuildClipChooser();

    // Only on a selection change, and the distinction matters: the project
    // observer is broadcast from inside a bus notification, and an observer
    // may not call back into the bus. A selection is changed by a click, never
    // by a command, so it is the one source it is safe to answer with one.
    if (source == &selection_)
        loopOverEditedClip();

    repaint();
}

void PianoRollPanel::resized()
{
    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    header = header.reduced(tokens_.integer("space.md"), tokens_.integer("space.xs"));

    addClip_.setBounds(header.removeFromRight(tokens_.integer("metric.pianoRoll.keyboardWidth")));
    header.removeFromRight(tokens_.integer("space.sm"));
    clipChooser_.setBounds(header.removeFromRight(tokens_.integer("metric.pianoRoll.keyboardWidth") * 2));
}

void PianoRollPanel::rebuildClipChooser()
{
    const auto* owner = track();
    const auto* edited = clip();

    clipChooser_.clear(juce::dontSendNotification);
    clipChooser_.setEnabled(owner != nullptr);
    addClip_.setEnabled(owner != nullptr);

    if (owner == nullptr)
        return;

    for (std::size_t index = 0; index < owner->clips.size(); ++index)
    {
        const auto& candidate = owner->clips[index];

        // Bars from one, like the transport readout: a clip that starts at
        // beat 16 starts at bar 5, and that is what a musician looks for.
        const auto bar = static_cast<int>(candidate.startBeats) / beatsPerBar + 1;
        clipChooser_.addItem("Clip " + juce::String(static_cast<int>(index) + 1) +
                                 juce::String(u8"  ·  mes. ") + juce::String(bar),
                             static_cast<int>(index) + 1);

        if (edited != nullptr && candidate.id == edited->id)
            clipChooser_.setSelectedId(static_cast<int>(index) + 1, juce::dontSendNotification);
    }
}

void PianoRollPanel::addClip()
{
    const auto* owner = track();
    if (owner == nullptr)
        return;

    // After the last one, so a new clip never lands on top of an existing one.
    auto start = newClipStartBeats;
    for (const auto& existing : owner->clips)
        start = std::max(start, existing.startBeats + existing.lengthBeats);

    const auto clipId = domain::ClipId::generate();
    if (bus_.execute(std::make_unique<domain::CreateMidiClip>(owner->id, clipId, start, newClipLengthBeats))
            .ok())
    {
        selection_.selectClip(owner->id, clipId);
        loopOverEditedClip();
    }
}

void PianoRollPanel::timerCallback()
{
    // Only the playhead moves on its own, so only its column is repainted. A
    // full repaint thirty times a second would redraw a grid that has not
    // changed since the project was opened.
    //
    // Two columns, not one: the one the playhead is moving to, and the one it
    // is leaving. Repainting only the first is what left a white line behind
    // at every frame until the panel was striped with them.
    const auto wanted = playheadX();
    if (wanted == paintedPlayheadX_)
        return;

    const auto area = gridArea();
    const auto column = [this, area](int x)
    {
        repaint(x - tokens_.integer("space.xs"),
                area.getY() - tokens_.integer("metric.pianoRoll.rulerHeight"),
                tokens_.integer("space.sm"),
                area.getHeight() + tokens_.integer("metric.pianoRoll.rulerHeight"));
    };

    if (paintedPlayheadX_.has_value())
        column(*paintedPlayheadX_);

    if (wanted.has_value())
        column(*wanted);

    paintedPlayheadX_ = wanted;
}

const domain::Track* PianoRollPanel::track() const
{
    return state_.findTrack(selection_.track());
}

const domain::Clip* PianoRollPanel::clip() const
{
    const auto* selected = track();
    if (selected == nullptr || selected->clips.empty())
        return nullptr;

    // The selected clip when there is one, the first otherwise: a track with a
    // single clip must not need a click before it can be edited.
    if (const auto* named = state_.findClip(selection_.clip()); named != nullptr)
        return named;

    return &selected->clips.front();
}

// --- geometry --------------------------------------------------------------

juce::Rectangle<int> PianoRollPanel::rulerArea() const
{
    const auto grid = gridArea();
    return {grid.getX(),
            grid.getY() - tokens_.integer("metric.pianoRoll.rulerHeight"),
            grid.getWidth(),
            tokens_.integer("metric.pianoRoll.rulerHeight")};
}

juce::Rectangle<int> PianoRollPanel::gridArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromLeft(tokens_.integer("metric.pianoRoll.keyboardWidth"));
    area.removeFromTop(tokens_.integer("metric.pianoRoll.rulerHeight"));
    return area;
}

int PianoRollPanel::rowsVisible() const
{
    const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
    return std::max(1, gridArea().getHeight() / keyHeight);
}

int PianoRollPanel::yForPitch(int pitch) const
{
    const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
    return gridArea().getY() + (topPitch_ - pitch) * keyHeight;
}

int PianoRollPanel::pitchAtY(int y) const
{
    const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
    const auto row = (y - gridArea().getY()) / keyHeight;
    return std::clamp(topPitch_ - row, lowestVisiblePitch, highestVisiblePitch);
}

double PianoRollPanel::beatAtX(int x) const
{
    const auto* edited = clip();
    const auto area = gridArea();
    if (edited == nullptr || area.getWidth() <= 0)
        return 0.0;

    const auto fraction = static_cast<double>(x - area.getX()) / static_cast<double>(area.getWidth());
    return std::clamp(fraction, 0.0, 1.0) * edited->lengthBeats;
}

int PianoRollPanel::xForBeat(double beats) const
{
    const auto* edited = clip();
    const auto area = gridArea();
    if (edited == nullptr || edited->lengthBeats <= 0.0)
        return area.getX();

    const auto fraction = std::clamp(beats / edited->lengthBeats, 0.0, 1.0);
    return area.getX() + static_cast<int>(std::llround(fraction * static_cast<double>(area.getWidth())));
}

double PianoRollPanel::quantise(double beats) const
{
    return std::max(0.0, std::floor(beats / gridStepBeats) * gridStepBeats);
}

const domain::Note* PianoRollPanel::noteAt(juce::Point<int> point) const
{
    const auto* edited = clip();
    if (edited == nullptr)
        return nullptr;

    const auto pitch = pitchAtY(point.getY());
    const auto beats = beatAtX(point.getX());

    for (const auto& note : edited->notes)
    {
        if (note.pitch == pitch && beats >= note.startBeats && beats < note.startBeats + note.lengthBeats)
            return &note;
    }

    return nullptr;
}

bool PianoRollPanel::isOnResizeGrip(const domain::Note& note, juce::Point<int> point) const
{
    const auto right = xForBeat(note.startBeats + note.lengthBeats);
    const auto grip = tokens_.integer("metric.pianoRoll.resizeGrip");

    // Half a grip at most: on a note one sixteenth wide the grip would
    // otherwise cover the whole note, and moving it would become impossible.
    const auto width = std::min(
        grip, std::max(xForBeat(note.startBeats + note.lengthBeats) - xForBeat(note.startBeats), 1) / 2);

    return point.getX() >= right - width;
}

// --- painting --------------------------------------------------------------

void PianoRollPanel::paint(juce::Graphics& g)
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
    // Twice the keyboard, because the title does not fit in one: a header
    // clipped to "PIANO-ROL" is the kind of detail a jury reads before it reads
    // anything else.
    g.drawText("PIANO-ROLL",
               header.removeFromLeft(tokens_.integer("metric.pianoRoll.keyboardWidth") * 2),
               juce::Justification::centredLeft,
               false);

    const auto* edited = clip();
    if (edited == nullptr)
    {
        paintEmpty(g);
        return;
    }

    const auto* owner = track();
    g.setColour(tokens_.colour("color.text.secondary"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));

    // The chooser and the button live on the right of the header; the name
    // stops before them rather than being drawn underneath.
    header.removeFromRight(tokens_.integer("metric.pianoRoll.keyboardWidth") * 3 +
                           tokens_.integer("space.sm") + tokens_.integer("space.md"));

    const auto count = static_cast<int>(edited->notes.size());
    g.drawText(juce::String(owner != nullptr ? owner->name : std::string{}) + juce::String(u8"  ·  ") +
                   juce::String(count) + (count > 1 ? " notes" : " note"),
               header,
               juce::Justification::centredLeft,
               false);

    const auto area = gridArea();

    auto keyboard = juce::Rectangle<int>{
        0, area.getY(), tokens_.integer("metric.pianoRoll.keyboardWidth"), area.getHeight()};

    auto ruler = juce::Rectangle<int>{area.getX(),
                                      area.getY() - tokens_.integer("metric.pianoRoll.rulerHeight"),
                                      area.getWidth(),
                                      tokens_.integer("metric.pianoRoll.rulerHeight")};

    paintGrid(g, area);
    paintNotes(g, area);
    paintKeyboard(g, keyboard);
    paintRuler(g, ruler);
    paintPlayhead(g, area);
}

void PianoRollPanel::paintEmpty(juce::Graphics& g) const
{
    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));

    const auto message =
        track() == nullptr ? u8"sélectionnez une piste" : u8"cliquez pour créer un clip et poser une note";

    g.drawText(message, getLocalBounds(), juce::Justification::centred, false);
}

void PianoRollPanel::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
    const auto hairline = tokens_.integer("stroke.hairline");

    for (int row = 0; row < rowsVisible(); ++row)
    {
        const auto pitch = topPitch_ - row;
        const auto y = area.getY() + row * keyHeight;

        g.setColour(isBlackKey(pitch) ? tokens_.colour("color.grid.rowBlack")
                                      : tokens_.colour("color.grid.rowWhite"));
        g.fillRect(area.getX(), y, area.getWidth(), keyHeight);

        g.setColour(tokens_.colour("color.grid.subdivision"));
        g.fillRect(area.getX(), y + keyHeight - hairline, area.getWidth(), hairline);
    }

    const auto* edited = clip();
    if (edited == nullptr)
        return;

    for (double beat = 0.0; beat <= edited->lengthBeats; beat += gridStepBeats)
    {
        const auto onBar = std::fmod(beat, static_cast<double>(beatsPerBar)) < gridStepBeats / 2.0;
        const auto onBeat = std::fmod(beat, 1.0) < gridStepBeats / 2.0;

        g.setColour(
            onBar ? tokens_.colour("color.grid.bar")
                  : (onBeat ? tokens_.colour("color.grid.beat") : tokens_.colour("color.grid.subdivision")));

        g.fillRect(xForBeat(beat), area.getY(), hairline, area.getHeight());
    }
}

void PianoRollPanel::paintNotes(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto* edited = clip();
    if (edited == nullptr)
        return;

    const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
    const auto inset = tokens_.integer("metric.pianoRoll.noteInset");
    const auto radius = tokens_.number("radius.sm");

    for (const auto& note : edited->notes)
    {
        const auto y = yForPitch(note.pitch);
        if (y < area.getY() || y >= area.getBottom())
            continue;

        const auto x = xForBeat(note.startBeats);
        const auto right = xForBeat(note.startBeats + note.lengthBeats);

        const juce::Rectangle<float> bounds{static_cast<float>(x),
                                            static_cast<float>(y + inset),
                                            static_cast<float>(std::max(right - x, inset * 2)),
                                            static_cast<float>(keyHeight - inset * 2)};

        // Velocity is the fill, not a number: the eye reads a shade faster than
        // it reads 0 to 127, and the exact value belongs to an editor nobody
        // needs while sketching.
        const auto soft = tokens_.colour("color.note.fillSoft");
        const auto full = tokens_.colour("color.note.fill");
        const auto amount = static_cast<float>(note.velocity - domain::Note::lowestVelocity) /
                            static_cast<float>(domain::Note::highestVelocity - domain::Note::lowestVelocity);

        g.setColour(soft.interpolatedWith(full, amount));
        g.fillRoundedRectangle(bounds, radius);

        if (note.id == selectedNote_)
        {
            g.setColour(tokens_.colour("color.note.selected"));
            g.drawRoundedRectangle(bounds, radius, tokens_.number("stroke.focus"));
        }
    }
}

void PianoRollPanel::paintKeyboard(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
    const auto hairline = tokens_.integer("stroke.hairline");

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(area);

    for (int row = 0; row < rowsVisible(); ++row)
    {
        const auto pitch = topPitch_ - row;
        const auto y = area.getY() + row * keyHeight;

        if (isBlackKey(pitch))
        {
            g.setColour(tokens_.colour("color.surface.base"));
            g.fillRect(area.getX(), y, area.getWidth(), keyHeight);
        }

        g.setColour(tokens_.colour("color.surface.sunken"));
        g.fillRect(area.getX(), y + keyHeight - hairline, area.getWidth(), hairline);

        // Only the C's are named. A label on every key is a column of noise
        // next to the thing the user is actually looking at.
        if (pitch % semitonesPerOctave == 0)
        {
            g.setColour(tokens_.colour("color.text.tertiary"));
            g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
            g.drawText(pitchName(pitch),
                       area.withY(y).withHeight(keyHeight).withTrimmedRight(tokens_.integer("space.sm")),
                       juce::Justification::centredRight,
                       false);
        }
    }

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(area.getRight() - hairline, area.getY(), hairline, area.getHeight());
}

void PianoRollPanel::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto* edited = clip();
    if (edited == nullptr)
        return;

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(area);

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(area.getX(),
               area.getBottom() - tokens_.integer("stroke.hairline"),
               area.getWidth(),
               tokens_.integer("stroke.hairline"));

    g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));

    for (double beat = 0.0; beat < edited->lengthBeats; beat += static_cast<double>(beatsPerBar))
    {
        const auto x = xForBeat(beat);
        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(x, area.getY(), tokens_.integer("stroke.hairline"), area.getHeight());

        g.setColour(tokens_.colour("color.text.disabled"));
        g.drawText(juce::String(static_cast<int>(beat) / beatsPerBar + 1),
                   area.withX(x + tokens_.integer("space.xs")).withWidth(tokens_.integer("space.xl")),
                   juce::Justification::centredLeft,
                   false);
    }
}

std::optional<int> PianoRollPanel::playheadX() const
{
    const auto* edited = clip();
    if (edited == nullptr)
        return {};

    const auto local = clock_.positionBeats() - edited->startBeats;
    if (local < 0.0 || local > edited->lengthBeats)
        return {};

    return xForBeat(local);
}

void PianoRollPanel::paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto x = playheadX();
    if (!x.has_value())
        return;

    g.setColour(tokens_.colour("color.accent.live"));
    g.fillRect(*x,
               area.getY() - tokens_.integer("metric.pianoRoll.rulerHeight"),
               tokens_.integer("stroke.playhead"),
               area.getHeight() + tokens_.integer("metric.pianoRoll.rulerHeight"));
}

// --- editing ---------------------------------------------------------------

void PianoRollPanel::loopOverEditedClip()
{
    // The beatmaker plays a pattern over and over, and the pattern is the clip
    // on screen. The loop is set when the user chooses a clip and never from
    // inside a bus notification: an observer may not call back into the bus.
    const auto* edited = clip();
    if (edited == nullptr)
    {
        static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(false, 0.0, 0.0)));
        return;
    }

    static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(
        true, edited->startBeats, edited->startBeats + edited->lengthBeats)));
}

void PianoRollPanel::movePlayheadTo(int x)
{
    const auto* edited = clip();
    if (edited == nullptr)
        return;

    // The axis of this panel is the span of the clip being edited, so what the
    // pixel says has to have the clip's own start added back before it means
    // anything on the timeline.
    const auto beats = edited->startBeats + beatAtX(x);

    static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(beats)));
}

void PianoRollPanel::addNoteAt(juce::Point<int> point)
{
    const auto* owner = track();
    if (owner == nullptr)
        return;

    auto clipId = clip() != nullptr ? clip()->id : domain::ClipId{};

    // Drawing in a track that has no clip creates the clip first. Two commands,
    // therefore two history entries: they are two decisions, and undoing the
    // note must not take the clip away with it.
    if (clipId.isNil())
    {
        clipId = domain::ClipId::generate();
        if (!bus_.execute(std::make_unique<domain::CreateMidiClip>(
                              owner->id, clipId, newClipStartBeats, newClipLengthBeats))
                 .ok())
            return;

        selection_.selectClip(owner->id, clipId);
        loopOverEditedClip();
    }

    domain::Note note{};
    note.id = domain::NoteId::generate();
    note.pitch = pitchAtY(point.getY());
    note.velocity = defaultVelocity;
    note.startBeats = quantise(beatAtX(point.getX()));
    note.lengthBeats = gridStepBeats;

    if (bus_.execute(std::make_unique<domain::AddNote>(clipId, note)).ok())
    {
        selectedNote_ = note.id;
        repaint();
    }
}

void PianoRollPanel::removeNote(domain::NoteId noteId)
{
    const auto* edited = clip();
    if (edited == nullptr || noteId.isNil())
        return;

    if (bus_.execute(std::make_unique<domain::RemoveNote>(edited->id, noteId)).ok())
    {
        selectedNote_ = {};
        repaint();
    }
}

void PianoRollPanel::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    // The ruler is where the playhead is moved, by click or by drag. It is a
    // transport command like any other: transient, projected, and visible to
    // anything else watching the bus.
    if (rulerArea().contains(event.getPosition()))
    {
        draggingPlayhead_ = true;
        movePlayheadTo(event.getPosition().getX());
        return;
    }

    // Outside the grid there is no music to edit. Without this, a click in the
    // header landed on a pitch clamped to 127 and wrote a note nobody asked
    // for -- and the header now holds two controls, so it is clicked on
    // purpose.
    if (!gridArea().contains(event.getPosition()))
        return;

    const auto* hit = noteAt(event.getPosition());

    if (event.mods.isRightButtonDown())
    {
        if (hit != nullptr)
            removeNote(hit->id);
        return;
    }

    if (hit == nullptr)
    {
        addNoteAt(event.getPosition());
        return;
    }

    selectedNote_ = hit->id;

    // The grab offset is what keeps a note from jumping under the cursor: the
    // user moves the note, not the point they clicked on.
    Drag drag{};
    drag.noteId = hit->id;

    // Alt turns the drag into a velocity drag. Velocity is read in the shade of
    // the note, so it is changed where it is read, on the note itself, rather
    // than in a lane underneath that would cost a third of the panel.
    if (event.mods.isAltDown())
        drag.mode = DragMode::velocity;
    else
        drag.mode = isOnResizeGrip(*hit, event.getPosition()) ? DragMode::resize : DragMode::move;

    drag.grabOffsetBeats = beatAtX(event.getPosition().getX()) - hit->startBeats;
    drag.grabPitch = hit->pitch;
    drag.grabY = event.getPosition().getY();
    drag.grabVelocity = hit->velocity;

    // Plain char and not u8: a gesture label is a std::string_view for the
    // domain, which carries UTF-8 bytes and never decodes them itself.
    const auto* label = "déplacer une note";
    if (drag.mode == DragMode::resize)
        label = "allonger une note";
    else if (drag.mode == DragMode::velocity)
        label = "vélocité d'une note";

    drag.gesture = bus_.beginGesture(label);
    drag_ = drag;

    repaint();
}

void PianoRollPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingPlayhead_)
    {
        movePlayheadTo(event.getPosition().getX());
        return;
    }

    if (!drag_.has_value())
        return;

    const auto* edited = clip();
    if (edited == nullptr)
        return;

    const auto start = quantise(beatAtX(event.getPosition().getX()) - drag_->grabOffsetBeats);
    const auto pitch = pitchAtY(event.getPosition().getY());

    const auto found =
        std::find_if(edited->notes.begin(),
                     edited->notes.end(),
                     [this](const domain::Note& candidate) { return candidate.id == drag_->noteId; });

    if (found == edited->notes.end())
        return;

    const auto* note = &(*found);

    domain::ExecuteOptions options{};
    options.gesture = drag_->gesture;

    if (drag_->mode == DragMode::velocity)
    {
        // Up is louder, and the movement is relative to where the note was.
        const auto travelled = static_cast<double>(drag_->grabY - event.getPosition().getY());
        const auto wanted = std::clamp(drag_->grabVelocity +
                                           static_cast<int>(std::llround(travelled / pixelsPerVelocityStep)),
                                       domain::Note::lowestVelocity,
                                       domain::Note::highestVelocity);

        if (wanted == note->velocity)
            return;

        if (bus_.execute(std::make_unique<domain::SetNoteVelocity>(edited->id, drag_->noteId, wanted),
                         options)
                .ok())
        {
            drag_->moved = true;
            repaint();
        }

        return;
    }

    if (drag_->mode == DragMode::resize)
    {
        // The right edge follows the cursor and the start stays where it is.
        // One sixteenth is the floor: a note of length zero is refused by the
        // domain, and a note the user cannot see is a note they cannot delete.
        const auto edge = quantise(beatAtX(event.getPosition().getX()) + gridStepBeats);
        const auto length = std::max(edge - note->startBeats, gridStepBeats);

        if (std::abs(note->lengthBeats - length) < gridStepBeats / 2.0)
            return;

        if (bus_.execute(std::make_unique<domain::ResizeNote>(edited->id, drag_->noteId, length), options)
                .ok())
            drag_->moved = true;

        return;
    }

    // Nothing changed at this pixel: no command, no history, no projection.
    // A drag emits one command per quantised step, not one per mouse move.
    if (note->pitch == pitch && std::abs(note->startBeats - start) < gridStepBeats / 2.0)
        return;

    if (bus_.execute(std::make_unique<domain::MoveNote>(edited->id, drag_->noteId, pitch, start), options)
            .ok())
        drag_->moved = true;
}

// The cursor is the only thing that says where the grip is. A note is fourteen
// pixels tall and often one sixteenth wide; anything drawn on it would be
// bigger than the note.
void PianoRollPanel::mouseMove(const juce::MouseEvent& event)
{
    const auto* hit = noteAt(event.getPosition());
    const auto onGrip = hit != nullptr && isOnResizeGrip(*hit, event.getPosition());

    setMouseCursor(onGrip ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void PianoRollPanel::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    draggingPlayhead_ = false;

    if (!drag_.has_value())
        return;

    static_cast<void>(bus_.endGesture(drag_->gesture));
    drag_.reset();
}

// The wheel moves the pitch window by semitones, the keys by octaves.
//
// J7 left the wheel out on the grounds that a trackpad sends it by accident.
// Using the screen says otherwise: thirty-four rows of a hundred and
// twenty-eight are visible, and reaching the others through a key that needs
// the keyboard focus first is a piano roll that hides most of the piano. A
// semitone per notch is small enough that an accidental notch costs a glance,
// not a lost position.
void PianoRollPanel::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(event);

    const auto steps = static_cast<int>(std::round(wheel.deltaY * static_cast<float>(semitonesPerOctave)));
    if (steps == 0)
        return;

    const auto wanted = topPitch_ + steps;
    const auto clamped = std::clamp(wanted, rowsVisible(), highestVisiblePitch);

    if (clamped == topPitch_)
        return;

    topPitch_ = clamped;
    repaint();
}

bool PianoRollPanel::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        removeNote(selectedNote_);
        return true;
    }

    if (key == juce::KeyPress::pageUpKey)
    {
        topPitch_ = std::min(highestVisiblePitch, topPitch_ + semitonesPerOctave);
        repaint();
        return true;
    }

    if (key == juce::KeyPress::pageDownKey)
    {
        topPitch_ = std::max(rowsVisible(), topPitch_ - semitonesPerOctave);
        repaint();
        return true;
    }

    return false;
}

} // namespace daw::ui
