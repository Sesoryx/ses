# ses — Implementation Design

**Status:** Architecture baseline (v1) · **Date:** 2026-11-20 · **Scope:** single-process, local-first C++/Qt audio workstation with in-process ggml inference via audio.cpp.

This document is the implementation contract for ses. It replaces the previous ONNX Runtime inference stack with **audio.cpp** (ggml) linked **in-process** through its C ABI. There is no Premiere SDK, no UXP/CEP, no WebSocket daemon, no separate backend process, no Python at runtime. All audio, models, inference, and state stay on the local machine.

---

## 0. Verified references (what this design is grounded in)

Checked against upstream `main` on 2026-11-20, not from memory:

| Source | What was verified |
|---|---|
| `0xShug0/audio.cpp` README (v0.9.5) | 149 model families; backends CPU/CUDA/HIP/Vulkan/Metal/WebGPU/SYCL; composite builds (`AUDIOCPP_MODEL_SET=custom`, `AUDIOCPP_MODELS=…`); `AUDIOCPP_DEPLOYMENT_BUILD`; native model manager; GGUF packages on HF org `audio-cpp`. |
| `include/audiocpp.h` (ABI **0.4.0**) | Full C ABI surface: registry/model/session/request/result/event handles, status codes, per-thread `audiocpp_last_error()`, borrowed-output contract, thread-safety contract, pull-based streaming, style setters, artifacts, word/segment timestamps. **No cancellation entry point exists.** |
| `docs/c_api.md` | Build: `-DAUDIOCPP_BUILD_C_API=ON`, target `audiocpp` → `libaudiocpp.so` (SOVERSION 0); release archives `audio-<tag>-lib-<platform>-<backend>`; **backend plugins are discovered beside the C API library**; library never calls `omp_set_num_threads()`; `audiocpp_log_set()` (ABI ≥ 0.3); Silero VAD path test needs no download. |
| `model_specs/*.json` | Exact package IDs used below (`qwen3_asr_0_6b_q8_0`, `sensevoice_small_q8`, `stable_audio_3_small_music_q8_0`, `seed_vc_mlx_q8_0`, `rvc_f16`, `mossformer2_sr_48k_f16`, `audiosr_basic_f32`, `universr_speech_orig`, …). |
| `docs/model_licenses.md` | Per-family licenses (Apache-2.0 / MIT / GPL-3.0 / Stability Community / FunASR / CC-BY). Drives §12.4. |
| `ysharma3501/LavaSR` + `tetherto/qvac` (`packages/tts-ggml`) | LavaSR upstream is **PyTorch** (Vocos-style ConvNeXt backbone + ISTFT spec head, 8–48 kHz). A **LavaSRcpp ONNX** release exists; qvac converts `enhancer_backbone.onnx` + `enhancer_spec_head.onnx` → a single `lavasr-enhancer.gguf` (f16/f32) that runs on ggml. **audio.cpp does not yet register a `lavasr` family** (no `docs/models/lavasr.md`, no spec). §7.5 handles this honestly. |
| Local probe (`lscpu`, `free`, `lspci`, toolchain) | Dev box: i7-3630QM (4C/8T, **AVX, no AVX2/FMA**), 16 GB RAM, Intel HD 4000 (no Vulkan ICD) + NVIDIA GT 635M (Fermi, no modern CUDA/Vulkan) → **CPU-only inference on this machine**. GCC 16.2.1, CMake 4.5.0, Ninja 1.13.2, Qt 6.12 (base/declarative/svg/tools) installed. |

Anything not in this table is a design decision, not an upstream claim.

---

## 1. Goals, constraints, non-goals

### 1.1 Goals
- One standalone desktop executable (plus bundled libraries). Double-click → works offline.
- Non-linear audio editor: multi-track timeline, clips, non-destructive trims/splits, fades, automation lanes, local project persistence.
- Split A/B auditioning with a crossfade fader.
- Minimalist dark/light theme toggle, entire UI styled from one token source.
- JSON snapshot history with undo/redo and time-travel restore.
- Local AI: STT/ASR, audio super-resolution (LavaSR target), voice-effect generation, music generation — quantized GGUF weights, CPU/GPU selection with graceful fallback.

### 1.2 Hard constraints
1. **Single process.** audio.cpp is linked as a library (`libaudiocpp`), never spawned as `audiocpp_server`/`audiocpp_cli`. No localhost sockets, no IPC daemon.
2. **Real-time audio thread is sacred:** no heap allocation, no locks, no Qt objects, no syscalls that can block, no exceptions (§4).
3. **Local-only.** No telemetry, no cloud calls. The only network traffic ever is an explicit, user-initiated model download (§11.4), and the app is fully functional for editing/playback without it.
4. **QSS styles the whole UI.** This forces a Widgets-first UI: **QSS does not apply to QML** (§10.9). QML is deferred, not mixed in silently.
5. **No ONNX Runtime anywhere in the shipped app.** LavaSR reaches ggml via GGUF conversion (§7.5), not via an ONNX sidecar.

### 1.3 Non-goals (v1)
- Video, MIDI sequencing, VST3/AU plugin hosting, time-stretch/pitch-shift (phase 2+), collaborative/cloud sync, mobile.
- Sample-accurate plugin delay compensation for third-party plugins (no third-party plugins in v1; internal FX are zero-latency).

### 1.4 UX vision & principles (drives every decision below)
**Target user.** A solo creator — podcaster, musician, sound designer, or a writer producing audio — on a modest laptop. They want to edit audio and use AI **without cloud, accounts, or a learning cliff**. They value speed, calm, and trust over feature density.

**Principles**
1. **Calm by default.** Minimalist dark/light, one accent color, no chrome clutter. The waveform is the hero; everything else recedes.
2. **Instant first value.** Open → drop a file → hear it in under three interactions. No setup wizard, no account, no mandatory model download.
3. **Everything undoable, nothing scary.** Non-destructive edits + a visible history. AI never silently overwrites source audio. Destructive actions confirm; reversible ones don't nag.
4. **Honest AI.** Show the real model size, backend, ETA, and what is actually running. Never fake progress, never hide a substitution (§7.6.1). Million-scale models are framed as "instant on CPU"; heavier ones show a calibrated wait.
5. **Keyboard-first, mouse-precise.** A complete shortcut layer; sample-accurate dragging with magnetic snap.
6. **Local & private, visibly.** An offline badge, models on disk, no network except an explicit download. Privacy is a visible state, not a policy page.
7. **One idea per surface.** Timeline = arrange, Mixer = balance, AI = assist, History = time. No modal maze.

**Measurable UX success criteria** (used as acceptance gates in §14):
- Time-to-first-sound from cold launch < 10 s.
- A/B comparison reachable in one click or keypress.
- Any AI result undoable in one step.
- Zero data loss on crash → a recovery offer on relaunch.
- Theme toggle < 100 ms, no flash, applied before first paint.
- Every primary action keyboard-accessible; text contrast ≥ 4.5:1; no information conveyed by color alone.

---

## 2. Process and thread model

One process. Thread inventory and ownership:

```
┌──────────────────────────── ses process ────────────────────────────┐
│                                                                          │
│  GUI thread (Qt main)                                                    │
│   • all QObject/UI, ProjectModel mutations, QUndoStack                   │
│   • builds immutable RenderPlan, publishes to audio thread               │
│   • drains RT telemetry ring (QTimer 30 Hz), applies inference results   │
│   • owns MediaCache lifetime, reclaims retired RenderPlans               │
│                                                                          │
│  Audio thread (miniaudio device callback)  ── REAL-TIME ──               │
│   • reads current RenderPlan (acquire), renders one block, writes out    │
│   • pushes meters/playhead/xruns into RT→UI SPSC ring                    │
│   • NO Qt, NO malloc, NO mutex, NO logging that allocates                │
│                                                                          │
│  Decode/IO pool (QThreadPool, 2 threads)                                 │
│   • file decode (miniaudio decoders / libsndfile), waveform peak LODs    │
│   • prefetch into per-clip streaming rings, bounce/export rendering      │
│                                                                          │
│  Inference worker (dedicated QThread, 1 execution slot by default)       │
│   • owns audio.cpp handles; runs chunked jobs; checks cancel flag        │
│   • ggml spawns its own compute threads internally (backend_config.threads)│
│   • results → GUI via queued signal; NEVER touches RenderPlan directly   │
│                                                                          │
│  Journal/autosave worker (QThreadPool, 1 thread)                         │
│   • snapshot serialize + zstd + atomic write; own file handles           │
│                                                                          │
│  Model download (QNetworkAccessManager, async on GUI; or native manager) │
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Communication rules (Qt signal/slot constraints)

| Path | Mechanism | Why |
|---|---|---|
| GUI → Audio | **Lock-free SPSC ring of immutable commands** + atomic `RenderPlan` pointer swap | Qt queued connections allocate and can block; forbidden on RT path. |
| Audio → GUI | **Lock-free SPSC ring of POD telemetry**, drained by a 30 Hz `QTimer` on the GUI thread | The audio thread must never call `QObject`/`emit`. A timer pull avoids a drainer thread entirely. |
| GUI ↔ Decode pool | `QThreadPool::start(QRunnable*)`, results via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` or queued signals | Standard Qt worker pattern; never block the GUI waiting. |
| GUI → Inference | Job struct posted to scheduler queue; `Qt::QueuedConnection` | Jobs are value types; handles stay on the worker. |
| Inference → GUI | Queued signals: `jobProgress(id, stage, fraction)`, `jobFinished(id, Result)`, `jobFailed(id, Error)` | Cross-thread auto-connection = queued. **Throttle progress to ≤ 10 Hz.** |
| Inference → Audio | **None directly.** Results land on GUI, become undoable commands, which publish a new RenderPlan | Keeps a single writer for all structural change; RT sees only immutable plans. |

Rules enforced in code review and tests:
- The audio callback and everything it calls is annotated `EGORT` (macro = `/* RT-safe */` + clang-tidy/custom check). No `new`, `delete`, `malloc`, `std::string`, `QString`, `QFile`, `std::function` (allocating), `shared_ptr` refcount churn, mutexes, or condition variables inside.
- No `Qt::DirectConnection` anywhere across thread boundaries. No `QMutex` in the engine core.
- audio.cpp handles are **not individually thread-safe** (verified contract). All calls for a given session happen on the inference worker. Separate sessions may run on separate threads only if the scheduler explicitly allows it (§7.3).
- `audiocpp_last_error()` is **per-thread**: read it immediately on the worker after a non-OK status, copy into the job's error struct, then signal. Never read it from the GUI thread.
- Borrowed `const float*`/`const char*` outputs are **copied** into owned buffers before the result handle is freed (verified contract). The copy happens on the worker, off-RT.

### 2.2 CPU budget on the dev box
4C/8T, AVX-only. Inference `backend_config.threads = 4` (leave ≥ 2 logical cores for GUI + audio callback + decode). Audio callback at 48 kHz / 256 frames has a 5.33 ms deadline; a 32-track float mix is < 0.5 ms on this CPU, so the risk is inference stealing cores, not the mix itself. The scheduler caps concurrency (§7.3) and the engine reports xruns so contention is visible (§9).

---

## 3. Module boundaries

CMake targets and the dependency rule: **dependencies point inward; `core` knows nothing about Qt GUI, audio I/O, or audio.cpp.**

