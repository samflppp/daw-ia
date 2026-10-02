#include "daw/domain/tidy/Roles.h"
#include "daw/ui/model/StyleLearning.h"
#include "daw/ui/model/StyleSource.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>

// The zone of generation in the playlist (S17): a rectangle over several
// lines, one prompt, a part per line.
//
//   Alt + drag            draws the zone, snapped to the bar, over the lines
//   ✦ Générer, Ctrl+G     opens the window under the playlist
//   Enter                 reads the prompt; again, another variant
//   Tab                   writes every part: one group, one history entry
//   Ctrl+Space            ▶ Écouter: every part at once, where it would be
//   Escape                closes the window, then forgets the zone
//
// What each line plays is read by tidy::classifyLane — its name, else what is
// filed on it — and what is written is ZoneProposal's. Nothing here writes the
// project but acceptZone().

namespace daw::ui
{

// --- the zone ---------------------------------------------------------------------

std::optional<PlaylistPanel::Zone> PlaylistPanel::zoneBetween(juce::Point<int> from,
                                                              juce::Point<int> to) const
{
    if (freeLaneCount() == 0)
        return std::nullopt;

    const auto grid = gridArea();
    const auto laneAt = [&](int y)
    {
        const auto raw =
            laneAtContentY(std::clamp(y, grid.getY(), grid.getBottom() - 1) - grid.getY() + firstLanePixel());
        return std::clamp(raw, 0, freeLaneCount() - 1);
    };

    // Whole bars, and at least one: a zone is a length of music.
    const auto bar = barBeats();
    auto start = std::floor(std::min(beatAtX(from.getX()), beatAtX(to.getX())) / bar) * bar;
    auto end = std::ceil(std::max(beatAtX(from.getX()), beatAtX(to.getX())) / bar) * bar;
    start = std::max(0.0, start);
    end = std::max(end, start + bar);

    return Zone{std::min(laneAt(from.getY()), laneAt(to.getY())),
                std::max(laneAt(from.getY()), laneAt(to.getY())),
                start,
                end};
}

juce::Rectangle<int> PlaylistPanel::zoneArea(const Zone& zone) const
{
    const auto grid = gridArea();
    const auto top = grid.getY() + laneTop(zone.firstLane) - firstLanePixel();
    const auto bottom = grid.getY() + laneTop(zone.lastLane + 1) - firstLanePixel();
    const auto left = xForBeat(zone.fromBeats);
    return {left, top, std::max(1, xForBeat(zone.toBeats) - left), bottom - top};
}

juce::Rectangle<int> PlaylistPanel::zonePill() const
{
    // Not while the window is open: it is the door, and the door is open.
    if (!zone_.has_value() || zoneStart_.has_value() || bar_.isVisible())
        return {};
    const auto area = zoneArea(*zone_);
    const auto height = tokens_.integer("metric.playlist.labelHeight") + tokens_.integer("space.xs") * 2;
    const auto width = tokens_.integer("metric.playlist.headerWidth") / 2;
    return {area.getRight() - width, area.getY() - height, width, height};
}

void PlaylistPanel::paintZone(juce::Graphics& g, juce::Rectangle<int> grid) const
{
    if (!zone_.has_value())
        return;

    g.saveState();
    g.reduceClipRegion(grid.withTop(grid.getY() - tokens_.integer("metric.playlist.laneHeight")));

    const auto area = zoneArea(*zone_);
    g.setColour(tokens_.colour("color.note.range"));
    g.fillRect(area);
    g.setColour(tokens_.colour("color.accent.primary"));
    g.drawRect(area, tokens_.integer("stroke.hairline"));

    // The parts proposed, in grey where they would be written, each with its
    // notes: judged by eye before the ear.
    if (zoneProposal_.has_value())
    {
        const auto radius = tokens_.number("radius.sm");
        for (const auto& part : zoneProposal_->parts())
        {
            const auto lane = state_.laneIndex(part.lane);
            if (!lane || part.notes.empty())
                continue;
            const auto left = xForBeat(part.songBeats + part.fromBeats);
            const auto right = xForBeat(part.songBeats + part.toBeats);
            const auto block =
                juce::Rectangle<int>{left,
                                     grid.getY() + laneTop(static_cast<int>(lane.value())) - firstLanePixel(),
                                     std::max(1, right - left),
                                     laneHeightOf(static_cast<int>(lane.value()))}
                    .withTrimmedTop(tokens_.integer("metric.playlist.blockInset"))
                    .withTrimmedBottom(tokens_.integer("metric.playlist.blockInset"));

            g.setColour(tokens_.colour("color.note.ghost"));
            g.fillRoundedRectangle(block.toFloat(), radius);

            auto low = 127;
            auto high = 0;
            for (const auto& note : part.notes)
            {
                low = std::min(low, note.pitch);
                high = std::max(high, note.pitch);
            }
            const auto span = std::max(1, high - low);
            const auto inner =
                block.reduced(tokens_.integer("space.xs"), tokens_.integer("stroke.hairline") * 2);
            g.setColour(tokens_.colour("color.preview.note"));
            for (const auto& note : part.notes)
            {
                const auto x = xForBeat(part.songBeats + note.startBeats);
                const auto w = std::max(1, xForBeat(part.songBeats + note.startBeats + note.lengthBeats) - x);
                const auto y =
                    inner.getBottom() - 2 -
                    static_cast<int>(std::lround(static_cast<double>(note.pitch - low) /
                                                 static_cast<double>(span) * (inner.getHeight() - 2)));
                g.fillRect(x, y, w, 2);
            }

            g.setColour(tokens_.colour("color.note.ghostOutline"));
            g.drawRoundedRectangle(
                block.toFloat(), radius, static_cast<float>(tokens_.integer("stroke.hairline")));
        }
    }

    g.restoreState();

    // The door, at the zone's corner: the gesture shows where it goes on.
    if (const auto pill = zonePill(); !pill.isEmpty() && !bar_.isVisible())
    {
        g.setColour(tokens_.colour("color.accent.primary"));
        g.fillRoundedRectangle(pill.toFloat(), tokens_.number("radius.sm"));
        g.setColour(tokens_.colour("color.note.label"));
        g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.medium"));
        g.drawText(juce::String::fromUTF8(u8"✦ Générer"), pill, juce::Justification::centred, false);
    }
}

// --- the window -------------------------------------------------------------------

void PlaylistPanel::openZonePrompt()
{
    if (!zone_.has_value())
        return;

    closeBand();
    closeZone();
    bar_.open(juce::String::fromUTF8(u8"par ex. : une boucle trap sombre en F#m"));
    bar_.setVisible(true);
    resized();
    bar_.field().grabKeyboardFocus();
    repaint();
}

bool PlaylistPanel::zoneKey(const juce::KeyPress& key)
{
    if (key == juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0})
    {
        if (!zone_.has_value())
            return false;
        openZonePrompt();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (bar_.isVisible())
            closeZone();
        else if (zone_.has_value())
            zone_.reset();
        else
            return false;
        repaint();
        return true;
    }

