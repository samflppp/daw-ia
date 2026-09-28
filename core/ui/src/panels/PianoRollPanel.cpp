#include "daw/ui/panels/PianoRollPanel.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/ui/model/PatternEditing.h"

#include <algorithm>
#include <cmath>

namespace daw::ui
{
namespace
{

constexpr int playheadRefreshMs = 33;

constexpr int semitonesPerOctave = 12;

constexpr double gridStepBeats = PianoRollPanel::gridStepBeats;

constexpr int defaultVelocity = 100;
constexpr int lowestVisiblePitch = 0;
constexpr int highestVisiblePitch = 127;

// The length a pattern gets when the user draws in a project that holds none:
// four bars.
constexpr double newPatternLengthBeats = 16.0;

// A notch of the wheel is about a quarter of a unit in JUCE: the zoom over the
// ruler doubles over four notches, as in the playlist, and a sideways notch
// moves an eighth of the view.
constexpr double zoomPerWheelUnit = 2.0;
constexpr double viewPerWheelUnit = 0.5;

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

// The range snaps to the nearest line, not the one before: a drag that stops
// just short of bar three means bar three.
[[nodiscard]] double snapped(double beats)
{
    return std::max(0.0, std::round(beats / gridStepBeats) * gridStepBeats);
}

[[nodiscard]] juce::String pitchName(int pitch)
{
    return juce::String("C") + juce::String(pitch / semitonesPerOctave - 1);
}

} // namespace

PianoRollPanel::PianoRollPanel(const PanelContext& context)
    : bar_(context.tokens, context.lookAndFeel)
    , reader_(context.prompts)
    , listening_(context.listening)
    , tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
    , clipboard_(context.clipboard)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);
    setWantsKeyboardFocus(true);

    // Choosing a channel is not an edit: it goes to the Selection, the one
    // the rack writes too, so a click in either moves both.
    channelChooser_.setWantsKeyboardFocus(false);
    channelChooser_.setTextWhenNothingSelected("aucun canal");
    channelChooser_.setTextWhenNoChoicesAvailable("aucun canal");
    channelChooser_.setTooltip(u8"Le canal du rack sur lequel s'écrivent les notes");
    channelChooser_.onChange = [this]
    {
        const auto index = channelChooser_.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(state_.tracks().size()))
            return;

        const auto chosen = state_.tracks()[static_cast<std::size_t>(index)].id;
        if (chosen != selection_.track())
            selection_.selectTrack(chosen);
    };
    addAndMakeVisible(channelChooser_);
    rebuildChannelChooser();

    // The way into generation, where it can be seen: a gesture known only
    // from the keyboard does not exist for someone new, and someone new is
    // the most frequent case.
    generate_.setButtonText(juce::String::fromUTF8(u8"✦ Générer"));
    generate_.setTooltip(juce::String::fromUTF8(u8"Choisis une zone (Maj + glisser sur la règle, ou des "
                                                u8"notes), puis dis ce que tu veux entendre (Ctrl+G)"));
    generate_.setWantsKeyboardFocus(false);
    generate_.onClick = [this] { openPrompt(); };
    addAndMakeVisible(generate_);

    // The generation window, hidden until asked for.
    bar_.onKey = [this](const juce::KeyPress& key) { return generationKey(key); };
    bar_.onVariant = [this](int delta) { showVariant(delta); };
    bar_.onAccept = [this] { acceptProposal(); };
    bar_.onListen = [this] { toggleListening(); };
    bar_.onClose = [this]
    {
        closeProposal();
        repaint();
    };
    bar_.onHeightChanged = [this]
    {
        placeBar();
        repaint();
    };
    addChildComponent(bar_);

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
    startTimer(playheadRefreshMs);

    // Pattern mode plays the pattern on screen from the first frame, without
    // waiting for the user to pick one they have already got.
    followCurrentPattern();
}

