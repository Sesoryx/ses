// tst_core — headless gate for the data layer (DESIGN.md §13).
#include "core/commands/Commands.h"
#include "core/media/MediaProbe.h"
#include "core/model/Project.h"
#include "core/model/ProjectModel.h"
#include "core/persistence/Settings.h"
#include "core/render/FadeCurve.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace ses::core;

namespace {

Project sampleProject()
{
    Project p;
    p.name = QStringLiteral("Demo");
    MediaItem m;
    m.id = QStringLiteral("m1");
    m.path = QStringLiteral("media/vox.wav");
    m.contentHash = QStringLiteral("sha256:abc");
    m.sampleRate = 48000;
    m.channels = 1;
    m.frames = 240000;
    m.format = QStringLiteral("wav");
    p.mediaPool.append(m);

    Track t;
    t.id = QStringLiteral("t1");
    t.name = QStringLiteral("VOX");
    t.color = QStringLiteral("#7aa2f7");
    t.gain = 0.9;
    t.pan = -0.2;
    t.abRouting = QStringLiteral("A");
    FxInstance fx;
    fx.id = QStringLiteral("fx1");
    fx.type = QStringLiteral("eq3");
    fx.params = QJsonObject{ { QStringLiteral("low"), 0.0 }, { QStringLiteral("mid"), -2.0 } };
    t.fxChain.append(fx);
    Clip c;
    c.id = QStringLiteral("c1");
    c.mediaId = QStringLiteral("m1");
    c.name = QStringLiteral("vox");
    c.timelineStart = 4800;
    c.sourceOffset = 4800;
    c.length = 192000;
    c.gain = 0.9;
    c.fadeIn = Fade{ 2400, QStringLiteral("equalPower"), {} };
    c.fadeOut = Fade{ 4800, QStringLiteral("sCurve"), {} };
    t.clips.append(c);
    AutomationLane lane;
    lane.id = QStringLiteral("a1");
    lane.target = QStringLiteral("track.gain");
    lane.points.append(AutomationPoint{ 0, 0.0, QStringLiteral("linear") });
    lane.points.append(AutomationPoint{ 48000, 1.0, QStringLiteral("linear") });
    t.automation.append(lane);
    p.tracks.append(t);

    p.markers.append(Marker{ QStringLiteral("mk1"), QStringLiteral("intro"), QStringLiteral("#e0af68"), 0, {} });
    p.ab.fader = 0.25;
    p.ab.mode = QStringLiteral("split");
    p.ab.busB.fxChain.append(FxInstance{ QStringLiteral("fxB"), QStringLiteral("comp"), true, {}, {} });
    return p;
}

} // namespace

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void jsonRoundTrip()
    {
        const Project p = sampleProject();
        const QJsonObject a = p.toJson();
        const Project q = Project::fromJson(a);
        QCOMPARE(q.toJson(), a);
        QCOMPARE(q.tracks.size(), 1);
        QCOMPARE(q.tracks[0].clips[0].fadeIn.curve, QStringLiteral("equalPower"));
        QCOMPARE(q.ab.mode, QStringLiteral("split"));
    }

    void unknownKeysPreserved()
    {
        QJsonObject o = sampleProject().toJson();
        o.insert(QStringLiteral("futureFeature"), QJsonObject{ { QStringLiteral("x"), 42 } });
        QJsonArray tracks = o.value(QStringLiteral("tracks")).toArray();
        QJsonObject t = tracks[0].toObject();
        t.insert(QStringLiteral("futureTrackKey"), QStringLiteral("keep"));
        QJsonArray clips = t.value(QStringLiteral("clips")).toArray();
        QJsonObject c = clips[0].toObject();
        c.insert(QStringLiteral("futureClipKey"), true);
        clips[0] = c;
        t.insert(QStringLiteral("clips"), clips);
        tracks[0] = t;
        o.insert(QStringLiteral("tracks"), tracks);

        const Project p = Project::fromJson(o);
        const QJsonObject back = p.toJson();
        QCOMPARE(back.value(QStringLiteral("futureFeature")).toObject().value(QStringLiteral("x")).toInt(), 42);
        QCOMPARE(back.value(QStringLiteral("tracks")).toArray()[0].toObject().value(QStringLiteral("futureTrackKey")).toString(), QStringLiteral("keep"));
        QCOMPARE(back.value(QStringLiteral("tracks")).toArray()[0].toObject()
                     .value(QStringLiteral("clips")).toArray()[0].toObject()
                     .value(QStringLiteral("futureClipKey")).toBool(), true);
    }

    void fadeGolden()
    {
        const double ps[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
        const double linIn[] = { 0, 0.25, 0.5, 0.75, 1 };
        const double eqIn[] = { 0, 0.3826834324, 0.7071067812, 0.9238795325, 1 };
        const double expIn[] = { 0, 0.0625, 0.25, 0.5625, 1 };
        const double sIn[] = { 0, 0.15625, 0.5, 0.84375, 1 };
        for (int i = 0; i < 5; ++i) {
            const double p = ps[i];
            QVERIFY(std::abs(fadeInGain(FadeCurveType::Linear, p) - linIn[i]) < 1e-9);
            QVERIFY(std::abs(fadeInGain(FadeCurveType::EqualPower, p) - eqIn[i]) < 1e-9);
            QVERIFY(std::abs(fadeInGain(FadeCurveType::Exp, p) - expIn[i]) < 1e-9);
            QVERIFY(std::abs(fadeInGain(FadeCurveType::SCurve, p) - sIn[i]) < 1e-9);
            // Out curves mirror in curves (complementary crossfade sums ~constant).
            QVERIFY(std::abs(fadeOutGain(FadeCurveType::Linear, p) - linIn[4 - i]) < 1e-9);
            QVERIFY(std::abs(fadeOutGain(FadeCurveType::EqualPower, p) - eqIn[4 - i]) < 1e-9);
            QVERIFY(std::abs(fadeOutGain(FadeCurveType::Exp, p) - expIn[4 - i]) < 1e-9);
            QVERIFY(std::abs(fadeOutGain(FadeCurveType::SCurve, p) - sIn[4 - i]) < 1e-9);
        }
        // Equal-power crossfade: sin²+cos² = 1 at every point.
        for (double p = 0; p <= 1.0; p += 0.1) {
            const double a = fadeInGain(FadeCurveType::EqualPower, p);
            const double b = fadeOutGain(FadeCurveType::EqualPower, p);
            QVERIFY(std::abs(a * a + b * b - 1.0) < 1e-9);
        }
    }

    void splitInvariant()
    {
        ProjectModel m;
        MediaItem media;
        media.id = QStringLiteral("m");
        media.frames = 1000;
        m.addMedia(media);
        Track t;
        t.id = QStringLiteral("t");
        m.insertTrack(0, t);
        Clip c;
        c.id = QStringLiteral("c");
        c.mediaId = QStringLiteral("m");
        c.timelineStart = 100;
        c.sourceOffset = 50;
        c.length = 600;
        m.insertClip(QStringLiteral("t"), 0, c);
        const QJsonObject before = m.project().toJson();

        m.push(new SplitClipsCommand(&m, QStringLiteral("c"), 400));
        QCOMPARE(m.project().clipCount(), 2);
        const Track& tt = m.project().tracks[0];
        QCOMPARE(tt.clips[0].length, 300);
        QCOMPARE(tt.clips[0].sourceOffset, 50);
        QCOMPARE(tt.clips[0].timelineStart, 100);
        QCOMPARE(tt.clips[1].timelineStart, 400);
        QCOMPARE(tt.clips[1].sourceOffset, 350);
        QCOMPARE(tt.clips[1].length, 300);

        m.undoStack().undo();
        QCOMPARE(m.project().toJson(), before);
        m.undoStack().redo();
        QCOMPARE(m.project().clipCount(), 2);

        // Out-of-bounds split is a no-op.
        auto* invalid = new SplitClipsCommand(&m, QStringLiteral("c"), 50);
        QVERIFY(!invalid->isValid());
        delete invalid;
    }

    void trimClamp()
    {
        ProjectModel m;
        MediaItem media;
        media.id = QStringLiteral("m");
        media.frames = 1000;
        m.addMedia(media);
        Track t;
        t.id = QStringLiteral("t");
        m.insertTrack(0, t);
        Clip c;
        c.id = QStringLiteral("c");
        c.mediaId = QStringLiteral("m");
        c.length = 500;
        m.insertClip(QStringLiteral("t"), 0, c);

        m.setClipTrim(QStringLiteral("c"), ClipTrim{ -5, 5000, -10 });
        const ClipTrim tr = m.clipTrim(QStringLiteral("c"));
        QCOMPARE(tr.sourceOffset, 0);
        QCOMPARE(tr.timelineStart, 0);
        QCOMPARE(tr.length, 1000);
    }

    void undoRedoExact()
    {
        ProjectModel m;
        MediaItem media;
        media.id = QStringLiteral("m");
        media.frames = 48000;
        m.addMedia(media); // media pool setup precedes the command sequence
        const QJsonObject initial = m.project().toJson();
        Track t;
        t.id = QStringLiteral("t");
        m.push(new AddTrackCommand(&m, t));
        Clip c;
        c.id = QStringLiteral("c");
        c.mediaId = QStringLiteral("m");
        c.length = 24000;
        m.push(new AddClipCommand(&m, QStringLiteral("t"), c));
        m.push(new TrimClipCommand(&m, QStringLiteral("c"), ClipTrim{ 100, 20000, 500 }));
        m.push(new SetClipGainCommand(&m, QStringLiteral("c"), 0.5));
        m.push(new SetFadeCommand(&m, QStringLiteral("c"), true, Fade{ 1000, QStringLiteral("exp"), {} }));
        m.push(new SetAbFaderCommand(&m, 0.8));
        m.push(new SetAbRoutingCommand(&m, QStringLiteral("t"), QStringLiteral("B")));
        const QJsonObject final = m.project().toJson();

        while (m.undoStack().canUndo())
            m.undoStack().undo();
        QCOMPARE(m.project().toJson(), initial);
        while (m.undoStack().canRedo())
            m.undoStack().redo();
        QCOMPARE(m.project().toJson(), final);
    }

    void dragCoalescing()
    {
        ProjectModel m;
        Track t;
        t.id = QStringLiteral("t");
        m.insertTrack(0, t);
        Clip c;
        c.id = QStringLiteral("c");
        c.length = 1000;
        m.insertClip(QStringLiteral("t"), 0, c);
        const ClipTrim original = m.clipTrim(QStringLiteral("c"));

        for (int i = 1; i <= 50; ++i)
            m.push(new TrimClipCommand(&m, QStringLiteral("c"), ClipTrim{ 0, 1000, i * 10 }));
        QCOMPARE(m.undoStack().count(), 1); // one drag = one undo step
        QCOMPARE(m.clipTrim(QStringLiteral("c")).timelineStart, 500);
        m.undoStack().undo();
        QCOMPARE(m.clipTrim(QStringLiteral("c")), original);
    }

    void randomCommandConsistency()
    {
        ProjectModel m;
        MediaItem media;
        media.id = QStringLiteral("m");
        media.frames = 96000;
        m.addMedia(media);
        for (int i = 0; i < 2; ++i) {
            Track t;
            t.id = QStringLiteral("t%1").arg(i);
            m.insertTrack(i, t);
            Clip c;
            c.id = QStringLiteral("c%1").arg(i);
            c.mediaId = QStringLiteral("m");
            c.timelineStart = i * 10000;
            c.length = 20000;
            m.insertClip(t.id, 0, c);
        }
        const QJsonObject initial = m.project().toJson();

        auto* rng = QRandomGenerator::global();
        for (int step = 0; step < 200; ++step) {
            QStringList ids;
            for (const auto& t : m.project().tracks)
                for (const auto& c : t.clips)
                    ids.append(c.id);
            if (ids.isEmpty())
                break;
            const QString id = ids[rng->bounded(ids.size())];
            const ClipTrim tr = m.clipTrim(id);
            switch (rng->bounded(5)) {
            case 0:
                m.push(new TrimClipCommand(&m, id, ClipTrim{ tr.sourceOffset, tr.length, tr.timelineStart + int(rng->bounded(2000)) - 1000 }));
                break;
            case 1:
                m.push(new SetClipGainCommand(&m, id, rng->bounded(200) / 100.0));
                break;
            case 2:
                m.push(new SetFadeCommand(&m, id, rng->bounded(2) == 0, Fade{ int64_t(rng->bounded(3000)), QStringLiteral("linear"), {} }));
                break;
            case 3:
                m.push(new SetAbFaderCommand(&m, rng->bounded(100) / 100.0));
                break;
            case 4: {
                const QString tid = m.trackIdOfClip(id);
                m.push(new SetAbRoutingCommand(&m, tid, rng->bounded(2) == 0 ? QStringLiteral("A") : QStringLiteral("both")));
                break;
            }
            }
        }
        const QJsonObject final = m.project().toJson();
        const int depth = m.undoStack().count();
        for (int i = 0; i < depth; ++i)
            m.undoStack().undo();
        QCOMPARE(m.project().toJson(), initial);
        for (int i = 0; i < depth; ++i)
            m.undoStack().redo();
        QCOMPARE(m.project().toJson(), final);
    }

    void settingsNormalize()
    {
        Settings s;
        s.theme = QStringLiteral("bogus");
        s.uiScale = 99.0;
        QVERIFY(s.save());
        const Settings loaded = Settings::load();
        QCOMPARE(loaded.theme, QStringLiteral("system"));
        QCOMPARE(loaded.uiScale, 1.6);

        // Unknown keys survive the round-trip.
        QJsonObject o = loaded.toJson();
        o.insert(QStringLiteral("futureSetting"), 7);
        QFile f(Settings::filePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(o).toJson());
        f.close();
        QCOMPARE(Settings::load().toJson().value(QStringLiteral("futureSetting")).toInt(), 7);
    }

    void wavProbeAndPeaks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/tone.wav");
        QVERIFY(writeTestWav(path, 1.0, 48000, 2));
        const MediaInfo info = probeWav(path);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.sampleRate, 48000);
        QCOMPARE(info.channels, 2);
        QCOMPARE(info.frames, 48000);

        const Peaks pk = computeWavPeaks(path, 100);
        QVERIFY(pk.valid());
        QVERIFY(pk.min.size() >= 90 && pk.min.size() <= 110);
        bool anySignal = false;
        for (int i = 0; i < pk.min.size(); ++i) {
            QVERIFY(pk.min[i] <= pk.max[i]);
            QVERIFY(pk.min[i] >= -1.001f && pk.max[i] <= 1.001f);
            if (pk.max[i] > 0.1f)
                anySignal = true;
        }
        QVERIFY(anySignal);

        // Non-WAV is reported honestly, not guessed.
        QFile bad(dir.path() + QStringLiteral("/fake.wav"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not a riff file at all");
        bad.close();
        QVERIFY(!probeWav(bad.fileName()).ok);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"