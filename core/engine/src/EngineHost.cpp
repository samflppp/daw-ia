#include "daw/engine/EngineHost.h"

namespace daw::engine
{

EngineHost::EngineHost(const juce::String& applicationName)
    : engine_{std::make_unique<tracktion::Engine>(applicationName)}
    , edit_{std::make_unique<tracktion::Edit>(*engine_, tracktion::Edit::forEditing)}
{
    // Tracktion has its own UndoManager. It stays unused: undo belongs to the
    // Command Bus, and two histories would drift apart the first time one of
    // them was asked to go back. Every mutation below passes nullptr where an
    // UndoManager is expected.
    edit_->getUndoManager().clearUndoHistory();
}

EngineHost::~EngineHost() = default;

} // namespace daw::engine
