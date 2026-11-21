#include "engine/BlockMixer.h"
#include "engine/SpscRing.h"

#include <QtTest>

#include <limits>

using namespace ses::engine;

class TestEngineCore : public QObject {
    Q_OBJECT
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
