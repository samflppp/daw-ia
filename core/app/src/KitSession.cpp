#include "KitSession.h"

#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/direction/Direction.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace daw::app
{
namespace
{

constexpr int progressPollMs = 100;
constexpr double previewRate = 48000.0;

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

// A sample read whole, mono, at the preview's rate (linear, enough to hear).
std::vector<float> readMono(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return {};
    const auto count = static_cast<int>(
        std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(10.0 * reader->sampleRate)));
    juce::AudioBuffer<float> buffer{static_cast<int>(std::max(1U, std::min(2U, reader->numChannels))), count};
    if (!reader->read(&buffer, 0, count, 0, true, buffer.getNumChannels() > 1))
        return {};
    const auto ratio = reader->sampleRate / previewRate;
    std::vector<float> mono(static_cast<std::size_t>(static_cast<double>(count) / ratio));
    for (std::size_t index = 0; index < mono.size(); ++index)
    {
        const auto at = static_cast<double>(index) * ratio;
        const auto low = static_cast<int>(at);
        const auto high = std::min(low + 1, count - 1);
        const auto frac = static_cast<float>(at - low);
        float value = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            value += (1.0f - frac) * buffer.getSample(channel, low) + frac * buffer.getSample(channel, high);
        mono[index] = value / static_cast<float>(buffer.getNumChannels());
    }
    return mono;
}

std::string channelName(domain::kit::Role role)
{
    auto name = std::string{domain::kit::nameOf(role)};
    if (!name.empty() && name[0] >= 'a' && name[0] <= 'z')
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return name;
}

} // namespace

KitSession::KitSession(domain::CommandBus& bus,
                       const domain::ProjectState& state,
                       ui::SampleHost& samples,
                       juce::File indexStore)
    : bus_{bus}
    , state_{state}
    , samples_{samples}
    , index_{std::move(indexStore)}
    , previewFile_{
          juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("daw-ia-kit-preview.wav")}
{
}

KitSession::~KitSession()
{
    alive_->store(false);
    cancelled_.store(true);
    join();
    stopTimer();
}

void KitSession::join()
{
    if (worker_.joinable())
        worker_.join();
}

void KitSession::setStage(Stage stage, std::string status)
{
    stage_ = stage;
    status_ = std::move(status);
    sendChangeMessage();
}

void KitSession::timerCallback()
{
    sendChangeMessage(); // the progress moved
}