```
ses/
├─ CMakeLists.txt                 # superbuild options, Qt6, FetchContent
├─ src/
│  ├─ core/                       # lib ses_core  (Qt6::Core ONLY)
│  │  ├─ model/                   # Project, Track, Clip, AutomationLane, MediaItem (POD-ish + JSON)
│  │  ├─ commands/                # QUndoCommand subclasses, command ids, merging
│  │  ├─ history/                 # SnapshotJournal, content-addressed store, patches
│  │  ├─ persistence/             # ProjectStore (atomic save/load), migrations, settings schema
│  │  └─ render/                  # RenderPlan, RenderPlanBuilder, automation/fade evaluators (pure math)
│  ├─ engine/                     # lib ses_engine  (NO Qt)
│  │  ├─ rt/                      # SpscRing, RenderPlanSlot, RtContext, block mixer, A/B crossfade
│  │  ├─ io/                      # miniaudio device wrapper, decode cache, streaming prefetch
│  │  └─ dsp/                     # gain/pan smoothers, fades, EQ, comp, limiter, resampler (soxr)
│  ├─ inference/                  # lib ses_inference  (Qt6::Core + audiocpp C ABI)
│  │  ├─ abi/                     # RAII handle wrappers, status→Error mapping, log bridge
│  │  ├─ catalog/                 # ModelCatalog (package ids, hashes, licenses), ModelStore
│  │  ├─ scheduler/               # JobQueue, InferenceScheduler, residency LRU, backend probe
│  │  └─ jobs/                    # AsrJob, SuperResJob, VoiceFxJob, MusicJob (chunking, cancel)
│  ├─ ui/                         # executable ses  (Qt6::Widgets + Svg)
│  │  ├─ theme/                   # ThemeManager, tokens dark/light, QSS generator, icon tint
│  │  ├─ timeline/                # TimelineWidget (QPainter), rulers, clip items, automation editor
│  │  ├─ mixer/                   # MixerStrip, AbFader, meters
│  │  ├─ panels/                  # Inspector, HistoryPanel, AiPanel (ASR/SR/Voice/Music), ModelManager
│  │  └─ shell/                   # MainWindow, transport bar, theme toggle, status bar
│  └─ app/                        # main.cpp, wiring, single-instance, --smoke/--render CLI
├─ tests/                         # tst_core, tst_engine, tst_inference, tst_ui (QTest, offscreen)
├─ assets/                        # icon.svg, qss templates, token json, bundled silero_vad
├─ packaging/                     # appimage/, windows/, macos/, scripts/build-*.sh
└─ docs/                          # this file, ROADMAP.md
```

Dependency graph (arrows = "links to"):

```
ui ──► core, engine, inference
inference ──► core, audiocpp (C ABI)
engine ──► core (render plan types only), miniaudio, soxr
core ──► Qt6::Core, zstd
app ──► ui
```

Boundary invariants:
- `core` is pure data + logic: it can be unit-tested headless with `QTEST_GUILESS_MAIN`. It never includes `engine/rt` or `audiocpp.h`.
- `engine` has **no Qt dependency**. It consumes `core::RenderPlan` (a plain struct graph) and exposes a C++-only API. This makes golden-render tests trivial and keeps RT code free of Qt temptation.
- `inference` is the **only** module that includes `audiocpp.h`. Everything else sees `ses::inference::Job`/`Result` value types.
- `ui` never calls audio.cpp or miniaudio directly; it goes through `engine`/`inference` facades and `core` commands.

---

## 4. Real-time audio engine

### 4.1 I/O backend decision

| Criterion | **miniaudio** | RtAudio | PortAudio | QAudioSink |
|---|---|---|---|---|
| License | MIT-0 / PD | MIT-like | MIT | LGPL (Qt) |
| Backends | WASAPI, ALSA, Pulse, JACK, CoreAudio, AAudio | ALSA/JACK/Pulse/WASAPI/CoreAudio | most | platform default |
| Decoders built in | wav/flac/mp3/vorbis | no | no | no |
| Callback control | period size, float32, device events | yes | yes | buffer-based, higher latency |
| Integration cost | single header | small lib | small lib | Qt-only, but opaque latency |
| **Verdict** | **✅ chosen** | fallback | fallback | ❌ not pro-audio grade |

**Decision: miniaudio.** Single-header (vendor `external/miniaudio/miniaudio.h`), permissive, JACK available for Linux pro users, decoders included for the decode pool. We use `ma_device` directly (playback, `format = ma_format_f32`, `channels = 2`, `periodSizeInFrames = 256`, `periodSizeInMilliseconds = 0`), **not** `ma_engine` (we own the graph). Device sample rate = project rate (48 kHz); if the device refuses 48 kHz, open at its native rate and resample the master output with soxr (off-RT-configured, RT-safe `soxr_process` on a preallocated buffer).

Swap cost: the device wrapper is one class (`AudioDevice`) behind an interface; replacing miniaudio with RtAudio later touches only `engine/io`.

### 4.2 Render graph

```
Transport (playhead, loop, preroll)
   │
   ▼
Per-track scheduler ──► active clips at playhead (binary search over sorted clip starts)
   │   per clip: source PCM (cache/stream ring) → trim window → fade gain → clip gain
   │   per track: automation (gain/pan/param) → insert FX (zero-latency) → track gain/pan
   │   per track: A/B routing (A | B | both)
   ▼
Bus A sum ──► Bus A master chain ─┐
                                 ├─► A/B crossfade fader ─► master limiter ─► device out
Bus B sum ──► Bus B master chain ─┘
   │
   └─► telemetry: per-track peak/RMS, master peak, playhead, xrun count → SPSC ring
```

- Internal format: **float32, interleaved stereo, 48 kHz**, block = 256 frames (configurable 128–1024).
- All DSP is block-based with **per-sample parameter ramps** where it matters (fades, crossfade, automation fast paths). Gain/pan use a one-pole smoother whose state lives in RT-owned mutable scratch, never in the immutable plan.
- v1 insert FX (all zero-latency, preallocated state): gain, 3-band parametric EQ (biquads), compressor (no lookahead), soft limiter on master. No convolution reverb in v1 (needs partitioned FFT + latency; phase 2).

### 4.3 Lock-free structural updates: the RenderPlan slot

The NLE changes topology constantly (drag, trim, add automation point). The RT thread must never see a half-built graph.

**Pattern: immutable plan + atomic pointer swap + grace-period reclamation (RCU-lite).**

```cpp
// engine/rt/RenderPlanSlot.h  — single producer (GUI), single consumer (audio)
class RenderPlanSlot {
public:
    // GUI thread only. Builds a complete immutable plan, then publishes.
    void publish(const core::RenderPlan* next) {
        const core::RenderPlan* old = current_.exchange(next, std::memory_order_acq_rel);
        if (old) retired_.push(old);           // lock-free reclaim queue
    }
    // Audio thread only, once per block.
    const core::RenderPlan* acquire() const {
        return current_.load(std::memory_order_acquire);
    }
    // GUI thread, on a 30 Hz timer: free plans no audio callback can still hold.
    void reclaim() {
        // A plan is safe to free once >= 2 callbacks have elapsed since retirement
        // (the audio thread only ever holds the pointer for one block).
        while (const core::RenderPlan* p = retired_.pop_if_quiescent(graceBlocks_))
            delete p;                          // GUI thread allocates/frees, never RT
    }
private:
    std::atomic<const core::RenderPlan*> current_{nullptr};
    ReclaimQueue retired_;                     // SPSC, fixed capacity
    int graceBlocks_ = 4;
};
```

Invariants:
- A `RenderPlan` is **deeply immutable** after publish: clip arrays, fade curves, automation breakpoint arrays, routing tables, and **raw `const float*` pointers into MediaCache buffers**. No RT mutation.
- Mutable RT state (smoother histories, compressor envelopes, streaming read cursors, playhead) lives in a separate `RtState` owned by the engine, sized for the max track/clip count at plan-build time. The plan carries *indices* into RtState, so a new plan never reallocates RT scratch.
- **MediaCache lifetime rule:** decoded PCM arenas are owned by the GUI-side `MediaCache` (`std::shared_ptr<AudioBuffer>`). A plan holds raw pointers; the cache may only evict a buffer after (a) no live or retired plan references it and (b) the reclaim grace has passed. Eviction is a GUI-thread operation. While a project is open, buffers are pinned by default; eviction happens only under memory pressure (§7.6) with the same grace protocol.
- Streaming clips (long files) use a per-clip **SPSC prefetch ring** filled by the decode pool; the RT consumer reads contiguous frames. If a ring underruns, RT outputs silence for that clip and increments an `underrun` telemetry counter — it never blocks (§9.2).

### 4.4 Transport and sample-accurate scheduling

- Playhead is an `int64_t` sample counter at project rate, advanced by the audio thread each block; GUI reads it from telemetry (30 Hz) and interpolates for the ruler.
- `play/stop/seek/loop` are RT commands in the GUI→RT ring, applied at the **next block boundary**. Seek sets playhead and resets streaming cursors (a `seek` command carries the target; RT re-primes rings via a flag the decode pool watches — never blocking RT).
- Clip scheduling: each track's clips are sorted by `timelineStart`; RT keeps a per-track active-window index and advances it monotonically, with a binary search on seek. Overlapping clips on one track sum (crossfade handled by fade curves, §5.4).
- Loop: wrap playhead at loop end, reset cursors; preroll = 1 block of silence to avoid clicks.

### 4.5 Split A/B auditioning fader

**Two parallel master buses, one fader.** A and B are two processing states of the same arrangement — e.g. A = dry reference, B = processed; or two mix versions; or two takes.

Data model (§5): each track has `abRouting ∈ {A, B, Both}`; each bus has its own master FX chain and gain; project has `abFader ∈ [0,1]` and `abMode ∈ {Crossfade, Split}`.

