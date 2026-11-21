#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace ses::engine {

// Fixed storage stereo mixer for pre-decoded interleaved float clips.
// Configuration is done off the audio thread; render() performs no allocation.
class BlockMixer {
public:
    static constexpr std::size_t MaxClips = 64;

    struct ClipView {
        const float* samples = nullptr; // interleaved stereo
        std::size_t frames = 0;
        long long timelineStart = 0;
        float gain = 1.0f;
        float pan = 0.0f; // -1 = left, +1 = right
        bool muted = false;
    };

    bool setClip(std::size_t index, ClipView clip) noexcept
    {
        if (index >= MaxClips || (clip.frames != 0 && clip.samples == nullptr)
            || clip.frames > std::numeric_limits<std::size_t>::max() / 2)
            return false;
        clip.gain = std::isfinite(clip.gain) ? std::clamp(clip.gain, 0.0f, 4.0f) : 1.0f;
        clip.pan = std::isfinite(clip.pan) ? std::clamp(clip.pan, -1.0f, 1.0f) : 0.0f;
        clips_[index] = clip;
        if (index >= clipCount_)
            clipCount_ = index + 1;
        return true;
    }

    void clear() noexcept
    {
        std::fill_n(clips_, MaxClips, ClipView{});
        clipCount_ = 0;
    }

    // Mixes [playhead, playhead+frames) into a caller-owned interleaved stereo
    // buffer. Missing/finished clips contribute silence; the output is replaced.
    void render(long long playhead, std::size_t frames, float* output) const noexcept
    {
        if (!output || frames > std::numeric_limits<std::size_t>::max() / 2)
            return;
        std::fill_n(output, frames * 2, 0.0f);
        for (std::size_t c = 0; c < clipCount_; ++c) {
            const ClipView& clip = clips_[c];
            if (clip.muted || !clip.samples || clip.frames == 0)
                continue;
            std::size_t outOffset = 0;
            std::size_t sourceOffset = 0;
            using UnsignedTimeline = std::make_unsigned_t<long long>;
            if (playhead < clip.timelineStart) {
                // Unsigned subtraction represents the full signed distance,
                // including ranges that would overflow signed arithmetic.
                const auto untilStart = static_cast<UnsignedTimeline>(clip.timelineStart)
                    - static_cast<UnsignedTimeline>(playhead);
                if (untilStart >= frames)
                    continue;
                outOffset = static_cast<std::size_t>(untilStart);
            } else {
                const auto elapsed = static_cast<UnsignedTimeline>(playhead)
                    - static_cast<UnsignedTimeline>(clip.timelineStart);
                if (elapsed >= clip.frames)
                    continue;
                sourceOffset = static_cast<std::size_t>(elapsed);
            }
            const std::size_t count = std::min(frames - outOffset, clip.frames - sourceOffset);
            const float leftPan = clip.pan > 0.0f ? 1.0f - clip.pan : 1.0f;
            const float rightPan = clip.pan < 0.0f ? 1.0f + clip.pan : 1.0f;
            for (std::size_t i = 0; i < count; ++i) {
                const float left = clip.samples[(sourceOffset + i) * 2];
                const float right = clip.samples[(sourceOffset + i) * 2 + 1];
                output[(outOffset + i) * 2] += (std::isfinite(left) ? left : 0.0f) * clip.gain * leftPan;
                output[(outOffset + i) * 2 + 1] += (std::isfinite(right) ? right : 0.0f) * clip.gain * rightPan;
            }
        }
    }

private:
    ClipView clips_[MaxClips]{};
    std::size_t clipCount_ = 0;
};

} // namespace ses::engine
