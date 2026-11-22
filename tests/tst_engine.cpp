#include "core/media/MediaProbe.h"
#include "engine/Engine.h"
#include "engine/dsp/Resampler.h"
#include "engine/io/AudioDevice.h"
#include "engine/io/Decode.h"
#include "engine/BlockMixer.h"
#include "engine/SpscRing.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <sndfile.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

using namespace ses::engine;

namespace {

// Sine fixture in a caller-chosen container (OGG/Vorbis, WAV, …). Returns
// false when this libsndfile build lacks the encoder — tests then skip honestly.
bool writeSineFile(const QString& path, double seconds, int sampleRate, int format)
{
    SF_INFO info{};
    info.samplerate = sampleRate;
    info.channels = 2;
    info.format = format;
    SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
    if (!file)
        return false;
    constexpr int kChunk = 512;
    std::vector<float> buf(std::size_t(kChunk) * 2);
    const int64_t total = int64_t(seconds * sampleRate);
    for (int64_t i = 0; i < total; i += kChunk) {
        const int n = int(std::min<int64_t>(kChunk, total - i));
        for (int j = 0; j < n; ++j) {
            const float v = 0.5f * std::sin(2.0 * M_PI * 440.0 * double(i + j) / sampleRate);
            buf[std::size_t(j) * 2] = v;
            buf[std::size_t(j) * 2 + 1] = v;
        }
        sf_writef_float(file, buf.data(), n);
    }
    sf_close(file);
    return true;
}

bool writeTestOgg(const QString& path, double seconds, int sampleRate)
{
    return writeSineFile(path, seconds, sampleRate, SF_FORMAT_OGG | SF_FORMAT_VORBIS);
}

// seconds of 440 Hz stereo sine (0.5 amplitude) at sampleRate.
std::vector<float> makeTone(int sampleRate, double seconds)
{
    const std::size_t frames = std::size_t(seconds * sampleRate);
    std::vector<float> pcm(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const float v = float(0.5 * std::sin(2.0 * M_PI * 440.0 * double(i) / sampleRate));
        pcm[i * 2] = v;
        pcm[i * 2 + 1] = v;
    }
    return pcm;
}

// Frequency from sign changes on channel 0 over the whole buffer.
double toneHertz(const std::vector<float>& pcm, int sampleRate)
{
    const std::size_t frames = pcm.size() / 2;
    if (frames < 2)
        return 0.0;
    int crossings = 0;
    for (std::size_t i = 1; i < frames; ++i)
        if ((pcm[(i - 1) * 2] < 0.0f) != (pcm[i * 2] < 0.0f))
            ++crossings;
    return crossings / 2.0 / (double(frames) / double(sampleRate));
}

// Converts a whole buffer through the streaming API (push + flush), mirroring
// the offline import path. Empty result = the resampler got stuck.
std::vector<float> convertAll(Resampler& rs, const std::vector<float>& in)
{
    std::vector<float> out;
    std::vector<float> scratch(in.size() + 1024);
    const std::size_t frames = in.size() / 2;
    std::size_t done = 0;
    while (done < frames) {
        std::size_t consumed = 0;
        const std::size_t made = rs.process(in.data() + done * 2, frames - done,
                                            scratch.data(), scratch.size() / 2,
                                            consumed);
        out.insert(out.end(), scratch.begin(),
                   scratch.begin() + std::ptrdiff_t(made * 2));
        done += consumed;
        if (consumed == 0 && made == 0)
            return {};
    }
    for (int guard = 0; guard < 64; ++guard) {
        std::size_t consumed = 0;
        const std::size_t made = rs.process(nullptr, 0, scratch.data(),
                                            scratch.size() / 2, consumed);
        if (made == 0)
            break;
        out.insert(out.end(), scratch.begin(),
                   scratch.begin() + std::ptrdiff_t(made * 2));
    }
    return out;
}

} // namespace

