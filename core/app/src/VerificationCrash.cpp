#include "CrashGuard.h"
#include "Verification.h"
#include "VoiceInput.h"
#include "VoiceService.h"
#include "daw/domain/serialization/Json.h"

#include <algorithm>
#include <string>

// A crash, and the launch after it (S26). Three processes, driven by
// scripts/verify-crash.ps1 with one --reglages folder, so the marker the first
// leaves is the one the second reads:
//   --verify-plantage  builds the song of the list's first twelve steps (two
//                      tracks, a pattern laid eight times, edits and undos),
//                      keeps its state as --verify does (etat.json), and
//                      crashes: an access violation, as a bug would;
//   --verify-reprise   another project open, reads what the crash left: the
//                      marker, the report, the minidump, daw.log; and
//                      « Rouvrir » names the crashed project;
//   --verify-reopen    on the crashed project, against etat.json: the same
//                      state, to the byte, and the same history.

namespace daw::app
{
namespace
{

// The list up to its copilot: what is played and edited before the copilot is
// asked anything.
constexpr std::size_t stepsBeforeCopilot = 12;

} // namespace

void Verification::buildCrash()
{
    buildList();
    steps_.resize(std::min(steps_.size(), stepsBeforeCopilot));

    add("l'état d'avant le plantage, gardé",
        [this]
        {
            const auto kept =
                domain::Value::object({{"state", state_.toValue()},
                                       {"undoDepth", domain::Value{static_cast<std::int64_t>(depth())}}});
            static_cast<void>(
                folder_.getChildFile("etat.json")
                    .replaceWithText(juce::String::fromUTF8(domain::json::write(kept).c_str())));
            note("profondeur d'historique : " + std::to_string(depth()));
            note("projet : " + projectFolder_.getFullPathName().toStdString());
        });

    add("le plantage provoqué",
        [this]
        {
            // The report is written now: nothing is written after a crash.
            report_.add({});
            report_.add(juce::String::fromUTF8("## Résultat"));
            report_.add("- " + juce::String(passed_) + juce::String::fromUTF8(" vérifications passées, ") +
                        juce::String(failed_) +
                        juce::String::fromUTF8(" en échec, puis le plantage provoqué"));
            static_cast<void>(
                folder_.getChildFile("rapport.md").replaceWithText(report_.joinIntoString("\n")));
            CrashGuard::crashForTest();
        });
}

void Verification::buildRecovery()
{
    add("ce que la session plantée a laissé",
        [this]
        {
            const auto previous = previousSession_ ? previousSession_() : std::nullopt;
            check(previous.has_value(), "la session d'avant ne s'est pas fermée : son marqueur est là");
            if (!previous.has_value())
                return;
            check(previous->crashed, "le marqueur dit un plantage, le " + previous->at.toStdString());
            check(previous->project.getChildFile("project.db").existsAsFile() &&
                      previous->project != projectFolder_,
                  "il nomme le projet planté, pas celui d'ici : " +
                      previous->project.getFullPathName().toStdString());
            check(previous->module.endsWithIgnoreCase("DAW IA.exe") && !previous->plugin,
                  "le module fautif : " + previous->module.toStdString() + ", pas un plugin");

            const auto report = previous->report.loadFileAsString();
            note("rapport : " + previous->report.getFullPathName().toStdString());
            check(report.contains(juce::String{"DAW IA "} + DAW_VERSION_LABEL), "il dit la version");
            check(report.containsIgnoreCase("exception 0xc0000005"), "l'exception : une violation d'accès");
            check(
                report.contains(juce::String::fromUTF8("dernière commande : ")) &&
                    !report.contains(juce::String::fromUTF8("dernière commande : aucune")),
                "la dernière commande : « " +
                    report.fromFirstOccurrenceOf(juce::String::fromUTF8("dernière commande : "), false, false)
                        .upToFirstOccurrenceOf("\n", false, false)
                        .toStdString() +
                    " »");
            check(report.contains("projet : " + previous->project.getFullPathName()), "le projet");
            check(report.contains("pile :") &&
                      report.fromFirstOccurrenceOf("pile :", false, false).trim().isNotEmpty(),
                  "la pile");

            const auto dump = previous->report.withFileExtension("dmp");
            check(dump.existsAsFile() && dump.getSize() > 0,
                  "un minidump à côté : " + std::to_string(dump.getSize() / 1024) + " Ko");

            const auto log = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("DAW IA")
                                 .getChildFile("daw.log");
            check(log.loadFileAsString().contains(juce::String::fromUTF8("plantage : ") + previous->at),
                  "daw.log garde le rapport");
        });

    add("« Rouvrir » : le projet planté",
        [this]
        {
            const auto previous = previousSession_ ? previousSession_() : std::nullopt;
            if (!previous.has_value() || !reopenAfterCrash_ || !reopenAsked_)
                return check(false, "la reprise est branchée");
            reopenAfterCrash_(false);
            check(reopenAsked_() == previous->project,
                  "rouvert : " + reopenAsked_().getFullPathName().toStdString());
        });
}

} // namespace daw::app

