#include "About.h"

#include "BuildInfo.h"

namespace daw::app
{

std::string About::version() const
{
    return DAW_VERSION_LABEL;
}

std::string About::commit() const
{
    return std::string{DAW_BUILD_COMMIT} + (DAW_BUILD_MODIFIED != 0 ? " + modifications" : "");
}

std::string About::mentions() const
{
    // What the licences ask to be said where the person sees it
    // (docs/licences-tierces.md): Tracktion's branding for the Personal
    // plan, and NVIDIA's attribution for Parakeet under CC-BY-4.0.
    return "Powered by Tracktion Engine.\n"
           "Construit avec JUCE.\n"
           "Reconnaissance vocale : Parakeet TDT 0.6B v3, de NVIDIA, sous licence CC-BY-4.0 "
           "(https://creativecommons.org/licenses/by/4.0/), quantifié en int8 par sherpa-onnx ; "
           "téléchargé à la première utilisation de la voix.\n"
           "Séparation en stems : demucs (MIT) et les poids HTDemucs de Meta, téléchargés à la première "
           "séparation.\n"
           "Les licences de chaque composant tiers sont ci-dessous.";
}

std::string About::licences() const
{
    const auto file = licencesFile();
    if (file == juce::File{})
        return "Le fichier des licences (licences.txt) manque à côté du programme.";
    return file.loadFileAsString().toStdString();
}

juce::String About::banner()
{
    return juce::String{"DAW IA "} + DAW_VERSION_LABEL + " (" + DAW_BUILD_COMMIT +
           (DAW_BUILD_MODIFIED != 0 ? " + modifications" : "") + ")";
}

juce::File About::licencesFile()
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto installed = executable.getSiblingFile("licences.txt");
    if (installed.existsAsFile())
        return installed;

    // Run from the build tree: the repository's document, found by walking up.
    for (auto folder = executable.getParentDirectory(); folder.getParentDirectory() != folder;
         folder = folder.getParentDirectory())
    {
        const auto document = folder.getChildFile("docs").getChildFile("licences-tierces.md");
        if (document.existsAsFile())
            return document;
    }
    return {};
}

} // namespace daw::app
