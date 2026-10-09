#include "daw/domain/generation/Phrase.h"
#include "daw/domain/rights/Rights.h"
#include "daw/ui/model/StyleLearning.h"
#include "daw/ui/model/StyleSource.h"
#include "daw/ui/panels/PianoRollPanel.h"

#include <algorithm>
#include <cmath>

// Generation in the piano roll: the range, the field, the grey notes, and the
// three keys that decide what becomes of them.
//
// Everything here reads the project and nothing here writes it, except
// acceptProposal(), which sends one group. The proposal itself is a
// GhostProposal (model/GhostProposal.h): the rules for what it may and may not
// touch are written and tested there.

namespace daw::ui
{
// --- keys ------------------------------------------------------------------------

bool PianoRollPanel::generationKey(const juce::KeyPress& key)
{
    if (key == juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0})
    {
        openPrompt();
        return true;
    }

    const auto open = proposing() || bar_.isVisible();
    if (key == juce::KeyPress::escapeKey)
    {
        if (open)
            closeProposal();
        else if (range_.has_value())
            range_.reset();
        else
            return false;
        repaint();
        return true;
    }

    if (!open)
        return false;

    if (key == juce::KeyPress::tabKey)
    {
        acceptProposal();
        return true;
    }

    if (key == juce::KeyPress{juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0})
    {
        toggleListening();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        generateFromPrompt();
        return true;
    }

    return false;
}

// --- the window ------------------------------------------------------------------------

std::pair<double, double> PianoRollPanel::zone() const
{
    if (range_.has_value())
        return *range_;

    const auto length = patternLength();
    const auto* edited = clip();
    if (edited != nullptr && !picked_.empty())
    {
        auto from = length;
        auto to = 0.0;
        for (const auto& note : edited->notes)
        {
            if (!isPicked(note.id))
                continue;
            from = std::min(from, note.startBeats);
            to = std::max(to, note.startBeats + note.lengthBeats);
        }
        if (to > from)
        {
            // Whole steps of the grid, so the zone covers the notes it was
            // drawn from and nothing is cut in the middle.
            from = std::floor(from / gridStepBeats + 1e-9) * gridStepBeats;
            to = std::min(length, std::ceil(to / gridStepBeats - 1e-9) * gridStepBeats);
            if (to > from)
                return {from, to};
        }
    }
    return {0.0, length};
}

bool PianoRollPanel::zoneHasNotes() const
{
    const auto* edited = clip();
    if (edited == nullptr)
        return false;
    const auto [from, to] = zone();
    return std::any_of(edited->notes.begin(),
                       edited->notes.end(),
                       [start = from, end = to](const domain::Note& note)
                       { return note.startBeats < end && note.startBeats >= start; });
}

void PianoRollPanel::openPrompt()
{
    if (track() == nullptr || pattern() == nullptr)
        return;

    // An example fitting what is on screen, as the placeholder. Nothing is
    // proposed yet: the window waits for the prompt.
    const auto hasNotes = zoneHasNotes();
    bar_.open(juce::String::fromUTF8(hasNotes ? u8"par ex. : une basse sombre en croches"
                                              : u8"par ex. : des accords tristes en la mineur"));

    proposal_.reset();
    rework_.reset();
    ghosts_.clear();
    promptedText_ = {};
    bar_.setVisible(true);
    placeBar();
    bar_.field().grabKeyboardFocus();
    repaint();
}