PianoRollPanel::~PianoRollPanel()
{
    stopListening();
    reader_.cancel();
    stopTimer();
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void PianoRollPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    rebuildChannelChooser();

    // Only on a selection change, and the distinction matters: the project
    // observer is broadcast from inside a bus notification, and an observer
    // may not call back into the bus. A selection is changed by a click, never
    // by a command, so it is the one source it is safe to answer with one.
    if (source == &selection_)
        followCurrentPattern();

    // Another row on screen: bring its notes into the window. The same row
    // edited keeps the window where the wheel left it.
    const auto* edited = clip();
    const auto* owner = track();
    const auto shown = (owner != nullptr ? owner->id.toString() : std::string{}) + "/" +
                       (edited != nullptr ? edited->id.toString() : std::string{});
    if (shown != revealedRow_)
    {
        revealedRow_ = shown;
        picked_.clear();
        revealNotes();
    }

    // A note an undo took away is no longer picked: a pick left pointing at
    // nothing would still count, and the velocity lane would think two notes
    // are picked when none of those on screen is.
    refreshProposal();

    picked_.erase(std::remove_if(picked_.begin(),
                                 picked_.end(),
                                 [edited](domain::NoteId id)
                                 {
                                     return edited == nullptr || std::none_of(edited->notes.begin(),
                                                                              edited->notes.end(),
                                                                              [id](const domain::Note& note)
                                                                              { return note.id == id; });
                                 }),
                  picked_.end());

    repaint();
}

void PianoRollPanel::revealNotes()
{
    const auto* edited = clip();
    if (edited == nullptr || edited->notes.empty())
    {
        // Nothing written yet: the channel's own pitch in the middle, where
        // the first note of a kick or a snare is going to be drawn.
        const auto* owner = track();
        const auto rows = rowsVisible();
        if (owner != nullptr && (owner->channelPitch > topPitch_ || owner->channelPitch <= topPitch_ - rows))
            topPitch_ = std::clamp(
                owner->channelPitch + rows / 2, std::min(rows - 1, highestVisiblePitch), highestVisiblePitch);
        return;
    }

    const auto [lowest, highest] = std::minmax_element(edited->notes.begin(),
                                                       edited->notes.end(),
                                                       [](const domain::Note& lhs, const domain::Note& rhs)
                                                       { return lhs.pitch < rhs.pitch; });
    const auto rows = rowsVisible();
    if (highest->pitch <= topPitch_ && lowest->pitch > topPitch_ - rows)
        return;

    // Two semitones of air above the highest note, and the lowest in sight
    // whenever the window is tall enough for both.
    auto top = std::min(highestVisiblePitch, highest->pitch + 2);
    if (lowest->pitch <= top - rows)
        top = std::max(highest->pitch, lowest->pitch + rows - 1);
    topPitch_ = std::clamp(top, std::min(rows - 1, highestVisiblePitch), highestVisiblePitch);
}

void PianoRollPanel::resized()
{
    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    header = header.reduced(tokens_.integer("space.md"), tokens_.integer("space.xs"));

    // After the title, where FL names the channel its piano roll writes for.
    if (!titled_)
        header.removeFromLeft(tokens_.integer("metric.pianoRoll.keyboardWidth") * 2);
    channelChooser_.setBounds(header.removeFromLeft(tokens_.integer("metric.pianoRoll.keyboardWidth") * 2));
    generate_.setBounds(header.removeFromRight(tokens_.integer("metric.generation.buttonWidth")));

    placeBar();

    // Smaller, the window may have lost the notes it showed.
    revealNotes();
}

// Here and not with the rest of generation: placing a child is for the panel
// file itself, as the hygiene rule 2 has it. The window takes the bottom of
// the panel, under the velocity lane: the grid shrinks, the notes stay seen.
void PianoRollPanel::placeBar()
{
    if (!bar_.isVisible())
        return;
    bar_.setBounds(getLocalBounds().removeFromBottom(bar_.preferredHeight()));
}

juce::Rectangle<int> PianoRollPanel::bodyArea() const
{
    auto area = getLocalBounds();
    if (bar_.isVisible())
        area.removeFromBottom(bar_.preferredHeight());
    return area;
}

void PianoRollPanel::rebuildChannelChooser()
{
    channelChooser_.clear(juce::dontSendNotification);
    for (std::size_t index = 0; index < state_.tracks().size(); ++index)
    {
        const auto& candidate = state_.tracks()[index];
        channelChooser_.addItem(juce::String::fromUTF8(candidate.name.c_str()), static_cast<int>(index) + 1);

        if (candidate.id == selection_.track())
            channelChooser_.setSelectedId(static_cast<int>(index) + 1, juce::dontSendNotification);
    }
    channelChooser_.setEnabled(!state_.tracks().empty());
}

void PianoRollPanel::selectOpened(const RowOpening& opening)
{
    const auto* owner = track();
    if (owner == nullptr)
        return;

    selection_.selectPattern(opening.patternId);
    selection_.selectClip(owner->id, opening.clipId);
    followCurrentPattern();
}

