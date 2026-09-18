#include "daw/engine/EngineHost.h"

namespace daw::engine
{

EngineHost::EngineHost(const juce::String& applicationName)
    : engine_{std::make_unique<tracktion::Engine>(applicationName)}
{
    // Tracktion opens the audio device while the Engine is being constructed,
    // but it builds its list of wave devices from an async update, so the list
    // is still empty when the constructor returns. An Edit created and played
    // before that update has run is bound to no output at all: the transport
    // reports that it is running, and not one sample leaves the machine.
    //
    // Flushing it here is what makes the constructor mean what it says: when
    // it returns, the engine is usable. The alternative — waiting for the
    // message loop — would make "is the engine ready" depend on who calls it
    // and when, which is exactly the kind of question a caller should not have
    // to ask.
    engine_->getDeviceManager().dispatchPendingUpdates();

    edit_ = std::make_unique<tracktion::Edit>(*engine_, tracktion::Edit::forEditing);

    // Tracktion has its own UndoManager. It stays unused: undo belongs to the
    // Command Bus, and two histories would drift apart the first time one of
    // them was asked to go back. Every mutation below passes nullptr where an
    // UndoManager is expected.
    edit_->getUndoManager().clearUndoHistory();
}

EngineHost::~EngineHost() = default;

} // namespace daw::engine