void PianoRollPanel::generateFromPrompt()
{
    const auto* owner = track();
    const auto* shown = pattern();
    if (owner == nullptr || shown == nullptr)
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

    // Enter on the words that made what is on screen asks for another answer
    // to the same question, not for the same answer again.
    if (proposing() && text == promptedText_)
    {
        showVariant(+1);
        return;
    }

    const auto [from, to] = zone();
    PromptReader::Zone asked;
    asked.lengthBeats = to - from;
    asked.beatsPerBar = state_.beatsPerBar();
    asked.hasNotes = zoneHasNotes();
    asked.tracks.push_back(owner->name);

    const auto patternId = shown->id;
    const auto trackId = owner->id;
    const auto zoneFrom = from;
    const auto zoneTo = to;

    // The grey notes of an older prompt leave: they answer another question.
    proposal_.reset();
    rework_.reset();
    ghosts_.clear();
    promptedText_ = text;
    bar_.showReading();
    repaint();

    reader_.read(
        text.toStdString(),
        std::move(asked),
        [this, patternId, trackId, zoneFrom, zoneTo](PromptReader::Reading reading)
        {
            // The row or the pattern changed while the prompt was read:
            // the answer is for a zone that is no longer on screen.
            const auto* nowOwner = track();
            const auto* nowShown = pattern();
            if (!bar_.isVisible() || nowOwner == nullptr || nowShown == nullptr || nowOwner->id != trackId ||
                nowShown->id != patternId)
                return;

            // The base, and what the person taught it: the projects
            // saved, and this one as it is now.
            const auto& base = styleModel();
            auto* learning = styleLearning();
            const auto started = juce::Time::getMillisecondCounterHiRes();
            auto model = learning != nullptr
                             ? learning->model(state_, base)
                             : std::shared_ptr<const domain::generation::StyleModel>{
                                   std::shared_ptr<const domain::generation::StyleModel>{}, &base};
            lastStyleMs_ = juce::Time::getMillisecondCounterHiRes() - started;

            // Notes in the zone: they are reworked, not written over. The
            // prompt says how; without a word for it, the rhythm is kept.
            domain::generation::Role role{};
            if (zoneHasNotes())
            {
                const auto how = reading.transform.value_or(domain::generation::Transform::keepRhythm);
                auto opened = TransformProposal::open(state_,
                                                      patternId,
                                                      trackId,
                                                      zoneFrom,
                                                      zoneTo,
                                                      reading.interpretation,
                                                      how,
                                                      std::move(model));
                if (!opened)
                {
                    juce::Logger::writeToLog("generation: refused: " + juce::String{opened.error().message});
                    bar_.showMessage(juce::String::fromUTF8(u8"Rien à retoucher dans cette zone."));
                    return;
                }
                rework_ = std::move(opened).value();
                role = rework_->constraints().role.value;
            }
            else
            {
                auto opened = GhostProposal::open(
                    state_, patternId, trackId, zoneFrom, zoneTo, reading.interpretation, std::move(model));
                if (!opened)
                {
                    juce::Logger::writeToLog("generation: refused: " + juce::String{opened.error().message});
                    bar_.showMessage(juce::String::fromUTF8(u8"Rien à proposer dans cette zone."));
                    return;
                }
                proposal_ = std::move(opened).value();
                role = proposal_->constraints().role.value;
            }
            styleLine_ = juce::String::fromUTF8(
                (learning != nullptr ? learning->describe(role) : base.origin()).c_str());
            styleShare_ = learning != nullptr ? learning->share(role) : 0.0;
            ghosts_ = proposal_.has_value() ? proposal_->notes() : rework_->notes();
            showProposal(reading);
            listenAgain();
            juce::Logger::writeToLog("generation: " + proposalLine() + " · " + juce::String(ghosts_.size()) +
                                     " notes · " + juce::String(lastGenerationMs(), 2) + " ms · style en " +
                                     juce::String(lastStyleMs_, 2) + " ms" +
                                     (reading.remote ? " · lu à distance" : " · lu en local"));
            repaint();
        });
}

void PianoRollPanel::showProposal(const PromptReader::Reading& reading)
{
    if (!proposing())
        return;

    GenerationPanel::Shown shown;
    shown.sentence = proposalSentence();
    shown.notice = juce::String::fromUTF8(reading.notice.c_str());
    const auto& ignored =
        proposal_.has_value() ? proposal_->interpretation().ignored : rework_->interpretation().ignored;
    if (!ignored.empty())
    {
        shown.unused = juce::String::fromUTF8(u8"Je n'ai pas utilisé :");
        for (const auto& word : ignored)
            shown.unused << " " << juce::String::fromUTF8(word.c_str());
    }
    shown.details = proposalLine();
    shown.rank = variantRank();
    shown.drawn = proposal_.has_value() ? proposal_->drawn() : rework_->drawn();
    bar_.showProposal(shown);
    lastReading_ = reading;
}

void PianoRollPanel::showVariant(int delta)
{
    if (!proposing())
        return;

    const auto before = variantRank();
    const auto after = proposal_.has_value() ? proposal_->shift(delta) : rework_->shift(delta);
    if (after == before)
        return;
    reshow();
}

void PianoRollPanel::reshow()
{
    ghosts_ = proposal_.has_value() ? proposal_->notes() : rework_->notes();
    showProposal(lastReading_);
    listenAgain();
    repaint();
}

std::optional<PianoRollPanel::Target> PianoRollPanel::target() const
{
    if (proposal_.has_value())
        return Target{proposal_->track(), proposal_->pattern(), proposal_->fromBeats(), proposal_->toBeats()};
    if (rework_.has_value())
        return Target{rework_->track(), rework_->pattern(), rework_->fromBeats(), rework_->toBeats()};
    return std::nullopt;
}

