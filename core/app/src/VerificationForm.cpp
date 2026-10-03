#include "Verification.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/panels/PianoRollPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace daw::app
{
namespace
{

// The sixteenths the notes of one bar of the range start on, inside the bar.
template <typename Notes>
[[nodiscard]] std::vector<int> motifOf(const Notes& notes, double from, double beatsPerBar, int bar)
{
    std::set<int> steps;
    const auto start = from + bar * beatsPerBar;
    for (const auto& note : notes)
    {
        if (note.startBeats >= start - 1e-9 && note.startBeats < start + beatsPerBar - 1e-9)
            steps.insert(static_cast<int>(std::lround((note.startBeats - start) * 4.0)));
    }
    return {steps.begin(), steps.end()};
}

[[nodiscard]] std::string spelled(const std::vector<int>& motif)
{
    std::string out;
    for (const auto step : motif)
        out += (out.empty() ? "" : " ") + std::to_string(step);
    return "{" + out + "}";
}

} // namespace

void Verification::addFormSteps()
{
    // --- S15: the form -------------------------------------------------------------
    //
    // Four bars on a fresh Lead channel. The zone deduces AABA and says it;
    // the proposal repeats its first bar in the second and the fourth and
    // not in the third, on screen and once written. "boucle" and "libre" are
    // understood. What is measured is the rhythm of each bar, not a label.

    const auto roll = [this] { return dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll")); };
    const auto enter = [this, roll](const juce::String& text)
    {
        if (auto* panel = roll(); panel != nullptr)
            prompt(panel->generationBar(), text);
    };
    const auto aaba = [this](const auto& notes, double from, const std::string& what)
    {
        const auto bar = state_.beatsPerBar();
        const auto first = motifOf(notes, from, bar, 0);
        note(what + " : mesures " + spelled(first) + " " + spelled(motifOf(notes, from, bar, 1)) + " " +
             spelled(motifOf(notes, from, bar, 2)) + " " + spelled(motifOf(notes, from, bar, 3)));
        check(!first.empty(), what + " : la mesure 1 joue");
        check(motifOf(notes, from, bar, 1) == first, what + " : la mesure 2 a le rythme de la 1");
        check(motifOf(notes, from, bar, 3) == first, what + " : la mesure 4 a le rythme de la 1");
        check(motifOf(notes, from, bar, 2) != first, what + " : la mesure 3 en diffère");
    };

    add("S15 : un canal Lead neuf, Maj + glisser sur quatre mesures, Ctrl+G, « propose » : « AABA (déduit) »",
        [this, roll, aaba]
        {
            press("PAT");
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            leadTrack_ = domain::TrackId::generate();
            static_cast<void>(bus_.execute(std::make_unique<domain::AddTrack>(leadTrack_, "Lead", 0.0)));
            selection_.selectPattern(state_.patterns().front().id);
            if (auto* rack = panel("channel_rack"); rack != nullptr)
                click(*rack, rackChannel(static_cast<int>(state_.tracks().size()) - 1));
            selection_.dispatchPendingMessages();
            check(selection_.track() == leadTrack_, "le canal Lead est choisi");

            if (auto* shown = panel("piano_roll"); shown == nullptr || !shown->isShowing())
                key(juce::KeyPress{juce::KeyPress::F7Key});
            auto* panel = roll();
            const auto* pattern = state_.findPattern(selection_.pattern());
            const auto bar = state_.beatsPerBar();
            if (panel == nullptr || pattern == nullptr || pattern->lengthBeats < 4.0 * bar - 1e-9)
            {
                note("pattern de moins de quatre mesures : cas sauté");
                return;
            }

            const auto y = panel->ruler().getCentreY();
            drag(*panel,
                 {panel->pointFor(0.0, 60).getX(), y},
                 {panel->pointFor(4.0 * bar, 60).getX(), y},
                 false,
                 false,
                 true);
            generationBaseline_ = domain::json::write(state_.toValue());
            generationDepth_ = depth();

            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            prompt(panel->generationBar(), juce::String::fromUTF8("propose"));
            check(panel->proposing(), "une proposition est à l'écran");
            note(panel->proposalLine().toStdString());
            check(panel->proposalLine().contains(juce::String::fromUTF8("AABA (déduit)")),
                  "la ligne dit « AABA (déduit) »");
            aaba(panel->ghostNotes(), 0.0, "proposé");
            check(domain::json::write(state_.toValue()) == generationBaseline_, "rien n'est écrit");
        });

    add("« boucle », Entrée : les quatre mesures au même rythme ; « libre » : la forme de la S14",
        [this, roll, enter]
        {
            auto* panel = roll();
            if (panel == nullptr || !panel->proposing())
                return;

            enter("boucle");
            check(panel->proposalLine().contains(juce::String::fromUTF8("boucle (imposé)")),
                  "la ligne dit « boucle (imposé) »");
            const auto bar = state_.beatsPerBar();
            const auto& notes = panel->ghostNotes();
            const auto first = motifOf(notes, 0.0, bar, 0);
            auto same = !first.empty();
            for (int other = 1; other < 4; ++other)
                same = same && motifOf(notes, 0.0, bar, other) == first;
            check(same, "les mesures 2, 3 et 4 ont le rythme de la 1");
            const auto* proposal = panel->proposal();
            check(proposal != nullptr && proposal->interpretation().ignored.empty(), "rien d'ignoré");

            enter("libre");
            check(panel->proposalLine().contains(juce::String::fromUTF8("libre (imposé)")),
                  "la ligne dit « libre (imposé) »");
            check(domain::json::write(state_.toValue()) == generationBaseline_, "rien n'est écrit");
        });

    add("« Am AABA mélodie », Tab : ce qui est écrit a la forme annoncée ; Ctrl+Z, tout défaire",
        [this, roll, enter, aaba]
        {
            auto* panel = roll();
            if (panel == nullptr || !panel->proposing())
                return;

            enter(juce::String::fromUTF8("Am AABA mélodie"));
            static_cast<void>(panel->keyPressed(juce::KeyPress{juce::KeyPress::tabKey}));
            check(depth() == generationDepth_ + 1, "une seule entrée d'historique");

            const auto* pattern = state_.findPattern(selection_.pattern());
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(leadTrack_) : nullptr;
            check(row != nullptr, "la ligne du Lead est écrite");
            if (row != nullptr)
                aaba(row->notes, 0.0, "écrit");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == generationBaseline_,
                  "Ctrl+Z : le projet d'avant Tab");
            while (depth() > savedDepth_)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_, "tout défait : le projet d'avant");
            key(juce::KeyPress{juce::KeyPress::F7Key});
            press("SONG");
        });
}

} // namespace daw::app
