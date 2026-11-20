#pragma once
// ses::engine — audio engine facade (DESIGN.md §3/§4). NO Qt dependency.
// UI-first slice: only the interface + a NullEngine exist. The real miniaudio
// device, RenderPlan slot, SPSC rings and block mixer land in P1. The UI binds
// to this interface and runs an explicitly-labelled preview transport meanwhile.

#include <cstdint>

namespace ses::engine {

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
};

} // namespace ses::engine