// soxr wrapper implementation (ROADMAP §3.2, P1 task 6). The only TU that
// includes <soxr.h>. Verified API notes (ROADMAP §3.2/Appendix A): ilen/olen
// are frames per channel; a null input with ilen 0 flushes the queued tail;
// soxr_create allocates (off-RT). Empirical task-8 finding: this soxr build
// allocates working buffers lazily in the first process call and soxr_clear()
// discards them, so the next process call allocates again. Realtime instances
// are therefore warmed up off-RT and never cleared on the audio thread; a
// bounded mute window replaces clear (see reset()).

#include "engine/dsp/Resampler.h"

#include <soxr.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace ses::engine {

namespace {

// soxr allocates some working buffers lazily on the first process call. The RT
// allocation guard (DESIGN §13) requires zero allocations in the callback, so
// Realtime instances are exercised here (off-RT) with a block at least as large
// as the master chain's scratch. The buffers then stay allocated: reset() must
// not call soxr_clear(), which would invalidate them.
constexpr std::size_t kRtWarmupInFrames = 8192;
constexpr std::size_t kRtWarmupOutFrames = kRtWarmupInFrames * 4 + 1024;
constexpr std::size_t kResetMuteMarginFrames = 64;

} // namespace

Resampler::~Resampler()
{
    close();
}

bool Resampler::init(int inRate, int outRate, int channels, Quality quality)
{
    close();
    if (inRate <= 0 || outRate <= 0 || channels <= 0)
        return false;
    inRate_ = inRate;
    outRate_ = outRate;
    channels_ = channels;
    realtime_ = quality == Quality::Realtime;
    if (inRate == outRate)
        return true; // passthrough — no soxr instance, zero latency

    soxr_io_spec_t io = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I); // §3.2
    soxr_quality_spec_t q =
        soxr_quality_spec(quality == Quality::Realtime ? SOXR_HQ : SOXR_VHQ, 0);
    soxr_runtime_spec_t rt = soxr_runtime_spec(1); // one thread — no OpenMP (§3.2)
    soxr_error_t err = nullptr;
    soxr_t resampler = soxr_create(double(inRate), double(outRate),
                                   static_cast<unsigned>(channels), &err, &io,
                                   &q, &rt);
    if (!resampler)
        return false;
    soxr_ = resampler;

    if (realtime_) {
        std::vector<float> warmIn(kRtWarmupInFrames * static_cast<std::size_t>(channels), 0.0f);
        std::vector<float> warmOut(kRtWarmupOutFrames * static_cast<std::size_t>(channels), 0.0f);
        std::size_t idone = 0;
        std::size_t odone = 0;
        soxr_process(resampler, warmIn.data(), kRtWarmupInFrames, &idone,
                     warmOut.data(), kRtWarmupOutFrames, &odone);
        // Warm-up is silence, so its history is inaudible; no clear (it would
        // discard the buffers that the guard regression found). The mute
        // window below covers the stale tail from later seeks.
        muteFrames_ = 2 * soxr_delay(resampler) + kResetMuteMarginFrames;
    }
    return true;
}

void Resampler::close()
{
    if (soxr_) {
        soxr_delete(static_cast<soxr_t>(soxr_));
        soxr_ = nullptr;
    }
    inRate_ = 0;
    outRate_ = 0;
    channels_ = 0;
    realtime_ = false;
    muteFrames_ = 0;
    muteRemaining_ = 0;
}

void Resampler::reset()
{
    if (!active())
        return;
    if (realtime_) {
        // Cover the stale tail without touching soxr's allocations (§3.3).
        muteRemaining_ = muteFrames_;
    } else {
        soxr_clear(static_cast<soxr_t>(soxr_));
    }
}

std::size_t Resampler::process(const float* in, std::size_t inFrames, float* out,
                               std::size_t outCapacity, std::size_t& inConsumed)
{
    inConsumed = 0;
    if (!valid() || !out || outCapacity == 0 || (!in && inFrames > 0))
        return 0;
    if (!active()) {
        if (!in || inFrames == 0)
            return 0; // nothing to flush in passthrough mode
        const std::size_t n = inFrames < outCapacity ? inFrames : outCapacity;
        std::memcpy(out, in, n * static_cast<std::size_t>(channels_) * sizeof(float));
        inConsumed = n;
        return n;
    }
    std::size_t idone = 0;
    std::size_t odone = 0;
    soxr_process(static_cast<soxr_t>(soxr_), in, inFrames, &idone, out,
                 outCapacity, &odone);
    inConsumed = idone;
    if (muteRemaining_ > 0 && odone > 0) {
        const std::size_t muted = std::min(odone, muteRemaining_);
        std::fill_n(out, muted * static_cast<std::size_t>(channels_), 0.0f);
        muteRemaining_ -= muted;
    }
    return odone;
}

} // namespace ses::engine
