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

    const auto open = proposal_.has_value() || prompt_.isVisible();
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

    if (key == juce::KeyPress::returnKey)
    {
        generateFromPrompt();
        return true;
    }

    return false;
}

// --- the field ------------------------------------------------------------------------

void PianoRollPanel::openPrompt()
{
    if (track() == nullptr || pattern() == nullptr)
        return;

    prompt_.setVisible(true);
    placePrompt();
    prompt_.grabKeyboardFocus();

    // Opening proposes at once, with whatever the field holds: an empty field
    // is a request too, and the answer to it is worth hearing before typing.
    promptedText_ = {};
    generateFromPrompt();
}

void PianoRollPanel::generateFromPrompt()
{
    const auto* owner = track();
    const auto* shown = pattern();
    if (owner == nullptr || shown == nullptr)
        return;

    const auto text = prompt_.getText();

    // Enter on the words that made what is on screen asks for another answer
    // to the same question, not for the same answer again.
    if (proposal_.has_value() && text == promptedText_)
    {
        showVariant(+1);
        return;
    }

    const auto from = range_.has_value() ? range_->first : 0.0;
    const auto to = range_.has_value() ? range_->second : shown->lengthBeats;
    const auto patternId = shown->id;
    const auto trackId = owner->id;

    interpreter_.interpret(
        text.toStdString(),
        [this, text, from, to, patternId, trackId](domain::generation::Interpretation read)
        {
            // The base, and what the person taught it: the projects saved, and
            // this one as it is now.
            const auto& base = styleModel();
            auto* learning = styleLearning();
            const auto started = juce::Time::getMillisecondCounterHiRes();
            auto model = learning != nullptr
                             ? learning->model(state_, base)
                             : std::shared_ptr<const domain::generation::StyleModel>{
                                   std::shared_ptr<const domain::generation::StyleModel>{}, &base};
            lastStyleMs_ = juce::Time::getMillisecondCounterHiRes() - started;

            auto opened =
                GhostProposal::open(state_, patternId, trackId, from, to, std::move(read), std::move(model));
            if (!opened)
            {
                juce::Logger::writeToLog("generation: refused: " + juce::String{opened.error().message});
                return;
            }

            proposal_ = std::move(opened).value();
            promptedText_ = text;
            const auto role = proposal_->constraints().role.value;
            styleLine_ = juce::String::fromUTF8(
                (learning != nullptr ? learning->describe(role) : base.origin()).c_str());
            ghosts_ = proposal_->notes();
            juce::Logger::writeToLog("generation: " + proposalLine() + " · " + juce::String(ghosts_.size()) +
                                     " notes · " + juce::String(proposal_->lastDrawMs(), 2) +
                                     " ms · style en " + juce::String(lastStyleMs_, 2) + " ms");
            placePrompt();
            repaint();
        });
}

void PianoRollPanel::showVariant(int delta)
{
    if (!proposal_.has_value())
        return;

    const auto before = proposal_->rank();
    if (proposal_->shift(delta) == before)
        return;

    ghosts_ = proposal_->notes();
    repaint();
}

void PianoRollPanel::closeProposal()
{
    proposal_.reset();
    ghosts_.clear();
    promptedText_ = {};
    prompt_.setVisible(false);
    grabKeyboardFocus();
    repaint();
}

void PianoRollPanel::acceptProposal()
{
    if (!proposal_.has_value())
        return;

    // Taken out first: the group below notifies, and a proposal still open
    // then would see its own notes as a change of context and regenerate.
    auto taken = std::move(*proposal_);
    closeProposal();

    const auto* owner = track();
    const auto* shown = pattern();
    if (owner == nullptr || shown == nullptr || owner->id != taken.track() || shown->id != taken.pattern())
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

    auto acceptance = taken.accept(state_, row);
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
    if (!proposal_.has_value())
        return;

    // Another row or another pattern on screen: the proposal was for the one
    // that left.
    const auto* owner = track();
    const auto* shown = pattern();
    if (owner == nullptr || shown == nullptr || owner->id != proposal_->track() ||
        shown->id != proposal_->pattern())
    {
        closeProposal();
        return;
    }

    switch (proposal_->refresh(state_))
    {
    case GhostProposal::Refresh::closed:
        closeProposal();
        return;
    case GhostProposal::Refresh::regenerated:
        ghosts_ = proposal_->notes();
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
    if (!proposal_.has_value())
        return {};

    auto line = juce::String::fromUTF8(domain::generation::describe(proposal_->constraints()).c_str());
    line << juce::String::fromUTF8(" · variante ") << (proposal_->rank() + 1) << "/" << proposal_->drawn();
    line << juce::String::fromUTF8(" · style : ") << styleLine_;

    const auto& read = proposal_->interpretation();
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

// --- range -----------------------------------------------------------------------------

std::optional<std::pair<double, double>> PianoRollPanel::shownRange() const
{
    if (proposal_.has_value())
        return std::make_pair(proposal_->fromBeats(), proposal_->toBeats());
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

void PianoRollPanel::paintProposalLine(juce::Graphics& g) const
{
    if (!prompt_.isVisible())
        return;

    const auto font = lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular");
    auto line = proposalLine();
    if (line.isEmpty())
        line = juce::String::fromUTF8("aucune proposition");

    const auto field = prompt_.getBounds();
    const auto grid = gridArea();
    const juce::Rectangle<int> area{
        grid.getX(), field.getBottom(), grid.getWidth(), tokens_.integer("metric.pianoRoll.promptHeight")};

    g.setFont(font);
    const auto width =
        std::min(area.getWidth(),
                 juce::GlyphArrangement::getStringWidthInt(font, line) + tokens_.integer("space.md") * 2);
    const auto box =
        area.withX(std::clamp(field.getX(), area.getX(), std::max(area.getX(), area.getRight() - width)))
            .withWidth(width);

    g.setColour(tokens_.colour("color.surface.overlay"));
    g.fillRect(box);
    g.setColour(tokens_.colour("color.text.secondary"));
    g.drawText(line, box.reduced(tokens_.integer("space.md"), 0), juce::Justification::centredLeft, true);
}

} // namespace daw::ui
