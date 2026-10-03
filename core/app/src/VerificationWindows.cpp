#include "Verification.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/ui/PageWindow.h"
#include "daw/ui/panels/ChannelRackPanel.h"
#include "daw/ui/panels/HistoryPanel.h"

#include <memory>
#include <string>

// S21, chantier 2 of S19 (--verify-canvas): the windows held together by the
// hand, and the channel rack and the history that scroll. One step a gesture:
// a window dragged near another lands on its edge; the edge they share,
// dragged, moves both; the wheel, the middle button and F in the rack; the
// wheel in the history, which keeps its rows when something is added under
// the hand and follows the newest at the top.

namespace daw::app
{
namespace
{

ui::PageWindow* windowOf(juce::Component* panel)
{
    return panel != nullptr ? panel->findParentComponentOfClass<ui::PageWindow>() : nullptr;
}

juce::Component* borderOf(ui::PageWindow& window)
{
    for (auto* child : window.getChildren())
    {
        if (dynamic_cast<juce::ResizableBorderComponent*>(child) != nullptr)
            return child;
    }
    return nullptr;
}

juce::MouseEvent mouseAt(juce::Component& target,
                         juce::Point<int> at,
                         juce::ModifierKeys held,
                         juce::Point<int> from,
                         bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(),
                            at.toFloat(),
                            held,
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
}

// A window moved by its title bar in one move of the hand. The verification's
// own drag goes through every pixel in coordinates of a target that does not
// move; a window moves under the pointer, so each step would count twice.
void dragTitle(ui::PageWindow& window, juce::Point<int> by, int titleHeight)
{
    const juce::Point<int> grip{titleHeight * 2, titleHeight / 2};
    const auto held = juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier};
    window.mouseDown(mouseAt(window, grip, held, grip, false));
    window.mouseDrag(mouseAt(window, grip + by, held, grip, true));
    window.mouseUp(mouseAt(window, grip + by, held.withoutMouseButtons(), grip, true));
}

} // namespace