PianoRollPanel::RowOpening PianoRollPanel::openRow() const
{
    RowOpening opening;
    const auto* owner = track();
    if (owner == nullptr)
        return opening;

    auto& commands = opening.commands;

    auto patternId = pattern() != nullptr ? pattern()->id : domain::PatternId{};

    // A project with no pattern gets one, laid down, in the same group: the
    // user asked for a place to write, not for three decisions.
    if (patternId.isNil())
    {
        auto created = patternEditing::newPattern(newPatternLengthBeats);
        patternId = created.patternId;
        for (auto& command : created.commands)
            commands.push_back(std::move(command));
    }

    auto row = patternEditing::rowFor(state_, patternId, owner->id);
    if (row.opening.empty() && !commands.empty())
    {
        // The pattern is being created in this very group, so the row it will
        // hold cannot be read from the state yet.
        row.clipId = domain::ClipId::generate();
        row.opening.push_back(std::make_unique<domain::AddPatternTrack>(patternId, row.clipId, owner->id));
    }

    for (auto& command : row.opening)
        commands.push_back(std::move(command));

    opening.patternId = patternId;
    opening.clipId = row.clipId;
    return opening;
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

const domain::Pattern* PianoRollPanel::pattern() const
{
    return patternEditing::current(state_, selection_);
}

const domain::Clip* PianoRollPanel::clip() const
{
    const auto* selected = track();
    const auto* shown = pattern();
    if (selected == nullptr || shown == nullptr)
        return nullptr;

    // What that track plays in that pattern, and nothing else. There is at
    // most one such row, so there is nothing to choose and nothing to guess.
    return shown->findClipForTrack(selected->id);
}

double PianoRollPanel::patternLength() const
{
    const auto* shown = pattern();
    return shown != nullptr ? shown->lengthBeats : 0.0;
}

double PianoRollPanel::transportBeat(double patternBeats) const
{
    const auto* shown = pattern();
    return shown != nullptr ? patternEditing::transportBeat(state_, shown->id, patternBeats) : patternBeats;
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
    auto area = bodyArea();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromLeft(tokens_.integer("metric.pianoRoll.keyboardWidth"));
    area.removeFromTop(tokens_.integer("metric.pianoRoll.rulerHeight"));
    area.removeFromBottom(tokens_.integer("metric.pianoRoll.velocityLaneHeight"));
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

double PianoRollPanel::fitBeatWidth() const
{
    const auto width = static_cast<double>(std::max(1, gridArea().getWidth()));
    const auto length = patternLength();
    return length > 0.0 ? width / length : width;
}

double PianoRollPanel::beatWidth() const
{
    const auto fit = fitBeatWidth();
    const auto widest = std::max(fit, static_cast<double>(tokens_.integer("metric.pianoRoll.beatWidthMax")));
    return std::clamp(zoom_.value_or(fit), fit, widest);
}

double PianoRollPanel::firstBeat() const
{
    // Never past the end of the pattern: there is nothing to write there.
    const auto visible = static_cast<double>(std::max(1, gridArea().getWidth())) / beatWidth();
    return std::clamp(firstBeat_, 0.0, std::max(0.0, patternLength() - visible));
}

void PianoRollPanel::setView(double first, std::optional<double> zoom)
{
    zoom_ = zoom;
    if (zoom_.has_value() && *zoom_ <= fitBeatWidth())
        zoom_.reset();

    firstBeat_ = first;
    firstBeat_ = firstBeat();
    repaint();
}

double PianoRollPanel::beatAtX(int x) const
{
    const auto length = patternLength();
    if (length <= 0.0)
        return 0.0;

    const auto beats = firstBeat() + static_cast<double>(x - gridArea().getX()) / beatWidth();
    return std::clamp(beats, 0.0, length);
}

int PianoRollPanel::xForBeat(double beats) const
{
    const auto area = gridArea();
    if (patternLength() <= 0.0)
        return area.getX();

    return area.getX() + static_cast<int>(std::llround((beats - firstBeat()) * beatWidth()));
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
    if (!titled_)
    {
        g.drawText("PIANO-ROLL",
                   header.removeFromLeft(tokens_.integer("metric.pianoRoll.keyboardWidth") * 2),
                   juce::Justification::centredLeft,
                   false);
    }

    // A track whose row is not open yet shows an empty grid, as FL does: the
    // first click opens the row and writes the note, in one gesture.
    const auto* edited = clip();
    if (track() == nullptr || pattern() == nullptr)
    {
        paintEmpty(g);
        return;
    }

    const auto* shown = pattern();
    g.setColour(tokens_.colour("color.text.secondary"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));

    // The channel is chosen in the header; the pattern is named and not
    // chosen here: the transport chooses, this panel follows. Saying which
    // one is on screen keeps the two readable as one thing.
    header.setLeft(channelChooser_.getRight() + tokens_.integer("space.md"));
    const auto count = edited != nullptr ? static_cast<int>(edited->notes.size()) : 0;
    const auto patternName = shown != nullptr ? patternEditing::displayName(state_, *shown) : std::string{};

    g.drawText(juce::String(patternName) + juce::String(u8"  ·  ") + juce::String(count) +
                   (count > 1 ? " notes" : " note"),
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
    paintGhosts(g, area);
    paintKeyboard(g, keyboard);
    paintRuler(g, ruler);
    paintRange(g, area);
    paintVelocityLane(g);
    paintPlayhead(g, area);
}

void PianoRollPanel::paintEmpty(juce::Graphics& g) const
{
    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));

    const auto message = track() == nullptr
                             ? u8"choisissez un canal, dans le menu ci-dessus ou dans le rack"
                             : u8"cliquez pour ouvrir la ligne de cette piste et poser une note";

    g.drawText(message, getLocalBounds(), juce::Justification::centred, false);
}

void PianoRollPanel::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) const
{
    // Zoomed in, lines and notes lie left and right of the window.
    const juce::Graphics::ScopedSaveState saved{g};
    g.reduceClipRegion(area);

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

    const auto length = patternLength();
    if (length <= 0.0)
        return;

    const auto barBeats = state_.beatsPerBar();
    for (double beat = 0.0; beat <= length; beat += gridStepBeats)
    {
        const auto intoBar = std::fmod(beat, barBeats);
        const auto onBar = intoBar < gridStepBeats / 2.0 || barBeats - intoBar < gridStepBeats / 2.0;
        const auto onBeat = std::fmod(beat, 1.0) < gridStepBeats / 2.0;

        g.setColour(
            onBar ? tokens_.colour("color.grid.bar")
                  : (onBeat ? tokens_.colour("color.grid.beat") : tokens_.colour("color.grid.subdivision")));

        g.fillRect(xForBeat(beat), area.getY(), hairline, area.getHeight());
    }
}