namespace daw::app
{

// --verify-services (S26): a Python service killed while the DAW runs, as a
// crash of it would end it. The DAW stays up, says it, the project does not
// move, and the service starts again when asked: the copilot by its button,
// the transcriber by the next press of the key. The separator's process is
// killed in --verify-stems, during a separation.
void Verification::buildServices()
{
    auto before = std::make_shared<std::string>();
    const auto copilotReady = [this] { return copilot_.status() == ui::CopilotHost::Status::ready; };

    add("le copilote prêt", [] {}, copilotReady, 90000.0);
    add(
        "son processus tué",
        [this, before]
        {
            *before = domain::json::write(state_.toValue());
            if (!killCopilot_)
                return check(false, "le copilote se laisse tuer");
            killCopilot_();
        },
        [this] { return copilot_.status() == ui::CopilotHost::Status::stopped; },
        10000.0);
    add("le DAW debout le dit, et le projet n'a pas bougé",
        [this, before]
        {
            check(copilot_.statusMessage().find("s'est arrêté") != std::string::npos,
                  "« " + copilot_.statusMessage() + " »");
            const auto lines = copilot_.transcript().size();
            copilot_.ask("Ajoute une piste de basse.");
            check(copilot_.transcript().size() == lines + 1 &&
                      copilot_.transcript().back().text.find("Relancez-le") != std::string::npos,
                  "une demande sans copilote : « " + copilot_.transcript().back().text + " »");
            check(domain::json::write(state_.toValue()) == *before, "le projet à l'octet");
            check(window_.isShowing(), "la fenêtre est là");
        });
    add("« Relancer » : le copilote revient", [this] { copilot_.restart(); }, copilotReady, 90000.0);

    add(
        "le transcripteur prêt",
        [this]
        {
            if (voice_ == nullptr)
                return check(false, "la voix est branchée");
            voice_->service().warmUp();
        },
        [this] { return voice_ == nullptr || voice_->service().stage() == VoiceService::Stage::ready; },
        60000.0);
    add(
        "son processus tué",
        [this, before]
        {
            if (voice_ == nullptr)
                return;
            *before = domain::json::write(state_.toValue());
            voice_->service().killForTest();
        },
        [this] { return voice_ == nullptr || voice_->service().stage() == VoiceService::Stage::stopped; },
        10000.0);
    add("le DAW debout le dit, et le projet n'a pas bougé",
        [this, before]
        {
            if (voice_ == nullptr)
                return;
            check(voice_->service().status().find("s'est arrêtée") != std::string::npos,
                  "« " + voice_->service().status() + " »");
            check(domain::json::write(state_.toValue()) == *before, "le projet à l'octet");
        });
    add(
        "relancé à la demande, comme le ferait la touche",
        [this]
        {
            if (voice_ != nullptr)
                voice_->service().warmUp();
        },
        [this] { return voice_ == nullptr || voice_->service().stage() == VoiceService::Stage::ready; },
        60000.0);
}

} // namespace daw::app