    if (!bar_.isVisible())
        return false;

    if (key == juce::KeyPress::tabKey)
    {
        acceptZone();
        return true;
    }
    if (key == juce::KeyPress{juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0})
    {
        toggleZoneListening();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        generateZone();
        return true;
    }
    return false;
}

void PlaylistPanel::generateZone()
{
    if (!zone_.has_value())
        return;

    const auto text = bar_.field().getText().trim();
    if (text.isEmpty())
    {
        bar_.showMessage(juce::String::fromUTF8(u8"Écris d'abord ce que tu veux entendre."));
        return;
    }
    if (zoneProposal_.has_value() && text == promptedText_)
    {
        showZoneVariant(+1);
        return;
    }

    // What the reader is told: the length, the signature, and the names of
    // the lines — they say the roles better than any track does.
    ZoneProposal::Zone asked{};
    PromptReader::Zone zone;
    zone.lengthBeats = zone_->toBeats - zone_->fromBeats;
    zone.beatsPerBar = state_.beatsPerBar();
    for (int lane = zone_->firstLane; lane <= zone_->lastLane && lane < freeLaneCount(); ++lane)
    {
        const auto& line = state_.lanes()[static_cast<std::size_t>(lane)];
        asked.lanes.push_back(line.id);
        zone.tracks.push_back(laneLabel(line, lane).toStdString());
    }
    asked.fromBeats = zone_->fromBeats;
    asked.toBeats = zone_->toBeats;

    stopZoneListening();
    zoneProposal_.reset();
    promptedText_ = text;
    bar_.showReading();
    repaint();

    reader_.read(
        text.toStdString(),
        std::move(zone),
        [this, asked](PromptReader::Reading reading)
        {
            if (!bar_.isVisible())
                return;

            const auto& base = styleModel();
            auto* learning = styleLearning();
            auto model = learning != nullptr
                             ? learning->model(state_, base)
                             : std::shared_ptr<const domain::generation::StyleModel>{
                                   std::shared_ptr<const domain::generation::StyleModel>{}, &base};

            auto opened = ZoneProposal::open(state_, asked, reading.interpretation, std::move(model));
            if (!opened)
            {
                juce::Logger::writeToLog("zone: refused: " + juce::String{opened.error().message});
                bar_.showMessage(juce::String::fromUTF8(opened.error().message.c_str()));
                return;
            }
            zoneProposal_ = std::move(opened).value();
            showZoneProposal(reading);
            listenZoneAgain();
            juce::Logger::writeToLog("zone: " + juce::String(zoneProposal_->parts().size()) + " parts, " +
                                     juce::String(zoneProposal_->skipped().size()) + " skipped" +
                                     (reading.remote ? " · lu à distance" : " · lu en local"));
            repaint();
        });
}

