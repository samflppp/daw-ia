// Generation in a band of the canvas (S19): the piano roll's S16 gesture,
// where the notes are.
//
// The zone is a range of one pattern, for one track:
//   Shift + drag in a band       draws it, in the block under the hand
//   or, without one              the range of the picked notes, when they are
//                                all in one row
//   or, without either           the whole pattern, in the band under the hand
// Then Ctrl+G opens the generation window docked under the canvas, Enter
// reads the prompt, the proposal is drawn in grey in the band of that track,
// in every block of the pattern (it is the pattern that will change),
// Alt + wheel over it or the arrows of the window walk through the variants,
// Ctrl+Space listens, Tab writes it as one group from the generator, Escape
// drops it. Notes already in the zone are reworked rather than written over,
// as in the piano roll.
//
// What changes from the piano roll: the zone is drawn in the band, not on a
// ruler the song shares; it stops at the edge of the block it starts in; the
// band grows to show the proposal while it is shown; and there is no header
// with a « Générer » button: Ctrl+G only.
//
// Nothing here writes the project but acceptBand(), which sends one group.

#include "daw/domain/generation/Phrase.h"
#include "daw/domain/rights/Rights.h"
#include "daw/ui/model/PatternEditing.h"
#include "daw/ui/model/StyleLearning.h"
#include "daw/ui/model/StyleSource.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>

namespace daw::ui
{
namespace
{

constexpr double stepBeats = 0.25;

} // namespace

// --- the zone ----------------------------------------------------------------------

std::optional<PlaylistPanel::BandTarget> PlaylistPanel::bandTarget() const
{
    if (bandRange_.has_value())
        return bandRange_;

    // The picked notes, when they are all in one row.
    if (!pickedNotes_.empty())
    {
        const auto row = pickedNotes_.front().clip;
        const auto sameRow = std::all_of(pickedNotes_.begin(),
                                         pickedNotes_.end(),
                                         [row](const PickedNote& one) { return one.clip == row; });
        const auto placement = shownPlacement(pickedNotes_.front().placement);
        const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
        const auto* clip = pattern != nullptr
                               ? pattern->findClipForTrack(
                                     [&]
                                     {
                                         const auto* found = state_.findClip(row);
                                         return found != nullptr ? found->trackId : domain::TrackId{};
                                     }())
                               : nullptr;
        if (clip != nullptr && clip->id != row)
            clip = nullptr;
        if (sameRow && clip != nullptr)
        {
            auto from = pattern->lengthBeats;
            auto to = 0.0;
            for (const auto& note : clip->notes)
            {
                if (!isPickedNote(note.id))
                    continue;
                from = std::min(from, note.startBeats);
                to = std::max(to, note.startBeats + note.lengthBeats);
            }
            from = std::floor(from / stepBeats + 1e-9) * stepBeats;
            to = std::min(pattern->lengthBeats, std::ceil(to / stepBeats - 1e-9) * stepBeats);
            if (to > from)
                return BandTarget{placement->id, pattern->id, clip->trackId, from, to};
        }
    }

    // The band under the hand: the whole pattern.
    if (const auto spot = spotAt(pointer_); spot.has_value())
        return BandTarget{spot->placement, spot->pattern, spot->band.band.track, 0.0, spot->patternLength};
    return std::nullopt;
}

bool PlaylistPanel::bandRangeMouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isShiftDown() || event.mods.isCtrlDown() || event.mods.isRightButtonDown())
        return false;
    const auto spot = spotAt(event.getPosition());
    if (!spot.has_value() || spot->edge != 0)
        return false;

    const auto anchor = std::clamp(
        std::floor(spot->beats / stepBeats) * stepBeats, 0.0, std::max(0.0, spot->patternLength - stepBeats));
    rangeAnchor_ = anchor;
    bandRange_ =
        BandTarget{spot->placement, spot->pattern, spot->band.band.track, anchor, anchor + stepBeats};
    repaint();
    return true;
}

bool PlaylistPanel::bandRangeMouseDrag(const juce::MouseEvent& event)
{
    if (!rangeAnchor_.has_value() || !bandRange_.has_value())
        return false;
    const auto placement = shownPlacement(bandRange_->placement);
    const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
    if (pattern == nullptr)
        return true;

    // Whole sixteenths, inside the block the zone started in.
    const auto beats =
        std::clamp(beatAtX(event.getPosition().getX()) - placement->startBeats, 0.0, pattern->lengthBeats);
    const auto here = std::floor(beats / stepBeats) * stepBeats;
    bandRange_->fromBeats = std::min(*rangeAnchor_, here);
    bandRange_->toBeats = std::min(pattern->lengthBeats, std::max(*rangeAnchor_, here) + stepBeats);
    repaint();
    return true;
}

