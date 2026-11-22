#pragma once
// ses::engine — soxr sample rate conversion (ROADMAP §3, P1 task 6). Qt-free;
// soxr types stay out of the header (the pimpl habit of engine/Engine.h), so
// only Resampler.cpp includes <soxr.h>.
//
// Two consumers (ROADMAP §3.1):
//   * master SRC on the RT callback (Quality::Realtime → SOXR_HQ): init and
//     buffer setup run off-RT; process()/reset() are allocation-free and only
//     ever run on the audio thread (§3.3).
//   * import / bounce / model I/O off-RT (Quality::Offline → SOXR_VHQ):
//     import quality matters, not speed (§2.8).
//
// Interleaved f32 throughout; frame counts are frames per channel (soxr
// semantics, §3.2).

#include <cstddef>

namespace ses::engine {

class Resampler {
public:
    enum class Quality {
        Realtime, // SOXR_HQ — cheap, for the RT master chain
        Offline,  // SOXR_VHQ — high quality, for import/bounce/model I/O
    };

    Resampler() = default;
    ~Resampler();
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    // Allocates — run off the RT thread. inRate == outRate is legal: the
    // resampler then passes samples through unchanged (no soxr instance).
    bool init(int inRate, int outRate, int channels = 2,
              Quality quality = Quality::Offline);
    void close();

    // Fresh signal after a seek: the old tail must not stay audible (§3.3).
    // RT-callable, allocation-free.
    void reset();

    // Streaming conversion. inConsumed reports the input frames eaten; the
    // return value is the frames written to out. Null in with inFrames == 0
    // flushes the queued tail. Allocation-free after init (RT-safe).
    std::size_t process(const float* in, std::size_t inFrames, float* out,
                        std::size_t outCapacity, std::size_t& inConsumed);

    bool valid() const { return inRate_ > 0; }
    bool active() const { return inRate_ > 0 && inRate_ != outRate_; }
    int inRate() const { return inRate_; }
    int outRate() const { return outRate_; }
    int channels() const { return channels_; }

private:
    void* soxr_ = nullptr; // soxr_t
    int inRate_ = 0;
    int outRate_ = 0;
    int channels_ = 0;
};

} // namespace ses::engine