#include "engine/io/AudioDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ses::engine {

namespace {

// Master SRC scratch (ROADMAP §3.3): sized once in init(), never resized on
// RT. The demand-driven render keeps the carry bounded to roughly one device
// block of project frames plus the latency margin.
constexpr std::size_t kSrcInCapFrames = 8192;
constexpr std::size_t kSrcMarginFrames = 64; // soxr latency headroom
constexpr int kSrcMaxIter = 64;              // pathological-input guard

} // namespace

bool AudioDevice::open(int sampleRate, ma_uint32 periodFrames, int deviceRate)
{
    return init(nullptr, 0, sampleRate, periodFrames, deviceRate);
}

bool AudioDevice::openOnBackend(ma_backend backend, int sampleRate,
                                ma_uint32 periodFrames, int deviceRate)
{
    return init(&backend, 1, sampleRate, periodFrames, deviceRate);
}

bool AudioDevice::init(const ma_backend* backends, ma_uint32 backendCount,
                       int sampleRate, ma_uint32 periodFrames, int deviceRate)
{
    close();
    if (sampleRate <= 0)
        return false;
    projectRate_ = sampleRate;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.sampleRate                = static_cast<ma_uint32>(deviceRate > 0
                                                           ? deviceRate
                                                           : sampleRate);
    cfg.periodSizeInFrames        = periodFrames;                       // 256
    cfg.periodSizeInMilliseconds  = 0;   // MUST be zero when frames are given (§2.10)
    cfg.performanceProfile        = ma_performance_profile_low_latency;
    cfg.playback.format           = ma_format_f32;
    cfg.playback.channels         = 2;
    cfg.playback.pDeviceID        = nullptr; // default device; enumeration in §2.5
    cfg.dataCallback              = &AudioDevice::dataCallback;
    cfg.notificationCallback      = &AudioDevice::notificationCallback;
    cfg.pUserData                 = this;
    cfg.noPreSilencedOutputBuffer = MA_TRUE; // BlockMixer::render already zeroes the buffer
    cfg.noClip                    = MA_TRUE; // the master limiter is ours (P4); miniaudio must not clip
    // stopCallback is DEPRECATED — the notification callback is used instead.

    const ma_result result = (backends != nullptr)
        ? ma_device_init_ex(backends, backendCount, nullptr, &cfg, &device_)
        : ma_device_init(nullptr, &cfg, &device_);
    if (result != MA_SUCCESS)
        return false;
    initialized_ = true;

    // The device may run at a rate other than the project rate (DESIGN §4.1,
    // §3.3): the master output is then resampled with soxr — instance and
    // scratch built here, off-RT; the callback only calls process().
    if (actualSampleRate() != projectRate_) {
        if (!src_.init(projectRate_, actualSampleRate(), 2,
                       Resampler::Quality::Realtime)) {
            close();
            return false;
        }
        srcIn_.assign(kSrcInCapFrames * 2, 0.0f);
        srcCarry_ = 0;
    }
    return true;
}

void AudioDevice::close()
{
    if (initialized_) {
        closing_.store(true, std::memory_order_release);
        ma_device_uninit(&device_);
        initialized_ = false;
        closing_.store(false, std::memory_order_release);
    }
    src_.close();
    srcIn_.clear();
    srcIn_.shrink_to_fit();
    srcCarry_ = 0;
}

bool AudioDevice::start()
{
    return initialized_ && ma_device_start(&device_) == MA_SUCCESS;
}

void AudioDevice::stop()
{
    if (initialized_)
        ma_device_stop(&device_);
}

int AudioDevice::actualSampleRate() const
{
    return static_cast<int>(device_.sampleRate);
}

void AudioDevice::dataCallback(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames)
{
    auto* self = static_cast<AudioDevice*>(dev->pUserData);
    self->applyCommands(); // ring drain; seek/playhead/loop happen here, at block boundaries

    const int64_t head = self->playhead_.load(std::memory_order_relaxed);
    float* dst = static_cast<float*>(out);
    if (self->playing_.load(std::memory_order_relaxed)) {
        if (self->src_.active()) {
            // mixer.render (project rate) → soxr → device out (§3.3); the
            // playhead/loop wrap happen inside, in project frames.
            self->renderThroughSrc(dst, frames);
        } else {
            self->mixer_.render(head, frames, dst); // noexcept, allocation-free (existing BlockMixer)
            int64_t next = head + frames;
            if (self->loop_ && self->loopEnd_ > self->loopStart_ && next >= self->loopEnd_)
                next = self->loopStart_; // loop wrap; keep cursor reset simple in P1
            self->playhead_.store(next, std::memory_order_relaxed);
        }
    } else {
        std::fill_n(dst, static_cast<size_t>(frames) * 2, 0.0f);
    }

    // Master peak (simple scan; per-track peak/RMS arrives in P4):
    float peak = 0.0f;
    for (ma_uint32 i = 0; i < frames * 2; ++i) {
        const float a = std::abs(dst[i]);
        if (a > peak)
            peak = a;
    }
    Telemetry t{head, peak, self->xruns_, self->underruns_};
    self->telemetry_.push(t); // dropped when full — telemetry loss is acceptable, blocking is not
}