RT rendering, per block:
```cpp
// Equal-power crossfade, click-free. x is the smoothed fader position.
const float theta = x * (float(M_PI) / 2.0f);
const float gA = std::cos(theta);   // x=0 → A only
const float gB = std::sin(theta);   // x=1 → B only
// ramp gA/gB per-sample from previous block's values (10 ms smoothing)
for (int i = 0; i < frames; ++i) {
    outL[i] = busA.L[i]*gA_r[i] + busB.L[i]*gB_r[i];
    outR[i] = busA.R[i]*gA_r[i] + busB.R[i]*gB_r[i];
}
```
- **Crossfade mode:** summed mono-compatible blend, equal-power so center is ~−3 dB per bus, constant perceived loudness.
- **Split mode:** A → left channel only, B → right channel only (headphone A/B comparison). The fader becomes a balance; at center both are full-level in their own ear.
- UI: a single horizontal `AbFader` widget with A/B end labels, a center detent, and **A-only / B-only solo buttons** that snap the fader (animated 80 ms) — plus keyboard `A`/`B`/`\` (toggle). Fader moves are RT parameter commands (smoothed), not plan rebuilds; routing/chain changes are plan rebuilds.
- Both buses render in the same callback → **zero added latency**, perfect phase alignment for null-testing (invert B against A to hear the difference — a "Δ" monitor option, phase 2).
- "Copy A→B", "Swap A/B" are undoable commands that clone bus chains/routing.

### 4.6 Automation and fades (RT evaluation)

- Automation lanes are sorted breakpoint lists `(sample, value, curve)` (§5.5). RT evaluates per block: locate the segment containing block start (cached index, monotonic advance), then **per-sample linear interpolation** for gain/pan (cheap, exact); exponential/step curves use a lookup or per-sample `powf` only on the affected parameter. Smoothed parameters additionally pass through the one-pole smoother to avoid zipper noise on coarse lanes.
- Fades are evaluated **per-sample** from the clip's fade curve (linear, equal-power, exp, S-curve) over the fade window; outside the window gain = 1. Crossfades between overlapping clips are just two complementary fade curves — no special RT case.
- All curve math lives in `core/render` as pure functions, unit-tested against golden values, and called identically by the RT mixer and the offline bounce renderer (§13).

### 4.7 Offline bounce / export

The same `core::RenderPlan` + DSP code renders to disk on the decode pool (non-RT, can allocate), block by block, with a progress signal. This guarantees **export == playback** by construction and gives us golden-render tests for free. Export formats: WAV (libsndfile/miniaudio encoder), FLAC; MP3 via a vendored encoder (phase 2).

---

## 5. Non-linear editor data model

### 5.1 Time base and IDs
- **All times are `int64_t` samples at the project sample rate** (default 48000). No floats for positions; UI converts to seconds/timecode/beats for display. This makes trims, splits, and automation sample-exact and JSON-stable.
- IDs are UUID strings (`QUuid::createUuid().toString(QUuid::WithoutBraces)`), stable across saves, referenced by clips/lanes/media.
- Durations/lengths are `int64_t` samples, always ≥ 0.

### 5.2 Entities

```
Project
 ├─ schemaVersion: int            (forward-only migrations, §11.2)
 ├─ id, name, createdAt, modifiedAt
 ├─ sampleRate: int (48000), bitDepthExport: int
 ├─ tempo: double (display only in v1), timeSignature
 ├─ mediaPool: MediaItem[]
 ├─ tracks: Track[]
 ├─ markers: Marker[]             (sample, label, color)
 ├─ ab: { fader: double, mode: "crossfade"|"split", busA: BusChain, busB: BusChain }
 ├─ master: { gain, limiter: {...} }
 └─ view: { zoom, scrollX, scrollY, theme, ... }   (UI state, saved but not audio)

MediaItem
 ├─ id, path (relative to project bundle), contentHash (sha256)
 ├─ sampleRate, channels, frames, format
 └─ peaks: { lodLevels: int, cacheKey }           (waveform LODs cached on disk)

Track
 ├─ id, name, kind: "audio", color, order
 ├─ mute, solo, gain: double, pan: double
 ├─ abRouting: "A"|"B"|"both"
 ├─ fxChain: FxInstance[]                        (id, type, params, enabled)
 ├─ clips: Clip[]
 └─ automation: AutomationLane[]

Clip
 ├─ id, mediaId, name, color, muted
 ├─ timelineStart: int64        (position on track)
 ├─ sourceOffset: int64         (trim in — into the media)
 ├─ length: int64               (trim out — sourceOffset+length ≤ media.frames)
 ├─ gain: double
 ├─ fadeIn:  { length: int64, curve: "linear"|"equalPower"|"exp"|"sCurve" }
 ├─ fadeOut: { length: int64, curve: ... }
 └─ loop: { enabled: bool, region: [int64,int64] }   (phase 2; v1 false)

AutomationLane
 ├─ id, target: ParamPath        (e.g. "track.gain", "track.pan", "fx.<id>.<param>")
 ├─ defaultValue: double
 └─ points: [ { t: int64, value: double, curve: "linear"|"exp"|"step" } ]  (sorted by t)

FxInstance / BusChain / Marker  … (plain records, JSON-round-trippable)
```

### 5.3 Non-destructive editing semantics
- **Trim** = change `sourceOffset`/`length`/`timelineStart`. Media is never modified.
- **Split** at sample `s` = replace clip with two clips sharing `mediaId`: left keeps `sourceOffset`, length `s − timelineStart`; right gets `timelineStart = s`, `sourceOffset += (s − oldStart)`, remaining length. One undoable macro-command.
- **Slip** = change `sourceOffset` only (content slides under a fixed window). **Slide** = change `timelineStart` only.
- **Delete clip** never deletes media; media is GC'd only when unreferenced *and* the user confirms (or on "Clean unused media").
- **Fades** are properties of the clip, not baked audio. **Crossfade** = overlapping clips with complementary fadeOut/fadeIn; an explicit "Create crossfade" command sets both curves over the overlap.
- AI outputs (transcript-aligned clips, SR renders, generated music) enter the project as **new MediaItems + clips** via the same command path — fully undoable, never mutating the source (§7.7).

### 5.4 Fade curves (exact math, shared by RT and bounce)
For fade progress `p ∈ [0,1]` (0 = silent end, 1 = full):
- linear: `g = p`
- equalPower: `g = sin(p·π/2)` (fade-in); fade-out uses `cos(p·π/2)` so two overlapping equal-power fades sum to ~constant power.
- exp: `g = p²` (in) / `(1−p)²` style out, parameterized.
- sCurve: `g = p²(3−2p)` (smoothstep).
All evaluated per-sample; unit tests pin exact values at p = 0, 0.25, 0.5, 0.75, 1.

### 5.5 Automation targets
`ParamPath` is a resolved string → `(entityId, paramEnum)` at plan-build time. v1 targets: `track.gain`, `track.pan`, `track.mute` (step), `fx.<id>.<param>`, `ab.fader`, `master.gain`. The plan builder flattens lanes into per-track sorted arrays the RT evaluator consumes. Unknown targets on load are preserved but flagged (§11.2).

### 5.6 JSON schema example (project.json, abbreviated)

```json
{
  "schemaVersion": 1,
  "id": "9f2c…", "name": "Demo", "sampleRate": 48000,
  "mediaPool": [
    { "id": "m1", "path": "media/vox.wav", "contentHash": "sha256:…",
      "sampleRate": 48000, "channels": 1, "frames": 240000, "format": "wav" }
  ],
  "tracks": [
    { "id": "t1", "name": "VOX", "kind": "audio", "color": "#7aa2f7",
      "mute": false, "solo": false, "gain": 1.0, "pan": 0.0, "abRouting": "both",
      "fxChain": [ { "id": "fx1", "type": "eq3", "enabled": true,
                     "params": { "low": 0.0, "mid": -2.0, "high": 1.5 } } ],
      "clips": [
        { "id": "c1", "mediaId": "m1", "timelineStart": 0, "sourceOffset": 4800,
          "length": 192000, "gain": 0.9, "muted": false,
          "fadeIn": { "length": 2400, "curve": "equalPower" },
          "fadeOut": { "length": 4800, "curve": "sCurve" } }
      ],
      "automation": [
        { "id": "a1", "target": "track.gain", "defaultValue": 1.0,
          "points": [ { "t": 0, "value": 0.0, "curve": "linear" },
                      { "t": 48000, "value": 1.0, "curve": "linear" } ] }
      ] }
  ],
  "ab": { "fader": 0.5, "mode": "crossfade",
          "busA": { "gain": 1.0, "fxChain": [] },
          "busB": { "gain": 1.0, "fxChain": [ { "id": "fxB", "type": "comp", "enabled": true, "params": {} } ] } },
  "master": { "gain": 1.0, "limiter": { "ceiling": -0.3, "release": 0.05 } },
  "markers": [], "view": { "zoom": 100.0, "scrollX": 0, "theme": "dark" }
}
```

Forward-compatibility rule (matches the user's standing convention): **unknown keys are preserved on load and re-emitted on save**; newer-schema files are never downgraded.

---

## 6. Commands, undo/redo, and JSON snapshot history

Two cooperating mechanisms — commands for fast in-memory undo/redo, a snapshot journal for persistence, time-travel, and crash recovery. They are kept consistent by a single rule: **every state change is a committed command, and every committed command advances the journal cursor.**

### 6.1 Command layer (QUndoStack)

- `core::ProjectModel` is the single source of truth, mutated **only** by `QUndoCommand::redo()/undo()` on the GUI thread.
- Each command stores the minimal before/after state of the entities it touches (JSON fragments or value copies), so `undo()` is exact and independent of other commands.
- Command catalog (v1): `AddTrack`, `RemoveTrack`, `ReorderTracks`, `AddClip`, `RemoveClips`, `MoveClips`, `TrimClip`, `SlipClip`, `SplitClips`, `SetClipGain`, `SetFade`, `CreateCrossfade`, `SetTrackParam`, `SetFxParam`, `AddFx`, `RemoveFx`, `AddAutomationPoint`, `MoveAutomationPoint`, `RemoveAutomationPoints`, `SetAbFader`, `SetAbRouting`, `SetBusChain`, `AddMarker`, `ImportMedia`, `ApplyAiResult` (macro), `RestoreSnapshot` (macro).
- **Drag coalescing:** interactive drags emit a stream of provisional updates (RT parameter commands for live preview) but commit **one** command on mouse-release. `QUndoCommand::id()` returns `(type, targetId)` and `mergeWith()` absorbs intermediate states so a drag is one undo step.
- **Macro commands** (`SplitClips`, `ApplyAiResult`, `RestoreSnapshot`) wrap children; undo/redo is atomic.
- After each committed command: `ProjectModel::changed` → (a) rebuild + publish RenderPlan, (b) mark dirty, (c) schedule journal append (coalesced 500 ms), (d) schedule autosave (idle 30 s).

```cpp
// core/commands/TrimClipCommand.h
class TrimClipCommand : public QUndoCommand {
public:
    enum { Id = 1001 };
    TrimClipCommand(ProjectModel* m, QString clipId,
                    int64_t newOffset, int64_t newLength, int64_t newStart)
        : model_(m), clipId_(std::move(clipId)),
          after_{newOffset, newLength, newStart} {
        before_ = model_->clipTrim(clipId_);          // value copy
        setText(QObject::tr("Trim clip"));
    }
    int id() const override { return Id; }
    bool mergeWith(const QUndoCommand* other) override {
        auto* o = static_cast<const TrimClipCommand*>(other);
        if (o->clipId_ != clipId_) return false;
        after_ = o->after_;                            // keep original before_
        return true;
    }
    void redo() override { model_->setClipTrim(clipId_, after_); }
    void undo() override { model_->setClipTrim(clipId_, before_); }
private:
    ProjectModel* model_; QString clipId_;
    ClipTrim before_, after_;
};
```

### 6.2 Snapshot journal (JSON history)

Purpose: durable history that survives restarts, a History panel you can click to jump to any point, and crash recovery. Storage is **content-addressed and compressed**.

```
history/
 ├─ index.json          # ordered entries: { seq, hash, parentHash, label, ts, commandId }
 └─ objects/
     └─ <sha256>.json.zst   # full project JSON, zstd-compressed, deduplicated by hash