// --- listening ------------------------------------------------------------------------

void PianoRollPanel::toggleListening()
{
    if (listeningHere_)
    {
        stopListening();
        return;
    }
    const auto where = target();
    if (!where.has_value())
        return;

    ListeningHost::Line line{where->track, where->pattern, where->fromBeats, where->toBeats, ghosts_};
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
    juce::Logger::writeToLog("generation: listening to " + juce::String(ghosts_.size()) + " notes");
}

void PianoRollPanel::listenAgain()
{
    const auto where = target();
    if (!listeningHere_ || !where.has_value())
        return;

    ListeningHost::Line line{where->track, where->pattern, where->fromBeats, where->toBeats, ghosts_};
    if (!listening_.listen({line}).empty())
        stopListening();
}

void PianoRollPanel::stopListening()
{
    if (!listeningHere_)
        return;
    listeningHere_ = false;
    listening_.onEnded = nullptr;
    listening_.stop();
    bar_.setListening(false);
}

void PianoRollPanel::closeProposal()
{
    stopListening();
    reader_.cancel();
    proposal_.reset();
    rework_.reset();
    ghosts_.clear();
    promptedText_ = {};
    bar_.setVisible(false);
    grabKeyboardFocus();
    resized();
    repaint();
}

void PianoRollPanel::acceptProposal()
{
    const auto where = target();
    if (!where.has_value())
        return;

    // Taken out first: the group below notifies, and a proposal still open
    // then would see its own notes as a change of context and regenerate.
    auto taken = std::move(proposal_);
    auto reworked = std::move(rework_);
    closeProposal();

    const auto* owner = track();
    const auto* shown = pattern();
    if (owner == nullptr || shown == nullptr || owner->id != where->track || shown->id != where->pattern)
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    RowOpening opening;
    domain::ClipId row{};
    if (const auto* edited = clip(); edited != nullptr)
    {
        row = edited->id;
    }
    else
    {
        opening = openRow();
        if (opening.commands.empty())
            return;
        row = opening.clipId;
        commands = std::move(opening.commands);
    }

    auto acceptance = taken.has_value() ? taken->accept(state_, row) : reworked->accept(state_, row);
    for (auto& command : acceptance.commands)
        commands.push_back(std::move(command));
    if (commands.empty())
        return;

    juce::Logger::writeToLog("generation: accepted " + juce::String(acceptance.added.size()) + " notes");
    const auto added = acceptance.added;
    const auto openedRow = !opening.clipId.isNil();
    if (!bus_.executeGroup(std::move(commands), acceptance.group).ok())
        return;

    if (openedRow)
        selectOpened(opening);
    picked_ = added;
    selectedNote_ = {};
    repaint();
}

void PianoRollPanel::refreshProposal()
{
    const auto where = target();
    if (!where.has_value())
        return;

    // Another row or another pattern on screen: the proposal was for the one
    // that left.
    const auto* owner = track();
    const auto* shown = pattern();
    if (owner == nullptr || shown == nullptr || owner->id != where->track || shown->id != where->pattern)
    {
        closeProposal();
        return;
    }

    switch (proposal_.has_value() ? proposal_->refresh(state_) : rework_->refresh(state_))
    {
    case GhostProposal::Refresh::closed:
        closeProposal();
        return;
    case GhostProposal::Refresh::regenerated:
        reshow();
        juce::Logger::writeToLog("generation: context changed, regenerated");
        break;
    case GhostProposal::Refresh::unchanged:
        break;
    }
}

double PianoRollPanel::lastGenerationMs() const noexcept
{
    return proposal_.has_value() ? proposal_->lastDrawMs() : 0.0;
}

juce::String PianoRollPanel::proposalLine() const
{
    if (!proposing())
        return {};

    const auto& constraints = proposal_.has_value() ? proposal_->constraints() : rework_->constraints();
    auto line = juce::String::fromUTF8(domain::generation::describe(constraints).c_str());
    if (rework_.has_value())
        line << juce::String::fromUTF8(" · retouche : ")
             << juce::String::fromUTF8(std::string{domain::generation::nameOf(rework_->transform())}.c_str())
             << " (" << static_cast<int>(rework_->source().size()) << " notes)";
    line << juce::String::fromUTF8(" · variante ") << (variantRank() + 1) << "/"
         << (proposal_.has_value() ? proposal_->drawn() : rework_->drawn());
    line << juce::String::fromUTF8(" · style : ") << styleLine_;

    const auto& read = proposal_.has_value() ? proposal_->interpretation() : rework_->interpretation();
    if (!read.ignored.empty())
    {
        line << juce::String::fromUTF8(" · ignoré :");
        for (const auto& word : read.ignored)
            line << " " << juce::String::fromUTF8(word.c_str());
    }
    for (const auto& conflict : read.conflicts)
        line << " · " << juce::String::fromUTF8(conflict.c_str());
    return line;
}