class TestEngineCore : public QObject {
    Q_OBJECT
private:
    // Drains telemetry until pred() sees a matching sample or the deadline
    // passes. Returns the last telemetry observed (or the initial value).
    static Telemetry waitFor(AudioDevice& dev, Telemetry start,
                             const std::function<bool(const Telemetry&)>& pred)
    {
        Telemetry t = start;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            while (dev.nextTelemetry(t)) {
                if (pred(t))
                    return t;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return t;
    }

    // Same idea for the Engine facade: polls state() until pred() matches or
    // the deadline passes.
    static TransportState waitForState(DeviceEngine& engine, int sleepMs,
                                        const std::function<bool(const TransportState&)>& pred)
    {
        TransportState st;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            st = engine.state();
            if (pred(st))
                return st;
            if (sleepMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
        return st;
    }

private slots:
    void spscBoundsAndOrder()
    {
        SpscRing<int, 2> ring;
        int value = 0;
        QVERIFY(!ring.pop(value));
        QVERIFY(ring.push(11));
        QVERIFY(ring.push(22));
        QVERIFY(!ring.push(33));
        QVERIFY(ring.pop(value));
        QCOMPARE(value, 11);
        QVERIFY(ring.push(33));
        QVERIFY(ring.pop(value));
        QCOMPARE(value, 22);
        QVERIFY(ring.pop(value));
        QCOMPARE(value, 33);
        QVERIFY(!ring.pop(value));
    }

    void blockMixerSchedulesClips()
    {
        const float source[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };
        BlockMixer mixer;
        QVERIFY(mixer.setClip(0, { source, 3, 2, 2.0f, 0.0f, false }));
        float output[10]{};
        mixer.render(0, 5, output);
        const float expected[] = { 0, 0, 0, 0, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 1.2f };
        for (int i = 0; i < 10; ++i)
            QVERIFY(std::abs(output[i] - expected[i]) < 1e-6f);
    }

    void blockMixerAddsPanAndMute()
    {
        const float source[] = { 0.5f, 0.25f, 0.5f, 0.25f };
        BlockMixer mixer;
        QVERIFY(mixer.setClip(0, { source, 2, 0, 1.0f, -1.0f, false }));
        QVERIFY(mixer.setClip(1, { source, 2, 0, 1.0f, 0.0f, true }));
        float output[4]{};
        mixer.render(0, 2, output);
        QCOMPARE(output[0], 0.5f);
        QCOMPARE(output[1], 0.0f);
        QCOMPARE(output[2], 0.5f);
        QCOMPARE(output[3], 0.0f);
    }

    void blockMixerRejectsInvalidClip()
    {
        BlockMixer mixer;
        QVERIFY(!mixer.setClip(BlockMixer::MaxClips, {}));
        QVERIFY(!mixer.setClip(0, { nullptr, 1, 0, 1.0f, 0.0f, false }));
        const float sample = 0.0f;
        QVERIFY(!mixer.setClip(0, { &sample,
                                    std::numeric_limits<std::size_t>::max() / 2 + 1,
                                    0, 1.0f, 0.0f, false }));
    }

    void audioDeviceNullBackendOpenStartStop()
    {
        AudioDevice dev;
        QVERIFY(dev.openOnBackend(ma_backend_null, 48000, 256));
        QCOMPARE(dev.actualSampleRate(), 48000);
        QVERIFY(!dev.masterSrcActive()); // matching rates — no SRC chain
        QVERIFY(dev.start());
        DeviceEvent event{};
        QVERIFY(dev.nextDeviceEvent(event));
        QCOMPARE(int(event.kind), int(DeviceEvent::Started));
        dev.stop();
        DeviceEvent stopped{};
        QVERIFY(dev.nextDeviceEvent(stopped));
        QCOMPARE(int(stopped.kind), int(DeviceEvent::Stopped));
        QVERIFY(!dev.playing());
        dev.close();
        QVERIFY(!dev.start()); // closed device refuses to start
    }

    void audioDeviceTransportCommandsOnNullBackend()
    {
        AudioDevice dev;
        QVERIFY(dev.openOnBackend(ma_backend_null, 48000, 256));
        QVERIFY(dev.start());

        // Silence while stopped: playhead never leaves zero.
        Telemetry t{};
        t = waitFor(dev, t, [](const Telemetry& x) { return x.playhead == 0; });
        QVERIFY(dev.sendCommand({RtCommand::Stop}));

        // Play → playhead advances past one period.
        QVERIFY(dev.sendCommand({RtCommand::Play}));
        t = waitFor(dev, t, [](const Telemetry& x) { return x.playhead >= 256; });

        // Seek to 1000 → the next block reports exactly that head.
        QVERIFY(dev.sendCommand({RtCommand::Seek, 1000, 0, 0.0f}));
        t = waitFor(dev, t, [](const Telemetry& x) { return x.playhead == 1000; });

        // Loop [1000, 1128) wraps: the head stays inside the loop region.
        QVERIFY(dev.sendCommand({RtCommand::SetLoop, 1000, 1128, 1.0f}));
        t = waitFor(dev, t, [](const Telemetry& x) {
            return x.playhead >= 1000 && x.playhead < 1128 && x.playhead != 1000;
        });
        QVERIFY(t.playhead < 1128);

        QVERIFY(dev.sendCommand({RtCommand::Stop}));
        dev.stop();
        dev.close();
    }

    void audioDeviceMixerRendersThroughCallback()
    {
        // One clip through the off-RT mixer handle; the callback's master peak
        // must reflect the mixed samples while playing.
        static const float source[] = { 0.5f, 0.5f, 0.5f, 0.5f };
        AudioDevice dev;
        QVERIFY(dev.openOnBackend(ma_backend_null, 48000, 256));
        BlockMixer::ClipView clip;
        clip.samples = source;
        clip.frames = 2;
        clip.timelineStart = 0;
        QVERIFY(dev.mixer().setClip(0, clip));
        QVERIFY(dev.start());
        QVERIFY(dev.sendCommand({RtCommand::Play}));
        Telemetry t{};
        t = waitFor(dev, t, [](const Telemetry& x) { return x.masterPeak > 0.4f; });
        QVERIFY(t.masterPeak <= 1.0f);
        dev.stop();
        dev.close();
    }

    void deviceEngineFacadeIsInertWhenUnavailable()
    {
        // No device open → every call is an honest no-op and state() stays at
        // the transport origin (the UI falls back to preview mode).
        DeviceEngine engine;
        QVERIFY(!engine.available());
        QCOMPARE(engine.actualSampleRate(), 0);
        engine.play();
        engine.stop();
        engine.seek(1000);
        engine.setLoop(true, 0, 100);
        engine.setAbFader(0.25);
        const TransportState st = engine.state();
        QVERIFY(!st.playing);
        QVERIFY(!st.loop);
        QCOMPARE(st.playhead, int64_t(0));
        Telemetry t{};
        QVERIFY(!engine.nextTelemetry(t));
        DeviceEvent e{};
        QVERIFY(!engine.nextDeviceEvent(e));
        engine.close(); // no-op
        QVERIFY(!engine.available());
    }

    void deviceEngineFacadeTransportOnNullBackend()
    {
        DeviceEngine engine;
        QVERIFY(engine.openOnNullBackend(48000, 256));
        QVERIFY(engine.available());
        QCOMPARE(engine.actualSampleRate(), 48000);

        TransportState st = engine.state();
        QVERIFY(!st.playing);
        QCOMPARE(st.playhead, int64_t(0));
        QCOMPARE(st.loopStart, int64_t(0));
        QCOMPARE(st.loopEnd, int64_t(0));

        // play() lazily starts the device pump (an idle app holds no running
        // device) and the Play command lands at the next block boundary.
        engine.play();
        st = waitForState(engine, 1, [](const TransportState& s) {
            return s.playing && s.playhead >= 256;
        });
        QVERIFY(st.playing);
        QVERIFY(st.playhead >= 256);
        QVERIFY(engine.start()); // explicit start is idempotent

        DeviceEvent event{};
        QVERIFY(engine.nextDeviceEvent(event)); // the pump announced itself
        QCOMPARE(int(event.kind), int(DeviceEvent::Started));

        // Stop parks the playhead — then the Seek is sample-exact and stable.
        engine.stop();
        st = waitForState(engine, 1, [](const TransportState& s) { return !s.playing; });
        QVERIFY(!st.playing);
        engine.seek(1000);
        st = waitForState(engine, 1, [](const TransportState& s) { return s.playhead == 1000; });
        QCOMPARE(st.playhead, int64_t(1000));
        QVERIFY(!st.playing);

        // Loop [1000, 1400) wraps at block boundaries: the head cycles
        // 1000 → 1256 → 1000 … and never leaves the region.
        engine.setLoop(true, 1000, 1400);
        st = engine.state();
        QVERIFY(st.loop);
        QCOMPARE(st.loopStart, int64_t(1000));
        QCOMPARE(st.loopEnd, int64_t(1400));
        engine.play();
        st = waitForState(engine, 1, [](const TransportState& s) {
            return s.playhead >= 1000 && s.playhead < 1400 && s.playhead != 1000;
        });
        QCOMPARE(st.playhead, int64_t(1256)); // inside the loop region, past the start
        st = waitForState(engine, 1, [](const TransportState& s) { return s.playhead == 1000; });
        QCOMPARE(st.playhead, int64_t(1000)); // wrapped back — not 1512

        // Telemetry flows through the facade while the pump runs (the ring
        // keeps the freshest head: 1000 or 1256 inside the loop region).
        Telemetry t{};
        bool gotTelemetry = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            while (engine.nextTelemetry(t))
                gotTelemetry = true;
            if (gotTelemetry)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        QVERIFY(gotTelemetry);
        QVERIFY(t.playhead >= 1000 && t.playhead < 1400);

        // Loop off → the playhead escapes the region again.
        engine.setLoop(false, 1000, 1400);
        QVERIFY(!engine.state().loop);
        st = waitForState(engine, 1, [](const TransportState& s) { return s.playhead >= 1400; });
        QVERIFY(st.playhead >= 1400);

        engine.stop();
        st = waitForState(engine, 1, [](const TransportState& s) { return !s.playing; });
        QVERIFY(!st.playing);
        engine.close();
        QVERIFY(!engine.available());
    }

    void resamplerPassesThroughWhenRatesMatch()
    {
        Resampler rs;
        QVERIFY(rs.init(48000, 48000, 2, Resampler::Quality::Offline));
        QVERIFY(!rs.active());
        const std::vector<float> in = makeTone(48000, 0.01);
        const std::vector<float> out = convertAll(rs, in);
        QCOMPARE(out.size(), in.size());
        for (std::size_t i = 0; i < in.size(); ++i)
            QCOMPARE(out[i], in[i]); // exact — no soxr instance, zero latency
    }

    void resamplerKeepsPitchAcrossRates()
    {
        const int from[] = { 44100, 48000, 48000 };
        const int to[] = { 48000, 44100, 22050 }; // import up, device-style down, TTS rate (§3.1)
        for (int c = 0; c < 3; ++c) {
            Resampler rs;
            QVERIFY(rs.init(from[c], to[c], 2, Resampler::Quality::Offline));
            QVERIFY(rs.active());
            const std::vector<float> in = makeTone(from[c], 0.5);
            const std::vector<float> out = convertAll(rs, in);
            QVERIFY(!out.empty());

            // 0.5 s of audio comes out as 0.5 s at the new rate (± flush tail).
            const double frames = double(out.size() / 2);
            const double expected = 0.5 * to[c];
            QVERIFY2(std::abs(frames - expected) <= 16.0,
                     qPrintable(QStringLiteral("%1→%2: %3 frames, expected %4")
                                    .arg(from[c])
                                    .arg(to[c])
                                    .arg(frames)
                                    .arg(expected)));

            // The tone is still 440 Hz and its amplitude survives.
            const double hz = toneHertz(out, to[c]);
            QVERIFY2(std::abs(hz - 440.0) < 8.0,
                     qPrintable(QStringLiteral("%1→%2: %3 Hz")
                                    .arg(from[c])
                                    .arg(to[c])
                                    .arg(hz)));
            float peak = 0.0f;
            for (float v : out)
                peak = std::max(peak, std::abs(v));
            QVERIFY(peak > 0.4f);
            QVERIFY(peak < 0.6f);
        }
    }

    void resamplerResetDropsStaleTail()
    {
        Resampler rs;
        QVERIFY(rs.init(44100, 48000, 2, Resampler::Quality::Realtime));
        // Loud DC into a tiny output window: frames stay queued inside soxr.
        const std::vector<float> dc(4410 * 2, 0.5f);
        std::vector<float> small(200, 0.0f);
        std::size_t consumed = 0;
        rs.process(dc.data(), 4410, small.data(), 100, consumed);
        rs.reset(); // the §3.3 seek behavior: the old tail must not stay audible

        std::vector<float> tail(8192 * 2, 0.0f);
        std::size_t flushed = 0;
        for (int i = 0; i < 8 && flushed == 0; ++i) {
            std::size_t eaten = 0;
            flushed = rs.process(nullptr, 0, tail.data(), tail.size() / 2, eaten);
        }
        float peak = 0.0f;
        for (std::size_t i = 0; i < flushed * 2; ++i)
            peak = std::max(peak, std::abs(tail[i]));
        QVERIFY2(peak < 0.05f,
                 "reset must drop the queued tail — a stale DC tail would read 0.5");
    }

    void audioDeviceMasterSrcEngagesOnRateMismatch()
    {
        // Timeline at the 48 kHz project rate, device at 44.1 kHz: the master
        // chain is mixer.render → soxr → device out (ROADMAP §3.3), and the
        // playhead advances in project frames (~256 × 48000/44100 ≈ 280 per
        // device block), not in device frames.
        static const std::vector<float> source(48000 * 2 * 2, 0.5f); // 2 s of 0.5
        AudioDevice dev;
        QVERIFY(dev.openOnBackend(ma_backend_null, 48000, 256, 44100));
        QCOMPARE(dev.actualSampleRate(), 44100);
        QVERIFY(dev.masterSrcActive());
        QVERIFY(dev.mixer().setClip(0, { source.data(), source.size() / 2, 0, 1.0f, 0.0f, false }));
        QVERIFY(dev.start());
        QVERIFY(dev.sendCommand({RtCommand::Play}));

        std::vector<int64_t> heads;
        float peak = 0.0f;
        Telemetry t{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline && t.playhead < 48000) {
            while (dev.nextTelemetry(t)) {
                heads.push_back(t.playhead);
                peak = std::max(peak, t.masterPeak);
                if (t.playhead >= 48000)
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        QVERIFY(t.playhead >= 48000); // one second of project time elapsed
        QVERIFY(peak > 0.4f);         // the resampled chain carries the clip
        QCOMPARE(t.underruns, 0u);    // every device block was filled

        // Median per-block advance across the run (dropped telemetry samples
        // only produce multiples of the true delta and cannot skew the median).
        std::vector<int64_t> deltas;
        for (std::size_t i = 1; i < heads.size(); ++i)
            if (heads[i] > heads[i - 1])
                deltas.push_back(heads[i] - heads[i - 1]);
        QVERIFY(deltas.size() >= 8);
        std::nth_element(deltas.begin(), deltas.begin() + std::ptrdiff_t(deltas.size() / 2),
                         deltas.end());
        const int64_t median = deltas[deltas.size() / 2];
        QVERIFY2(median >= 276 && median <= 284,
                 qPrintable(QStringLiteral("median block advance %1 project frames — "
                                           "expected ≈280 (device frames would read 256)")
                                .arg(median)));

        dev.stop();
        dev.close();
    }

    void blockMixerHandlesExtremeTimelineAndNonFiniteValues()
    {
        const float source[] = { 1.0f, std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity(), -1.0f };
        BlockMixer mixer;
        QVERIFY(mixer.setClip(0, { source, 2, std::numeric_limits<long long>::max(),
                                   std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity(), false }));
        float output[4] = { 9, 9, 9, 9 };
        mixer.render(std::numeric_limits<long long>::min(), 2, output);
        QCOMPARE(output[0], 0.0f);
        QCOMPARE(output[1], 0.0f);
        mixer.render(std::numeric_limits<long long>::max(), 2, output);
        QCOMPARE(output[0], 1.0f); // invalid gain/pan are made safe
        QCOMPARE(output[1], 0.0f); // non-finite source samples are silence
        QCOMPARE(output[2], 0.0f);
        QCOMPARE(output[3], -1.0f);

        float sentinel = 7.0f;
        mixer.render(0, std::numeric_limits<std::size_t>::max() / 2 + 1, &sentinel);
        QCOMPARE(sentinel, 7.0f); // reject output-size overflow without writing
    }

    void decodeWavToProjectRateWithPeakLods()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/tone.wav");
        QVERIFY(ses::core::writeTestWav(wav, 1.0, 48000, 2));

        DecodedAudio out;
        QVERIFY2(decodeFile(wav, out), qPrintable(out.error));
        QVERIFY(out.error.isEmpty());
        QCOMPARE(out.sampleRate, 48000);
        QCOMPARE(out.channels, 2);
        QCOMPARE(out.frames, int64_t(48000));
        QCOMPARE(out.pcm.size(), std::size_t(96000));
        float peak = 0.0f;
        for (float v : out.pcm) {
            QVERIFY(std::isfinite(v));
            peak = std::max(peak, std::abs(v));
        }
        QVERIFY(peak > 0.1f);

        // Peak LODs are folded in the same read pass (§2.7); a ground-truth
        // fold of the decoded PCM must match bucket for bucket.
        QVERIFY(out.peaks.valid());
        const ses::core::PeakLevel& level = out.peaks.levels.first();
        QCOMPARE(level.samplesPerBucket, ses::core::PeakPyramid::kBaseSpan);
        const int64_t buckets = (out.frames + level.samplesPerBucket - 1) / level.samplesPerBucket;
        QCOMPARE(level.min.size(), int(buckets));
        QVector<float> gmin(int(buckets), 1.0f), gmax(int(buckets), -1.0f);
        for (int64_t i = 0; i < out.frames; ++i) {
            float m = (out.pcm[std::size_t(i) * 2] + out.pcm[std::size_t(i) * 2 + 1]) * 0.5f;
            if (!std::isfinite(m))
                m = 0.0f;
            m = std::clamp(m, -1.0f, 1.0f);
            const int b = int(i / level.samplesPerBucket);
            gmin[b] = std::min(gmin[b], m);
            gmax[b] = std::max(gmax[b], m);
        }
        for (int b = 0; b < gmin.size(); ++b) {
            QCOMPARE(level.min[b], gmin[b]);
            QCOMPARE(level.max[b], gmax[b]);
        }
    }

    void decodeRejectsGarbageHonestly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString junk = dir.path() + QStringLiteral("/junk.flac");
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not audio at all");
        f.close();

        DecodedAudio out;
        QVERIFY(!decodeFile(junk, out));
        QVERIFY(!out.error.isEmpty());
        QVERIFY(out.pcm.empty());
        QCOMPARE(out.frames, int64_t(0));
    }

    void decodeOggFallsBackToSndfileAndResamples()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ogg = dir.path() + QStringLiteral("/tone.ogg");
        if (!writeTestOgg(ogg, 0.5, 44100))
            QSKIP("this libsndfile build has no OGG/Vorbis encoder — external .ogg files still decode");

        DecodedAudio out;
        QVERIFY2(decodeFile(ogg, out), qPrintable(out.error));
        QCOMPARE(out.sampleRate, 48000); // 44.1 kHz source → project rate
        QCOMPARE(out.channels, 2);
        // 0.5 s at 44.1 kHz → 24000 frames at 48 kHz (± resampler tail).
        QVERIFY(std::abs(out.frames - 24000) <= 32);
        float peak = 0.0f;
        for (float v : out.pcm) {
            QVERIFY(std::isfinite(v));
            peak = std::max(peak, std::abs(v));
        }
        QVERIFY(peak > 0.1f);
        QVERIFY(out.peaks.valid());
        // 44.1→48 kHz import keeps the pitch (soxr VHQ, §2.8).
        QVERIFY(std::abs(toneHertz(out.pcm, 48000) - 440.0) < 8.0);
    }

    void decodeWavImportResamplesToProjectRate()
    {
        // 44.1 kHz WAV through the ma_decoder route: decoded at the native
        // rate, imported through soxr VHQ (ROADMAP §2.8/§3.2).
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/tone44.wav");
        QVERIFY(writeSineFile(wav, 0.5, 44100, SF_FORMAT_WAV | SF_FORMAT_PCM_16));

        DecodedAudio out;
        QVERIFY2(decodeFile(wav, out), qPrintable(out.error));
        QCOMPARE(out.sampleRate, 48000);
        QCOMPARE(out.channels, 2);
        QVERIFY(std::abs(out.frames - 24000) <= 16); // 0.5 s at 48 kHz (± flush tail)
        QVERIFY(std::abs(toneHertz(out.pcm, 48000) - 440.0) < 8.0);
        QVERIFY(out.peaks.valid());
    }

    void probeReportsFileMetadata()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/p.wav");
        QVERIFY(ses::core::writeTestWav(wav, 1.0, 48000, 2));
        int rate = 0, channels = 0;
        int64_t frames = 0;
        QString error;
        QVERIFY(probeFile(wav, rate, channels, frames, error));
        QCOMPARE(rate, 48000);
        QCOMPARE(channels, 2);
        QCOMPARE(frames, int64_t(48000));

        const QString junk = dir.path() + QStringLiteral("/p.bin");
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("garbage");
        f.close();
        QVERIFY(!probeFile(junk, rate, channels, frames, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(frames, int64_t(0));
    }

    void decodePoolFillsMediaCache()
    {
        ses::core::MediaCache cache;
        DecodePool pool(&cache);
        QSignalSpy readySpy(&pool, &DecodePool::ready);
        QSignalSpy failSpy(&pool, &DecodePool::failed);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/pool.wav");
        QVERIFY(ses::core::writeTestWav(wav, 1.0, 48000, 2));

        pool.request(wav);
        QVERIFY(readySpy.wait(10000));
        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(readySpy.first().at(0).toString(), wav);
        QCOMPARE(cache.size(), 1);

        const auto buffer = cache.find(wav);
        QVERIFY(buffer);
        QCOMPARE(buffer->frames, int64_t(48000));
        QCOMPARE(buffer->sampleRate, 48000);
        QVERIFY(buffer->peaks.valid());

        // A cached path answers immediately — no second decode.
        pool.request(wav);
        QCOMPARE(readySpy.count(), 2);

        // Failures are honest and leave the cache clean.
        const QString junk = dir.path() + QStringLiteral("/pool.flac");
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not audio");
        f.close();
        pool.request(junk);
        QVERIFY(failSpy.wait(10000));
        QCOMPARE(failSpy.count(), 1);
        QVERIFY(!failSpy.first().at(1).toString().isEmpty());
        QVERIFY(!cache.contains(junk));

        pool.waitForDone();
        QVERIFY(!pool.busy());
    }
};

QTEST_GUILESS_MAIN(TestEngineCore)
#include "tst_engine.moc"