```

- On journal append (coalesced): serialize `ProjectModel` → JSON → sha256 → if the object already exists, reuse it (dedup); else write `objects/<hash>.json.zst` atomically; append an index entry. The index is the undo/redo timeline.
- **Cursor semantics:** `undo()` moves the cursor back one entry and applies `command.undo()`; `redo()` forward. The journal is append-only; undo/redo does **not** delete entries — it moves a cursor, so the History panel shows the full branchable timeline. A new command after undo truncates the *redo branch* in the index (standard semantics) but objects remain until GC.
- **Time-travel restore:** clicking entry `N` computes a JSON diff (current vs target) and applies it as one `RestoreSnapshot` macro-command (so the jump is itself undoable). For v1 the diff is "replace whole project state" wrapped as a command; RFC 6902 patch generation is an optimization for large projects (phase 2).
- **Bounds:** ring cap default **500 entries / 256 MB** (whichever first). GC removes oldest unreferenced objects on a worker. Consecutive identical states are deduped (no-op commands don't grow history).
- **Why full snapshots + dedup instead of pure deltas:** simplicity and crash-safety (any single object + index reconstructs a state); zstd makes typical project snapshots a few KB; dedup collapses unchanged regions. A delta-chain mode (periodic full + RFC 6902 patches) is specified as the scale path when projects exceed ~5 MB of JSON.

### 6.3 Consistency rule
`ProjectModel` state == journal entry at cursor == `project.json` on disk (after save). Tests assert this triple after random command sequences (§13). Undo/redo never touches disk synchronously; journal/autosave are async workers with their own file handles.

---

## 7. Inference layer (audio.cpp, in-process)

### 7.1 Why the C ABI, not the server, not the CLI

Verified: `audiocpp_server` is a separate process with a port (forbidden by §1.2); `audiocpp_cli` reloads weights per request (no warm sessions). The **C ABI (`libaudiocpp`) is the only surface that keeps a session warm inside our process** and benefits from graph/cache reuse. We link it directly and manage handles ourselves.

Build integration (verified flags):
```cmake
# Option A: prebuilt release archive (fastest CI/dev): audio-<tag>-lib-<platform>-<backend>
#   → ships include/audiocpp.h + libs/ (libaudiocpp.so + ggml backend plugins).
# Option B: FetchContent source build with composite model set:
FetchContent_Declare(audiocpp GIT_REPOSITORY https://github.com/0xShug0/audio.cpp.git
                     GIT_TAG v0.9.5)   # pin a release tag, never main
set(AUDIOCPP_BUILD_C_API ON CACHE BOOL "" FORCE)
set(AUDIOCPP_DEPLOYMENT_BUILD ON CACHE BOOL "" FORCE)   # embed model specs
set(AUDIOCPP_MODEL_SET "custom" CACHE STRING "" FORCE)
set(AUDIOCPP_MODELS
    "qwen3_asr,sense_asr,silero_vad,seed_vc,rvc,tone_color_vc,qwen3_tts,kokoro_tts,stable_audio,mossformer2,audiosr,universr"
    CACHE STRING "" FORCE)
set(ENGINE_ENABLE_NATIVE_CPU OFF CACHE BOOL "" FORCE)   # portable AppImage kernels
# backend flags per target platform: ENGINE_ENABLE_VULKAN/CUDA/METAL
FetchContent_MakeAvailable(audiocpp)
target_link_libraries(ses_inference PRIVATE audiocpp)
```
Composite builds keep the binary lean (only the families we ship). **Backend plugins must sit beside `libaudiocpp.so`** in the package (verified discovery rule) — packaging copies them together (§12).

### 7.2 RAII wrapper and error mapping

`inference/abi` wraps every handle in a move-only RAII type and maps `audiocpp_status` → `ses::Error`:

```cpp
struct AudioCppDeleter { void operator()(audiocpp_registry* p) const { audiocpp_registry_free(p); } };
using RegistryPtr = std::unique_ptr<audiocpp_registry, AudioCppDeleter>;
// … ModelPtr, SessionPtr, OptionsPtr, RequestPtr, ResultPtr, EventPtr likewise.

