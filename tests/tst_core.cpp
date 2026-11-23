// tst_core — headless gate for the data layer (DESIGN.md §13).
#include "core/commands/Commands.h"
#include "core/media/AudioBuffer.h"
#include "core/media/MediaCache.h"
#include "core/media/MediaProbe.h"
#include "core/model/Project.h"
#include "core/model/ProjectModel.h"
#include "core/persistence/Settings.h"
#include "core/render/FadeCurve.h"
#include "core/render/RenderPlan.h"
#include "core/render/RenderPlanBuilder.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

using namespace ses::core;

namespace {

bool writeFloatWav(const QString& path, const QVector<float>& samples)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const auto put16 = [&file](uint16_t value) {
        char bytes[2];
        qToLittleEndian(value, bytes);
        return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
    };
    const auto put32 = [&file](uint32_t value) {
        char bytes[4];
        qToLittleEndian(value, bytes);
        return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
    };
    const uint32_t dataSize = uint32_t(samples.size() * sizeof(float));
    if (file.write("RIFF", 4) != 4 || !put32(36 + dataSize) || file.write("WAVEfmt ", 8) != 8
        || !put32(16) || !put16(3) || !put16(1) || !put32(48000) || !put32(192000)
        || !put16(4) || !put16(32) || file.write("data", 4) != 4 || !put32(dataSize))
        return false;
    for (float sample : samples) {
        uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        if (!put32(bits))
            return false;
    }
    return file.error() == QFile::NoError;
}

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

    void trimUndoRestoresFades()
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
        c.length = 800;
        c.fadeIn = Fade{ 600, QStringLiteral("linear"), {} };
        c.fadeOut = Fade{ 700, QStringLiteral("exp"), {} };
        m.insertClip(QStringLiteral("t"), 0, c);

        m.push(new TrimClipCommand(&m, QStringLiteral("c"), ClipTrim{ 0, 300, 0 }));
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeIn.length, 300);
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeOut.length, 300);
        m.push(new TrimClipCommand(&m, QStringLiteral("c"), ClipTrim{ 0, 750, 0 }));
        QCOMPARE(m.undoStack().count(), 1);
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeIn.length, 600);
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeOut.length, 700);

        m.undoStack().undo();
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeIn.length, 600);
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeOut.length, 700);

        m.undoStack().redo();
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeIn.length, 600);
        QCOMPARE(m.project().clipById(QStringLiteral("c"))->fadeOut.length, 700);
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

    void removeCommandsIgnoreDuplicateAndMissingIds()
    {
        ProjectModel m;
        Track track;
        track.id = QStringLiteral("t");
        m.insertTrack(0, track);
        Clip clip;
        clip.id = QStringLiteral("c");
        clip.length = 100;
        m.insertClip(track.id, 0, clip);

        m.push(new RemoveClipsCommand(&m, { QStringLiteral("c"), QStringLiteral("c") }));
        QCOMPARE(m.project().clipCount(), 0);
        m.undoStack().undo();
        QCOMPARE(m.project().clipCount(), 1);

        m.push(new RemoveTrackCommand(&m, QStringLiteral("missing")));
        QCOMPARE(m.project().tracks.size(), 1);
        m.undoStack().undo();
        QCOMPARE(m.project().tracks.size(), 1);
        QCOMPARE(m.project().tracks.first().id, QStringLiteral("t"));
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
            c.mediaId = media.id;
            c.timelineStart = i * 10000;
            c.length = 20000;
            c.fadeIn = Fade{ 1000, QStringLiteral("equalPower"), {} };
            c.fadeOut = Fade{ 2000, QStringLiteral("sCurve"), {} };
            m.insertClip(t.id, 0, c);
        }

        QRandomGenerator rng(0x5e5u); // Failures must be exactly reproducible.
        QVector<QJsonObject> history{ m.project().toJson() };
        int nextTrack = 2;
        int nextClip = 2;

        const auto addTrack = [&]() -> QUndoCommand* {
            Track t;
            t.id = QStringLiteral("t%1").arg(nextTrack++);
            t.name = QStringLiteral("Track %1").arg(nextTrack);
            return new AddTrackCommand(&m, t, rng.bounded(m.project().tracks.size() + 1));
        };
        const auto addClip = [&]() -> QUndoCommand* {
            const auto& tracks = m.project().tracks;
            const QString trackId = tracks[rng.bounded(tracks.size())].id;
            Clip c;
            c.id = QStringLiteral("c%1").arg(nextClip++);
            c.mediaId = media.id;
            c.timelineStart = rng.bounded(100000);
            c.sourceOffset = rng.bounded(80000);
            c.length = rng.bounded(1, int(media.frames - c.sourceOffset + 1));
            c.fadeIn = Fade{ int64_t(rng.bounded(int(c.length + 1))), QStringLiteral("linear"), {} };
            return new AddClipCommand(&m, trackId, c);
        };
        const auto checkState = [&](int step, const char* action) {
            const QString context = QStringLiteral("step %1 (%2)").arg(step).arg(QString::fromLatin1(action));
            const QJsonObject state = m.project().toJson();
            QVERIFY2(state == history[m.undoStack().index()], qPrintable(context + QStringLiteral(": wrong history state")));
            QVERIFY2(Project::fromJson(state).toJson() == state,
                     qPrintable(context + QStringLiteral(": JSON round-trip changed the state")));

            QSet<QString> trackIds;
            QSet<QString> clipIds;
            for (const Track& t : m.project().tracks) {
                QVERIFY2(!t.id.isEmpty() && !trackIds.contains(t.id),
                         qPrintable(context + QStringLiteral(": duplicate/empty track id")));
                trackIds.insert(t.id);
                QVERIFY2(t.gain >= 0.0 && t.gain <= 4.0 && t.pan >= -1.0 && t.pan <= 1.0,
                         qPrintable(context + QStringLiteral(": invalid track parameters")));
                for (const Clip& c : t.clips) {
                    QVERIFY2(!c.id.isEmpty() && !clipIds.contains(c.id),
                             qPrintable(context + QStringLiteral(": duplicate/empty clip id")));
                    clipIds.insert(c.id);
                    QVERIFY2(c.mediaId == media.id && c.timelineStart >= 0 && c.sourceOffset >= 0
                                 && c.length >= 0 && c.sourceOffset + c.length <= media.frames,
                             qPrintable(context + QStringLiteral(": clip escaped media bounds")));
                    QVERIFY2(c.fadeIn.length >= 0 && c.fadeIn.length <= c.length
                                 && c.fadeOut.length >= 0 && c.fadeOut.length <= c.length,
                             qPrintable(context + QStringLiteral(": fade escaped clip bounds")));
                }
            }
            QCOMPARE(m.project().clipCount(), clipIds.size());
        };

        for (int step = 0; step < 600; ++step) {
            auto& stack = m.undoStack();
            // Deterministically visit both directions and then branch after undo.
            if (step % 13 == 9 && stack.canUndo()) {
                stack.undo();
                checkState(step, "undo");
                continue;
            }
            if (step % 13 == 10 && stack.canRedo()) {
                stack.redo();
                checkState(step, "redo");
                continue;
            }

            const int previousIndex = stack.index();
            if (m.project().tracks.isEmpty()) {
                m.push(addTrack());
            } else if (m.project().clipCount() == 0) {
                m.push(addClip());
            } else {
                QStringList clipIds;
                for (const auto& t : m.project().tracks)
                    for (const auto& c : t.clips)
                        clipIds.append(c.id);
                const QString clipId = clipIds[rng.bounded(clipIds.size())];
                const QString trackId = m.trackIdOfClip(clipId);
                const ClipTrim tr = m.clipTrim(clipId);

                switch (rng.bounded(12)) {
                case 0:
                    m.push(new TrimClipCommand(&m, clipId,
                        ClipTrim{ rng.bounded(-2000, 100000), rng.bounded(-1000, 40000),
                                  rng.bounded(-5000, 120000) }));
                    break;
                case 1:
                    m.push(new MoveClipsCommand(&m,
                        QMap<QString, ClipTrim>{ { clipId, { tr.sourceOffset, tr.length,
                            rng.bounded(-5000, 120000) } } }));
                    break;
                case 2:
                    m.push(new SetClipGainCommand(&m, clipId, rng.bounded(-100, 501) / 100.0));
                    break;
                case 3:
                    m.push(new SetFadeCommand(&m, clipId, rng.bounded(2) == 0,
                        Fade{ rng.bounded(-1000, 50000), QStringLiteral("exp"), {} }));
                    break;
                case 4:
                    if (tr.length > 1) {
                        auto* split = new SplitClipsCommand(&m, clipId,
                            tr.timelineStart + rng.bounded(1, int(tr.length)));
                        if (split->isValid())
                            m.push(split);
                        else
                            delete split;
                    }
                    break;
                case 5:
                    m.push(new RemoveClipsCommand(&m, { clipId, clipId }));
                    break;
                case 6:
                    m.push(addClip());
                    break;
                case 7:
                    m.push(addTrack());
                    break;
                case 8:
                    m.push(new RemoveTrackCommand(&m, trackId));
                    break;
                case 9:
                    m.push(new SetTrackParamCommand(&m, trackId, TrackField::Gain,
                                                    rng.bounded(-100, 501) / 100.0));
                    break;
                case 10:
                    if (rng.bounded(2) == 0)
                        m.push(new SetAbFaderCommand(&m, rng.bounded(-100, 201) / 100.0));
                    else
                        m.push(new SetAbModeCommand(&m, rng.bounded(2) == 0
                            ? QStringLiteral("split") : QStringLiteral("invalid")));
                    break;
                case 11:
                    m.push(new SetAbRoutingCommand(&m, trackId, rng.bounded(2) == 0
                        ? QStringLiteral("A") : QStringLiteral("both")));
                    break;
                }
            }

            const int index = m.undoStack().index();
            const QJsonObject state = m.project().toJson();
            QCOMPARE(m.undoStack().count(), index); // push() always discards the redo tail
            if (index == previousIndex) {
                // A merged command replaces the expected state at the current position.
                history.resize(index + 1);
                history[index] = state;
            } else {
                QCOMPARE(index, previousIndex + 1);
                history.resize(index);
                history.append(state);
            }
            QCOMPARE(m.undoStack().count() + 1, history.size());
            checkState(step, "push/merge");
        }

        while (m.undoStack().canUndo()) {
            m.undoStack().undo();
            checkState(600, "full undo");
        }
        QCOMPARE(m.project().toJson(), history.first());
        while (m.undoStack().canRedo()) {
            m.undoStack().redo();
            checkState(601, "full redo");
        }
        QCOMPARE(m.project().toJson(), history.last());
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

        o.insert(QStringLiteral("schemaVersion"), Settings::kSchemaVersion + 1);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(o).toJson());
        f.close();
        const Settings newer = Settings::load();
        QCOMPARE(newer.schemaVersion, Settings::kSchemaVersion + 1);
        QCOMPARE(newer.toJson().value(QStringLiteral("schemaVersion")).toInt(), Settings::kSchemaVersion + 1);
    }

    void floatWavPeaksSanitizeNonFiniteAndOutOfRangeSamples()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/float.wav");
        const QVector<float> samples{ std::numeric_limits<float>::quiet_NaN(),
                                      std::numeric_limits<float>::infinity(), 3.0f, -3.0f };
        QVERIFY(writeFloatWav(path, samples));
        const MediaInfo info = probeWav(path);
        QVERIFY2(info.ok, qPrintable(info.error));

        // Peak LODs are folded from the decode stream in one pass (P1 task 5);
        // the sanitize/clamp contract ported from computeWavPeaks is asserted
        // on the builder at a 1-frame span (per-sample buckets), and end to end
        // through decodeFile in tst_engine.
        PeakBuilder builder(1);
        for (float sample : samples)
            builder.pushFrame(sample, sample);
        const PeakPyramid pyramid = builder.finish();
        QVERIFY(pyramid.valid());
        const PeakLevel& peaks = pyramid.levels.first();
        QCOMPARE(peaks.samplesPerBucket, 1);
        QCOMPARE(peaks.min.size(), samples.size());
        for (int i = 0; i < peaks.min.size(); ++i) {
            QVERIFY(std::isfinite(peaks.min[i]));
            QVERIFY(std::isfinite(peaks.max[i]));
            QVERIFY(peaks.min[i] >= -1.0f);
            QVERIFY(peaks.max[i] <= 1.0f);
        }
        QCOMPARE(peaks.min[0], 0.0f);
        QCOMPARE(peaks.max[0], 0.0f);
        QCOMPARE(peaks.min[1], 0.0f);
        QCOMPARE(peaks.max[1], 0.0f);
        QCOMPARE(peaks.min[2], 1.0f);
        QCOMPARE(peaks.max[3], -1.0f);
    }

    void wavProbeValidatesFixtures()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/tone.wav");
        QVERIFY(writeTestWav(path, 1.0, 48000, 2));
        QVERIFY(!writeTestWav(dir.path() + QStringLiteral("/invalid.wav"),
                              std::numeric_limits<double>::quiet_NaN(), 48000, 2));
        QVERIFY(!writeTestWav(dir.path() + QStringLiteral("/invalid.wav"), 1.0, 48000, 3));
        const MediaInfo info = probeWav(path);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.sampleRate, 48000);
        QCOMPARE(info.channels, 2);
        QCOMPARE(info.frames, 48000);

        // Non-WAV is reported honestly, not guessed.
        QFile bad(dir.path() + QStringLiteral("/fake.wav"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not a riff file at all");
        bad.close();
        QVERIFY(!probeWav(bad.fileName()).ok);

        // Reject a truncated chunk rather than accepting its declared format.
        QFile truncated(dir.path() + QStringLiteral("/truncated.wav"));
        QVERIFY(truncated.open(QIODevice::WriteOnly));
        truncated.write("RIFF", 4);
        const auto write32 = [](QFile& file, uint32_t value) {
            char bytes[4];
            qToLittleEndian(value, bytes);
            file.write(bytes, 4);
        };
        write32(truncated, 0);
        truncated.write("WAVEdata", 8);
        write32(truncated, 64); // Declares 64 bytes, but no payload follows.
        truncated.close();
        const MediaInfo malformed = probeWav(truncated.fileName());
        QVERIFY(!malformed.ok);

        // A float WAV cannot claim more frames than its data chunk contains.
        QFile badFact(dir.path() + QStringLiteral("/bad-fact.wav"));
        QVERIFY(badFact.open(QIODevice::WriteOnly));
        badFact.write("RIFF", 4);
        write32(badFact, 0); // patched below with full RIFF size
        badFact.write("WAVEfmt ", 8);
        write32(badFact, 16);
        char fmt[16] = {};
        qToLittleEndian(uint16_t(3), fmt);
        qToLittleEndian(uint16_t(1), fmt + 2);
        qToLittleEndian(uint32_t(48000), fmt + 4);
        qToLittleEndian(uint32_t(192000), fmt + 8);
        qToLittleEndian(uint16_t(4), fmt + 12);
        qToLittleEndian(uint16_t(32), fmt + 14);
        badFact.write(fmt, sizeof(fmt));
        badFact.write("fact", 4);
        write32(badFact, 4);
        write32(badFact, 10);
        badFact.write("data", 4);
        write32(badFact, 4);
        write32(badFact, 0);
        badFact.seek(4);
        char riffSize[4];
        qToLittleEndian(uint32_t(badFact.size() - 8), riffSize);
        badFact.write(riffSize, 4);
        badFact.close();
        const MediaInfo invalidFrames = probeWav(badFact.fileName());
        QVERIFY(!invalidFrames.ok);
        QVERIFY(invalidFrames.error.contains(QStringLiteral("frame count")));
    }

    void peakPyramidFoldsLodsInOnePass()
    {
        // 1124 frames at a 256-frame base: level 0 holds 5 buckets, each level
        // above folds two of the level below; the tail must not vanish at any
        // LOD (levels end 5, 3, 2, 1 — the top bucket covers the whole signal).
        PeakBuilder builder(256);
        const int frames = 256 * 4 + 100;
        for (int i = 0; i < frames; ++i) {
            const float v = i % 2 == 0 ? 0.5f : -0.25f;
            builder.pushFrame(v, v);
        }
        const PeakPyramid pyramid = builder.finish();
        QVERIFY(pyramid.valid());
        QCOMPARE(pyramid.levels.size(), 4);
        const int expectedBuckets[] = { 5, 3, 2, 1 };
        for (int k = 0; k < pyramid.levels.size(); ++k) {
            const PeakLevel& level = pyramid.levels[k];
            QCOMPARE(level.samplesPerBucket, int64_t(256) << k);
            QCOMPARE(level.min.size(), expectedBuckets[k]);
            for (int b = 0; b < level.min.size(); ++b) {
                QCOMPARE(level.min[b], -0.25f);
                QCOMPARE(level.max[b], 0.5f);
            }
        }

        // pickLevel: the coarsest level whose buckets still fit one pixel
        // column; never finer, never past the top.
        QCOMPARE(pyramid.pickLevel(1).samplesPerBucket, int64_t(256));
        QCOMPARE(pyramid.pickLevel(300).samplesPerBucket, int64_t(256));
        QCOMPARE(pyramid.pickLevel(600).samplesPerBucket, int64_t(512));
        QCOMPARE(pyramid.pickLevel(1124).samplesPerBucket, int64_t(1024));
        QCOMPARE(pyramid.pickLevel(100000).samplesPerBucket, int64_t(2048));
    }

    void mediaCachePinsHoldsAndEvicts()
    {
        MediaCache cache;
        const auto makeBuffer = [] {
            auto buffer = std::make_shared<AudioBuffer>();
            buffer->frames = 1;
            return buffer;
        };
        cache.insert(QStringLiteral("a"), makeBuffer());
        QCOMPARE(cache.size(), 1);
        QVERIFY(cache.contains(QStringLiteral("a")));
        QVERIFY(cache.find(QStringLiteral("a")));
        QCOMPARE(cache.pinnedCount(), 0);

        // (a) A live holder keeps the arena — never evict under a reference.
        {
            auto held = cache.find(QStringLiteral("a"));
            QVERIFY(held);
            QCOMPARE(cache.evict(std::chrono::milliseconds(0)), 0);
            QVERIFY(cache.contains(QStringLiteral("a")));
        }
        // Pinned buffers survive even unreferenced (the project-open default).
        cache.pin(QStringLiteral("a"));
        QCOMPARE(cache.pinnedCount(), 1);
        QCOMPARE(cache.evict(std::chrono::milliseconds(0)), 0);
        QVERIFY(cache.contains(QStringLiteral("a")));
        cache.unpin(QStringLiteral("a"));
        // (b) Unpinned + unreferenced + past the grace → evicted.
        QCOMPARE(cache.evict(std::chrono::milliseconds(0)), 1);
        QCOMPARE(cache.size(), 0);

        // The reclaim grace must actually pass first.
        cache.insert(QStringLiteral("b"), makeBuffer());
        QCOMPARE(cache.evict(std::chrono::hours(1)), 0);
        QVERIFY(cache.contains(QStringLiteral("b")));
        // pinAll keeps everything while the project is open.
        cache.pinAll();
        QCOMPARE(cache.pinnedCount(), 1);
        QCOMPARE(cache.evict(std::chrono::milliseconds(0)), 0);
        QVERIFY(cache.contains(QStringLiteral("b")));
        // clear() drops the entries; holders keep their shared_ptr alive.
        const auto held = cache.find(QStringLiteral("b"));
        cache.clear();
        QCOMPARE(cache.size(), 0);
        QVERIFY(held);
    }

    // ---- render plan (P2) ---------------------------------------------------------

    // A 1-frame-per-value arena: pcm[i*2] = pcm[i*2+1] = values[i].
    static std::shared_ptr<AudioBuffer> makeArena(const QVector<float>& values, int rate = 48000)
    {
        auto buffer = std::make_shared<AudioBuffer>();
        buffer->pcm.resize(values.size() * 2);
        for (int i = 0; i < values.size(); ++i) {
            buffer->pcm[std::size_t(i) * 2] = values[i];
            buffer->pcm[std::size_t(i) * 2 + 1] = values[i];
        }
        buffer->sampleRate = rate;
        buffer->channels = 2;
        buffer->frames = values.size();
        buffer->peaks = PeakPyramid{};
        return buffer;
    }

    // Project with one media item + one track + one clip over `frames` frames.
    static Project planProject(int64_t frames, const QString& path = QStringLiteral("m.wav"))
    {
        Project p;
        MediaItem m;
        m.id = QStringLiteral("m1");
        m.path = path;
        m.sampleRate = 48000;
        m.channels = 2;
        m.frames = frames;
        m.format = QStringLiteral("wav");
        p.mediaPool.append(m);
        Track t;
        t.id = QStringLiteral("t1");
        t.name = QStringLiteral("VOX");
        Clip c;
        c.id = QStringLiteral("c1");
        c.mediaId = m.id;
        c.timelineStart = 0;
        c.sourceOffset = 0;
        c.length = frames;
        t.clips.append(c);
        p.tracks.append(t);
        return p;
    }

    void renderPlanBuilderSchedulesClipsAndKeepsArenas()
    {
        const QVector<float> values{ 0.25f, 0.5f, 0.75f, 1.0f };
        MediaCache cache;
        cache.insert(QStringLiteral("m.wav"), makeArena(values));

        const Project p = planProject(4);
        RenderPlanBuilder builder(&cache);
        const RenderPlan plan = builder.build(p);

        QCOMPARE(plan.sampleRate, 48000);
        QCOMPARE(plan.clips.size(), 1);
        const PlanClip& pc = plan.clips.first();
        QCOMPARE(pc.samples, cache.find(QStringLiteral("m.wav"))->pcm.data());
        QCOMPARE(pc.timelineStart, int64_t(0));
        QCOMPARE(pc.length, int64_t(4));
        QCOMPARE(pc.arenaFrames, int64_t(4));

        // The plan holds the arena: the cache can't evict it while published.
        QCOMPARE(plan.bufferRefs.size(), std::size_t(1));
        QCOMPARE(plan.bufferRefs.front().use_count(), 2); // cache + plan
        QCOMPARE(cache.evict(std::chrono::milliseconds(0)), 0);
    }

    void renderPlanBuilderClampsToMediaBounds()
    {
        // Clip asks for 10 frames from offset 2 of a 4-frame arena: the plan
        // must clamp the window instead of reading past the arena.
        const QVector<float> values{ 0.1f, 0.2f, 0.3f, 0.4f };
        MediaCache cache;
        cache.insert(QStringLiteral("m.wav"), makeArena(values));
        Project p = planProject(10);
        p.tracks.first().clips.first().sourceOffset = 2;
        p.tracks.first().clips.first().length = 10;

        const RenderPlan plan = RenderPlanBuilder(&cache).build(p);
        QCOMPARE(plan.clips.size(), 1);
        QCOMPARE(plan.clips.first().sourceOffset, int64_t(2));
        QCOMPARE(plan.clips.first().length, int64_t(2));
        QCOMPARE(plan.clips.first().samples, cache.find(QStringLiteral("m.wav"))->pcm.data());
    }

    void renderPlanBuilderResolvesMuteSoloAndRate()
    {
        const QVector<float> values{ 0.5f, 0.5f };
        MediaCache cache;
        cache.insert(QStringLiteral("m.wav"), makeArena(values));

        Project p = planProject(2);
        p.tracks.first().clips.first().timelineStart = 0;
        Track second;
        second.id = QStringLiteral("t2");
        second.mute = true;
        Clip c2;
        c2.id = QStringLiteral("c2");
        c2.mediaId = QStringLiteral("m1");
        c2.length = 2;
        c2.timelineStart = 100;
        second.clips.append(c2);
        p.tracks.append(second);

        // Muted track: only the first clip is audible.
        const RenderPlan muted = RenderPlanBuilder(&cache).build(p);
        QCOMPARE(muted.clips.size(), 1);
        QCOMPARE(muted.clips.first().timelineStart, int64_t(0));

        // Solo on the first track silences the (unmuted, but non-solo) second.
        p.tracks.first().solo = true;
        const RenderPlan soloed = RenderPlanBuilder(&cache).build(p);
        QCOMPARE(soloed.anySolo, true);
        QCOMPARE(soloed.clips.size(), 1);
        QCOMPARE(soloed.clips.first().timelineStart, int64_t(0));

        // Clip-level mute is silent too.
        p.tracks.first().solo = false;
        p.tracks.first().clips.first().muted = true;
        p.tracks[1].mute = false;
        const RenderPlan clipMuted = RenderPlanBuilder(&cache).build(p);
        QCOMPARE(clipMuted.clips.size(), 1); // only the second track's clip
        QCOMPARE(clipMuted.clips.first().timelineStart, int64_t(100));

        // An arena at a foreign rate is not playable at the project rate.
        MediaCache odd;
        odd.insert(QStringLiteral("m.wav"), makeArena(values, 44100));
        const RenderPlan wrongRate = RenderPlanBuilder(&odd).build(p);
        QCOMPARE(wrongRate.clips.size(), 0);
    }

    void renderPlanBuilderSortsClipsAndDedupesArenas()
    {
        const QVector<float> values{ 0.5f, 0.5f };
        MediaCache cache;
        cache.insert(QStringLiteral("m.wav"), makeArena(values));

        Project p = planProject(2);
        p.tracks.first().clips.first().timelineStart = 500;
        Clip second = p.tracks.first().clips.first();
        second.id = QStringLiteral("c2");
        second.timelineStart = 100;
        p.tracks.first().clips.append(second);

        const RenderPlan plan = RenderPlanBuilder(&cache).build(p);
        QCOMPARE(plan.clips.size(), 2);
        QVERIFY(plan.clips[0].timelineStart < plan.clips[1].timelineStart);
        // Both clips share one arena: the plan must hold exactly one reference.
        QCOMPARE(plan.bufferRefs.size(), std::size_t(1));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"