bool PlaylistPanel::bandRangeMouseUp()
{
    if (!rangeAnchor_.has_value())
        return false;
    rangeAnchor_.reset();
    return true;
}

// --- keys --------------------------------------------------------------------------

bool PlaylistPanel::bandGenKey(const juce::KeyPress& key)
{
    if (!canvas_)
        return false;

    if (key == juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0} && !zone_.has_value())
    {
        openBandPrompt();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (bandGen_.has_value())
            closeBand();
        else if (bandRange_.has_value())
            bandRange_.reset();
        else
            return false;
        repaint();
        return true;
    }

    if (!bandGen_.has_value())
        return false;

    if (key == juce::KeyPress::tabKey)
    {
        acceptBand();
        return true;
    }
    if (key == juce::KeyPress{juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0})
    {
        toggleBandListening();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        generateInBand();
        return true;
    }
    return false;
}

// --- the window --------------------------------------------------------------------

void PlaylistPanel::openBandPrompt()
{
    const auto where = bandTarget();
    if (!where.has_value())
        return;

    closeBand();
    closeZone();
    bandGen_ = BandGeneration{};
    bandGen_->where = *where;
    bandRange_ = where;

    bar_.open(juce::String::fromUTF8(bandZoneHasNotes() ? u8"par ex. : une basse sombre en croches"
                                                        : u8"par ex. : des accords tristes en la mineur"));
    bar_.setVisible(true);
    resized();
    bar_.field().grabKeyboardFocus();
    repaint();
}

bool PlaylistPanel::bandZoneHasNotes() const
{
    if (!bandGen_.has_value())
        return false;
    const auto& where = bandGen_->where;
    const auto* pattern = state_.findPattern(where.pattern);
    const auto* row = pattern != nullptr ? pattern->findClipForTrack(where.track) : nullptr;
    return row != nullptr &&
           std::any_of(row->notes.begin(),
                       row->notes.end(),
                       [&where](const domain::Note& note)
                       { return note.startBeats < where.toBeats && note.startBeats >= where.fromBeats; });
}

void PlaylistPanel::generateInBand()
{
    if (!bandGen_.has_value())
        return;
    const auto text = bar_.field().getText().trim();
    if (text.isEmpty())
    {
        bar_.showMessage(juce::String::fromUTF8(u8"Écris d'abord ce que tu veux entendre."));
        return;
    }
    if (!domain::rights::allows(domain::rights::Feature::generation))
    {
        bar_.showMessage(
            juce::String::fromUTF8(domain::rights::refusal(domain::rights::Feature::generation).c_str()));
        return;
    }

    // Enter on the same words asks for another answer.
    if (bandProposing() && text == bandGen_->prompted)
    {
        showBandVariant(+1);
        return;
    }

    const auto where = bandGen_->where;
    const auto* track = state_.findTrack(where.track);
    if (track == nullptr)
        return;

    PromptReader::Zone asked;
    asked.lengthBeats = where.toBeats - where.fromBeats;
    asked.beatsPerBar = state_.beatsPerBar();
    asked.hasNotes = bandZoneHasNotes();
    asked.tracks.push_back(track->name);

    stopListening();
    bandGen_->proposal.reset();
    bandGen_->rework.reset();
    setGhosts({});
    bandGen_->prompted = text;
    bar_.showReading();
    repaint();

    reader_.read(text.toStdString(),
                 std::move(asked),
                 [this, where](PromptReader::Reading reading)
                 {
                     if (!bar_.isVisible() || !bandGen_.has_value() ||
                         bandGen_->where.pattern != where.pattern || bandGen_->where.track != where.track)
                         return;

                     const auto& base = styleModel();
                     auto* learning = styleLearning();
                     auto model = learning != nullptr
                                      ? learning->model(state_, base)
                                      : std::shared_ptr<const domain::generation::StyleModel>{
                                            std::shared_ptr<const domain::generation::StyleModel>{}, &base};

                     domain::generation::Role role{};
                     if (bandZoneHasNotes())
                     {
                         const auto how =
                             reading.transform.value_or(domain::generation::Transform::keepRhythm);
                         auto opened = TransformProposal::open(state_,
                                                               where.pattern,
                                                               where.track,
                                                               where.fromBeats,
                                                               where.toBeats,
                                                               reading.interpretation,
                                                               how,
                                                               std::move(model));
                         if (!opened)
                         {
                             bar_.showMessage(juce::String::fromUTF8(u8"Rien à retoucher dans cette zone."));
                             return;
                         }
                         bandGen_->rework = std::move(opened).value();
                         role = bandGen_->rework->constraints().role.value;
                     }
                     else
                     {
                         auto opened = GhostProposal::open(state_,
                                                           where.pattern,
                                                           where.track,
                                                           where.fromBeats,
                                                           where.toBeats,
                                                           reading.interpretation,
                                                           std::move(model));
                         if (!opened)
                         {
                             bar_.showMessage(juce::String::fromUTF8(u8"Rien à proposer dans cette zone."));
                             return;
                         }
                         bandGen_->proposal = std::move(opened).value();
                         role = bandGen_->proposal->constraints().role.value;
                     }
                     bandGen_->styleShare = learning != nullptr ? learning->share(role) : 0.0;
                     bandGen_->reading = reading;
                     setGhosts(bandGen_->proposal.has_value() ? bandGen_->proposal->notes()
                                                              : bandGen_->rework->notes());
                     showBandProposal();
                     juce::Logger::writeToLog(
                         "generation (canvas): " + juce::String(bandGen_->ghosts.size()) + " notes");
                 });
}