void KitSession::index()
{
    if (stage_ == Stage::indexing)
        return;
    join();
    cancelled_.store(false);
    progress_.store(0.0);
    setStage(Stage::indexing, "Je mesure tes samples…");
    startTimer(progressPollMs);

    worker_ = std::thread{
        [this, alive = alive_, folders = samples_.folders()]
        {
            std::vector<juce::File> files;
            for (const auto& folder : folders)
                for (const auto& found : juce::RangedDirectoryIterator(
                         folder, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
                    if (ui::SampleHost::isSampleFile(found.getFile()))
                        files.push_back(found.getFile());
            std::sort(files.begin(), files.end());

            const auto threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
            auto built = std::make_shared<engine::SampleIndex::Built>(index_.build(
                files,
                cancelled_,
                [this](std::size_t done, std::size_t total) {
                    progress_.store(total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 1.0);
                },
                threads));

            juce::MessageManager::callAsync(
                [this, alive, built]
                {
                    if (!alive->load())
                        return;
                    stopTimer();
                    if (built->cancelled)
                    {
                        setStage(library_.empty() ? Stage::idle : Stage::ready, "Index annulé.");
                        return;
                    }
                    built_ = std::make_unique<engine::SampleIndex::Built>(*built);
                    library_ = engine::SampleIndex::library(*built_);
                    juce::Logger::writeToLog(toJuce(
                        "kit: index de " + std::to_string(built_->entries.size()) + " samples, " +
                        std::to_string(built_->measured) + " mesurés, " + std::to_string(built_->reused) +
                        " repris, " + std::to_string(built_->unreadable) + " illisibles, " +
                        juce::String(built_->seconds, 1).toStdString() + " s"));
                    setStage(Stage::ready,
                             std::to_string(library_.size()) + " samples d'un kit reconnus sur " +
                                 std::to_string(built_->entries.size()) + " mesurés.");
                });
        }};
}

void KitSession::cancel()
{
    cancelled_.store(true);
}

domain::kit::Axes KitSession::directionAxes() const
{
    // The scales are a choice, said as one: 10 dB more in the top three
    // octaves than in the bottom three is « brillant » at +1; a side share
    // of 0.05 above 0.1 is « ample » at +1.
    const auto combined = domain::direction::combine(state_.direction());
    domain::kit::Axes axes;
    if (combined.tilt)
    {
        const auto& tilt = *combined.tilt;
        const auto top = (tilt[7] + tilt[8] + tilt[9]) / 3.0;
        const auto bottom = (tilt[0] + tilt[1] + tilt[2]) / 3.0;
        axes.bright = std::clamp((top - bottom) / 10.0, -2.0, 2.0) * combined.amount;
    }
    if (combined.sideShare)
        axes.ample = std::clamp((*combined.sideShare - 0.1) / 0.05, -2.0, 2.0) * combined.amount;
    return axes;
}

void KitSession::choose(const domain::kit::Axes& axes)
{
    const auto combined = domain::direction::combine(state_.direction());
    std::optional<int> tonic;
    if (combined.key)
        tonic = combined.key->tonic;
    kit_ = domain::kit::choose(library_, tonic, axes);
    std::string said = std::to_string(kit_->picks.size()) + " éléments choisis";
    if (!kit_->missing.empty())
        said += " ; " + kit_->missing.front();
    setStage(Stage::ready, said + ".");
}

void KitSession::preview()
{
    if (!kit_)
        return;

    // Two bars of 4/4 at the project's tempo: the kick on 1 and 3, the snare
    // on 2 and 4, the closed hat on the eighths, the open one on the last
    // eighth of each bar, the 808 on each downbeat, a percussion on the
    // offbeat of 3.
    const auto bpm = state_.tempoAt(0.0);
    const auto beat = 60.0 / std::max(bpm, 1.0);
    const auto length = static_cast<std::size_t>(8.0 * beat * previewRate);
    std::vector<float> mix(length, 0.0f);
    const auto lay = [&](const std::vector<float>& sound, double beats, float gain)
    {
        const auto start = static_cast<std::size_t>(beats * beat * previewRate);
        for (std::size_t index = 0; index < sound.size() && start + index < length; ++index)
            mix[start + index] += gain * sound[index];
    };
    for (const auto& pick : kit_->picks)
    {
        const auto sound = readMono(juce::File{toJuce(pick.path)});
        using domain::kit::Role;
        switch (pick.role)
        {
        case Role::kick:
            for (const double at : {0.0, 2.0, 4.0, 6.0})
                lay(sound, at, 0.6f);
            break;
        case Role::snare:
        case Role::clap:
            for (const double at : {1.0, 3.0, 5.0, 7.0})
                lay(sound, at, 0.5f);
            break;
        case Role::closedHat:
            for (int eighth = 0; eighth < 16; ++eighth)
                if (eighth % 8 != 7)
                    lay(sound, eighth * 0.5, 0.3f);
            break;
        case Role::openHat:
            for (const double at : {3.5, 7.5})
                lay(sound, at, 0.3f);
            break;
        case Role::percussion:
            for (const double at : {2.5, 6.5})
                lay(sound, at, 0.3f);
            break;
        case Role::bass808:
            for (const double at : {0.0, 4.0})
                lay(sound, at, 0.5f);
            break;
        }
    }
    for (auto& sample : mix)
        sample = std::clamp(sample, -1.0f, 1.0f);

    juce::AudioBuffer<float> buffer{1, static_cast<int>(mix.size())};
    buffer.copyFrom(0, 0, mix.data(), static_cast<int>(mix.size()));
    static_cast<void>(previewFile_.deleteFile());
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(previewFile_);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream,
                                          juce::AudioFormatWriterOptions{}
                                              .withSampleRate(previewRate)
                                              .withNumChannels(1)
                                              .withBitsPerSample(24));
        if (writer == nullptr || !writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
        {
            setStage(stage_, "L'écoute du kit n'a pas pu être préparée.");
            return;
        }
    }
    samples_.audition(previewFile_);
}

void KitSession::listenTo(std::size_t pick)
{
    if (kit_ && pick < kit_->picks.size())
        samples_.audition(juce::File{toJuce(kit_->picks[pick].path)});
}

void KitSession::stop()
{
    samples_.stopAudition();
}

bool KitSession::pose()
{
    if (!kit_ || kit_->picks.empty())
        return false;

    // The bytes into the project's store first, as every import; then the
    // channels, in one group: one Ctrl+Z takes the kit away.
    std::vector<std::unique_ptr<domain::Command>> commands;
    std::vector<std::string> unread;
    for (const auto& pick : kit_->picks)
    {
        const auto sample = samples_.import(juce::File{toJuce(pick.path)});
        if (!sample.ok())
        {
            unread.push_back(pick.path);
            continue;
        }
        const auto track = domain::TrackId::generate();
        commands.push_back(std::make_unique<domain::AddTrack>(track, channelName(pick.role)));
        commands.push_back(std::make_unique<domain::SetTrackSample>(track, sample.value()));
    }
    if (commands.empty())
        return false;

    domain::GroupOptions group{};
    group.label = "poser le kit";
    if (!bus_.executeGroup(std::move(commands), group).ok())
    {
        setStage(stage_, "Le kit n'a pas pu être posé.");
        return false;
    }
    std::string said = "Kit posé : " + std::to_string(kit_->picks.size() - unread.size()) + " pistes";
    for (const auto& missing : kit_->missing)
        said += " ; " + missing;
    if (!unread.empty())
        said += " ; " + std::to_string(unread.size()) + " sample(s) illisible(s)";
    setStage(stage_, said + ".");
    return true;
}

} // namespace daw::app