Error mapStatus(audiocpp_status s) {
    switch (s) {
    case AUDIOCPP_OK: return {};
    case AUDIOCPP_ERR_INVALID_ARGUMENT: return {ErrCode::InvalidArgument, audiocpp_last_error()};
    case AUDIOCPP_ERR_UNSUPPORTED_FAMILY: return {ErrCode::UnsupportedFamily, audiocpp_last_error()};
    case AUDIOCPP_ERR_LOAD_FAILED: return {ErrCode::ModelLoadFailed, audiocpp_last_error()};
    case AUDIOCPP_ERR_OUT_OF_MEMORY: return {ErrCode::OutOfMemory, audiocpp_last_error()};
    case AUDIOCPP_ERR_RUNTIME: return {ErrCode::InferenceRuntime, audiocpp_last_error()};
    case AUDIOCPP_ERR_NOT_AVAILABLE: return {ErrCode::NotAvailable, audiocpp_last_error()};
    default: return {ErrCode::InferenceRuntime, audiocpp_last_error()};
    }
}
```
- ABI gate at startup: `if ((audiocpp_abi_version() >> 16) != AUDIOCPP_ABI_VERSION_MAJOR) → disable AI, show "inference library incompatible"`. Gate `audiocpp_result_custom_schema` on minor ≥ 0x0400, `audiocpp_log_set` on ≥ 0x0300 (verified versioning rules).
- `audiocpp_log_set(bridge, this)` routes ggml/engine logs into our logging sink (worker thread → queued signal to GUI log panel; never into RT).
- **Borrowed outputs are copied** into `Result` (owned `std::vector<float>` PCM, `QString` text, segment/word vectors) before `audiocpp_result_free`.

### 7.3 Scheduler, residency, and concurrency

- `InferenceScheduler` owns a **priority job queue** and **one execution slot** by default (configurable to 2 on machines with ≥ 16 GB and a discrete GPU). Rationale: audio.cpp sessions are not individually thread-safe; ggml already parallelizes internally; two large models resident + compute can exhaust RAM/VRAM. Serial execution is the safe default; parallelism is opt-in.
- **Model residency LRU:** `ModelStore` keeps loaded `ModelPtr`s keyed by `(packageId, backend)`. Because the default catalog is million-scale (§7.6, < ~200 MB total), the cap is **6 light models + 1 heavy model** (the opt-in 230 M music model). On ≤ 8 GB RAM: 4 light + 1 heavy. Loading past the cap unloads the least-recently-used *idle* model in its class (`audiocpp_model_free`). Sessions are created per job from a resident model and freed after; weights stay warm. Mirrors the server's `max_loaded_models` behavior (verified) but in-process, with a light/heavy split so a tiny TTS model is never evicted to make room for another tiny model while the heavy music model sits idle.
- **Job lifecycle:** `Queued → Loading(model) → Preparing(session) → Running(chunk i/n) → Finalizing → Done | Failed | Cancelled`. Each stage emits a throttled progress signal. `audiocpp_session_prepare()` is called with a representative request to pay allocation before the timed run (verified note).
- **Threads:** `backend_config.threads` = clamp(physicalCores − 2, 1, 8) on CPU; on GPU, 1–2 host threads. The library never calls `omp_set_num_threads()` (verified) — we set threads per session, so we don't pollute process-global OpenMP state.
- **Cancellation:** the ABI has **no abort entry point** (verified). Cancellation is therefore **cooperative at chunk granularity** (§7.4). A cancel request sets `std::atomic<bool>`; the worker checks it between chunks and between stages. A single in-flight `audiocpp_session_run` cannot be interrupted — jobs are sized so one run is bounded (≤ ~30 s of audio / one music segment). If the user cancels during an uninterruptible run, the job is marked cancelled, the run finishes in the background, and its result is discarded (documented tradeoff; no thread is ever killed).

### 7.4 Chunking strategy (also gives us progress + cancel)

- **ASR (long audio):** run bundled **Silero VAD** (MIT, ships in `assets/framework/models`, no download — verified) in streaming mode to get speech windows; merge gaps < 0.3 s, pad 0.2 s, cap window 30 s. One `session_run(asr)` per window; accumulate segments/words with absolute sample offsets (`start_sample` from the window). Cancel checks between windows. Partial text from streaming ASR (where the family supports it) is shown live via `audiocpp_stream_push`/`next_event` (pull-based, verified).
- **Super-resolution / enhancement:** fixed windows (e.g. 10 s) with **0.5 s overlap-add** (Hann) to hide seams; resample input to the model's expected rate first (soxr, off-RT). Cancel between windows.
- **Voice conversion:** same windowing as SR; reference voice audio set once via `audiocpp_request_set_voice_audio` (copied by ABI).
- **Music / TTS generation:** bounded-duration requests (≤ 30 s segments for music; sentence-split for long TTS), concatenated with short crossfades; cancel between segments. Seed exposed as a request option for reproducibility (generative families draw noise per run — verified parity note).

### 7.5 Capability: super-resolution — LavaSR, honestly

**Requirement:** LavaSR. **Verified reality:** upstream LavaSR is PyTorch; a LavaSRcpp ONNX release exists; qvac's `tts-ggml` converts the LavaSR enhancer (ConvNeXt backbone + ISTFT spec head) to a single GGUF that runs on ggml. **audio.cpp v0.9.5 does not register a `lavasr` family.** We will not ship ONNX Runtime (§1.2).

Design: a single `SuperResolution` capability interface with a pluggable family, so the model can change without touching UI/jobs/persistence.

```
interface SuperResolutionProvider {
    Result enhance(pcm, srcRate, targetRate=48000, denoise, cancelToken, progress);
}
```
- **Shipping default (day one), million-scale-first (§7.6):** the lightweight enhancer path — **FlashSR** (`builtin_audio_utils`) and the **LavaSR enhancer** (small, port below). `mossformer2_sr_48k_f16` (Apache-2.0) is kept as the *heavier quality alternate*, not the default; `audiosr_basic_f32` (diffusion, slow) and `universr_speech_orig` (CC-BY-4.0) are opt-in. Exact SR enhancer parameter counts are not published in the upstream docs checked — labeled "small (enhancer-only)", not given a fabricated number.
- **LavaSR target (tracked work item):** register a `lavasr` family in audio.cpp (community model tree) using the GGUF enhancer converted from the LavaSRcpp ONNX release (qvac's `convert-lavasr-enhancer-to-gguf.py` is the reference converter; backbone + spec head → one GGUF, f16). The ConvNeXt + ISTFT ops are ggml-friendly and audio.cpp already ships Vocos/ISTFT-family vocoder code, so this is a port, not a research project. Until it lands and passes parity validation, the UI labels SR honestly ("MossFormer2 SR" / "LavaSR (experimental)") — **no silent substitution and no claim that LavaSR is running when it isn't.**
- The `SuperResJob` is identical for all providers: window → enhance → overlap-add → write new MediaItem → `ApplyAiResult` command. Swapping providers is a catalog entry.

### 7.6 Capability catalog — million-parameter first (verified sizes)

**Design rule (user constraint):** default every capability to the *smallest model that does the job* — **million-parameter scale wherever such a model exists.** All parameter counts below are verified from upstream model docs/specs (2026-11-20), not estimated. Where no million-scale model exists, the smallest real option is listed and **explicitly flagged** in §7.6.1 — never silently swapped for a bigger model presented as small.

| Capability | Default (ultra-light) | Params (verified) | Package ID | License (weights) |
|---|---|---|---|---|
| VAD / chunking | **Silero VAD** (bundled) | ~1–2 M (tiny) | `silero_vad` (in `assets/framework/models`) | MIT |
| VAD (sub-million alt) | PulseVAD student / teacher | **2.1 K / 81 K** | `pulsevad` | MIT |
| ASR — **English** | **Niagara 19M Batch** | **19 M** (38 M alt) | `niagara_asr` → `niagara-19m-batch.en-f32.gguf` | ABR Open License v1.1 (**conditional**, < $1M revenue) |
| TTS — **Turkish** | **sanoTTS `tr`** (piperlite) | **1,562,284 (~1.56 M)** | `sanotts_tr_orig` | **GPL-3.0** (copyleft) · needs eSpeak-ng + `tr` data |
| TTS — English (smallest) | **tiny-voca** | **4.5 M** | `tiny_voca_orig` | Apache-2.0 |
| TTS — English (alt) | PocketTTS Small | **2 M / 7 M / 18 M** | `pocket_tts_small_english_2m_f32` (…7m/18m_refl) | CC-BY-4.0 (attribution) |
| TTS — English (quality alt) | Gradium Phonon 31M | 31 M | `gradium_phonon_31m_en_q8_0` | Apache-2.0 |
| Speech enhancement / denoise | **GTCRN** (tiny) / RNNoise / DeepFilterNet2 | sub-M … ~2 M | `builtin_audio_utils` (`gtcrn`, `rnnoise`, `deepfilternet2`) | BSD-3 (RNNoise) / MIT-Apache (DFN) |
| Super-resolution | **LavaSR enhancer** (port, §7.5) / FlashSR | small (enhancer-only) | `lavasr` (tracked) / `builtin_audio_utils` FlashSR | see §7.5 |
| SR — quality alt (heavier) | MossFormer2 SR 48K | not published (larger) | `mossformer2_sr_48k_f16` | Apache-2.0 |
| Voice conversion ⚠ | MeanVC2 / OpenVoiceV2 | **not million-scale** (tens–hundreds M) | `meanvc2_120ms_40ms_f32`, `tone_color_vc` | Apache-2.0 / MIT |
| Music / SFX generation ⚠ | Magenta RealTime 2 **Small** | **230 M** (smallest available) | `magenta_realtime_2` (Small) | CC-BY-4.0 |

⚠ = no million-parameter model exists for this capability; see §7.6.1. Heavier alternates (Qwen3-ASR 0.6B/1.7B, Qwen3-TTS, Kokoro 82M, Stable Audio, ACE-Step, SeedVC) remain available as **opt-in** catalog entries for users who want quality over size, but are **not defaults**.

#### 7.6.1 The million-parameter constraint — what fits and what doesn't (honest)

**Fits million-scale (default, always-on light AI):**
- VAD: Silero (~1–2 M) / PulseVAD (**2.1 K–81 K**, sub-million).
- English ASR: Niagara **19 M** (offline batch; English-only).
- TTS **including Turkish**: sanoTTS `tr` **1.56 M**; English tiny-voca **4.5 M** / PocketTTS **2 M**.
- Speech enhancement/denoise: GTCRN / RNNoise / DeepFilterNet2 (sub-M … ~2 M).
- Lightweight SR: LavaSR enhancer / FlashSR (small).

**Does NOT fit — no million-parameter model exists in audio.cpp (verified):**
1. **Turkish ASR.** Niagara and Moonshine are **English-only**. The smallest Turkish-capable ASR is **SenseVoice-Small ≈ 244 M** (upstream HF: "similar number of parameters to Whisper-Small"; FunASR Model License, credit required). Options: (a) English-only ASR at 19 M, (b) accept SenseVoice ≈ 244 M as the *single* ASR exception for Turkish, (c) drop ASR. **Default: (a)**; Turkish ASR offered as an explicit, size-labeled opt-in.
2. **Voice conversion.** Zero-shot VC needs a content encoder + speaker encoder + decoder; the smallest (MeanVC2 / OpenVoiceV2 / RVC) is **tens–hundreds of M**, not millions. Options: accept a heavier opt-in, or drop VC and keep TTS-based voice design only. **Default: opt-in, not installed by default.**
3. **Music / SFX generation.** Fundamentally larger models. The smallest available is **Magenta RealTime 2 Small = 230 M** (verified); Stable Audio Small is ~1 B-class. **No million-parameter music generator exists.** Options: (a) make music the *single optional heavier* capability (230 M, still slow on this CPU), (b) drop music gen from v1. **Default: (a), opt-in, size shown before download.**

**Licensing flags on the light picks (verified `docs/model_licenses.md`):**
- **sanoTTS = GPL-3.0 (copyleft).** The 1.56 M Turkish voice is the only million-scale Turkish TTS, but linking it into a distributed binary triggers copyleft. Offer it as an explicit opt-in download with a license warning; permissive-license small Turkish alternatives (KittenTTS2 — Stellon Community License, conditional; Supertonic — OpenRAIL-M) are larger and/or conditional. For a fully permissive commercial build, Turkish TTS is the one capability where size and license trade off — surface this to the user, don't hide it.
- **Niagara = ABR Open License v1.1 (conditional):** free under $1 M yearly revenue, else non-commercial research only. Flag in ModelManager.
- tiny-voca (Apache-2.0), PocketTTS (CC-BY-4.0 attribution), Gradium Phonon (Apache-2.0), PulseVAD/Silero (MIT), GTCRN/RNNoise/DFN (BSD/MIT/Apache), Magenta (CC-BY-4.0 attribution) are otherwise clean.

**Memory budget (16 GB dev box) — light catalog is tiny:** sanoTTS 1.56 M f32 ≈ 6 MB; tiny-voca 4.5 M ≈ 18 MB; PocketTTS 2 M ≈ 8 MB; Niagara 19 M f32 ≈ 76 MB; GTCRN/RNNoise < 1 MB. **The entire default light catalog stays resident in < ~200 MB.** The only heavy resident is the opt-in 230 M music model (≈ 0.5–1 GB). Residency: cap **6 light models** + **1 heavy** (§7.3). On ≤ 8 GB machines this is still trivial; the scheduler refuses any job whose estimated footprint exceeds available RAM with a clear message (never OOM-crash).

### 7.7 Result → timeline integration (all undoable)

- **ASR:** `Result.segments/words` (sample offsets, verified accessors) → (a) a **Transcript** document attached to the project (JSON, searchable), (b) optional **marker per segment** and/or **clip split at word boundaries** via a macro command. Nothing mutates source media.
- **SR / Voice FX:** output PCM → new WAV in `media/` (new MediaItem, hashed) → new clip on a new track named e.g. "VOX (SR 48k)" aligned to the source clip's `timelineStart`, original muted (not deleted). One `ApplyAiResult` macro command; undo removes the added media/clip and unmutes.
- **Music:** generated PCM → new MediaItem + clip at playhead (or a dedicated "Generated" track), with the prompt/seed/model stored in the clip's `aiMeta` for reproducibility and re-generation.
- Every AI action is a normal command → normal undo/redo → normal journal entry. AI is not a special persistence path.

---

## 8. Backend selection and GPU/CPU fallback

### 8.1 What ships
- Linux: CPU + Vulkan (CUDA optional in a separate NVIDIA build). macOS: Metal + CPU. Windows: CPU + Vulkan + CUDA (cudart archive beside libs, verified).
- ggml backend plugins are discovered **beside `libaudiocpp`** (verified) — packaging keeps them together; no working-directory dependence.

### 8.2 Probe and selection
- Setting: `inference.backend ∈ {auto, cuda, vulkan, metal, cpu}` (default `auto`), plus `inference.threads`.
- **Startup probe (worker, non-blocking):** for each candidate backend in platform order, create a session on the **bundled Silero VAD** and run a 0.5 s buffer; record success + wall time. Cache the result in settings keyed by `(backend, driverVersion, abiVersion)` so we don't probe every launch. This is cheap because Silero needs no download (verified path-test pattern).
- `auto` picks the first working backend in order **cuda → vulkan → metal → cpu** (platform-filtered), preferring GPU only if the probe succeeded and estimated model footprint fits device memory.
- **Per-job fallback chain:** try selected backend; on `LOAD_FAILED` / `OUT_OF_MEMORY` / `RUNTIME` → retry **once on CPU**; if CPU also fails → job fails with a user-readable error. The status bar shows the active backend and a "degraded to CPU" badge when fallback happened. Device loss mid-run surfaces as `RUNTIME` → same retry path.
- The probe never runs on the GUI thread and never blocks playback.

### 8.3 This machine (verified, honest)
HD 4000 (Ivy Bridge) has **no Vulkan ICD**; GT 635M (Fermi, compute 2.1) has **no modern CUDA or Vulkan**. SYCL/oneAPI targets Gen9+ (not Gen7). Therefore the probe on this dev box terminates at **CPU**, and that is the correct, expected result — not a bug. The app must show "CPU (no compatible GPU detected)" plainly.

**Performance honesty:** AVX-only (no AVX2/FMA) means ggml uses its AVX kernels, not the AVX2/FMA fast paths; llamafile SGEMM (default ON) won't engage its fastest kernels here. **With the million-scale catalog (§7.6) this barely matters:** Niagara 19 M batch ASR, sanoTTS 1.56 M / tiny-voca 4.5 M TTS, and GTCRN/RNNoise enhancement are small enough to run **at or above realtime on this CPU** — the light AI feels instant. The exception is the opt-in **music model (Magenta Small 230 M, or heavier Stable Audio): still slow — minutes per 30 s segment on this CPU.** The UI shows a calibrated ETA for the heavy music path and never pretends GPU speed. GPU acceleration is designed for users' modern machines; the dev laptop is a CPU-only validation target, and the light catalog is chosen precisely so CPU-only is a good experience there.

---

## 9. Error handling and resilience

### 9.1 Taxonomy
`ses::Error { ErrCode code; QString message; QString detail; }`, codes grouped:
- **Audio:** `DeviceOpenFailed`, `DeviceLost`, `Xrun`, `StreamUnderrun`, `UnsupportedFormat`.
- **Inference:** `InvalidArgument`, `UnsupportedFamily`, `ModelLoadFailed`, `OutOfMemory`, `InferenceRuntime`, `NotAvailable`, `BackendUnavailable`, `Cancelled`, `ModelMissing`, `HashMismatch`.
- **Persistence:** `SaveFailed`, `LoadFailed`, `CorruptProject`, `SchemaTooNew`, `MediaMissing`, `DiskFull`, `LockHeld`.
- **System:** `AbiMismatch`, `Internal`.

### 9.2 Real-time error path
The audio thread **never throws, never allocates, never logs through Qt**. Errors are POD events pushed to the telemetry ring: `{type, trackIndex, count}`. GUI drains and surfaces them:
- **Xrun / underrun:** increment counters; status bar shows a subtle warning if rate > threshold; optional log. Playback continues (silence for the starved clip), never stops.
- **Device lost** (miniaudio reroute/notification callback, which runs on a miniaudio thread, not RT): the wrapper posts a queued event to GUI → GUI shows "Audio device disconnected", attempts reopen with backoff (0.5 s, 1 s, 2 s, max 5), preserves transport state, resumes on success. The RT callback itself just outputs silence while the device is down.

### 9.3 Inference errors
- Worker maps status → Error (copying `audiocpp_last_error()` on the worker thread), emits `jobFailed`. GUI shows a non-modal error in the AI panel with: what failed, the backend tried, whether CPU fallback was attempted, and a suggested action (install model / pick smaller model / free memory / switch backend).
- `ModelMissing` → AI panel shows an install card (package id, size, license, download button) — never a dead button.
- `HashMismatch` after download → quarantine the file, offer re-download; never load unverified weights.
- Cancellation is a first-class terminal state, not an error toast.

### 9.4 Persistence errors and corruption
- **Atomic writes everywhere:** `QSaveFile` (temp + rename) for `project.json`, `index.json`, settings; objects written to temp then renamed. `fsync` before rename on the journal index.
- **Single writer:** `project.lock` (QLockFile) prevents two instances editing one bundle; second instance offers read-only open.
- **Corrupt project.json:** fall back to the newest valid journal object; offer "Restore from history" with the timestamp. Never silently discard user work.
- **Media missing:** relink dialog that matches by `contentHash` (scan candidate folder) before asking the user to locate files; unresolved media renders as a hatched placeholder clip, playback skips it.
- **SchemaTooNew:** refuse to downgrade; open read-only with a clear message (matches the forward-only convention).
- **Disk full:** detect on write failure, stop autosave, warn prominently, keep the in-memory session alive so the user can free space and retry.

### 9.5 Logging
- `qInstallMessageHandler` → rotating file log in `~/.cache/ses/logs/` + optional console. audio.cpp logs bridged via `audiocpp_log_set`. RT thread does not log (telemetry only). `--smoke`/`--render` force stderr logging (`QT_FORCE_STDERR_LOGGING=1`) so headless gates capture output (matches the user's AppImage validation convention).

---

## 10. UX design & UI architecture

This section is UX-led: it specifies the *experience* (principles → layout → journeys → interaction → states) first, then the widget/QSS *implementation* that delivers it (§10.9–10.12). Engineering choices in §4–§9 exist to make these UX outcomes possible (instant playback, click-free A/B, undoable AI, honest progress).

### 10.1 Information architecture & layout
One window, four surfaces, no modal maze. Persistent transport bar; a vertical track-header column; the timeline as the centerpiece; a right dock that switches between **AI / History / Models**; a collapsible mixer along the bottom.

```
┌────────────────────────────────────────────────────────────────────────────┐
│ ▶ ⏹ ⟲   00:00:00.000 / 00:03:12.400   [A────●────B]  A B \   ☀/☾   CPU·light │ Transport
├───────────┬──────────────────────────────────────────────┬─────────────────┤
│ TRACKS    │  ruler: time · timecode · samples             │  DOCK (tabs)    │
│ ┌───────┐ │  ┌────────────────────────────────────────┐   │  ┌───────────┐  │
│ │VOX    │ │  │ ▓▓▓ clip ▓▓▓◤fade        ┃playhead     │   │  │ AI        │  │
│ │M S ⚙  │ │  │ ~~~~ waveform ~~~~                     │   │  │ History   │  │
│ │A/B:Both│ │  ├────────────────────────────────────────┤   │  │ Models    │  │
│ ├───────┤ │  │ MUSIC ▓▓▓▓   automation ∿∿∿∿           │   │  │           │  │
│ │MUSIC  │ │  └────────────────────────────────────────┘   │  │ (context  │  │
│ │M S ⚙  │ │  loop ▭▭▭▭                                    │  │  panel)   │  │
│ └───────┘ │                                               │  └───────────┘  │
├───────────┴──────────────────────────────────────────────┴─────────────────┤
│ MIXER  [VOX ▁▂▃│fader│pan│M│S]  [MUSIC ▁▂▃│fader│pan│M│S]  [MASTER ▁▂▃│lim]  │
└────────────────────────────────────────────────────────────────────────────┘
```
- **Progressive disclosure:** the dock and mixer collapse; a brand-new user sees essentially *transport + timeline + drop zone*. Advanced controls (automation, FX, A/B routing) appear in context, not all at once.
- **Selection is the connective tissue:** the current clip/range selection scopes the AI panel, the inspector, and the export range. One selection, everywhere.
- **Status bar (bottom):** offline badge · active backend + model · xrun/underrun indicator (only when nonzero) · sample rate · cursor position. Quiet by default; speaks up only when something needs attention.

### 10.2 Core user journeys
**J1 — First sound (cold launch, < 10 s).** Launch → empty state with a large drop zone and three actions (*Import audio · Open project · Recent*) → drag a WAV → inline decode progress + skeleton waveform → clip appears, auto zoom-to-fit → `Space` plays. No wizard, no account. Three dismissible coach marks (drop / split / A-B) appear once, never block.

**J2 — Arrange & edit.** Import several files → drag clips to position/reorder → pull edge handles to trim → `S` splits at playhead → drag fade triangles → toggle an automation lane and draw gain → `Ctrl+Z` anytime. Snap is magnetic and toggleable; zoom is wheel-at-cursor.

**J3 — A/B audition (one gesture).** Set a track's routing chip to A or B (or process two versions) → press `A` / `B` / `\` or drag the always-visible fader → hear the difference instantly → *Copy A→B* / *Swap* to iterate. The comparison is never more than one keypress away.

**J4 — AI assist (transcribe).** Select a clip → AI panel · ASR tab → if the model is absent, an **install card** shows real size + license + backend → download (progress + verify) → **Run** → stage progress + live partial text → result preview → choose *Add markers* / *Split at words* / *Insert transcript* → applied as one undoable command. **Cancel** is always available.

**J5 — Enhance / super-resolve.** Select a clip → Enhance tab → light provider by default → Run → result auto-routed to **B** against the original on **A** so the user *hears* the improvement before keeping it → *Keep* (new clip, original muted) or *Discard*.

**J6 — Export.** File → Export → range = selection / loop / whole → WAV/FLAC → offline bounce with progress → *Reveal file*. Export uses the same render path as playback (§4.7), so what you heard is what you get.

**J7 — Crash recovery.** Relaunch after a crash → a non-blocking banner: *"Recover unsaved work from 14:32?"* → Restore (as a command) or Dismiss. Nothing is overwritten without consent (§11.5).

### 10.3 Timeline interaction design
**Tools (keyboard):** `V` select · `R` razor · `F` fade · `Shift+A` automation. The active tool is shown in a small toolbar; select is default.

| Gesture | Action |
|---|---|
| Drag clip body | Move (`timelineStart`), with magnetic snap |
| Drag clip edge | Trim (`sourceOffset`/`length`), non-destructive |
| `Alt`+drag body | Slip (slide content under a fixed window) |
| Drag top-corner triangle | Fade length; right-click → curve (linear/equal-power/exp/S) |
| Click ruler / drag ruler | Seek / scrub playhead |
| Wheel / `Ctrl`+wheel | Zoom at cursor / vertical zoom; `Ctrl+0` fit |
| Click automation polyline | Add point; drag point; double-click delete; right-click curve |
| Click / `Shift`+click / marquee | Select / multi-select / range-select |

- **Snap** targets: grid, clip edges, markers, playhead, zero-crossing (for click-free edits). Toggle with a magnet button; hold `Ctrl` to temporarily disable.
- **Sample accuracy:** at max zoom the ruler shows individual samples; trims land on exact sample boundaries (§5.1).
- **Every gesture maps to a command** (§6): drags preview live via RT parameter commands but commit one undoable command on release (drag coalescing). The user never accumulates 200 undo steps for one drag.
- **Feedback:** hovered handles highlight; trim shows a live time tooltip; the playhead is interpolated between 30 Hz telemetry updates so it glides, not steps.

### 10.4 A/B auditioning UX
- The **fader lives in the transport bar**, always visible — A on the left, B on the right, center detent. `A`/`B` snap to ends (80 ms eased), `\` toggles.
- **Routing chip** per track (header + mixer): `A · B · Both`, color-and-label coded (A = accent, B = secondary; never color alone). Routed clips carry a subtle A/B tint.
- **Mode popover:** *Crossfade* (constant-power blend) vs *Split* (A→left ear, B→right ear). Persisted per project.
- **Iteration actions:** *Copy A→B*, *Swap A/B*, *Reset* — each undoable.
- **Null/Δ monitor** (phase 2): invert B against A to hear only the difference — the killer feature for judging processing.
- UX guarantee: comparing two states is **one keypress**, click-free, zero added latency (§4.5).

### 10.5 AI UX (honest, calm, undoable)
- **Capability cards** (ASR / Enhance / Voice / Music) each show: model name · **real parameter size** · license · backend badge · install state. Options are discovered at runtime from the model (§7.2 introspection), so the UI never hardcodes a family.
- **Install flow:** missing model → install card (size, license, *Download*) → resumable progress + sha256 verify → ready. A missing model is never a dead button (§9.3).
- **Run flow:** `Queued → Loading → Preparing → Running (chunk i/n) → Finalizing`. Stage + chunk progress; streaming ASR shows **live partial text**. **Cancel** always present (cooperative at chunk boundaries, §7.3). Within a single uninterruptible run the spinner is honestly indeterminate — no fake percentage.
- **Result flow:** preview (play + waveform) → explicit *Apply* choices → one undoable command. AI output lands as **new media/clips**, original muted not deleted (§7.7). The user always sees *what will change* before it changes.
- **Size framing (million-scale policy, §7.6):** light models are labeled *"instant on CPU"*; the opt-in heavier models (Turkish ASR ≈244M, VC, music 230M+) show a **calibrated ETA** and their real size before download. No silent upsizing.
- **Backend transparency:** status bar shows e.g. `CPU · Niagara 19M`; a fallback shows `degraded to CPU` (§8.2). On this GPU-less laptop it plainly says *"CPU — no compatible GPU detected"*.

### 10.6 History & undo UX
- **History panel** = a vertical timeline (newest top): each entry has an icon, label (*"Trim clip"*, *"Apply SR"*), and timestamp. The current cursor is highlighted.
- **Click any entry → restore-as-command** (a big jump asks once; small jumps don't). The jump is itself undoable.
- **Undo/redo** buttons + `Ctrl+Z`/`Ctrl+Shift+Z`; after an undo + new edit, a subtle branch indicator shows the redo path was replaced.
- **Confidence:** the panel makes "I can always go back" *visible*, which is what lets users edit boldly. *Clear history* is guarded and never automatic.

### 10.7 States, feedback & motion
**States (every surface designs its empty/loading/error/offline case, not just happy path):**
- *Empty project:* drop zone + 3 actions + recent projects. *Empty track:* "drop audio here".
- *Decoding:* skeleton waveform + inline progress on the clip.
- *AI model missing:* install card. *AI running:* stage/chunk progress + cancel. *AI failed:* inline non-modal card with cause + next action (retry / install / free memory / switch backend) — never a blocking modal for a recoverable error.
- *Offline:* badge; editing fully works; AI install shows "connect to download".
- *Device lost:* toast + automatic reopen with backoff; playback resumes (§9.2).
- *Success:* subtle toast + the new result highlighted + an "undo" hint.

**Feedback latency budget:** any UI action acknowledges in < 100 ms; long work is async with progress; the RT path never animates.

**Motion:** 80–120 ms eased transitions for fader snaps, theme crossfade, and panel slides; meters at 30 Hz; gliding playhead. A **reduced-motion** setting disables non-essential animation. Motion is decoration on the GUI thread only — never on audio.

### 10.8 Accessibility & keyboard-first
- Complete shortcut layer (see §10.12); every primary action reachable without a mouse; visible focus rings; logical tab order.
- `QAccessible` names/roles on custom widgets (the painted timeline exposes clips/lanes as accessible objects with actions).
- Contrast ≥ 4.5:1 in both themes; scalable UI font; **no information by color alone** (A/B uses color + label + position; meters use shape + number).
- Reduced-motion honored; destructive actions confirm; reversible actions don't nag.

### 10.9 The QSS/QML precision point (decision)
**QSS (Qt Style Sheets) styles Qt Widgets only. It does not apply to QML/Qt Quick** (QML uses its own styling/controls). The requirement "entire UI in Qt Widgets/QML styled via QSS" is therefore satisfied cleanly by a **Widgets-only v1**:
- Shell, panels, mixer, dialogs, transport: **Qt Widgets**, styled by a single application-wide QSS.
- **Timeline: a custom `QWidget` painted with `QPainter`** (not QGraphicsView, not QML). Reasons: sample-accurate zoom from whole-project to single-sample, virtualized drawing (only visible clips/lanes), waveform LOD blitting, zero scene-graph overhead, and full control over hit-testing for trims/fades/automation. It inherits theme colors from the same token source.
- **QML is deferred.** If a future surface needs it (e.g. a node graph), it gets a `ThemeBridge` QML singleton exposing the *same* tokens — but it will not be QSS-styled, and that is documented, not hidden. v1 ships no QML, so "entire UI styled via QSS" is literally true.

### 10.10 Theme token pipeline (single source of truth)

```
assets/theme/tokens.dark.json ─┐
                               ├─► ThemeManager ─► generates app.qss ─► qApp->setStyleSheet()
