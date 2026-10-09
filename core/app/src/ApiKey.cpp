#include "ApiKey.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h, which it needs: a separate block, so the sorting of
// includes leaves the order alone.
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <cstdlib>
#endif

#include <vector>

namespace daw::app
{

namespace
{

#if JUCE_WINDOWS
// The environment Windows hands to the processes this one starts.
juce::String environmentValue(const char* name)
{
    const auto wide = juce::String{name};
    const auto length = GetEnvironmentVariableW(wide.toWideCharPointer(), nullptr, 0);
    if (length == 0)
        return {};
    std::vector<wchar_t> buffer(length);
    GetEnvironmentVariableW(wide.toWideCharPointer(), buffer.data(), length);
    return juce::String{buffer.data()};
}
#else
juce::String environmentValue(const char* name)
{
    const auto* value = std::getenv(name);
    return value != nullptr ? juce::String::fromUTF8(value) : juce::String{};
}
#endif

} // namespace

ApiKey::ApiKey(juce::String target)
    : target_(std::move(target))
{
}

void ApiKey::load()
{
    fromEnvironment_ = environmentValue(variable).trim().isNotEmpty();
    if (fromEnvironment_)
    {
        source_ = Source::environment;
        return;
    }
    const auto kept = readVault();
    if (kept.isEmpty())
    {
        source_ = Source::none;
        return;
    }
    setEnvironment(kept);
    source_ = Source::vault;
}

void ApiKey::forgetEnvironmentForTest()
{
    setEnvironment({});
    fromEnvironment_ = false;
    source_ = Source::none;
}

void ApiKey::setEnvironment(const juce::String& key)
{
#if JUCE_WINDOWS
    SetEnvironmentVariableW(juce::String{variable}.toWideCharPointer(),
                            key.isEmpty() ? nullptr : key.toWideCharPointer());
#else
    if (key.isEmpty())
        unsetenv(variable);
    else
        setenv(variable, key.toRawUTF8(), 1);
#endif
}

#if JUCE_WINDOWS

bool ApiKey::store(const juce::String& key, juce::String& error)
{
    const auto trimmed = key.trim();
    if (trimmed.isEmpty())
    {
        error = juce::String::fromUTF8("la clé est vide");
        return false;
    }

    const juce::MemoryBlock bytes{trimmed.toRawUTF8(), trimmed.getNumBytesAsUTF8()};
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target_.toWideCharPointer());
    credential.UserName = const_cast<LPWSTR>(L"DAW IA");
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.getSize());
    credential.CredentialBlob = static_cast<LPBYTE>(const_cast<void*>(bytes.getData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (CredWriteW(&credential, 0) == FALSE)
    {
        error = juce::String::fromUTF8("Windows n'a pas rangé la clé (erreur ") +
                juce::String{static_cast<int>(GetLastError())} + ")";
        return false;
    }

    if (fromEnvironment_)
    {
        error = juce::String::fromUTF8("rangée ; mais la variable ") + variable +
                juce::String::fromUTF8(" passe avant elle tant qu'elle est posée");
        return true;
    }
    setEnvironment(trimmed);
    source_ = Source::vault;
    return true;
}

bool ApiKey::remove(juce::String& error)
{
    if (CredDeleteW(target_.toWideCharPointer(), CRED_TYPE_GENERIC, 0) == FALSE &&
        GetLastError() != ERROR_NOT_FOUND)
    {
        error = juce::String::fromUTF8("Windows n'a pas retiré la clé (erreur ") +
                juce::String{static_cast<int>(GetLastError())} + ")";
        return false;
    }
    if (!fromEnvironment_)
    {
        setEnvironment({});
        source_ = Source::none;
    }
    return true;
}

juce::String ApiKey::readVault() const
{
    PCREDENTIALW credential = nullptr;
    if (CredReadW(target_.toWideCharPointer(), CRED_TYPE_GENERIC, 0, &credential) == FALSE)
        return {};
    const auto key = juce::String::fromUTF8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                            static_cast<int>(credential->CredentialBlobSize));
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return key;
}

#else

// Windows is the only target: elsewhere, the variable alone.
bool ApiKey::store(const juce::String&, juce::String& error)
{
    error = juce::String::fromUTF8("le coffre de Windows n'existe pas sur ce système");
    return false;
}

bool ApiKey::remove(juce::String&)
{
    return true;
}

juce::String ApiKey::readVault() const
{
    return {};
}

#endif

} // namespace daw::app
