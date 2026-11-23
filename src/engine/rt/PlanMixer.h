#pragma once
// ses::engine — RT plan mixer (DESIGN.md §4.2, ROADMAP P2).
//
// Reads an immutable core::RenderPlan and renders one block: clips are
// scheduled by timeline position, gain/pan/mute applied, fade-in/out evaluated
// per sample from the same core::fadeInGain/fadeOutGain the bounce renderer
// uses. No allocation, no locks, no Qt, no logging — the callback rules of
// DESIGN §2/§4 apply to everything in this header and its caller.
//
// Same math as the offline bounce (DESIGN §4.7): export == playback by
// construction. Golden-render tests compare the two.

#include "core/render/RenderPlan.h"

#include <cstddef>

namespace ses::engine {

class PlanMixer {
public:
    static constexpr std::size_t kMaxClips = 64;

    // Renders [playhead, playhead+frames) of `plan` into `output` (interleaved
    // stereo, replaced). `plan` must stay alive and immutable for the whole
    // call — the slot guarantees that across the callback.
    void render(const core::RenderPlan& plan, long long playhead,
                std::size_t frames, float* output) const noexcept;

    // True when no clip in the plan overlaps [playhead, playhead+frames).
    bool silentAt(const core::RenderPlan& plan, long long playhead,
                  std::size_t frames) const noexcept;
};

} // namespace ses::engine
