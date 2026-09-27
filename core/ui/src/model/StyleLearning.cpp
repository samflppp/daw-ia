#include "daw/ui/model/StyleLearning.h"

#include "daw/domain/serialization/Json.h"

#include <cmath>
#include <optional>
#include <utility>

namespace daw::ui
{
namespace
{

constexpr auto projectFormat = "daw-ia.learned-project";
constexpr auto settingsName = "reglages.json";

StyleLearning* current = nullptr;

[[nodiscard]] std::optional<domain::Value> readJson(const juce::File& file)
{
    if (!file.existsAsFile())
        return std::nullopt;
    auto value = domain::json::read(file.loadFileAsString().toStdString());
    if (!value)
        return std::nullopt;
    return std::move(value).value();
}

// Written beside, then moved over: a file read by the next launch is never
// half written.
void writeJson(const juce::File& file, const domain::Value& value)
{
    static_cast<void>(file.getParentDirectory().createDirectory());
    const auto temporary = file.getSiblingFile(file.getFileName() + ".tmp");
    if (temporary.replaceWithText(
            juce::String::fromUTF8(domain::json::write(value).c_str()), false, false, "\n"))
        static_cast<void>(temporary.moveFileTo(file));
}

[[nodiscard]] domain::Value
projectValue(const std::string& name, bool excluded, const domain::generation::Learned* learned)
{
    return domain::Value::object({{"format", domain::Value{projectFormat}},
                                  {"version", domain::Value{1}},
                                  {"project", domain::Value{name}},
                                  {"excluded", domain::Value{excluded}},
                                  {"learned", learned != nullptr ? learned->toValue() : domain::Value{}}});
}

[[nodiscard]] std::shared_ptr<const domain::generation::StyleModel>
borrowed(const domain::generation::StyleModel& base)
{
    // Not owned: the base lives as long as the process.
    return {std::shared_ptr<const domain::generation::StyleModel>{}, &base};
}

} // namespace

StyleLearning::StyleLearning(juce::File folder)
    : folder_{std::move(folder)}
{
    if (const auto settings = readJson(folder_.getChildFile(settingsName)); settings.has_value())
    {
        if (const auto* enabled = settings->find("enabled"); enabled != nullptr && enabled->asBool())
            enabled_ = enabled->asBool().value();
    }
}

StyleLearning::~StyleLearning()
{
    pool_.removeAllJobs(false, 10000);
    if (current == this)
        current = nullptr;
}

juce::File StyleLearning::defaultFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DAW IA")
        .getChildFile("generation")
        .getChildFile("appris");
}

juce::File StyleLearning::fileFor(const std::string& projectId) const
{
    return folder_.getChildFile(juce::String{projectId} + ".json");
}

void StyleLearning::attach(const std::string& projectId,
                           const std::string& projectName,
                           const std::vector<domain::Value>& journal)
{
    projectId_ = projectId;
    projectName_ = projectName;
    machine_ = {};
    machine_.read(journal);
    dirty_ = stale_ = true;

    excluded_ = false;
    if (const auto mine = readJson(fileFor(projectId_)); mine.has_value())
    {
        if (const auto* excluded = mine->find("excluded"); excluded != nullptr && excluded->asBool())
            excluded_ = excluded->asBool().value();
    }

    pool_.addJob([this] { readOthers(); });
}

void StyleLearning::readOthers()
{
    domain::generation::Learned sum{};
    int count = 0;
    for (const auto& file : folder_.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        if (file.getFileName() == settingsName || file == fileFor(projectId_))
            continue;
        const auto value = readJson(file);
        if (!value.has_value())
            continue;
        const auto format = value->stringAt("format");
        if (!format || format.value() != projectFormat)
            continue;
        if (const auto* excluded = value->find("excluded");
            excluded != nullptr && excluded->asBool() && excluded->asBool().value())
            continue;
        const auto* learnedValue = value->find("learned");
        if (learnedValue == nullptr || learnedValue->isNull())
            continue;
        auto learned = domain::generation::Learned::fromValue(*learnedValue);
        if (!learned || learned.value().notes() <= 0.0)
            continue;
        sum.add(learned.value(), 1.0);
        ++count;
    }

    const std::lock_guard lock{mutex_};
    others_ = std::move(sum);
    otherCount_ = count;
}

void StyleLearning::writeSettings() const
{
    writeJson(folder_.getChildFile(settingsName),
              domain::Value::object({{"enabled", domain::Value{enabled_}}}));
}

void StyleLearning::writeProject(const domain::generation::Learned* learned) const
{
    if (projectId_.empty())
        return;
    writeJson(fileFor(projectId_), projectValue(projectName_, excluded_, learned));
}

