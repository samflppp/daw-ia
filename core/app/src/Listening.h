#pragma once

#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ProjectProjector.h"
#include "daw/ui/model/ListeningHost.h"

namespace daw::app
{

// A proposal heard through the projector (ProjectProjector::listen), in the
// application. It turns a range of a pattern into a loop on the Edit's
// timeline: in pattern mode the pattern is at beat 0; in song mode, at its
// first placement.
class Listening final : public ui::ListeningHost
{
public:
    Listening(engine::ProjectProjector& projector, const domain::ProjectState& state);
    ~Listening() override;

    Listening(const Listening&) = delete;
    Listening& operator=(const Listening&) = delete;
    Listening(Listening&&) = delete;
    Listening& operator=(Listening&&) = delete;

    [[nodiscard]] std::string listen(const std::vector<Line>& lines) override;
    void stop() override;
    [[nodiscard]] bool listening() const override { return projector_.listening(); }

private:
    engine::ProjectProjector& projector_;
    const domain::ProjectState& state_;
};

} // namespace daw::app
