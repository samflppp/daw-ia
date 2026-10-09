#pragma once

#include "SampleLibrary.h"
#include "StemSeparation.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ContentStore.h"
#include "daw/ui/model/StemHost.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace daw::app
{

// The application's answer to StemHost (S22): a separation of an audio clip
// of the project, from its bytes in the content store to its stems laid on
// their tracks.
//
//   1. the clip's sample, copied out of the store under its own extension,
//      is handed to a StemSeparation (another process; cached);
//   2. its four stems enter the store like any sample (by digest, never by
//      value in the journal);
//   3. domain::stems::commandsFor lays them, the clip removed, in one group.
//
// The project can move during the minutes a separation takes: the clip is
// looked for again when the stems arrive, and a clip that is gone leaves the
// stems unlaid, said so.
class StemSession final : public ui::StemHost
{
public:
    struct Wiring
    {
        domain::CommandBus& bus;
        const domain::ProjectState& state;
        std::function<engine::ContentStore*()> store;
        SampleLibrary& samples;
        juce::File services; // the folder holding pyproject.toml
        juce::File cache;    // %LOCALAPPDATA%\DAW IA\stems
        std::string fake;    // "fake": the band filters, for a verification without a model
    };

    explicit StemSession(Wiring wiring);
    ~StemSession() override;

    StemSession(const StemSession&) = delete;
    StemSession& operator=(const StemSession&) = delete;
    StemSession(StemSession&&) = delete;
    StemSession& operator=(StemSession&&) = delete;

    [[nodiscard]] Stage stage() const override;
    [[nodiscard]] double progress() const override;
    [[nodiscard]] std::string status() const override;
    void separate(domain::AudioClipId clip, Quality quality) override;
    // For a check only (S26): the separation's process killed (StemSeparation).
    void killForTest();
    void cancel() override;

    // What the copilot asks (`stems.separate`, {clipId, quality}): started or
    // not, and why.
    [[nodiscard]] domain::Value startFromCopilot(const domain::Value& arguments);

    // The last separation laid, for a verification: its tracks, whether it
    // came from the cache, and the time it took.
    struct Laid
    {
        std::vector<domain::TrackId> tracks;
        std::map<std::string, juce::File> files;
        bool fromCache{false};
        double seconds{0.0};
    };
    [[nodiscard]] const std::optional<Laid>& lastLaid() const noexcept { return laid_; }

    // The default cache folder: %LOCALAPPDATA%\DAW IA\stems.
    [[nodiscard]] static juce::File defaultCache();

private:
    void finished(domain::AudioClipId clip, domain::Result<StemSeparation::Separated> result);
    void lay(engine::ContentStore* store,
             domain::AudioClipId clip,
             StemSeparation::Separated separated,
             std::map<std::string, domain::Result<domain::SampleRef>> imported);
    void setFailure(std::string message);

    Wiring wiring_;
    std::unique_ptr<StemSeparation> separation_;
    std::string failure_;
    std::optional<Laid> laid_;
    bool importing_{false};       // the stems entering the store, off the message thread
    std::uint64_t generation_{0}; // cancel() moves it: a late import is dropped

    // False once this session is gone: the import's answer is then dropped.
    std::shared_ptr<std::atomic<bool>> alive_;

    // Polls the separation for the panel: progress is read on a timer of the
    // message thread, never pushed from the separation's thread.
    struct Poll;
    std::unique_ptr<Poll> poll_;
};

} // namespace daw::app