void PlaylistPanel::showZoneProposal(const PromptReader::Reading& reading)
{
    if (!zoneProposal_.has_value())
        return;

    GenerationPanel::Shown shown;
    shown.sentence = juce::String::fromUTF8(zoneProposal_->sentence(state_.beatsPerBar()).c_str());

    // What was read of each line, and the lines left out: the person judges
    // the roles as much as the notes.
    std::string notice = reading.notice;
    for (const auto& skipped : zoneProposal_->skipped())
    {
        const auto index = state_.laneIndex(skipped.lane);
        const auto label = index ? laneLabel(state_.lanes()[index.value()], static_cast<int>(index.value()))
                                 : juce::String{};
        notice += (notice.empty() ? "" : " ") + label.toStdString() + " : " + skipped.why;
    }
    shown.notice = juce::String::fromUTF8(notice.c_str());

    std::string details;
    for (const auto& part : zoneProposal_->parts())
    {
        const auto* track = state_.findTrack(part.track);
        details += (details.empty() ? "" : " · ") + std::string{domain::generation::describe(part.role)} +
                   " sur " + (track != nullptr ? track->name : std::string{"?"});
        if (const auto why = domain::tidy::because(part.guess); !why.empty())
            details += " (" + why + ")";
        details += part.isNew ? ", nouveau pattern" : ", dans le bloc de la ligne";
    }
    shown.details = juce::String::fromUTF8(details.c_str());
    shown.rank = zoneProposal_->rank();
    shown.drawn = zoneProposal_->drawn();
    bar_.showProposal(shown);
    lastReading_ = reading;
}

void PlaylistPanel::showZoneVariant(int delta)
{
    if (!zoneProposal_.has_value())
        return;
    const auto before = zoneProposal_->rank();
    if (zoneProposal_->shift(delta) == before)
        return;
    showZoneProposal(lastReading_);
    listenZoneAgain();
    repaint();
}

void PlaylistPanel::acceptZone()
{
    if (!zoneProposal_.has_value())
        return;

    // Taken out first: the group below notifies, and a proposal still open
    // would see its own parts as a change under the zone.
    auto taken = std::move(zoneProposal_);
    zoneProposal_.reset();
    closeZone();

    auto acceptance = taken->accept(state_);
    if (acceptance.commands.empty())
        return;
    if (bus_.executeGroup(std::move(acceptance.commands), acceptance.group).ok())
        zone_.reset();
    repaint();
}

void PlaylistPanel::closeZone()
{
    stopZoneListening();
    reader_.cancel();
    zoneProposal_.reset();
    promptedText_ = {};
    if (bar_.isVisible())
    {
        bar_.setVisible(false);
        resized();
    }
    repaint();
}

// --- listening --------------------------------------------------------------------

void PlaylistPanel::toggleZoneListening()
{
    if (listeningHere_)
    {
        stopZoneListening();
        return;
    }
    if (!zoneProposal_.has_value())
        return;

    const auto refused = listening_.listen(zoneProposal_->lines());
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

void PlaylistPanel::listenZoneAgain()
{
    if (!listeningHere_ || !zoneProposal_.has_value())
        return;
    if (!listening_.listen(zoneProposal_->lines()).empty())
        stopZoneListening();
}

void PlaylistPanel::stopZoneListening()
{
    if (!listeningHere_)
        return;
    listeningHere_ = false;
    listening_.onEnded = nullptr;
    listening_.stop();
    bar_.setListening(false);
}

} // namespace daw::ui
