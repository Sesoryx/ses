#pragma once
// ses::engine — audio engine facade (DESIGN.md §3/§4). NO Qt dependency.
//
// The Engine interface is what the UI binds to. NullEngine is the honest
// placeholder (no audio, never pretends); DeviceEngine (P1 task 3) is the
// real miniaudio-backed facade: transport commands cross a lock-free ring and
// are applied on the audio thread at block boundaries (ROADMAP §2.3).

#include "engine/io/DeviceTypes.h"

#include "core/render/RenderPlan.h"

#include <cstdint>
#include <memory>

namespace ses::engine {

class AudioDevice; // pimpl; defined in engine/io/AudioDevice.h

struct TransportState {
    bool playing = false;
    bool loop = false;
    int64_t playhead = 0; // samples at project rate
    int64_t loopStart = 0;
    int64_t loopEnd = 0;
};

class Engine {
public:
    virtual ~Engine() = default;

    // False until a real device-backed engine is attached (P1).
    virtual bool available() const = 0;

    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void seek(int64_t sample) = 0;
    virtual void setLoop(bool on, int64_t start, int64_t end) = 0;
    virtual void setAbFader(double x) = 0; // RT parameter command, smoothed
    virtual TransportState state() const = 0;

    // GUI-thread drains for the 30 Hz telemetry/meter path (P1 task 4).
    // False when nothing is queued (or no device is attached).
    virtual bool nextTelemetry(Telemetry& out) = 0;
    virtual bool nextDeviceEvent(DeviceEvent& out) = 0;

    // P2: publish a freshly built render plan and reclaim retired ones.
    // The engine owns the plan after publish(); the slot frees it after the
    // grace window. Returns false when no device is attached (the plan is
    // still owned by the slot and will be freed by the destructor).
    virtual void publishPlan(core::RenderPlan* plan) = 0;
    virtual void reclaimPlans() = 0;
};

// Inert placeholder: reports unavailable, remembers nothing. Never pretends
// to produce audio.
class NullEngine final : public Engine {
public:
    bool available() const override { return false; }
    void play() override {}
    void stop() override {}
    void seek(int64_t) override {}
    void setLoop(bool, int64_t, int64_t) override {}
    void setAbFader(double) override {}
    TransportState state() const override { return {}; }
    bool nextTelemetry(Telemetry&) override { return false; }
    bool nextDeviceEvent(DeviceEvent&) override { return false; }
    // NullEngine has no slot: the plan would leak, so take ownership and free.
    void publishPlan(core::RenderPlan* plan) override { delete plan; }
    void reclaimPlans() override {}
};

// Real device-backed engine (ROADMAP P1 task 3). Owns the AudioDevice and
// turns the Engine transport API into RT commands applied at block boundaries.
// The playhead is the RT playhead in project-rate samples; loop wrap happens
// in the audio callback. Upstream of the device the engine is silent until
// clips are fed to mixer() — that wiring arrives with the render plan (P2).
class DeviceEngine final : public Engine {
public:
    DeviceEngine();
    ~DeviceEngine() override; // out-of-line: AudioDevice is a pimpl
    DeviceEngine(const DeviceEngine&) = delete;
    DeviceEngine& operator=(const DeviceEngine&) = delete;

    // Default output device at the project rate. On failure available() stays
    // false and the caller honestly falls back to preview mode.
    bool open(int sampleRate = 48000, unsigned periodFrames = 256);

    // Headless tests (ROADMAP §2.5): the callback runs silently at the
    // requested rate/period, no audio hardware involved.
    bool openOnNullBackend(int sampleRate = 48000, unsigned periodFrames = 256);

    // Starts the device pump (it outputs silence until play()). play() also
    // starts it lazily, so an idle app holds no running device.
    bool start();
    void close();

    bool available() const override; // true while a device is open

    void play() override;
    void stop() override;
    void seek(int64_t sample) override;
    void setLoop(bool on, int64_t start, int64_t end) override;
    void setAbFader(double x) override; // RT parameter command, smoothed (P4)
    TransportState state() const override;

    // GUI-thread drains for events and meters (30 Hz QTimer in ui::Transport).
    bool nextTelemetry(Telemetry& out) override;
    bool nextDeviceEvent(DeviceEvent& out) override;

    // P2: plan publish + grace reclaim (GUI thread).
    void publishPlan(core::RenderPlan* plan) override;
    void reclaimPlans() override;

    int actualSampleRate() const; // 0 when no device is open

private:
    std::unique_ptr<AudioDevice> device_;
    bool loop_ = false;
    int64_t loopStart_ = 0;
    int64_t loopEnd_ = 0;
};

} // namespace ses::engine