#include "WorkspaceSwitch.h"

#include <juce_core/juce_core.h>

#include <utility>

namespace daw::app
{

WorkspaceSwitch::WorkspaceSwitch(const ui::Workspaces& workspaces, std::string current)
    : workspaces_(workspaces)
    , current_(std::move(current))
{
}

std::vector<ui::WorkspaceHost::Entry> WorkspaceSwitch::available() const
{
    std::vector<Entry> entries;
    entries.reserve(workspaces_.all().size());

    for (const auto& manifest : workspaces_.all())
        entries.push_back(Entry{manifest.id, manifest.label});

    return entries;
}

std::string WorkspaceSwitch::current() const
{
    return current_;
}

void WorkspaceSwitch::request(std::string_view id)
{
    const auto* manifest = workspaces_.find(id);
    if (manifest == nullptr)
    {
        juce::Logger::writeToLog("no such workspace: " + juce::String(std::string{id}));
        return;
    }

    current_ = manifest->id;

    if (onShow)
        onShow(*manifest);
}

} // namespace daw::app
