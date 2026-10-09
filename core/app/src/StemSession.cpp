#include "StemSession.h"

#include "daw/domain/rights/Rights.h"
#include "daw/domain/stems/Laying.h"

#include <juce_events/juce_events.h>

#include <thread>
#include <utility>

namespace daw::app
{

namespace
{

// How often the panel is told the separation moved: a progress bar, not an
// animation.
constexpr int pollHz = 10;

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

} // namespace

// The panel reads progress on its own repaints; this timer only says
// "something moved" while a separation runs, ten times a second.
struct StemSession::Poll final : juce::Timer
{
    explicit Poll(StemSession& owner)
        : session{owner}
    {
    }

    void timerCallback() override
    {
        session.sendChangeMessage();
        if (session.separation_ == nullptr || !session.separation_->running())
            stopTimer();
    }

    StemSession& session;
};

StemSession::StemSession(Wiring wiring)
    : wiring_{std::move(wiring)}
    , alive_{std::make_shared<std::atomic<bool>>(true)}
    , poll_{std::make_unique<Poll>(*this)}
{
    separation_ = wiring_.fake.empty()
                      ? std::make_unique<StemSeparation>(wiring_.services, wiring_.cache)
                      : std::make_unique<StemSeparation>(wiring_.services, wiring_.cache, wiring_.fake);
}

StemSession::~StemSession()
{
    alive_->store(false);
    poll_->stopTimer();
}

juce::File StemSession::defaultCache()
{
    const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    const auto base = local.isNotEmpty()
                          ? juce::File{local}
                          : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    return base.getChildFile("DAW IA").getChildFile("stems");
}

StemSession::Stage StemSession::stage() const
{
    if (importing_)
        return Stage::separating;
    switch (separation_->stage())
    {
    case StemSeparation::Stage::installing:
        return Stage::installing;
    case StemSeparation::Stage::separating:
        return Stage::separating;
    case StemSeparation::Stage::failed:
        return Stage::failed;
    case StemSeparation::Stage::idle:
    case StemSeparation::Stage::done:
    case StemSeparation::Stage::cancelled:
        break;
    }
    return failure_.empty() ? Stage::idle : Stage::failed;
}

double StemSession::progress() const
{
    return separation_->progress();
}

std::string StemSession::status() const
{
    if (!failure_.empty())
        return failure_;
    if (importing_)
        return "Les stems entrent dans le projet…";
    return separation_->status();
}

void StemSession::setFailure(std::string message)
{
    failure_ = std::move(message);
    juce::Logger::writeToLog(toJuce("stems: " + failure_));
    sendChangeMessage();
}

void StemSession::separate(domain::AudioClipId clip, Quality quality)
{
    if (!domain::rights::allows(domain::rights::Feature::stems))
    {
        setFailure(domain::rights::refusal(domain::rights::Feature::stems));
        return;
    }
    if (importing_)
    {
        setFailure("Une séparation est déjà en cours.");
        return;
    }
    failure_.clear();
    const auto* found = wiring_.state.findAudioClip(clip);
    auto* store = wiring_.store ? wiring_.store() : nullptr;
    if (found == nullptr || store == nullptr)
    {
        setFailure("Rien à séparer : le clip n'est plus là.");
        return;
    }

    // The sample, out of the store under its own extension: the separator
    // reads a format by its name as much as by its bytes.
    const auto source = wiring_.cache.getChildFile("sources").getChildFile(toJuce(
        found->sample.blob.digest + "." + (found->sample.format.empty() ? "wav" : found->sample.format)));
    if (!source.existsAsFile())
    {
        const auto bytes = store->get(found->sample.blob);
        static_cast<void>(source.getParentDirectory().createDirectory());
        if (!bytes || !source.replaceWithData(bytes.value().getData(), bytes.value().getSize()))
        {
            setFailure("Le son du clip n'a pas pu être lu.");
            return;
        }
    }

    const auto model = quality == Quality::fast ? StemSeparation::Model::fast : StemSeparation::Model::best;
    auto started = separation_->start(source,
                                      found->sample.blob.digest,
                                      model,
                                      [this, clip](domain::Result<StemSeparation::Separated> result)
                                      { finished(clip, std::move(result)); });
    if (!started)
    {
        setFailure(started.error().code == domain::ErrorCode::conflict ? "Une séparation est déjà en cours."
                                                                       : started.error().message);
        return;
    }
    juce::Logger::writeToLog("stems: separating " + toJuce(found->sample.name));
    poll_->startTimerHz(pollHz);
    sendChangeMessage();
}

void StemSession::cancel()
{
    ++generation_; // an import in flight is dropped when it answers
    importing_ = false;
    separation_->cancel();
    juce::Logger::writeToLog("stems: annulé");
    sendChangeMessage();
}

void StemSession::finished(domain::AudioClipId clip, domain::Result<StemSeparation::Separated> result)
{
    if (!result)
    {
        setFailure(result.error().message);
        return;
    }

    auto* store = wiring_.store ? wiring_.store() : nullptr;
    if (store == nullptr || wiring_.state.findAudioClip(clip) == nullptr)
    {
        setFailure("Les stems sont prêts, mais le clip a été retiré entre-temps : rien n'est posé.");
        return;
    }

    // Into the store off the message thread: a digest and a copy of four
    // files the length of the song take seconds, and the window must not
    // stop for them (S22, --verify-stems saw 656 ms for 20 s of audio).
    importing_ = true;
    const auto generation = generation_;
    sendChangeMessage();
    std::thread{
        [alive = alive_, store, clip, generation, separated = std::move(result).value(), this]() mutable
        {
            std::map<std::string, domain::Result<domain::SampleRef>> imported;
            for (const auto name : domain::stems::names)
            {
                const auto file = separated.stems.find(std::string{name});
                imported.emplace(std::string{name},
                                 file == separated.stems.end()
                                     ? domain::Result<domain::SampleRef>{domain::fail(
                                           domain::ErrorCode::notFound, "missing stem")}
                                     : SampleLibrary::importInto(*store, file->second));
            }
            juce::MessageManager::callAsync(
                [alive,
                 store,
                 clip,
                 generation,
                 separated = std::move(separated),
                 imported = std::move(imported),
                 this]() mutable
                {
                    if (!alive->load() || generation != generation_)
                        return;
                    lay(store, clip, std::move(separated), std::move(imported));
                });
        }}
        .detach();
}

void StemSession::lay(engine::ContentStore* store,
                      domain::AudioClipId clip,
                      StemSeparation::Separated separated,
                      std::map<std::string, domain::Result<domain::SampleRef>> imported)
{
    importing_ = false;
    const auto* found = wiring_.state.findAudioClip(clip);
    if (found == nullptr || (wiring_.store ? wiring_.store() : nullptr) != store)
    {
        setFailure("Les stems sont prêts, mais le clip a été retiré entre-temps : rien n'est posé.");
        return;
    }

    domain::stems::Laying laying;
    laying.sourceName = found->sample.name;
    laying.startBeats = found->startBeats;
    laying.replaced = clip;
    for (const auto name : domain::stems::names)
    {
        auto& sample = imported.at(std::string{name});
        if (!sample)
        {
            setFailure("Le stem " + std::string{name} +
                       " n'a pas pu entrer dans le projet : " + sample.error().message);
            return;
        }
        auto named = sample.value();
        named.name = std::string{domain::stems::label(name)} + " — " + laying.sourceName;
        laying.stems.push_back(domain::stems::Stem{
            std::string{name}, named, domain::TrackId::generate(), domain::AudioClipId::generate()});
    }

    auto commands = domain::stems::commandsFor(wiring_.state, laying);
    if (!commands)
    {
        setFailure(commands.error().message);
        return;
    }
    domain::GroupOptions group{};
    group.label = domain::stems::groupLabel(laying);
    if (auto done = wiring_.bus.executeGroup(std::move(commands).value(), group); !done)
    {
        setFailure("Les stems n'ont pas pu être posés : " + done.error().message);
        return;
    }

    Laid laid;
    for (const auto& stem : laying.stems)
        laid.tracks.push_back(stem.track);
    laid.files = separated.stems;
    laid.fromCache = separated.fromCache;
    laid.seconds = separated.seconds;
    laid_ = std::move(laid);
    juce::Logger::writeToLog("stems: " + toJuce(laying.sourceName) + " posé en " +
                             juce::String(static_cast<int>(laying.stems.size())) + " pistes");
    sendChangeMessage();
}

domain::Value StemSession::startFromCopilot(const domain::Value& arguments)
{
    using domain::Value;
    const auto clipText = arguments.stringAt("clipId");
    if (!clipText || !domain::AudioClipId::parse(clipText.value()))
        return Value::object(
            {{"started", Value{false}}, {"status", Value{std::string{"clipId manquant ou illisible"}}}});
    const auto clip = domain::AudioClipId::parse(clipText.value()).value();

    const auto quality = arguments.stringAt("quality");
    separate(clip, quality && quality.value() == "fast" ? Quality::fast : Quality::best);
    return Value::object(
        {{"started", Value{failure_.empty()}},
         {"status", Value{status()}},
         {"says",
          Value{std::string{
              "La séparation tourne en fond, plusieurs minutes ; sa progression s'affiche dans la "
              "playlist et la personne peut l'annuler. Les stems remplaceront le clip, sur quatre "
              "pistes, en une seule entrée d'historique."}}}});
}

} // namespace daw::app