void StyleLearning::saved(const domain::ProjectState& state)
{
    if (!enabled_ || projectId_.empty() || !dirty_)
        return;
    dirty_ = false;

    if (excluded_)
    {
        writeProject(nullptr);
        return;
    }

    // The message thread copies; the count and the write happen elsewhere.
    auto copy = std::make_shared<domain::ProjectState>(state);
    auto notes = std::make_shared<std::map<std::string, domain::Note>>(machine_.notes());
    const auto file = fileFor(projectId_);
    const auto name = projectName_;
    pool_.addJob(
        [copy, notes, file, name]
        {
            const auto learned = domain::generation::learn(*copy, *notes);
            writeJson(file, projectValue(name, false, &learned));
        });
}

void StyleLearning::reload()
{
    model_.reset();
    pool_.addJob([this] { readOthers(); });
}

void StyleLearning::settle()
{
    while (pool_.getNumJobs() > 0)
        juce::Thread::sleep(2);
}

std::shared_ptr<const domain::generation::StyleModel>
StyleLearning::model(const domain::ProjectState& state, const domain::generation::StyleModel& base)
{
    baseOrigin_ = base.origin();
    if (!enabled_)
    {
        current_ = {};
        total_ = {};
        return borrowed(base);
    }

    // Counted again only when the project changed since the last Ctrl+G: a
    // second proposal on the same notes costs nothing.
    if (stale_)
    {
        current_ =
            excluded_ ? domain::generation::Learned{} : domain::generation::learn(state, machine_.notes());
        stale_ = false;
    }
    domain::generation::Learned total{};
    {
        const std::lock_guard lock{mutex_};
        total = others_;
    }
    total.add(current_, domain::generation::currentProjectWeight);

    if (total.notes() <= 0.0)
    {
        total_ = std::move(total);
        return borrowed(base);
    }
    if (model_ != nullptr && modelBase_ == &base && total == total_)
        return model_;

    model_ = std::make_shared<const domain::generation::StyleModel>(domain::generation::blend(base, total));
    modelBase_ = &base;
    total_ = std::move(total);
    return model_;
}

std::string StyleLearning::describe(domain::generation::Role role) const
{
    if (!enabled_)
        return baseOrigin_ + " (apprentissage coupé)";

    const auto share = domain::generation::learnedShare(total_, role);
    if (share <= 0.0)
        return excluded_ ? baseOrigin_ + " (projet exclu)" : baseOrigin_;

    double notes = current_.role(role).notes;
    int projects = current_.role(role).notes > 0.0 ? 1 : 0;
    {
        const std::lock_guard lock{mutex_};
        notes += others_.role(role).notes;
        projects += otherCount_;
    }

    const auto percent = static_cast<int>(std::lround(share * 100.0));
    return std::to_string(percent) + " % appris (" + std::to_string(projects) +
           (projects > 1 ? " projets, " : " projet, ") +
           std::to_string(static_cast<long long>(std::lround(notes))) + " notes) · " +
           std::to_string(100 - percent) + " % " + baseOrigin_;
}

void StyleLearning::setEnabled(bool enabled)
{
    enabled_ = enabled;
    dirty_ = stale_ = true;
    model_.reset();
    writeSettings();
}

void StyleLearning::setProjectExcluded(bool excluded)
{
    excluded_ = excluded;
    dirty_ = stale_ = true;
    model_.reset();
    // Said on disk at once: the counts of a project left out go now, not at
    // the next save.
    if (excluded_)
        writeProject(nullptr);
    else
        static_cast<void>(fileFor(projectId_).deleteFile());
}

void StyleLearning::forget()
{
    settle();
    for (const auto& file : folder_.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        if (file.getFileName() != settingsName)
            static_cast<void>(file.deleteFile());
    }
    {
        const std::lock_guard lock{mutex_};
        others_ = {};
        otherCount_ = 0;
    }
    excluded_ = false;
    dirty_ = stale_ = true;
    model_.reset();
}

int StyleLearning::otherProjects() const
{
    const std::lock_guard lock{mutex_};
    return otherCount_;
}

void StyleLearning::onExecuted(const domain::Receipt& receipt)
{
    machine_.observe(receipt);
    dirty_ = stale_ = true;
}

void StyleLearning::onCoalesced(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    dirty_ = stale_ = true;
}

void setStyleLearning(StyleLearning* learning) noexcept
{
    current = learning;
}

StyleLearning* styleLearning() noexcept
{
    return current;
}

} // namespace daw::ui
