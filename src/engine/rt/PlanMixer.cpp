// PlanMixer implementation (ROADMAP P2, DESIGN §4.2/§4.6). RT-safe: the only
// operations are reads of the immutable plan, float math and writes into the
// caller's buffer. `noexcept` because the callback must never throw.

#include "engine/rt/PlanMixer.h"

#include "core/render/FadeCurve.h"

#include <algorithm>
#include <cmath>

namespace ses::engine {

namespace {

// Per-sample fade multiplier for a clip at source frame `src` (absolute
// within the clip window). Both fades evaluate the shared core curve math, so
// playback and bounce agree exactly (DESIGN §4.6/§4.7).
inline float fadeGainAt(const core::PlanClip& clip, int64_t src) noexcept
{
    float g = 1.0f;
    if (clip.fadeIn.length > 0 && src < clip.fadeIn.length) {
        const double p = double(src) / double(clip.fadeIn.length);
        g *= float(core::fadeInGain(core::fadeCurveFromString(clip.fadeIn.curve), p));
    }
    if (clip.fadeOut.length > 0) {
        const int64_t fromEnd = clip.length - src;
        if (fromEnd >= 0 && fromEnd < clip.fadeOut.length) {
            const double p = 1.0 - double(fromEnd) / double(clip.fadeOut.length);
            g *= float(core::fadeOutGain(core::fadeCurveFromString(clip.fadeOut.curve), p));
        }
    }
    return g;
}

} // namespace

void PlanMixer::render(const core::RenderPlan& plan, long long playhead,
                       std::size_t frames, float* output) const noexcept
{
    if (!output || frames == 0)
        return;
    std::fill_n(output, frames * 2, 0.0f);

    std::size_t used = 0;
    for (const core::PlanClip& clip : plan.clips) {
        if (used >= kMaxClips)
            break; // bounded scratch: the plan builder caps at kMaxClips
        if (clip.muted || !clip.samples || clip.length <= 0)
            continue;

        std::size_t outOffset = 0;
        std::size_t sourceOffset = 0;
        using UnsignedTimeline = std::make_unsigned_t<long long>;
        if (playhead < clip.timelineStart) {
            const auto untilStart = static_cast<UnsignedTimeline>(clip.timelineStart)
                - static_cast<UnsignedTimeline>(playhead);
            if (untilStart >= frames)
                continue;
            outOffset = static_cast<std::size_t>(untilStart);
        } else {
            const auto elapsed = static_cast<UnsignedTimeline>(playhead)
                - static_cast<UnsignedTimeline>(clip.timelineStart);
            if (elapsed >= clip.length)
                continue;
            sourceOffset = static_cast<std::size_t>(elapsed);
        }

        const std::size_t count = std::min(frames - outOffset, std::size_t(clip.length - sourceOffset));
        if (count == 0)
            continue;

        const float gain = std::isfinite(clip.gain) ? clip.gain : 0.0f;
        const float pan = std::isfinite(clip.pan) ? std::clamp(clip.pan, -1.0f, 1.0f) : 0.0f;
        const float leftPan = pan > 0.0f ? 1.0f - pan : 1.0f;
        const float rightPan = pan < 0.0f ? 1.0f + pan : 1.0f;
        const float* src = clip.samples + (clip.sourceOffset + sourceOffset) * 2;

        for (std::size_t i = 0; i < count; ++i) {
            const float g = gain * fadeGainAt(clip, sourceOffset + i);
            const float left = src[i * 2];
            const float right = src[i * 2 + 1];
            output[(outOffset + i) * 2] += (std::isfinite(left) ? left : 0.0f) * g * leftPan;
            output[(outOffset + i) * 2 + 1] += (std::isfinite(right) ? right : 0.0f) * g * rightPan;
        }
        ++used;
    }
}

bool PlanMixer::silentAt(const core::RenderPlan& plan, long long playhead,
                          std::size_t frames) const noexcept
{
    for (const core::PlanClip& clip : plan.clips) {
        if (clip.muted || !clip.samples || clip.length <= 0)
            continue;
        if (playhead < clip.end() && clip.timelineStart < playhead + static_cast<long long>(frames))
            return false;
    }
    return true;
}

} // namespace ses::engine
