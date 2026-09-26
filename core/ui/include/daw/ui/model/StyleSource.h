#pragma once

#include "daw/domain/generation/StyleModel.h"

namespace daw::ui
{

// The style model the piano roll generates with, loaded once per process.
//
// The corpus pipeline (services/harmony) writes it to
// %APPDATA%\DAW IA\generation\markov.json, outside the repository: the corpus
// is the user's published work, and so is anything counted from it. The
// variable DAW_IA_STYLE_MODEL names another file; "repli" forces the
// hand-written model. A file that is missing or does not read falls back to
// the hand-written model too, and the log says which one is in use.
[[nodiscard]] const domain::generation::StyleModel& styleModel();

} // namespace daw::ui
