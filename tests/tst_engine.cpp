#include "engine/io/AudioDevice.h"
#include "engine/BlockMixer.h"
#include "engine/SpscRing.h"

#include <QtTest>

#include <chrono>
#include <functional>
#include <limits>
#include <thread>

using namespace ses::engine;

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
};

QTEST_GUILESS_MAIN(TestEngineCore)
#include "tst_engine.moc"