void Verification::addWindowSteps()
{
    add("S21 : le rack glissé près du bord du bureau s'y pose",
        [this]
        {
            for (const auto* id : {"channel_rack", "history"})
                static_cast<void>(view_.showPage(id, true));
            auto* rack = windowOf(panel("channel_rack"));
            auto* history = windowOf(panel("history"));
            if (rack == nullptr || history == nullptr || !rack->isShowing() || !history->isShowing() ||
                !rack->desktop)
            {
                check(false, "le rack et l'historique sont des fenêtres ouvertes");
                return;
            }
            const auto desktop = rack->desktop();
            const auto r = rack->getBounds();
            dragTitle(*rack, {desktop.getX() + 6 - r.getX(), 0}, tokens_.integer("metric.page.titleHeight"));
            note("bureau " + desktop.toString().toStdString() + ", rack posé en " +
                 rack->getBounds().toString().toStdString());
            check(rack->getX() == desktop.getX(), "lâché à 6 px du bord gauche, il est sur le bord");
        });

    add("S21 : la page Historique glissée près du rack se pose sur son bord",
        [this]
        {
            auto* rack = windowOf(panel("channel_rack"));
            auto* history = windowOf(panel("history"));
            if (rack == nullptr || history == nullptr || !rack->desktop)
                return;
            const auto desktop = rack->desktop();

            // Room on its right for the history, made by the hand: the rack's
            // right edge pulled in first if the desktop has not enough.
            const auto missing = history->getWidth() + 6 - (desktop.getRight() - rack->getRight());
            if (missing > 0)
            {
                auto* border = borderOf(*rack);
                if (border == nullptr || rack->getWidth() - missing < tokens_.integer("metric.page.minWidth"))
                {
                    check(false, "de la place à droite du rack");
                    return;
                }
                const juce::Point<int> grip{rack->getWidth() - 2, rack->getHeight() / 2};
                drag(*border, grip, grip - juce::Point<int>{missing, 0});
            }

            const auto r = rack->getBounds();
            const auto h = history->getBounds();
            dragTitle(*history,
                      {r.getRight() + 6 - h.getX(), r.getY() + 4 - h.getY()},
                      tokens_.integer("metric.page.titleHeight"));

            const auto placed = history->getBounds();
            note("rack " + r.toString().toStdString() + ", historique posé en " +
                 placed.toString().toStdString());
            check(placed.getX() == r.getRight(),
                  "son bord gauche est sur le bord droit du rack, lâché à 6 px");
            check(placed.getY() == r.getY(), "son haut sur celui du rack, lâché à 4 px");
        });

    add("S21 : tirer le bord qu'ils partagent les déplace ensemble",
        [this]
        {
            auto* rack = windowOf(panel("channel_rack"));
            auto* history = windowOf(panel("history"));
            if (rack == nullptr || history == nullptr)
                return;
            auto& left = *rack;
            auto& right = *history;
            auto* border = borderOf(left);
            if (border == nullptr)
            {
                check(false, "la fenêtre a un bord à tirer");
                return;
            }

            const auto before = right.getBounds();
            const auto by = left.getWidth() - 40 >= tokens_.integer("metric.page.minWidth") ? -40 : 40;
            const juce::Point<int> grip{left.getWidth() - 2, left.getHeight() / 2};
            drag(*border, grip, grip + juce::Point<int>{by, 0});

            note("bord tiré de " + std::to_string(by) + " px : " + left.getBounds().toString().toStdString() +
                 " | " + right.getBounds().toString().toStdString());
            check(left.getRight() == before.getX() + by, "le bord a bougé de " + std::to_string(by) + " px");
            check(right.getX() == left.getRight(), "la voisine le suit : elles se touchent toujours");
            check(right.getRight() == before.getRight(), "son autre bord n'a pas bougé");
        });

    add("S21 : le rack défile à la molette, au clic-molette, et F montre le canal choisi",
        [this]
        {
            auto* rack = dynamic_cast<ui::ChannelRackPanel*>(panel("channel_rack"));
            if (rack == nullptr)
            {
                check(false, "le rack");
                return;
            }

            // More channels than the page shows.
            domain::TrackId last{};
            for (int index = 1; index <= 30; ++index)
            {
                last = domain::TrackId::generate();
                static_cast<void>(bus_.execute(
                    std::make_unique<domain::AddTrack>(last, "Défile " + std::to_string(index))));
            }

            const auto inside = rack->channelBounds(0).getCentre();
            check(rack->scrolled() == 0, "il part du haut");
            wheel(*rack, inside, -0.25f);
            const auto wheeled = rack->scrolled();
            check(wheeled > 0, "un cran de molette le descend de " + std::to_string(wheeled) + " px");

            const auto at = rack->getLocalBounds().getCentre();
            drag(*rack, at, at + juce::Point<int>{0, 20}, false, true);
            check(rack->scrolled() == std::max(0, wheeled - 20), "le clic-molette le remonte de 20 px");

            selection_.selectTrack(last);
            check(rack->keyPressed(juce::KeyPress{'f'}), "F est pris par le rack");
            const auto shown = rack->channelBounds(static_cast<int>(state_.tracks().size()) - 1);
            check(rack->getLocalBounds().contains(shown), "F montre le dernier canal, choisi");
        });

    add(
        "S21 : l'historique défile, et garde sous la main ce qu'il montre",
        [this]
        {
            auto* history = dynamic_cast<ui::HistoryPanel*>(panel("history"));
            if (history == nullptr || state_.tracks().empty())
            {
                check(false, "l'historique");
                return;
            }
            const auto inside = history->getLocalBounds().getCentre();

            wheel(*history, inside, -0.25f);
            const auto down = history->scrolled();
            check(down > 0, "un cran de molette le descend de " + std::to_string(down) + " px");

            // A command while scrolled down: the same entries stay in sight.
            // The list hears of it on the next message.
            historyScrolled_ = down;
            static_cast<void>(bus_.execute(
                std::make_unique<domain::RenameTrack>(state_.tracks().back().id, "Défile, renommée")));
        },
        [this]
        {
            auto* history = dynamic_cast<ui::HistoryPanel*>(panel("history"));
            return history != nullptr && history->scrolled() != historyScrolled_;
        },
        2000.0);

    add(
        "S21 : une entrée ajoutée sous la main, puis en haut, puis F",
        [this]
        {
            auto* history = dynamic_cast<ui::HistoryPanel*>(panel("history"));
            if (history == nullptr)
                return;
            const auto rowHeight = tokens_.integer("metric.history.rowHeight");
            const auto inside = history->getLocalBounds().getCentre();
            check(history->scrolled() == historyScrolled_ + rowHeight,
                  "une entrée ajoutée : il descend d'une ligne avec elle (" +
                      std::to_string(history->scrolled()) + " px)");

            // At the top, it shows the newest.
            wheel(*history, inside, 10.0f);
            check(history->scrolled() == 0, "remonté en haut");
            static_cast<void>(bus_.execute(
                std::make_unique<domain::RenameTrack>(state_.tracks().back().id, "Défile, encore")));
        },
        [this]
        {
            auto* history = dynamic_cast<ui::HistoryPanel*>(panel("history"));
            return history != nullptr && history->entryCount() == history_.entries().size();
        },
        2000.0);

    add("S21 : en haut il reste en haut ; F revient à l'entrée en cours",
        [this]
        {
            auto* history = dynamic_cast<ui::HistoryPanel*>(panel("history"));
            if (history == nullptr)
                return;
            const auto inside = history->getLocalBounds().getCentre();
            check(history->scrolled() == 0, "en haut, il reste en haut et montre la dernière");

            wheel(*history, inside, -10.0f);
            check(history->scrolled() > 0, "redescendu");
            check(history->keyPressed(juce::KeyPress{'f'}), "F est pris par l'historique");
            check(history->scrolled() == 0, "F remonte à l'entrée en cours, la dernière");
        });
}

} // namespace daw::app