void PianoRollPanel::paintNotes(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const juce::Graphics::ScopedSaveState saved{g};
    g.reduceClipRegion(area);

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

        // A note Tab would replace is drawn faded under the grey ones.
        if (proposal_.has_value() && proposal_->replaces(note))
            g.setColour(tokens_.colour("color.note.replaced"));
        else
            g.setColour(soft.interpolatedWith(full, amount));
        g.fillRoundedRectangle(bounds, radius);

        if (note.id == selectedNote_ || isPicked(note.id))
        {
            g.setColour(tokens_.colour("color.note.selected"));
            g.drawRoundedRectangle(bounds, radius, tokens_.number("stroke.focus"));
        }
    }

    if (band_.has_value())
    {
        g.setColour(tokens_.colour("color.state.selected"));
        g.fillRect(*band_);
        g.setColour(tokens_.colour("color.accent.primary"));
        g.drawRect(*band_, tokens_.integer("stroke.hairline"));
    }
}

bool PianoRollPanel::isPicked(domain::NoteId id) const
{
    return std::find(picked_.begin(), picked_.end(), id) != picked_.end();
}

juce::Point<int> PianoRollPanel::pointFor(double beats, int pitch) const
{
    // A pixel into the cell, so the click does not fall on the line before.
    return {xForBeat(beats) + tokens_.integer("stroke.hairline") * 2,
            yForPitch(pitch) + tokens_.integer("metric.pianoRoll.keyHeight") / 2};
}

juce::Rectangle<int> PianoRollPanel::noteBounds(const domain::Note& note) const
{
    const auto x = xForBeat(note.startBeats);
    return {x,
            yForPitch(note.pitch),
            std::max(1, xForBeat(note.startBeats + note.lengthBeats) - x),
            tokens_.integer("metric.pianoRoll.keyHeight")};
}

