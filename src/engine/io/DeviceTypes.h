#pragma once
// ses::engine — POD types shared across the RT boundary and the GUI-side
// facade. Every type here crosses a lock-free SpscRing between threads:
// trivially copyable, no heap, no Qt (DESIGN.md §2/§4). Split out of
// AudioDevice.h so engine/Engine.h stays free of the 95k-line miniaudio header.

#include <cstdint>

namespace ses::engine {

// POD telemetry: audio thread → GUI (drained by a 30 Hz QTimer). A full ring
// drops pushes; never blocks.
struct Telemetry {
    int64_t playhead;
    float masterPeak;
    uint32_t xruns;
    uint32_t underruns;
};

// POD device event: miniaudio notification thread → GUI.
struct DeviceEvent {
    enum Kind : uint8_t { Started, Stopped, Rerouted, InterruptionBegan, InterruptionEnded } kind;
};

// GUI → RT commands (play/stop/seek/loop/abFader), applied at block boundaries.
struct RtCommand {
    enum Kind : uint8_t { Play, Stop, Seek, SetLoop, SetAbFader } kind;
    int64_t a = 0, b = 0;
    float x = 0.0f;
};

} // namespace ses::engine