bool PlaylistPanel::bandProposing() const
{
    return bandGen_.has_value() && (bandGen_->proposal.has_value() || bandGen_->rework.has_value());
}

void PlaylistPanel::showBandProposal()
{
    if (!bandProposing())
        return;
    auto& gen = *bandGen_;

    GenerationPanel::Shown shown;
    if (gen.rework.has_value())
        shown.sentence = juce::String::fromUTF8(
            std::string{domain::generation::describe(gen.rework->transform())}.c_str());
    else
    {
        const domain::generation::PhraseInput input{gen.proposal->constraints(),
                                                    gen.proposal->toBeats() - gen.proposal->fromBeats(),
                                                    state_.beatsPerBar(),
                                                    gen.styleShare};
        shown.sentence = juce::String::fromUTF8(domain::generation::phrase(input).c_str());
    }
    shown.notice = juce::String::fromUTF8(gen.reading.notice.c_str());
    const auto& read =
        gen.proposal.has_value() ? gen.proposal->interpretation() : gen.rework->interpretation();
    if (!read.ignored.empty())
    {
        shown.unused = juce::String::fromUTF8(u8"Je n'ai pas utilisé :");
        for (const auto& word : read.ignored)
            shown.unused << " " << juce::String::fromUTF8(word.c_str());
    }
    const auto& constraints =
        gen.proposal.has_value() ? gen.proposal->constraints() : gen.rework->constraints();
    shown.details = juce::String::fromUTF8(domain::generation::describe(constraints).c_str());
    shown.rank = gen.proposal.has_value() ? gen.proposal->rank() : gen.rework->rank();
    shown.drawn = gen.proposal.has_value() ? gen.proposal->drawn() : gen.rework->drawn();
    bar_.showProposal(shown);
    repaint();
}

void PlaylistPanel::showBandVariant(int delta)
{
    if (!bandProposing())
        return;
    auto& gen = *bandGen_;
    const auto before = gen.proposal.has_value() ? gen.proposal->rank() : gen.rework->rank();
    const auto after = gen.proposal.has_value() ? gen.proposal->shift(delta) : gen.rework->shift(delta);
    if (after == before)
        return;
    setGhosts(gen.proposal.has_value() ? gen.proposal->notes() : gen.rework->notes());
    showBandProposal();
    listenBandAgain();
}

// The ghosts can reach rows the band does not show: the band grows while
// they are on screen, and the lines under it move.
void PlaylistPanel::setGhosts(std::vector<domain::generation::GhostNote> ghosts)
{
    if (!bandGen_.has_value())
        return;
    bandGen_->ghosts = std::move(ghosts);
    ++version_;
    updateScrollBars();
    repaint();
}

void PlaylistPanel::acceptBand()
{
    if (!bandProposing())
        return;
    auto gen = std::move(*bandGen_);
    closeBand();
    bandRange_.reset();

    const auto& where = gen.where;
    const auto* pattern = state_.findPattern(where.pattern);
    if (pattern == nullptr)
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    domain::ClipId row{};
    if (const auto* clip = pattern->findClipForTrack(where.track); clip != nullptr)
        row = clip->id;
    else
    {
        auto opening = patternEditing::rowFor(state_, where.pattern, where.track);
        row = opening.clipId;
        commands = std::move(opening.opening);
    }

    auto acceptance =
        gen.proposal.has_value() ? gen.proposal->accept(state_, row) : gen.rework->accept(state_, row);
    for (auto& command : acceptance.commands)
        commands.push_back(std::move(command));
    if (commands.empty())
        return;

    const auto added = acceptance.added;
    if (!bus_.executeGroup(std::move(commands), acceptance.group).ok())
        return;

    pickedNotes_.clear();
    selected_.clear();
    for (const auto id : added)
        pickedNotes_.push_back({where.placement, row, id});
    juce::Logger::writeToLog("generation (canvas): accepted " + juce::String(static_cast<int>(added.size())) +
                             " notes");
    repaint();
}

