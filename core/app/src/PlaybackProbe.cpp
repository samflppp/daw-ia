#include "PlaybackProbe.h"

#include "daw/engine/MeterTap.h"

#include <sstream>

namespace daw::app
{
namespace
{

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

std::string milliseconds(double value)
{
    return juce::String(value, 1).toStdString() + " ms";
}

} // namespace

PlaybackProbe::PlaybackProbe(domain::CommandBus& bus,
                             const domain::ProjectState& state,
                             tracktion::Edit& edit,
                             const TransportSync& sync)
    : bus_(bus)
    , state_(state)
    , edit_(edit)
    , sync_(sync)
{
    token_ = bus_.addObserver(*this);
    bus_.setRefusalListener([this](const domain::Refusal& refusal) { refused(refusal); });
}

PlaybackProbe::~PlaybackProbe()
{
    bus_.setRefusalListener(nullptr);
    bus_.removeObserver(token_);
}

std::string PlaybackProbe::describe() const
{
    // Read on the message thread only: the Edit and the state belong to it.
    const auto& transport = edit_.getTransport();
    const auto& event = sync_.lastEvent();

    std::ostringstream text;
    text << "domaine " << (state_.transport().playing ? "en lecture" : "à l'arrêt") << " au temps "
         << juce::String(state_.transport().positionBeats, 2) << ", moteur "
         << (transport.isPlaying() ? "en lecture" : "à l'arrêt") << " à "
         << juce::String(transport.getPosition().inSeconds(), 3) << " s";
    text << " ; TransportSync : « " << event.what << " », il y a " << milliseconds(nowMs() - event.atMs);
    text << " ; bus : " << bus_.undoDepth() << " annulables, " << bus_.redoDepth() << " rétablissables";
    if (bus_.openGesture().has_value())
        text << ", geste ouvert « " << bus_.openGestureLabel() << " »";
    text << " ; derniers appliqués :";
    for (const auto& line : applied_)
        text << " [" << line << "]";
    return text.str();
}

std::string PlaybackProbe::describeAudio() const
{
    auto& devices = edit_.engine.getDeviceManager();
    std::ostringstream text;

    text << "carte son : ";
    if (auto* device = devices.deviceManager.getCurrentAudioDevice(); device != nullptr)
        text << "« " << device->getName() << " », " << (device->isOpen() ? "ouverte" : "fermée") << ", "
             << (device->isPlaying() ? "en marche" : "arrêtée") << ", " << device->getCurrentSampleRate()
             << " Hz, " << device->getCurrentBufferSizeSamples() << " échantillons";
    else
        text << "aucune";
    text << " ; charge Tracktion " << juce::String(devices.getCpuUsage(), 3) << " ; temps du flux "
         << juce::String(devices.getCurrentStreamTime(), 3) << " s";
    if (auto* out = devices.getDefaultWaveOutDevice(); out != nullptr)
        text << " ; sortie « " << out->getName() << " » " << (out->isEnabled() ? "active" : "INACTIVE");
    else
        text << " ; AUCUNE sortie par défaut";

    const auto& transport = edit_.getTransport();
    text << " ; transport " << (transport.isPlaying() ? "en lecture" : "à l'arrêt") << " à "
         << juce::String(transport.getPosition().inSeconds(), 3) << " s"
         << (transport.looping.get() ? ", en boucle" : "");
    if (auto* context = transport.getCurrentPlaybackContext(); context != nullptr)
        text << " ; contexte "
             << (context->isPlaybackGraphAllocated() ? "graphe alloué" : "GRAPHE NON ALLOUÉ") << ", "
             << (context->isPlaying() ? "joue" : "ne joue pas")
             << (context->isPlayPending() ? ", lecture en attente" : "") << " à "
             << juce::String(context->getPosition().inSeconds(), 3) << " s";
    else
        text << " ; AUCUN contexte de lecture";

    const auto describeTaps = [&text](tracktion::PluginList& plugins)
    {
        for (auto* plugin : plugins)
        {
            if (plugin == nullptr)
                continue;
            text << " " << plugin->getName();
            if (!plugin->isEnabled())
                text << " (éteint)";
            if (auto* tap = dynamic_cast<engine::MeterTapPlugin*>(plugin); tap != nullptr)
                text << " (" << tap->blocksSeen() << " blocs" << (tap->audible() ? "" : ", NON AUDIBLE")
                     << ")";
        }
    };

    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track == nullptr)
            continue;
        text << " ; piste « " << track->getName() << " »";
        if (const auto role = track->state.getProperty("dawDomainRole").toString(); role.isNotEmpty())
            text << " [" << role << "]";
        text << (track->isMuted(true) ? " MUETTE" : "") << (track->isSolo(false) ? " SOLO" : "") << ", "
             << track->getClips().size() << " clips, plugins :";
        describeTaps(track->pluginList);
    }
    text << " ; master :";
    describeTaps(edit_.getMasterPluginList());
    return text.str();
}

void PlaybackProbe::onExecuted(const domain::Receipt& receipt)
{
    remember("exécuté", receipt);
}

void PlaybackProbe::onCoalesced(const domain::Receipt& receipt)
{
    remember("fusionné", receipt);
}

void PlaybackProbe::onUndone(const domain::Receipt& receipt)
{
    remember("annulé", receipt);
}

void PlaybackProbe::onRedone(const domain::Receipt& receipt)
{
    remember("rétabli", receipt);
}

void PlaybackProbe::remember(const char* verb, const domain::Receipt& receipt)
{
    // The age is taken when describe() is read, so the moment is kept, not
    // the delay.
    std::ostringstream line;
    line << verb << " " << receipt.type << " à " << juce::String(nowMs(), 0);
    if (receipt.gesture.has_value())
        line << ", geste";
    if (receipt.group.has_value())
        line << ", groupe « " << receipt.group->label << " »";
    if (receipt.origin.actor != domain::Actor::user)
        line << ", copilote";

    applied_.push_back(line.str());
    while (applied_.size() > kept)
        applied_.pop_front();
}

void PlaybackProbe::refused(const domain::Refusal& refusal)
{
    std::ostringstream thread;
    thread << refusal.caller;
    const auto onOwner = refusal.caller == refusal.owner;
    const auto onMessage = juce::MessageManager::existsAndIsCurrentThread();

    std::ostringstream line;
    line << "bus refused " << refusal.operation;
    if (!refusal.commandType.empty())
        line << " " << refusal.commandType;
    line << ": " << domain::describe(refusal.error.code) << " (" << refusal.error.message << ")";
    line << "; thread " << thread.str() << (onOwner ? " (owner" : " (NOT the owner")
         << (onMessage ? ", message thread)" : ", not the message thread)");
    line << "; mutating " << (refusal.mutating ? "yes" : "no");
    if (!refusal.openGesture.empty())
        line << "; gesture open: " << refusal.openGesture;
    line << "; undo " << refusal.undoDepth << ", redo " << refusal.redoDepth;

    // The rest reads the Edit and the state, and keeps a list, which only the
    // message thread may touch. A refusal from elsewhere is wrongThread, and
    // the log line says enough by itself.
    if (!onMessage)
    {
        juce::Logger::writeToLog(juce::String::fromUTF8(line.str().c_str()));
        return;
    }

    line << "; " << describe();
    const auto text = line.str();
    juce::Logger::writeToLog(juce::String::fromUTF8(text.c_str()));

    ++refusalCount_;
    refusals_.push_back(text);
    while (refusals_.size() > kept)
        refusals_.pop_front();
}

} // namespace daw::app
