#pragma once
// ses::engine — real device transport (ROADMAP §2.3, P1 tasks 1–2).
// Qt-free. The audio callback obeys the RT rules (DESIGN §2/§4): no
// allocation, no locks, no Qt, no logging. GUI ↔ RT traffic crosses only the
// SpscRing members below; mutable RT state lives here, never in shared plans.

#include "engine/BlockMixer.h"
#include "engine/SpscRing.h"
#include "engine/dsp/Resampler.h"
#include "engine/io/DeviceTypes.h"

#include <miniaudio.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace ses::engine {

// Telemetry / DeviceEvent / RtCommand: see engine/io/DeviceTypes.h.
class AudioDevice {
public:
    ~AudioDevice() { close(); }

    // Default output device at the project rate. periodSizeInMilliseconds
    // stays 0 (ROADMAP §2.10). deviceRate (0 = request sampleRate) opens the
    // device at a different rate than the project timeline — the master
    // output is then resampled with soxr (§3.3).
    bool open(int sampleRate = 48000, ma_uint32 periodFrames = 256,
              int deviceRate = 0);

    // Explicit backend — e.g. ma_backend_null for headless tests (§2.5).
    bool openOnBackend(ma_backend backend, int sampleRate = 48000,
                       ma_uint32 periodFrames = 256, int deviceRate = 0);

    void close();
    bool start();
    void stop();
    int actualSampleRate() const; // device_.sampleRate after init (may differ from the request)
    bool playing() const { return playing_.load(std::memory_order_relaxed); }
    bool started() const { return initialized_ && ma_device_is_started(&device_); }
    bool isOpen() const { return initialized_; }
    int64_t playhead() const { return playhead_.load(std::memory_order_relaxed); }

    // True while the master output runs through the soxr chain because the
    // device rate differs from the project rate (ROADMAP §3.3).
    bool masterSrcActive() const { return src_.active(); }

    // GUI-thread drains:
    bool nextTelemetry(Telemetry& out) { return telemetry_.pop(out); }
    bool nextDeviceEvent(DeviceEvent& out) { return events_.pop(out); }
    bool sendCommand(const RtCommand& cmd) { return commands_.push(cmd); }

    // Off-RT configuration (GUI thread, while the device is stopped or via the
    // plan-publish protocol).
    BlockMixer& mixer() { return mixer_; }

private:
    bool init(const ma_backend* backends, ma_uint32 backendCount,
              int sampleRate, ma_uint32 periodFrames, int deviceRate);

    static void dataCallback(ma_device* dev, void* out, const void* in, ma_uint32 frames);
    static void notificationCallback(const ma_device_notification* n);
    void applyCommands(); // RT: drain the command ring at block boundaries
    void renderThroughSrc(float* dst, std::size_t frames); // RT: mixer → soxr → out

    ma_device device_{};
    bool initialized_ = false;
    BlockMixer mixer_;
    SpscRing<Telemetry, 64> telemetry_;
    SpscRing<DeviceEvent, 16> events_;
    SpscRing<RtCommand, 64> commands_;
    std::atomic<int64_t> playhead_{0};
    std::atomic<bool> playing_{false};
    // close() is an API-initiated teardown, not a device loss: notifications
    // from the dying device must not reach the recovery path (ROADMAP §2.6).
    std::atomic<bool> closing_{false};
    int64_t loopStart_ = 0, loopEnd_ = 0;
    bool loop_ = false;
    float abFader_ = 0.5f; // wired to the bus crossfade in P4
    uint32_t xruns_ = 0, underruns_ = 0;

    // Master SRC (ROADMAP §3.3): project rate → device rate. Built in init()
    // (off-RT); the callback only calls process()/reset(). The scratch and
    // carry are RT-owned after open and never resized there.
    Resampler src_;
    std::vector<float> srcIn_;
    std::vector<float> srcOut_;    // soxr output carry (one large scratch block)
    std::size_t srcCarry_ = 0;     // input frames not yet consumed by soxr
    std::size_t srcOutFrames_ = 0; // output frames produced but not yet played
    int64_t srcRenderPos_ = 0;     // input cursor (may lead the audible head)
    int64_t srcPlayRem_ = 0;       // fractional playhead accumulator
    int projectRate_ = 48000;
};

} // namespace ses::engine