void PianoRollPanel::copyPicked()
{
    const auto* shown = pattern();
    const auto* owner = track();
    if (shown == nullptr || owner == nullptr)
        return;

    auto notes = picked_;
    if (notes.empty() && !selectedNote_.isNil())
        notes.push_back(selectedNote_);
    if (notes.empty())
        return;

    auto copied = copyNotes(*shown, owner->id, notes);
    if (!copied.rows.empty())
        clipboard_.notes = std::move(copied);
}

void PianoRollPanel::pasteNotes(bool duplicate)
{
    const auto* shown = pattern();
    const auto* owner = track();
    if (shown == nullptr || owner == nullptr)
        return;

    if (duplicate)
        copyPicked();
    if (!clipboard_.notes.has_value())
        return;

    // Ctrl+V at the playhead, on the grid, when the pattern on screen is the
    // one sounding; where the copy came from otherwise. Ctrl+B right after
    // the copy, the pattern lengthened when it has to be.
    const auto& copied = *clipboard_.notes;
    auto at = copied.originBeats;
    if (duplicate)
        at = duplicateAt(copied, copied.originBeats, state_.beatsPerBar());
    else if (const auto local = patternEditing::localBeats(state_, shown->id, clock_.positionBeats());
             local.has_value())
        at = quantise(*local);

    auto plan = planPaste(state_, shown->id, copied, {owner->id}, at, duplicate);
    if (plan.commands.empty())
        return;

    domain::GroupOptions group{};
    group.label = (duplicate ? "dupliquer " : "coller ") + std::to_string(plan.pasted.size()) + " notes";
    const auto pasted = plan.pasted;
    if (bus_.executeGroup(std::move(plan.commands), group).ok())
    {
        // The copies become the picked notes: a second Ctrl+B goes on.
        picked_ = pasted;
        selectedNote_ = {};
        repaint();
    }
}

void PianoRollPanel::removePicked()
{
    const auto* edited = clip();
    if (edited == nullptr)
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    for (const auto& note : edited->notes)
    {
        if (isPicked(note.id) || note.id == selectedNote_)
            commands.push_back(std::make_unique<domain::RemoveNote>(edited->id, note.id));
    }
    if (commands.empty())
        return;

    domain::GroupOptions group{};
    group.label = "retirer " + std::to_string(commands.size()) + " notes";
    if (bus_.executeGroup(std::move(commands), group).ok())
    {
        picked_.clear();
        selectedNote_ = {};
        repaint();
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
    const auto length = patternLength();
    if (length <= 0.0)
        return;

    const juce::Graphics::ScopedSaveState saved{g};
    g.reduceClipRegion(area);

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(area);

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(area.getX(),
               area.getBottom() - tokens_.integer("stroke.hairline"),
               area.getWidth(),
               tokens_.integer("stroke.hairline"));

    g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));

    const auto barBeats = state_.beatsPerBar();
    int bar = 1;
    for (double beat = 0.0; beat < length; beat += barBeats, ++bar)
    {
        const auto x = xForBeat(beat);
        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(x, area.getY(), tokens_.integer("stroke.hairline"), area.getHeight());

        g.setColour(tokens_.colour("color.text.disabled"));
        g.drawText(juce::String(bar),
                   area.withX(x + tokens_.integer("space.xs")).withWidth(tokens_.integer("space.xl")),
                   juce::Justification::centredLeft,
                   false);
    }
}

std::optional<int> PianoRollPanel::playheadX() const
{
    const auto length = patternLength();
    if (length <= 0.0)
        return {};

    // The axis is the pattern. In pattern mode the transport plays it from
    // beat 0; in song mode the placement the transport is in comes off before
    // the playhead means anything here. One playhead on one grid, whichever
    // laying is sounding.
    const auto* shown = pattern();
    const auto local = shown != nullptr
                           ? patternEditing::localBeats(state_, shown->id, clock_.positionBeats())
                           : std::nullopt;
    if (!local)
        return {};

    // Zoomed in, the playhead may be out of the window.
    const auto x = xForBeat(*local);
    const auto area = gridArea();
    if (x < area.getX() || x > area.getRight())
        return {};
    return x;
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

void PianoRollPanel::followCurrentPattern()
{
    // Pattern mode plays the pattern on screen. Set when the user picks a
    // pattern and never from inside a bus notification: an observer may not
    // call back into the bus.
    const auto* shown = pattern();
    patternEditing::follow(bus_, state_, shown != nullptr ? shown->id : domain::PatternId{});
}

void PianoRollPanel::movePlayheadTo(int x)
{
    if (patternLength() <= 0.0)
        return;

    // The axis of this panel is the pattern, so what the pixel says has to
    // be read back into transport beats before it means anything there.
    const auto beats = transportBeat(beatAtX(x));

    static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(beats)));
}

