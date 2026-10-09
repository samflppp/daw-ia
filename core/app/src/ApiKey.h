#pragma once

#include <juce_core/juce_core.h>

namespace daw::app
{

// The Anthropic API key (S26, decided on 9 October 2026).
//
// Read first from DAW_IA_ANTHROPIC_API_KEY, as it was set when the process
// started: the founder's and the CI's. Else from Windows' credential vault
// (Credential Manager, a generic credential, kept for this Windows session on
// this machine and encrypted by Windows under it): what a person typed in
// Fichier > Clé d'API. Never in a file, never in daw.log, never in an
// argument of a process.
//
// The services read the key from their environment, which they inherit from
// this process: a key taken from the vault is set in this process's
// environment, and nowhere else.
class ApiKey final
{
public:
    static constexpr const char* variable = "DAW_IA_ANTHROPIC_API_KEY";

    enum class Source
    {
        none,
        environment, // the variable, set before this process started
        vault,       // typed in the application, kept by Windows
    };

    // `target` names the vault's entry: a verification uses its own, and
    // never reads, writes or removes the person's.
    explicit ApiKey(juce::String target = defaultTarget);

    // Once, at start: remembers whether the variable was set, and when it
    // was not, hands the vault's key to this process's environment.
    void load();

    [[nodiscard]] Source source() const noexcept { return source_; }
    [[nodiscard]] bool present() const noexcept { return source_ != Source::none; }

    // Keeps `key` in the vault and hands it to the environment. When the
    // variable was set at start, it still wins: said by `error`, and the
    // vault keeps the typed key for a start without the variable.
    bool store(const juce::String& key, juce::String& error);

    // Removes the vault's key, and from the environment unless the variable
    // set it.
    bool remove(juce::String& error);

    // The vault's key, empty when there is none. For the checks.
    [[nodiscard]] juce::String readVault() const;

    // For a check only: this process forgets the variable it started with,
    // the way a machine without it would start.
    void forgetEnvironmentForTest();

    static constexpr const char* defaultTarget = "DAW IA/anthropic";

private:
    static void setEnvironment(const juce::String& key);

    juce::String target_;
    Source source_{Source::none};
    bool fromEnvironment_{false};
};

} // namespace daw::app
