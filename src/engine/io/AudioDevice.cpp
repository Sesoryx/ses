#include "engine/io/AudioDevice.h"

#include <algorithm>
#include <cmath>

namespace ses::engine {

bool AudioDevice::open(int sampleRate, ma_uint32 periodFrames)
{
    return init(nullptr, 0, sampleRate, periodFrames);
}

bool AudioDevice::openOnBackend(ma_backend backend, int sampleRate, ma_uint32 periodFrames)
{
    return init(&backend, 1, sampleRate, periodFrames);
}

bool AudioDevice::init(const ma_backend* backends, ma_uint32 backendCount,
                       int sampleRate, ma_uint32 periodFrames)
{
    close();

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.sampleRate                = static_cast<ma_uint32>(sampleRate); // target 48000
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
    // device_.sampleRate may differ from the request (e.g. the device opened at
    // 44100). If so, the master output is resampled with soxr (§3.3).
    return true;
}

void AudioDevice::close()
{
    if (initialized_) {
        ma_device_uninit(&device_);
        initialized_ = false;
    }
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
        self->mixer_.render(head, frames, dst); // noexcept, allocation-free (existing BlockMixer)
        int64_t next = head + frames;
        if (self->loop_ && self->loopEnd_ > self->loopStart_ && next >= self->loopEnd_)
            next = self->loopStart_; // loop wrap; keep cursor reset simple in P1
        self->playhead_.store(next, std::memory_order_relaxed);
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

void AudioDevice::notificationCallback(const ma_device_notification* n)
{
    auto* self = static_cast<AudioDevice*>(n->pDevice->pUserData);
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
            break;
        case RtCommand::Seek:
            // Streaming-cursor-reset flag arrives with the decode pool (P2);
            // RT is never blocked (DESIGN §4.4).
            playhead_.store(cmd.a, std::memory_order_relaxed);
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