void PianoRollPanel::addNoteAt(juce::Point<int> point)
{
    const auto* owner = track();
    if (owner == nullptr)
        return;

    // Drawing in a project that has no pattern, or in a track that has no row
    // in it, opens what is missing first. It is one thing the user did, so it
    // is one group and one Ctrl+Z — the note and the row it needed go back
    // together, because a row left behind by an undone note is a row nobody
    // asked for.
    domain::Note note{};
    note.id = domain::NoteId::generate();
    note.pitch = pitchAtY(point.getY());
    note.velocity = defaultVelocity;
    note.startBeats = quantise(beatAtX(point.getX()));
    note.lengthBeats = gridStepBeats;

    if (clip() == nullptr)
    {
        auto opening = openRow();
        if (opening.commands.empty())
            return;

        opening.commands.push_back(std::make_unique<domain::AddNote>(opening.clipId, note));
        domain::GroupOptions group{};
        group.label = "poser une note";
        if (!bus_.executeGroup(std::move(opening.commands), group).ok())
            return;

        selectOpened(opening);
        selectedNote_ = note.id;
        picked_ = {note.id};
        repaint();
        return;
    }

    if (bus_.execute(std::make_unique<domain::AddNote>(clip()->id, note)).ok())
    {
        selectedNote_ = note.id;
        picked_ = {note.id};
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

    // The middle button drags the view, wherever it is pressed: the hand
    // moves the paper, not the notes.
    if (event.mods.isMiddleButtonDown())
    {
        pan_ = Pan{event.getPosition(), firstBeat(), topPitch_};
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    // The ruler is where the playhead is moved, by click or by drag. It is a
    // transport command like any other: transient, projected, and visible to
    // anything else watching the bus.
    if (rulerArea().contains(event.getPosition()))
    {
        // Shift picks the range generation writes into; a plain click moves
        // the playhead, as it always has.
        if (event.mods.isShiftDown() && patternLength() > 0.0)
        {
            const auto anchor =
                std::min(snapped(beatAtX(event.getPosition().getX())), patternLength() - gridStepBeats);
            rangeAnchor_ = anchor;
            range_ = std::make_pair(anchor, anchor + gridStepBeats);
            repaint();
            return;
        }

        // The chip at the end of the range: the window, for that range.
        if (const auto chip = rangeChip(); chip.has_value() && chip->contains(event.getPosition()))
        {
            openPrompt();
            return;
        }

        draggingPlayhead_ = true;
        movePlayheadTo(event.getPosition().getX());
        return;
    }

    if (velocityArea().contains(event.getPosition()))
    {
        if (!event.mods.isRightButtonDown() && clip() != nullptr)
        {
            velocityStroke_.emplace();
            strokeLast_ = event.getPosition();
            strokeVelocity(strokeLast_, strokeLast_);
        }
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

    // Ctrl + click adds a note to the picked ones or takes it out; Ctrl +
    // drag on empty space picks what the band touches. Neither writes.
    if (event.mods.isCtrlDown())
    {
        if (hit != nullptr)
        {
            if (isPicked(hit->id))
                picked_.erase(std::find(picked_.begin(), picked_.end(), hit->id));
            else
                picked_.push_back(hit->id);
            selectedNote_ = {};
        }
        else
        {
            bandStart_ = event.getPosition();
            band_ = juce::Rectangle<int>{bandStart_, bandStart_};
        }
        repaint();
        return;
    }

    if (hit == nullptr)
    {
        picked_.clear();
        addNoteAt(event.getPosition());
        return;
    }

    selectedNote_ = hit->id;
    if (!isPicked(hit->id))
        picked_ = {hit->id};

    // The grab offset is what keeps a note from jumping under the cursor: the
    // user moves the note, not the point they clicked on.
    Drag drag{};
    drag.noteId = hit->id;

    // Alt turns the drag into a velocity drag, on the note itself: quicker
    // than the lane underneath for one note, since the eye is already there.
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
    if (pan_.has_value())
    {
        const auto delta = event.getPosition() - pan_->start;
        const auto keyHeight = std::max(1, tokens_.integer("metric.pianoRoll.keyHeight"));
        topPitch_ = std::clamp(pan_->topPitch + delta.getY() / keyHeight, rowsVisible(), highestVisiblePitch);
        setView(pan_->firstBeat - static_cast<double>(delta.getX()) / beatWidth(), zoom_);
        return;
    }

    if (rangeAnchor_.has_value())
    {
        const auto at = snapped(beatAtX(event.getPosition().getX()));
        const auto from = std::min(*rangeAnchor_, at);
        const auto to = std::max(*rangeAnchor_ + gridStepBeats, at);
        range_ = std::make_pair(from, std::min(to, patternLength()));
        repaint();
        return;
    }

    if (band_.has_value())
    {
        band_ = juce::Rectangle<int>{bandStart_, event.getPosition()}.getIntersection(gridArea());
        repaint();
        return;
    }

    if (draggingPlayhead_)
    {
        movePlayheadTo(event.getPosition().getX());
        return;
    }

    if (velocityStroke_.has_value())
    {
        strokeVelocity(strokeLast_, event.getPosition());
        strokeLast_ = event.getPosition();
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
    rangeAnchor_.reset();

    if (pan_.has_value())
    {
        pan_.reset();
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (velocityStroke_.has_value())
    {
        commitVelocityStroke();
        return;
    }

    if (band_.has_value())
    {
        const auto area = *band_;
        band_.reset();
        if (const auto* edited = clip(); edited != nullptr)
        {
            for (const auto& note : edited->notes)
            {
                if (noteBounds(note).intersects(area) && !isPicked(note.id))
                    picked_.push_back(note.id);
            }
        }
        repaint();
        return;
    }

    if (!drag_.has_value())
        return;

    static_cast<void>(bus_.endGesture(drag_->gesture));
    drag_.reset();
}

// The wheel moves the pitch window by semitones, the keys by octaves. Over the
// ruler it zooms around the pointer; sideways, or with Shift, it moves along
// the pattern.
//
// J7 left the wheel out on the grounds that a trackpad sends it by accident.
// Using the screen says otherwise: thirty-four rows of a hundred and
// twenty-eight are visible, and reaching the others through a key that needs
// the keyboard focus first is a piano roll that hides most of the piano. A
// semitone per notch is small enough that an accidental notch costs a glance,
// not a lost position.
void PianoRollPanel::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto grid = gridArea();

    // Alt + wheel walks through the variants of a proposal, wherever the
    // pointer is: the eye is on the grey notes, not on a control.
    if (event.mods.isAltDown() && proposal_.has_value())
    {
        if (wheel.deltaY != 0.0f)
            showVariant(wheel.deltaY < 0.0f ? +1 : -1);
        return;
    }

    if (rulerArea().contains(event.getPosition()))
    {
        // Around the pointer: the beat under it stays under it.
        const auto x = std::clamp(event.getPosition().getX(), grid.getX(), grid.getRight());
        const auto anchor = beatAtX(x);
        const auto widest =
            std::max(fitBeatWidth(), static_cast<double>(tokens_.integer("metric.pianoRoll.beatWidthMax")));
        const auto width =
            std::clamp(beatWidth() * std::pow(zoomPerWheelUnit, static_cast<double>(wheel.deltaY)),
                       fitBeatWidth(),
                       widest);
        setView(anchor - static_cast<double>(x - grid.getX()) / width, width);
        return;
    }

    const auto sideways =
        wheel.deltaX != 0.0f ? wheel.deltaX : (event.mods.isShiftDown() ? wheel.deltaY : 0.0f);
    if (sideways != 0.0f)
    {
        const auto visible = static_cast<double>(std::max(1, grid.getWidth())) / beatWidth();
        setView(firstBeat() - static_cast<double>(sideways) * viewPerWheelUnit * visible, zoom_);
        return;
    }

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
    if (generationKey(key))
        return true;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        removePicked();
        return true;
    }

    const auto ctrl = juce::ModifierKeys::ctrlModifier;
    if (key == juce::KeyPress{'c', ctrl, 0})
    {
        copyPicked();
        return true;
    }
    if (key == juce::KeyPress{'v', ctrl, 0})
    {
        pasteNotes(false);
        return true;
    }
    if (key == juce::KeyPress{'b', ctrl, 0})
    {
        pasteNotes(true);
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
