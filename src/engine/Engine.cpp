#include "engine/Engine.h"

// Intentionally empty for the UI-first slice: NullEngine is header-only.
// P1 adds AudioDevice (miniaudio), RtContext, RenderPlanSlot, block mixer here.

namespace ses::engine {
// Anchor TU so the static library is never empty.
const char* engineModuleName()
{
    return "ses_engine (stub — real RT engine lands in P1)";
}
} // namespace ses::engine