#include "Verification.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/engine/MeterTap.h"
#include "daw/engine/Rendering.h"

#include <array>
#include <memory>
#include <string>

// --verify-lecture (S21): the silent playback of S12, seen again in S20 at
// steps 58 and 60 of --verify — a looped bar that plays and is not heard,
// every meter at -100 dBFS, the engine playing — once in three runs. A whole
// --verify takes too long to reproduce something that rare, so this run
// builds the same project with the same steps, then plays its first bar
// sixty times.
//
// Each play follows one of five actions, in turn. The first four were the
// suspects; the fifth is the cause found by them (S21):
//   - nothing: the play alone;
//   - an offline render of the live Edit: the steps before 58 render it, and
//     the render frees the playback context and turns every plugin off;
//   - a command: the projection rebuilds the graph while nothing plays;
//   - a sample auditioned: a second callback on the engine's audio device;
//   - the sound card lost, the way a Bluetooth headset that goes away leaves
//     it: the device closed, and none open. The first run that reproduced
//     the silence found it there — 113 readings of 114 with no device, the
//     transport playing. AudioOutputKeeper must reopen one within a second.
// The report ends on how many plays were silent after each action.

namespace daw::app
{
namespace
{

constexpr int cyclesPerAction = 15;
constexpr double heardDb = -60.0;
constexpr double secondReadingMs = 500.0;

constexpr std::array<const char*, 5> actions{
    "rien", "rendu hors ligne", "commande", "écoute d'un sample", "carte son perdue"};

} // namespace

void Verification::addPlaybackCycles()
{
    const auto master = engine::MeterTapPlugin::masterStrip.toStdString();

    for (int cycle = 0; cycle < cyclesPerAction * static_cast<int>(actions.size()); ++cycle)
    {
        const std::string action = actions[static_cast<std::size_t>(cycle) % actions.size()];
        const auto name = "cycle " + std::to_string(cycle + 1) + " (" + action + ")";

        add(
            name + " : arrêt",
            [this]
            {
                if (clock_.isPlaying())
                    key(juce::KeyPress{juce::KeyPress::spaceKey});
            },
            [this] { return !clock_.isPlaying(); },
            3000.0);

        add(
            name + " : " + action,
            [this, action]
            {
                if (action == "rendu hors ligne")
                {
                    check(engine::renderAsPlayed(edit_, folder_.getChildFile("cycle-rendu.wav")),
                          "le rendu hors ligne de l'Edit vivant aboutit");
                }
                else if (action == "commande")
                {
                    // Net nothing: the first track muted, then unmuted. Two
                    // commands, two projections, the graph rebuilt twice.
                    if (!state_.tracks().empty())
                    {
                        const auto track = state_.tracks().front().id;
                        static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackMuted>(track, true)));
                        static_cast<void>(
                            bus_.execute(std::make_unique<domain::SetTrackMuted>(track, false)));
                    }
                }
                else if (action == "écoute d'un sample")
                {
                    clickBrowserSample("Kick 808.wav");
                }
                else if (action == "carte son perdue")
                {
                    if (output_ == nullptr)
                    {
                        check(false, "le gardien de la carte son est branché");
                        return;
                    }
                    reopenedBefore_ = output_->reopenCount();
                    output_->loseOutputForTest();
                    check(output_->outputName().isEmpty(), "la carte son est fermée, comme un casque parti");
                }
            },
            [this, action]
            {
                if (action == "écoute d'un sample")
                    return samples_.auditionPeakDb() > heardDb;
                if (action == "carte son perdue")
                    return output_ != nullptr && output_->outputName().isNotEmpty();
                return true;
            },
            3000.0);

        if (action == "carte son perdue")
            add(name + " : une carte rouverte, et l'écran le dit",
                [this]
                {
                    if (output_ == nullptr)
                        return;
                    note("sortie rouverte : « " + output_->outputName().toStdString() + " »");
                    check(output_->reopenCount() == reopenedBefore_ + 1, "rouverte une fois");
                    check(clock_.outputNotice().starts_with("sortie : "),
                          "sous la position : « " + clock_.outputNotice() + " »");
                    check(!clock_.outputLost(), "la sortie n'est plus dite perdue");
                });

        add(
            name + " : Espace, la boucle s'entend",
            [this]
            {
                press("SONG");
                static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(true, 0.0, 4.0)));
                static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
                key(juce::KeyPress{juce::KeyPress::spaceKey});
            },
            [this, master] { return clock_.isPlaying() && levelOf(master).peakDb > heardDb; },
            6000.0);

        add(
            name + " : relevé",
            [this, master, action]
            {
                const auto level = levelOf(master).peakDb;
                cycleSilent_ = level <= heardDb;
                ++cyclesByAction_[action];
                if (!cycleSilent_)
                {
                    note("master : crête " + juce::String(level, 1).toStdString() + " dBFS");
                    return;
                }

                ++silentByAction_[action];
                const auto path = probe_ != nullptr ? probe_->describeAudio() : std::string{"pas de sonde"};
                note("SILENCE. chemin audio : " + path);
                juce::Logger::writeToLog(
                    juce::String::fromUTF8(("lecture muette, " + action + " : " + path).c_str()));
            },
            [this] { return juce::Time::getMillisecondCounterHiRes() - startedAtMs_ >= secondReadingMs; },
            secondReadingMs + 1000.0);

        add(name + " : second relevé, puis arrêt",
            [this, action]
            {
                if (cycleSilent_)
                {
                    const auto path =
                        probe_ != nullptr ? probe_->describeAudio() : std::string{"pas de sonde"};
                    note("0,5 s après : " + path);
                    juce::Logger::writeToLog(
                        juce::String::fromUTF8(("lecture muette, 0,5 s après : " + path).c_str()));
                }
                if (clock_.isPlaying())
                    key(juce::KeyPress{juce::KeyPress::spaceKey});
            });
    }

    add("le compte, par action",
        [this]
        {
            int silent = 0;
            for (const auto* action : actions)
            {
                const auto count = silentByAction_[action];
                silent += count;
                note(std::string{action} + " : " + std::to_string(count) + " lectures muettes sur " +
                     std::to_string(cyclesByAction_[action]));
            }
            check(silent == 0,
                  "aucune lecture muette sur " +
                      std::to_string(cyclesPerAction * static_cast<int>(actions.size())));
        });
}

} // namespace daw::app
