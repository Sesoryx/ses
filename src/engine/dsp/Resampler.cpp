// soxr wrapper implementation (ROADMAP §3.2, P1 task 6). The only TU that
// includes <soxr.h>. Verified API notes (ROADMAP §3.2/Appendix A): ilen/olen
// are frames per channel; a null input with ilen 0 flushes the queued tail;
// soxr_create allocates (off-RT), soxr_process/soxr_clear do not (RT-safe).

#include "engine/dsp/Resampler.h"

#include <soxr.h>

#include <cstring>

namespace ses::engine {

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
}

void Resampler::reset()
{
    if (active())
        soxr_clear(static_cast<soxr_t>(soxr_)); // RT-callable (§3.3)
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
    return odone;
}

} // namespace ses::engine