juce::String PianoRollPanel::proposalSentence() const
{
    // A rework says what it did to the notes, in a musician's words.
    if (rework_.has_value())
        return juce::String::fromUTF8(
            std::string{domain::generation::describe(rework_->transform())}.c_str());
    if (!proposal_.has_value())
        return {};

    const domain::generation::PhraseInput input{proposal_->constraints(),
                                                proposal_->toBeats() - proposal_->fromBeats(),
                                                state_.beatsPerBar(),
                                                styleShare_};
    return juce::String::fromUTF8(domain::generation::phrase(input).c_str());
}

// --- range -----------------------------------------------------------------------------

std::optional<std::pair<double, double>> PianoRollPanel::shownRange() const
{
    if (const auto where = target(); where.has_value())
        return std::make_pair(where->fromBeats, where->toBeats);
    return range_;
}

// --- painting --------------------------------------------------------------------------

void PianoRollPanel::paintRange(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto shown = shownRange();
    if (!shown.has_value())
        return;

    const juce::Graphics::ScopedSaveState saved{g};
    const auto ruler = rulerArea();
    g.reduceClipRegion(area.getUnion(ruler));

    const auto left = xForBeat(shown->first);
    const auto right = xForBeat(shown->second);
    g.setColour(tokens_.colour("color.note.range"));
    g.fillRect(left, ruler.getY(), std::max(right - left, 1), area.getBottom() - ruler.getY());

    g.setColour(tokens_.colour("color.accent.primary"));
    g.fillRect(left, ruler.getY(), std::max(right - left, 1), tokens_.integer("stroke.focus"));

    // The chip that says what a range is for, and opens the window.
    if (const auto chip = rangeChip(); chip.has_value())
    {
        const auto bounds = chip->toFloat();
        g.setColour(tokens_.colour("color.actor.generator"));
        g.fillRoundedRectangle(bounds, tokens_.number("radius.sm"));
        g.setColour(tokens_.colour("color.surface.base"));
        g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.medium"));
        g.drawText(juce::String::fromUTF8(u8"✦ Générer"), *chip, juce::Justification::centred, false);
    }
}

std::optional<juce::Rectangle<int>> PianoRollPanel::rangeChip() const
{
    // Only for a range picked and not yet generated into: once the window is
    // open, the window says the rest.
    if (!range_.has_value() || bar_.isVisible() || rangeAnchor_.has_value())
        return std::nullopt;

    const auto ruler = rulerArea();
    const auto width = tokens_.integer("metric.generation.buttonWidth");
    const auto inset = tokens_.integer("space.xxs");
    const auto right = std::clamp(xForBeat(range_->second), ruler.getX() + width, ruler.getRight());
    return juce::Rectangle<int>{right - width, ruler.getY(), width, ruler.getHeight()}.reduced(inset);
}

void PianoRollPanel::paintGhosts(juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (ghosts_.empty())
        return;

    const juce::Graphics::ScopedSaveState saved{g};
    g.reduceClipRegion(area);

    const auto keyHeight = tokens_.integer("metric.pianoRoll.keyHeight");
    const auto inset = tokens_.integer("metric.pianoRoll.noteInset");
    const auto radius = tokens_.number("radius.sm");

    for (const auto& ghost : ghosts_)
    {
        const auto y = yForPitch(ghost.pitch);
        const auto x = xForBeat(ghost.startBeats);
        const auto right = xForBeat(ghost.startBeats + ghost.lengthBeats);
        const juce::Rectangle<float> bounds{static_cast<float>(x),
                                            static_cast<float>(y + inset),
                                            static_cast<float>(std::max(right - x, inset * 2)),
                                            static_cast<float>(keyHeight - inset * 2)};

        g.setColour(tokens_.colour("color.note.ghost"));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(tokens_.colour("color.note.ghostOutline"));
        g.drawRoundedRectangle(bounds, radius, tokens_.number("stroke.hairline"));
    }
}

} // namespace daw::ui
