#include "ApiKey.h"
#include "Verification.h"

#include <cstring>
#include <string>
#include <vector>

// --verify-cle (S26): the API key, as a person who has none types one.
//
// The run forgets DAW_IA_ANTHROPIC_API_KEY for its own process, so the
// copilot starts without a key and says so. Then a key made up for the run is
// typed in Fichier > Clé d'API; Windows keeps it in an entry of its own (never
// the person's); the copilot is started again and stops saying it has none.
// Its bytes, in UTF-8 and in UTF-16, are then looked for in every file the
// application writes: its settings and log, its models' folder but the
// weights, the project, the run's folder, and what changed in the temporary
// folder since the run began. Last, the same window removes it.

namespace daw::app
{
namespace
{

constexpr double copilotMs = 90000.0;

// Files skipped by the search: the weights are hundreds of megabytes the
// application never writes a key into, downloaded as they are.
bool skipped(const juce::File& file)
{
    return file.getFullPathName().containsIgnoreCase("DAW IA\\models\\") ||
           file.getFullPathName().containsIgnoreCase("DAW IA/models/");
}

bool contains(const juce::MemoryBlock& haystack, const juce::MemoryBlock& needle)
{
    if (needle.getSize() == 0 || haystack.getSize() < needle.getSize())
        return false;
    const auto* bytes = static_cast<const char*>(haystack.getData());
    const auto* wanted = static_cast<const char*>(needle.getData());
    const auto last = haystack.getSize() - needle.getSize();
    for (std::size_t at = 0; at <= last; ++at)
    {
        if (bytes[at] == wanted[0] && std::memcmp(bytes + at, wanted, needle.getSize()) == 0)
            return true;
    }
    return false;
}

} // namespace

void Verification::buildKey()
{
    if (apiKey_ == nullptr)
    {
        add("la clé est branchée", [this] { check(false, "la clé d'API est donnée à la vérification"); });
        return;
    }

    auto startedAt = std::make_shared<juce::Time>(juce::Time::getCurrentTime());
    const auto openKeyWindow = [this]
    {
        auto* file = button(titleBar_, "Fichier");
        if (file == nullptr)
        {
            check(false, "le bouton Fichier");
            return;
        }
        click(*file, file->getLocalBounds().getCentre());
        // Nouveau, Ouvrir, Enregistrer, Enregistrer sous, Exporter,
        // Génération, Affichage, then Clé d'API: the eighth.
        chooseMenuItem(8);
    };
    const auto keyWindowOpen = []
    {
        auto* dialog = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
        return dialog != nullptr && dialog->getTextEditor("cle") != nullptr;
    };
    const auto copilotReady = [this] { return copilot_.status() == ui::CopilotHost::Status::ready; };

    add(
        "sans la variable ni clé rangée : le copilote le dit",
        [this]
        {
            apiKey_->forgetEnvironmentForTest();
            juce::String error;
            check(apiKey_->remove(error), "l'entrée de la vérification est vide : " + error.toStdString());
            check(!apiKey_->present(), "aucune clé");
            copilot_.restart();
        },
        copilotReady,
        copilotMs);
    add("le copilote prêt, sans clé, et le dit",
        [this]
        {
            check(copilot_.statusMessage().find("sans clé") != std::string::npos,
                  "« " + copilot_.statusMessage() + " »");
        });

    add("Fichier > Clé d'API...", [openKeyWindow] { openKeyWindow(); }, keyWindowOpen, 3000.0);
    add(
        "une clé tapée, Enregistrer",
        [this]
        {
            juce::Random random;
            fakeKey_ = "sk-ant-verif-";
            for (int index = 0; index < 40; ++index)
                fakeKey_ << juce::String::toHexString(random.nextInt(16));
            answerDialog("cle", fakeKey_);
        },
        [this, copilotReady] { return copilotReady() && copilot_.statusMessage().empty(); },
        copilotMs);
    add("rangée par Windows, et le copilote ne dit plus qu'il n'en a pas",
        [this]
        {
            check(keySaid_ && keySaid_() == juce::String::fromUTF8("Clé rangée."),
                  "« " + (keySaid_ ? keySaid_().toStdString() : std::string{}) + " »");
            check(apiKey_->source() == ApiKey::Source::vault, "la clé vient du coffre de Windows");
            check(apiKey_->readVault() == fakeKey_, "le coffre la rend telle qu'elle a été tapée");
        });

    add("la clé n'est en clair dans aucun fichier écrit",
        [this, startedAt]
        {
            const juce::MemoryBlock utf8{fakeKey_.toRawUTF8(), fakeKey_.getNumBytesAsUTF8()};
            juce::MemoryBlock utf16;
            for (auto character : fakeKey_)
            {
                const auto unit = static_cast<char16_t>(character);
                utf16.append(&unit, sizeof(unit));
            }

            std::vector<juce::File> files;
            const auto collect = [&files](const juce::File& folder, const juce::Time& since)
            {
                if (!folder.isDirectory())
                    return;
                for (const auto& entry : juce::RangedDirectoryIterator{
                         folder, true, "*", juce::File::findFiles, juce::File::FollowSymlinks::no})
                {
                    const auto& file = entry.getFile();
                    if (skipped(file) || entry.getModificationTime() < since)
                        continue;
                    files.push_back(file);
                }
            };
            const juce::Time always{};
            const auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                     .getChildFile("DAW IA");
            const auto localAppData =
                juce::File{juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {})}.getChildFile(
                    "DAW IA");
            collect(appData, always);
            collect(localAppData, always);
            collect(folder_, always);
            collect(projectFolder_, always);
            collect(juce::File::getSpecialLocation(juce::File::tempDirectory), *startedAt);

            int found = 0;
            juce::int64 bytes = 0;
            for (const auto& file : files)
            {
                juce::MemoryBlock content;
                if (!file.loadFileAsData(content))
                    continue;
                bytes += static_cast<juce::int64>(content.getSize());
                if (contains(content, utf8) || contains(content, utf16))
                {
                    ++found;
                    note("trouvée dans " + file.getFullPathName().toStdString());
                }
            }
            note(std::to_string(files.size()) + " fichiers lus, " + std::to_string(bytes / 1024) +
                 " Ko : réglages et daw.log, dossier des modèles hors poids, projet, dossier de la "
                 "vérification, "
                 "fichiers temporaires écrits depuis le début");
            check(found == 0, "la clé n'est en clair dans aucun");
            const auto log = appData.getChildFile("daw.log");
            check(log.existsAsFile() && !log.loadFileAsString().contains(fakeKey_), "ni dans daw.log");
        });

    add(
        "Fichier > Clé d'API..., pour la retirer",
        [openKeyWindow] { openKeyWindow(); },
        keyWindowOpen,
        3000.0);
    add(
        "Retirer la clé",
        []
        {
            if (auto* dialog =
                    dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent()))
                dialog->exitModalState(2);
        },
        [this, copilotReady]
        { return copilotReady() && copilot_.statusMessage().find("sans clé") != std::string::npos; },
        copilotMs);
    add("retirée : le coffre ne l'a plus, et le copilote redit qu'il n'en a pas",
        [this]
        {
            check(keySaid_ && keySaid_() == juce::String::fromUTF8("Clé retirée."),
                  "« " + (keySaid_ ? keySaid_().toStdString() : std::string{}) + " »");
            check(apiKey_->readVault().isEmpty(), "le coffre est vide");
            check(!apiKey_->present(), "aucune clé");
        });
}

} // namespace daw::app