assets/theme/tokens.light.json ┘        │
                                        ├─► QPalette (for native dialogs / fallback)
                                        ├─► TimelinePalette (QPainter colors)
                                        └─► IconTint (SVG recolor)
```

- Tokens: `bg`, `bgAlt`, `panel`, `border`, `text`, `textSecondary`, `accent`, `accentText`, `danger`, `warning`, `ok`, `waveform`, `waveformPeak`, `clipFill`, `automation`, `selection`, radii, spacing, font sizes. **Secondary text is derived from a contrast-checked role, never `QPalette::Mid`** (the user's documented readability rule).
- `ThemeManager::apply(mode)` builds the QSS by substituting tokens into `assets/theme/app.qss.template`, sets `qApp->setStyleSheet`, updates `QPalette`, repaints the timeline, re-tints icons. **One shared apply path** for startup, toggle, and system-change, with memoization to skip redundant re-applies (matches the user's convention).
- Styling uses **object names + dynamic properties**, not per-widget stylesheets:
  ```css
  QWidget#MainWindow { background: @bg; }
  QFrame#Panel { background: @panel; border: 1px solid @border; border-radius: @radius; }
  QPushButton { background: @bgAlt; color: @text; border: 1px solid @border; padding: 6px 12px; }
  QPushButton:hover { border-color: @accent; }
  QPushButton[variant="primary"] { background: @accent; color: @accentText; }
  QSlider#abFader::groove { height: 4px; background: @border; }
  QSlider#abFader::handle { width: 18px; height: 18px; border-radius: 9px; background: @accent; }
  QLabel[role="secondary"] { color: @textSecondary; }
  ```
- **Dark/light toggle:** a single checkable `QToolButton` in the transport bar (sun/moon icon). Modes: `dark`, `light`, `system`. `system` follows `QStyleHints::colorSchemeChanged` (Qt 6.5+). Persisted in settings; applied before the main window shows (no flash).
- **Icons:** monochrome SVGs tinted at runtime to `text`/`accent` via a small `QIconEngine` that recolors `QSvgRenderer` output (no bundled per-theme raster sets). The app ships its own minimal icon set (this is ses, not the KDE-integrated Egoboard; the "inherit system icons" rule is Egoboard-specific and does not apply here).

### 10.11 Key widgets & UX components
Implementation of the UX in §10.1–10.8:
- `MainWindow`: transport bar (play/stop/loop, time display, **AbFader**, A/B solo, theme toggle, backend badge), central `QSplitter` (timeline top, mixer bottom), left track-header column, right dock (AI/History/Models), status bar.
- `TimelineWidget`: ruler (time/timecode/samples), track lanes, clip blocks (waveform + fade triangles + gain handle), automation lanes, playhead, loop region, selection marquee. Gestures per §10.3 map to commands (§6). Painting is virtualized and allocation-free per frame (preallocated buffers, cached waveform LODs). Exposes `QAccessible` objects for clips/lanes.
- `MixerStrip`: gain fader, pan, mute/solo, **A/B routing chip**, FX list, meter (telemetry).
- `AbFader`: the split A/B crossfader (§4.5/§10.4) with mode popover and Copy/Swap/Reset.
- `HistoryPanel`: journal timeline (§10.6), click to restore-as-command, cursor + branch indicator.
- `AiPanel`: capability cards ASR / Enhance / Voice / Music (§10.5); runtime-discovered options (`audiocpp_model_option_*`), run/cancel, stage progress, result preview + Apply choices.
- `ModelManagerPanel`: catalog (package id, **real size**, license, installed?), download/verify/remove, models-root.
- **UX components (shared, theme-aware):** `EmptyState` (drop zone + actions), `InstallCard` (model size/license/download), `ProgressCard` (stage + chunk + cancel), `ResultPreview` (play + waveform + Apply), `Toast` (non-blocking success/error), `CoachMarks` (dismissible first-run tips), `RecoveryBanner` (crash restore). These are the reusable vocabulary that keeps every surface's states consistent (§10.7).

### 10.12 Accessibility & input
**Shortcut map** (configurable; shown in a cheat-sheet overlay on `?`):

| Group | Keys |
|---|---|
| Transport | `Space` play/stop · `Enter` to-start · `L` loop · `,`/`.` nudge playhead |
| Tools | `V` select · `R` razor · `F` fade · `Shift+A` automation |
| Edit | `S` split at playhead · `Del` remove · `Ctrl+Z`/`Ctrl+Shift+Z` undo/redo · `Ctrl+D` duplicate · `Ctrl+A` select all |
| Audition | `A` / `B` / `\` toggle · `Shift+1`/`Shift+2` route track to A/B |
| Track | `M` mute · `Shift+S` solo |
| View | `+`/`−` zoom · `Ctrl+0` fit · `T` theme · `Tab` next panel · `?` cheat sheet |

- Focus handling avoids the "slider steals global shortcuts" trap (clear focus after slider interaction; the user's documented Qt gotcha). Shortcuts work app-wide except when a text field has focus.
- Contrast-checked text in both themes; secondary text never below 4.5:1 (never `QPalette::Mid`). Visible focus rings; logical tab order; `QAccessible` on custom widgets.
- No information by color alone (A/B = color + label + position; meters = shape + number). Reduced-motion setting honored. Scalable UI font.

---

## 11. Local persistence

### 11.1 Project bundle (directory, not a single file)
```
MyProject.ego/
 ├─ project.json            # current state (§5.6), atomic write
 ├─ project.lock            # single-writer (QLockFile)
 ├─ history/
 │   ├─ index.json
 │   └─ objects/<sha256>.json.zst
 ├─ media/                  # imported audio (copy-on-import by default; reference option)
 ├─ renders/                # AI outputs + bounces (also registered as MediaItems)
 ├─ peaks/                  # waveform LOD cache (rebuildable, disposable)
 └─ autosave/               # rotating autosave snapshots (recovery)
