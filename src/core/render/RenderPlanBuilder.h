#pragma once
// ses::core — RenderPlanBuilder (DESIGN.md §4.2/§4.3, ROADMAP P2).
//
// GUI thread only. Turns a core::Project into an immutable core::RenderPlan:
// per-track mute/solo resolution, A/B routing for the dual buses (P4 wires
// the crossfade), fade windows, and raw pointers into MediaCache arenas.
//
// The builder takes the decoded arenas it references (shared_ptr) so the plan
// keeps them alive while published. It never decodes and never touches the
// audio thread.
//
// Qt6::Core only — the same code path renders offline bounces (DESIGN §4.7).

#include "core/media/MediaCache.h"
#include "core/model/Project.h"
#include "core/render/RenderPlan.h"

#include <QVector>

#include <memory>

namespace ses::core {

class RenderPlanBuilder {
public:
    // cache: decoded arenas keyed by media path (may be nullptr — a missing
    // arena is an honest silence for that clip, not a build failure).
    // bus: which dual-bus output the plan renders (P4 crossfades A/B; v1 both
    // routes through the single mix, matching the AbRouting "both" default).
    explicit RenderPlanBuilder(MediaCache* cache = nullptr) : cache_(cache) {}

    RenderPlan build(const Project& project);

private:
    MediaCache* cache_;
};

} // namespace ses::core
