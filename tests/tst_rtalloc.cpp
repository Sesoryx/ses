// RT allocation gate (DESIGN §13, P1 task 8).
//
// Runs real playback through AudioDevice on the null backend while an
// LD_PRELOAD malloc interposer counts every allocation made on the audio
// thread during the callback (including allocations inside dynamically linked
// soxr). The gate is zero allocations. Both the direct path (48 kHz) and the
// master-SRC path (44.1 kHz device over a 48 kHz timeline) are exercised.
//
// Exit codes: 0 pass, 1 fail, 77 skip (interposer not preloaded; ctest sets
// SES_RT_ALLOC_REQUIRED=1 so a missing interposer fails the gate instead).

#include "engine/io/AudioDevice.h"

#include <dlfcn.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>


using namespace ses::engine;

namespace {

using CountFn = uint64_t (*)();
using ResetFn = void (*)();
using EnterFn = void (*)();
using LeaveFn = void (*)();

struct Hooks {
    CountFn count = nullptr;
    ResetFn reset = nullptr;
    EnterFn enter = nullptr;
    LeaveFn leave = nullptr;

    bool complete() const { return count && reset && enter && leave; }
};

Hooks loadHooks()
{
    Hooks hooks;
    hooks.count = reinterpret_cast<CountFn>(dlsym(RTLD_DEFAULT, "ses_rt_alloc_count"));
    hooks.reset = reinterpret_cast<ResetFn>(dlsym(RTLD_DEFAULT, "ses_rt_alloc_reset"));
    hooks.enter = reinterpret_cast<EnterFn>(dlsym(RTLD_DEFAULT, "ses_rt_alloc_enter"));
    hooks.leave = reinterpret_cast<LeaveFn>(dlsym(RTLD_DEFAULT, "ses_rt_alloc_leave"));
    return hooks;
}

bool waitForPlayhead(AudioDevice& dev, int64_t target)
{
    Telemetry t{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        while (dev.nextTelemetry(t)) {
            if (t.playhead >= target)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool runPhase(const Hooks& hooks, int projectRate, int deviceRate, const char* label)
{
    // 1 s of 0.25-amplitude tone, interleaved stereo, at the 48 kHz project rate.
    static const std::vector<float> tone = [] {
        std::vector<float> pcm(48000 * 2);
        for (std::size_t i = 0; i < pcm.size() / 2; ++i) {
            const float value =
                0.25f * float(std::sin(2.0 * 3.14159265358979323846 * 440.0 * double(i) / 48000.0));
            pcm[i * 2] = value;
            pcm[i * 2 + 1] = value;
        }
        return pcm;
    }();

    AudioDevice dev;
    if (!dev.openOnBackend(ma_backend_null, projectRate, 256, deviceRate)) {
        std::fprintf(stderr, "%s: could not open the null backend\n", label);
        return false;
    }
    if (!dev.mixer().setClip(0, { tone.data(), tone.size() / 2, 0, 1.0f, 0.0f, false })) {
        std::fprintf(stderr, "%s: could not configure the mixer\n", label);
        return false;
    }

    // Reset before the pump starts: the very first callback (and soxr's first
    // process call) must be covered, not only the steady state.
    hooks.reset();
    if (!dev.start()) {
        std::fprintf(stderr, "%s: could not start the device\n", label);
        return false;
    }
    if (!dev.sendCommand({ RtCommand::Play })) {
        std::fprintf(stderr, "%s: command ring rejected Play\n", label);
        return false;
    }

    const bool early = waitForPlayhead(dev, 6000); // first ~0.125 s
    const uint64_t earlyAllocations = hooks.count();
    const bool advanced = waitForPlayhead(dev, 24000); // 0.5 s of project time
    const uint64_t beforeStop = hooks.count();
    dev.sendCommand({ RtCommand::Stop });
    dev.stop();
    const uint64_t allocations = hooks.count();
    dev.close();

    std::printf("%s: %llu allocation(s) in the RT callback "
                "(%llu first 0.125 s, %llu on stop/close)\n",
                label, static_cast<unsigned long long>(allocations),
                static_cast<unsigned long long>(earlyAllocations),
                static_cast<unsigned long long>(allocations - beforeStop));
    if (!early)
        return false;
    if (!advanced) {
        std::fprintf(stderr, "%s: playhead did not advance (gate would be vacuous)\n", label);
        return false;
    }
    return allocations == 0;
}

} // namespace

int main()
{
    const Hooks hooks = loadHooks();
    if (!hooks.complete()) {
        const bool required = std::getenv("SES_RT_ALLOC_REQUIRED") != nullptr;
        std::fprintf(stderr, "RT allocation interposer not preloaded%s\n",
                     required ? " — failing the gate" : " — skipping");
        return required ? 1 : 77;
    }

    // Prove the interposer is live before trusting a zero result.
    hooks.reset();
    hooks.enter();
    // Runtime-sized and escaped to inline asm so -O3 cannot elide the pair.
    const std::size_t probeSize = 16 + static_cast<std::size_t>(hooks.count());
    void* probe = std::malloc(probeSize);
    asm volatile("" : : "r"(probe) : "memory");
    std::free(probe);
    hooks.leave();
    if (hooks.count() == 0) {
        std::fprintf(stderr, "interposer did not observe a deliberate allocation\n");
        return 1;
    }

    const bool direct = runPhase(hooks, 48000, 48000, "48k master");
    const bool resampled = runPhase(hooks, 48000, 44100, "44.1k master SRC");
    if (!direct || !resampled) {
        std::fprintf(stderr, "RT allocation gate FAILED\n");
        return 1;
    }
    std::printf("RT allocation gate passed: zero allocations in both phases\n");
    return 0;
}