```
- **Copy-on-import by default** makes the bundle portable (zip it, move it). A "reference external media" option stores absolute paths + hash for large libraries; missing-media relink by hash (§9.4).
- `peaks/` is a cache: disposable, rebuilt on demand by the decode pool.

### 11.2 Schema versioning & migrations
- `schemaVersion: int`. **Forward-only migrations** (`migrate_1_to_2`, …), never DROP user data. Unknown keys preserved on round-trip. A file with `schemaVersion` newer than the app supports opens read-only (`SchemaTooNew`). Settings schema is versioned the same way; invalid stored values normalize to safe defaults on read (the user's convention).

### 11.3 App state locations (QStandardPaths, XDG-correct)
- Settings: `~/.config/ses/settings.json` (AppConfigLocation). Keys: theme, backend, threads, modelsRoot, recentProjects, autosaveInterval, journalCaps, defaultSRprovider, abMode.
- Models: `~/.local/share/ses/models/` (AppDataLocation) **or** a portable `models/` beside the executable (user-selectable; portable mode for USB/offline kits). Layout mirrors audio.cpp packages: `models/<Family>-GGUF/<file>.gguf` + a local `installed.json` manifest (package id, version, sha256, license, installedAt).
- Cache/logs: `~/.cache/ses/` (peaks shared cache, logs).
- Bundled Silero VAD ships in app resources (`assets/framework/models/silero_vad`), copied to the models root on first run so the ABI path test pattern works without a download.

### 11.4 Model download (the only network path)
- Explicit user action in ModelManager. Prefer audio.cpp's **native C++ model manager** when built with `AUDIOCPP_BUILD_NATIVE_MODEL_MANAGER=ON` (shares the verified downloader, no Python); otherwise a Qt `QNetworkAccessManager` downloader hitting the HF `audio-cpp` package URLs from `model_specs/*.json`.
- Requirements: HTTPS, **sha256 verification** against the spec, resumable (Range), progress + cancel, quarantine on mismatch, license shown *before* download. No other network calls exist; a build flag `ses_OFFLINE_ONLY` compiles the downloader out entirely for air-gapped kits.

### 11.5 Autosave & crash recovery
- Autosave every 30 s (idle-debounced) to `autosave/` (rotating, last 3). On launch, if `autosave/` or the journal is newer than `project.json` (crash detected via a clean-shutdown flag), offer recovery with timestamps. Recovery loads the chosen state as a normal project; nothing is auto-overwritten without consent.

---

## 12. Packaging and deployment

### 12.1 Build system
- CMake ≥ 3.24 + Ninja. Qt6 (Widgets, Svg, Core; Quick reserved unused). FetchContent/vendor: miniaudio (header), soxr, zstd, libsndfile, audio.cpp (C ABI, composite). **CMake 4.x note:** if a vendored project declares `cmake_minimum_required` < 3.5, set `CMAKE_POLICY_VERSION_MINIMUM=3.5` for that subbuild (dev box has CMake 4.5).
- Dev gate (matches the user's standing convention): `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` → build → `ctest --test-dir build --output-on-failure` → `QT_QPA_PLATFORM=offscreen ./build/ses --smoke` exits 0. No CI pipeline; the local gate is the source of truth.
- Portable CPU kernels: `ENGINE_ENABLE_NATIVE_CPU=OFF` for distributed builds (dev box may use native for speed).

### 12.2 Linux (primary dev target)
- **AppImage** via linuxdeployqt: bundle Qt libs + platform plugins (**xcb and wayland**), SVG iconengine/imageformats, `libaudiocpp.so` **with its ggml backend plugins in the same dir** (verified discovery rule), soxr/zstd/sndfile. Desktop entry + icon (the existing `assets/icon.svg`).
- Validate the AppImage headless inside the bundle (`--smoke`, forced stderr logging) — desktop/icon/metadata, platform plugins, bundled-lib count (the user's AppImage validation convention).
- CachyOS/Arch dev: native `pacman` Qt6 is used for dev builds; the AppImage bundles its own Qt for distribution. Optional AUR package later.
- JACK users: miniaudio JACK backend works when the AppImage can see `libjack` (documented; not bundled to avoid conflicts).

### 12.3 Windows / macOS
- **Windows:** MSVC 2022, `windeployqt`, NSIS installer (or MSIX). Ship `audiocpp.dll` + ggml backend DLLs together; CUDA build adds the matching `cudart` archive contents beside them (verified). Vulkan portable package available for CPUs without AVX/AVX2 (verified) — relevant to old machines.
- **macOS:** `macdeployqt`, Metal backend (embedded shader lib), `.app` + codesign + notarize; arm64 (x64 optional). CPU-only build via `ENGINE_ENABLE_METAL=OFF` (verified flags).
- Models are **never** bundled in the installer (size/license); first-run ModelManager offers the default catalog. The app is fully usable for editing/playback with zero models.

### 12.4 Licensing compliance (must ship)
- **Qt:** LGPLv3 — dynamic linking, provide license text + relink ability (AppImage/Windows DLLs satisfy this).
- **audio.cpp:** Apache-2.0 — include LICENSE + NOTICE. **ggml:** MIT. **miniaudio:** MIT-0/PD. **soxr:** LGPLv2.1 (dynamic). **zstd:** BSD. **libsndfile:** LGPL (dynamic).
- **Model weights keep their own licenses** (verified `docs/model_licenses.md`): show per-model license in ModelManager and in About. Specific obligations:
  - `stable_audio`: Stability AI Community License — free under $1M revenue, **register commercial use + display "Powered by Stability AI"**; Gemma text-encoder terms. Offer ACE-Step (MIT) as the permissive default for users who want no conditions.
  - `seed_vc`: **GPL-3.0 (copyleft)** — linking it into a distributed binary triggers copyleft obligations. Default to `tone_color_vc` (MIT) / `rvc` (MIT base); make SeedVC an explicit opt-in download with a license warning, or keep it out of commercial builds.
  - `sense_asr`: FunASR Model License — credit source/authors, keep model names.
  - `rvc` packaged voices: unclear license → ship the base only; users supply their own reference voices.
  - Avoid non-commercial families (e.g. `xtts_v2` CPML, `f5_tts` CC-BY-NC) as defaults.
- A `THIRD_PARTY_LICENSES.md` + in-app "Licenses" dialog enumerate everything.

---

## 13. Testing and verification strategy

| Layer | Tests | Gate |
|---|---|---|
| core | JSON round-trip (incl. unknown-key preservation), migrations, command undo/redo exactness, random command sequences asserting model==journal==save triple, fade/automation golden math, split/trim invariants | `tst_core` (QTEST_GUILESS_MAIN, QTemporaryDir) |
| engine | SPSC ring correctness under **ThreadSanitizer**, RenderPlan publish/reclaim grace (no use-after-free), **RT allocation guard** (hook malloc in the callback region; assert zero allocations), golden offline render of a fixture project → PCM sha256, A/B equal-power sum ≈ constant, xrun/underrun paths | `tst_engine` |
| inference | ABI contract test mirroring audio.cpp's Silero path test (bundled model, no download): status codes, borrowed-output copy, out-of-order frees, offline vs streaming mode refusal; chunking/overlap-add seam test; cancel-between-chunks test; backend probe + CPU fallback test; model tests **skip (exit 77) when weights absent** | `tst_inference` |
| ui | offscreen smoke (`--smoke` exits 0), theme toggle applies to all widgets + timeline + icons, undo/redo via UI actions, history restore | `tst_ui` (QT_QPA_PLATFORM=offscreen) |
| ux journeys | time-to-first-sound < 10 s on a fixture; a scripted drag yields exactly one undo step; A/B reachable in one key; AI result undoable in one step; install card shown when model absent (never a dead button); crash → recovery banner; theme toggle < 100 ms; contrast ≥ 4.5:1 computed from tokens | `tst_ui` journey slots + token contrast check |
| integration | `--render project.ego out.wav` offline bounce == golden; `--smoke` opens a fixture, plays 2 s headless, exits 0 | ctest |

Verification gate before any "done": build succeeds, full ctest green, offscreen `--smoke` exits 0. Existing assertions are never weakened to pass.

---

## 14. Implementation plan (phases, acceptance criteria)

Each phase is independently shippable and test-gated. Estimates are rough senior-dev effort on the dev box.

### P0 — Skeleton, theme, settings (≈ 2–3 days)
- CMake superbuild, Qt6 Widgets app, module libs stubbed, `--smoke`.
- ThemeManager + dark/light tokens + QSS generation + toggle + persistence; icon tinting.
- Settings schema (versioned, normalizing), QStandardPaths locations, single-instance.
- **Accept:** builds; offscreen `--smoke` exits 0; toggle restyles shell + persists; ctest green.

### P1 — Audio engine + transport + playback (≈ 4–6 days)
- miniaudio device wrapper, RtContext, SPSC rings, RenderPlanSlot + reclaim, block mixer (gain/pan/smoothers), transport commands, telemetry + meters.
- Decode pool: decode wav/flac/mp3, MediaCache, waveform peak LODs, single-clip playback.
- **Accept:** play a WAV with sample-accurate start/stop/seek; RT allocation guard passes; TSan clean; meters move; device-unplug → silence + recover.

### P2 — NLE model + timeline + commands/undo (≈ 6–9 days)
- core entities + JSON, ProjectModel, QUndoStack + command catalog (move/trim/slip/split/fades/gain), drag coalescing.
- TimelineWidget (QPainter): ruler, lanes, clips, waveform, fade handles, trim/split interactions, zoom/scroll virtualization, playhead.
- Multi-track mixing, mute/solo.
- **Accept:** import → arrange → trim/split/fade → undo/redo exact; golden render matches; random-command triple-consistency test green.

### P3 — Persistence + snapshot history (≈ 4–5 days)
- ProjectStore atomic save/load, bundle layout, migrations, lock, media copy-on-import + relink-by-hash.
- SnapshotJournal (content-addressed zstd objects + index), cursor undo/redo, HistoryPanel restore-as-command, autosave + crash recovery.
- **Accept:** save/close/reopen identical; crash → recovery offer; history jump undoable; corrupt project.json → journal fallback; caps/GC work.

### P4 — A/B + automation + mixer polish (≈ 4–6 days)
- Dual-bus rendering, AbFader (crossfade + split), A/B solo, routing commands, bus chains.
- Automation lanes (model + RT eval + editor UI), full mixer strips, master limiter, offline bounce/export WAV/FLAC.
- **Accept:** A/B null-test coherent; equal-power constant loudness; automation sample-exact in bounce; export == playback golden.

### P5 — Inference (≈ 8–12 days, the long pole)
- ABI RAII wrapper + error/log bridge + version gate; ModelCatalog/ModelStore + residency LRU; scheduler + job queue + cancel + progress; backend probe + fallback.
- ModelManager (download/verify/license) + bundled Silero.
- **Light AI first (million-scale, §7.6):** bundled Silero VAD → **English ASR** (Niagara 19M, VAD-windowed, transcript + markers/splits) → **TTS** (sanoTTS `tr` 1.56M + tiny-voca 4.5M / PocketTTS 2M) → **enhancement/SR** (GTCRN / FlashSR; LavaSR port tracked separately). **Opt-in heavier capabilities** behind explicit size/license cards: Turkish ASR (SenseVoice ≈244M), voice conversion (MeanVC2 / OpenVoiceV2), music (Magenta Small 230M, bounded segments).
- **Accept:** each capability runs end-to-end on CPU on the dev box, result lands as undoable clips/media, cancel works between chunks, CPU fallback verified, model-missing install card works, hash verification enforced. LavaSR explicitly labeled experimental until parity-validated.

### P6 — Packaging + hardening (≈ 4–6 days)
- AppImage build + validation; Windows/macOS packaging scripts; THIRD_PARTY_LICENSES + About; perf pass (timeline paint, plan rebuild cost, inference thread caps); error-path audit; docs/ROADMAP.
- **Accept:** validated AppImage runs `--smoke` inside the bundle on a clean machine; licenses complete; no RT allocations; xrun-free 32-track playback on the dev box at 256 frames.

**Critical path / risks:** P5 dominates. Biggest risks: (1) audio.cpp source build friction with GCC 16 / CMake 4.5 (mitigate: pin a release tag, use prebuilt lib archives for dev, composite model set); (2) LavaSR port effort (mitigate: ship FlashSR/MossFormer2 behind the same interface, track LavaSR as a separate validated work item); (3) CPU music-gen speed on AVX-only hardware (mitigate: honest ETA UI, lightest model Magenta Small 230M, bounded segments); (4) RAM pressure from the opt-in heavy music model (mitigate: light/heavy residency split + footprint estimate + refuse-with-message); (5) **million-scale gaps for Turkish ASR / voice conversion / music** (mitigate: explicit opt-in with real verified sizes, §7.6.1 — never silent upsizing).

**UX acceptance gates (cross-phase, from §1.4):** time-to-first-sound < 10 s (P1/P2); a drag commits exactly one undo step (P2); A/B compare in one keypress, click-free (P4); theme toggle < 100 ms no flash (P0); every AI result undoable in one step + install card never a dead button + cancel always present (P5); crash → recovery offer (P3); contrast ≥ 4.5:1 and full keyboard reach (P6). These are tested, not aspirational (§13).

---

## 15. Open decisions (defaults chosen, flag to change)
1. **UI toolkit:** Widgets-only v1 (QSS-true). QML deferred. — *Default locked unless you want a QML timeline.*
2. **Model size policy (user constraint):** million-parameter-first defaults (§7.6). Turkish ASR, voice conversion, and music have **no million-scale model** → opt-in heavier with real sizes shown (§7.6.1). — *Confirm: keep these three as opt-in, or drop any from v1?*
3. **SR default:** FlashSR / LavaSR-enhancer (light) now; MossFormer2-SR as quality alt; LavaSR family a tracked port. — *If you want LavaSR-only at launch, P5 grows by the port effort.*
4. **Music default:** Magenta RealTime 2 **Small (230M, CC-BY-4.0)** — the lightest available; Stable Audio / ACE-Step opt-in heavier. — *Or drop music gen from v1 entirely.*
5. **Turkish TTS license:** sanoTTS `tr` **1.56M is GPL-3.0 (copyleft)** — the only million-scale Turkish voice. — *Accept GPL for the tiny Turkish voice, or prefer a larger permissive/conditional voice (KittenTTS2 / Supertonic)?*
6. **Voice-conversion default:** MeanVC2 (Apache-2.0) / OpenVoiceV2 (MIT), opt-in; SeedVC (GPL) opt-in. — *Not million-scale; keep or drop.*
7. **Project format:** directory bundle `.ego/`. — *Single-file zip variant is a later export option.*
8. **Sample rate:** fixed 48 kHz project rate. — *44.1 kHz support is a plan-builder resample path, phase 2.*

---

*End of design baseline. Next step on request: expand §14 into a bite-sized, file-path-and-code-level task plan (writing-plans format), or begin P0.*