// RT: mixer.render at the project rate → soxr_process → device out (ROADMAP
// §3.3). All scratch was allocated in init(); nothing here allocates, locks,
// or blocks. Input is rendered demand-driven (enough project frames for the
// remaining output plus a latency margin), so the carry between callbacks
// stays bounded and the playhead advances only by frames soxr actually ate.
void AudioDevice::renderThroughSrc(float* dst, std::size_t frames)
{
    const int64_t head = playhead_.load(std::memory_order_relaxed);
    const double ratio = double(projectRate_) / double(actualSampleRate());
    std::size_t bufFrames = srcCarry_; // scratch holds [head, head+srcCarry_)
    std::size_t consumed = 0;
    std::size_t produced = 0;
    int guard = 0;
    while (produced < frames && guard++ < kSrcMaxIter) {
        if (bufFrames == 0) {
            const std::size_t want = std::min(
                kSrcInCapFrames,
                std::size_t(double(frames - produced) * ratio) + kSrcMarginFrames);
            mixer_.render(head + static_cast<int64_t>(consumed), want, srcIn_.data());
            bufFrames = want;
        }
        std::size_t idone = 0;
        const std::size_t made = src_.process(srcIn_.data(), bufFrames,
                                              dst + produced * 2,
                                              frames - produced, idone);
        produced += made;
        consumed += idone;
        bufFrames -= idone;
        if (idone > 0 && bufFrames > 0) {
            std::memmove(srcIn_.data(), srcIn_.data() + idone * 2,
                         bufFrames * 2 * sizeof(float));
        }
        if (idone == 0 && made == 0)
            break; // no progress — fill the rest with silence below
    }
    srcCarry_ = bufFrames;

    int64_t next = head + static_cast<int64_t>(consumed);
    if (loop_ && loopEnd_ > loopStart_ && next >= loopEnd_) {
        next = loopStart_; // loop wrap at the block boundary (P1 simplicity)
        // The carried tail was rendered past the loop end — drop it.
        src_.reset();
        srcCarry_ = 0;
    }
    playhead_.store(next, std::memory_order_relaxed);

    if (produced < frames) {
        // Honest underrun: the chain could not fill the device block.
        std::fill_n(dst + produced * 2, (frames - produced) * 2, 0.0f);
        ++underruns_;
    }
}

void AudioDevice::notificationCallback(const ma_device_notification* n)
{
    auto* self = static_cast<AudioDevice*>(n->pDevice->pUserData);
    if (self->closing_.load(std::memory_order_acquire))
        return; // teardown noise, not a loss the GUI should recover from
    DeviceEvent e{};
    switch (n->type) {
    case ma_device_notification_type_started:            e.kind = DeviceEvent::Started; break;
    case ma_device_notification_type_stopped:            e.kind = DeviceEvent::Stopped; break;
    case ma_device_notification_type_rerouted:           e.kind = DeviceEvent::Rerouted; break;
    case ma_device_notification_type_interruption_began: e.kind = DeviceEvent::InterruptionBegan; break;
    case ma_device_notification_type_interruption_ended: e.kind = DeviceEvent::InterruptionEnded; break;
    default: return; // unlocked, etc.
    }
    self->events_.push(e);
}

void AudioDevice::applyCommands()
{
    RtCommand cmd;
    while (commands_.pop(cmd)) {
        switch (cmd.kind) {
        case RtCommand::Play:
            playing_.store(true, std::memory_order_relaxed);
            break;
        case RtCommand::Stop:
            playing_.store(false, std::memory_order_relaxed);
            if (src_.active()) {
                src_.reset(); // drop the stale tail while parked (§3.3)
                srcCarry_ = 0;
            }
            break;
        case RtCommand::Seek:
            // Streaming-cursor-reset flag arrives with the decode pool (P2);
            // RT is never blocked (DESIGN §4.4).
            playhead_.store(cmd.a, std::memory_order_relaxed);
            if (src_.active()) {
                src_.reset(); // §3.3: clear on seek, or the old tail stays audible
                srcCarry_ = 0;
            }
            break;
        case RtCommand::SetLoop:
            loopStart_ = cmd.a;
            loopEnd_ = cmd.b;
            loop_ = cmd.x != 0.0f && loopEnd_ > loopStart_;
            break;
        case RtCommand::SetAbFader:
            abFader_ = cmd.x; // feeds the smoother target in P4
            break;
        }
    }
}

} // namespace ses::engine
