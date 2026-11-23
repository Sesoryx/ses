#pragma once
// ses::core — immutable render plan (DESIGN.md §4.3, ROADMAP P2).
//
// The GUI builds a complete plan from the Project, then publishes it through
// the atomic RenderPlanSlot (engine/rt/RenderPlanSlot.h). The audio callback
// only ever reads a published plan: no half-built graph, no locks. A plan is
// deeply immutable after publish — clip views, fade lengths, automation
// breakpoints and raw `const float*` pointers into MediaCache arenas.
//
// Media arenas are owned by the GUI-side core::MediaCache. A plan holds raw
// pointers, so the GUI must not drop a plan that still points into an arena
// the cache could evict: RenderPlan::bufferRefs() hands the slot a
// shared_ptr per arena at publish time (GUI thread only, before the atomic
// swap), and the slot's reclaim() clears them after the grace period. That
// keeps the MediaCache use_count() > 1 while any live or retired plan
// references the arena, which is exactly the DESIGN §4.3 eviction rule.
//
// Qt6::Core + std only — no engine, no GUI. Golden-render tests consume the
// same structs the RT mixer does.

#include "core/media/AudioBuffer.h"
#include "core/model/Project.h"

#include <QString>
#include <QVector>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace ses::core {

// One clip scheduled onto the RT mixer: a window into an arena plus the
// per-sample gain math the callback applies.
struct PlanClip {
    const float* samples = nullptr;   // interleaved stereo f32 @ the plan rate
    int64_t arenaFrames = 0;          // frames available at `samples`
    int64_t sourceOffset = 0;         // first source frame the clip window starts at
    int64_t timelineStart = 0;        // absolute project sample of the clip start
    int64_t length = 0;               // frames the clip plays
    float gain = 1.0f;                // clip gain (track gain/multipliers are NOT baked here)
    float pan = 0.0f;                 // -1 .. 1
    bool muted = false;
    Fade fadeIn;
    Fade fadeOut;

    int64_t end() const { return timelineStart + length; }
};

// A published, deeply immutable arrangement snapshot.
struct RenderPlan {
    int sampleRate = 48000;
    QVector<PlanClip> clips; // mixer order; sorted by timelineStart for stable indexing

    // Set by the builder after solo resolution: a track sounds only when it is
    // not muted and either no track is soloed or it is soloed (DESIGN §4.2).
    bool anySolo = false;

    bool trackAudible(const Track& t) const
    {
        if (t.mute)
            return false;
        return !anySolo || t.solo;
    }

    // GUI-side arena holders taken at publish time (see the header note). Kept
    // alive until the slot reclaims the plan; never touched by RT.
    std::vector<std::shared_ptr<const AudioBuffer>> bufferRefs;

    bool empty() const { return clips.isEmpty(); }
};

} // namespace ses::core