void PlaylistPanel::closeBand()
{
    if (!bandGen_.has_value())
        return;
    stopListening();
    reader_.cancel();
    bandGen_.reset();
    ++version_;
    updateScrollBars();
    if (bar_.isVisible())
    {
        bar_.setVisible(false);
        resized();
    }
    grabKeyboardFocus();
    repaint();
}

// The project changed under the proposal: the S16 rules, kept in the
// proposal itself.
void PlaylistPanel::refreshBand()
{
    if (!bandProposing())
        return;
    auto& gen = *bandGen_;
    switch (gen.proposal.has_value() ? gen.proposal->refresh(state_) : gen.rework->refresh(state_))
    {
    case GhostProposal::Refresh::closed:
        closeBand();
        return;
    case GhostProposal::Refresh::regenerated:
        setGhosts(gen.proposal.has_value() ? gen.proposal->notes() : gen.rework->notes());
        showBandProposal();
        listenBandAgain();
        break;
    case GhostProposal::Refresh::unchanged:
        break;
    }
}

// Alt + wheel over the proposal: its variants (the S18 grammar: Alt + wheel
// tunes what is under the hand).
bool PlaylistPanel::bandGenWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!bandProposing() || !event.mods.isAltDown() || wheel.deltaY == 0.0f)
        return false;
    const auto spot = spotAt(event.getPosition());
    const auto& where = bandGen_->where;
    if (!spot.has_value() || spot->pattern != where.pattern || spot->band.band.track != where.track ||
        spot->beats < where.fromBeats || spot->beats >= where.toBeats)
        return false;
    showBandVariant(wheel.deltaY > 0.0f ? -1 : +1);
    return true;
}

// --- listening ---------------------------------------------------------------------

void PlaylistPanel::toggleBandListening()
{
    if (listeningHere_)
    {
        stopListening();
        return;
    }
    if (!bandProposing())
        return;
    const auto& where = bandGen_->where;
    ListeningHost::Line line{where.track, where.pattern, where.fromBeats, where.toBeats, bandGen_->ghosts};
    const auto refused = listening_.listen({line});
    if (!refused.empty())
    {
        bar_.showMessage(juce::String::fromUTF8(refused.c_str()));
        return;
    }
    listeningHere_ = true;
    listening_.onEnded = [this]
    {
        listeningHere_ = false;
        listening_.onEnded = nullptr;
        bar_.setListening(false);
    };
    bar_.setListening(true);
}

void PlaylistPanel::listenBandAgain()
{
    if (!listeningHere_ || !bandProposing())
        return;
    const auto& where = bandGen_->where;
    ListeningHost::Line line{where.track, where.pattern, where.fromBeats, where.toBeats, bandGen_->ghosts};
    if (!listening_.listen({line}).empty())
        stopListening();
}

void PlaylistPanel::stopListening()
{
    stopZoneListening();
}

// --- painting ----------------------------------------------------------------------

void PlaylistPanel::paintBandGeneration(juce::Graphics& g,
                                        const domain::Placement& placement,
                                        const domain::Pattern& pattern,
                                        const std::vector<BandArea>& areas,
                                        juce::Rectangle<int> content) const
{
    const auto range = bandGen_.has_value() ? std::optional<BandTarget>{bandGen_->where} : bandRange_;
    if (!range.has_value() || range->pattern != pattern.id)
        return;

    g.saveState();
    g.reduceClipRegion(content);
    for (const auto& area : areas)
    {
        if (area.band.track != range->track)
            continue;
        const auto left = xForBeat(placement.startBeats + range->fromBeats);
        const auto right = xForBeat(placement.startBeats + range->toBeats);
        g.setColour(tokens_.colour("color.note.range"));
        g.fillRect(
            juce::Rectangle<int>{left, area.area.getY(), std::max(1, right - left), area.area.getHeight()});

        if (!bandGen_.has_value())
            continue;
        for (const auto& ghost : bandGen_->ghosts)
        {
            domain::Note shown{};
            shown.pitch = ghost.pitch;
            shown.startBeats = ghost.startBeats;
            shown.lengthBeats = ghost.lengthBeats;
            if (shown.pitch < area.band.low || shown.pitch > area.band.high)
                continue;
            const auto rect = noteRect(area, placement.startBeats, pattern.lengthBeats, shown);
            g.setColour(tokens_.colour("color.note.ghost"));
            g.fillRect(rect);
            g.setColour(tokens_.colour("color.note.ghostOutline"));
            g.drawRect(rect, tokens_.integer("stroke.hairline"));
        }
    }
    g.restoreState();
}

} // namespace daw::ui
