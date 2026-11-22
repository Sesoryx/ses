// tst_ui — offscreen gate for the shell (DESIGN.md §13): theme, contrast,
// A/B fader, timeline math, the smoke journey, shortcut guard.
#include "core/media/MediaCache.h"
#include "core/media/MediaProbe.h"
#include "core/persistence/Settings.h"
#include "ui/mixer/AbFader.h"
#include "ui/mixer/MixerPanel.h"
#include "ui/shell/DeviceRecovery.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Smoke.h"
#include "ui/shell/Transport.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"
#include "ui/timeline/TimelineWidget.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryDir>
#include <QtTest>

using namespace ses::ui;

namespace {

class FakeEngine final : public ses::engine::Engine {
public:
    bool available() const override { return online; }
    void play() override { st.playing = true; }
    void stop() override { st.playing = false; }
    void seek(int64_t sample) override { st.playhead = sample; }
    void setLoop(bool on, int64_t start, int64_t end) override
    {
        st.loop = on;
        st.loopStart = start;
        st.loopEnd = end;
    }
    void setAbFader(double x) override { abFader = x; }
    ses::engine::TransportState state() const override
    {
        return online ? st : ses::engine::TransportState{};
    }
    bool nextTelemetry(ses::engine::Telemetry&) override { return false; }
    bool nextDeviceEvent(ses::engine::DeviceEvent&) override { return false; }

    bool online = true;
    double abFader = 0.5;
    ses::engine::TransportState st;
};

} // namespace

class TestUi : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(assets);
        QStandardPaths::setTestModeEnabled(true);
    }

    void themeToggleRestyles()
    {
        MainWindow w;
        ThemeManager* tm = ThemeManager::instance();
        w.applyThemeMode(QStringLiteral("dark"));
        const QString darkBg = tm->color(QStringLiteral("bg")).name();
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("palette(window)")));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("palette(mid)")));
        QCOMPARE(qApp->palette().color(QPalette::Window).name(), darkBg);
        QCOMPARE(w.palette().color(QPalette::Window).name(), darkBg);
        QVERIFY(tm->isDark());
        auto iconPixel = [] {
            const QImage image = sesIcon(QStringLiteral("play")).pixmap(24, 24).toImage();
            QColor result;
            int maxAlpha = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor pixel = image.pixelColor(x, y);
                    if (pixel.alpha() > maxAlpha) {
                        maxAlpha = pixel.alpha();
                        result = pixel;
                    }
                }
            }
            return result;
        };
        QCOMPARE(iconPixel().rgb(), tm->color(QStringLiteral("text")).rgb());

        w.applyThemeMode(QStringLiteral("light"));
        const QString lightBg = tm->color(QStringLiteral("bg")).name();
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("palette(window)")));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("palette(mid)")));
        QCOMPARE(qApp->palette().color(QPalette::Window).name(), lightBg);
        QCOMPARE(w.palette().color(QPalette::Window).name(), lightBg);
        QVERIFY(!tm->isDark());
        QCOMPARE(iconPixel().rgb(), tm->color(QStringLiteral("text")).rgb());
        QCOMPARE(ses::core::Settings::load().theme, QStringLiteral("light"));

        // Secondary text uses the contrast-checked role, never Mid.
        QCOMPARE(qApp->palette().color(QPalette::PlaceholderText).name(),
                 tm->color(QStringLiteral("textSecondary")).name());
    }

    void tokenContrast()
    {
        ThemeManager* tm = ThemeManager::instance();
        for (const QString& mode : { QStringLiteral("dark"), QStringLiteral("light") }) {
            tm->setMode(ThemeManager::modeFromString(mode));
            const QColor bg = tm->color(QStringLiteral("bg"));
            const QColor panel = tm->color(QStringLiteral("panel"));
            const QColor text = tm->color(QStringLiteral("text"));
            const QColor secondary = tm->color(QStringLiteral("textSecondary"));
            const QColor accent = tm->color(QStringLiteral("accent"));
            const QColor accentText = tm->color(QStringLiteral("accentText"));
            QVERIFY2(ThemeManager::contrastRatio(text, bg) >= 4.5, qPrintable(mode + " text/bg"));
            QVERIFY2(ThemeManager::contrastRatio(secondary, bg) >= 4.5, qPrintable(mode + " secondary/bg"));
            QVERIFY2(ThemeManager::contrastRatio(secondary, panel) >= 4.5, qPrintable(mode + " secondary/panel"));
            QVERIFY2(ThemeManager::contrastRatio(accentText, accent) >= 4.5, qPrintable(mode + " accentText/accent"));
        }
    }

    void abFaderDetentAndSolo()
    {
        AbFader f;
        f.setReducedMotion(true);
        QCOMPARE(AbFader::snapDetent(0.49), 0.5);
        QCOMPARE(AbFader::snapDetent(0.51), 0.5);
        QCOMPARE(AbFader::snapDetent(0.47), 0.47);
        QCOMPARE(AbFader::snapDetent(0.0), 0.0);
        QCOMPARE(AbFader::snapDetent(1.0), 1.0);
        f.setValue(0.0);
        QCOMPARE(f.value(), 0.0);
        f.setValue(1.0);
        QCOMPARE(f.value(), 1.0);
    }

    void timelineZoomMath()
    {
        MainWindow w;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/z.wav");
        QVERIFY(ses::core::writeTestWav(wav, 2.0));
        w.importFiles({ wav });
        qApp->processEvents();
        TimelineWidget* tl = w.timeline();
        QVERIFY(tl->zoom() > 0);

        const int64_t s = tl->sampleAtX(200);
        QVERIFY(std::abs(tl->xOfSample(s) - 200) <= 1);

        const int64_t anchor = tl->sampleAtX(300);
        tl->zoomAt(2.0, QPoint(300, 0));
        QVERIFY(std::abs(tl->sampleAtX(300) - anchor) <= 2);
        tl->zoomToFit();
        QVERIFY(tl->xOfSample(w.model().project().duration()) <= tl->viewport()->width());
    }

    void metersMoveAndDecayHonestly()
    {
        // Honest idle: with metering off, peaks are ignored — no fake signal.
        LevelMeter idle;
        idle.setPeak(0.5f);
        QCOMPARE(idle.level(), 0.0);
        QCOMPARE(idle.peakHold(), 0.0);

        LevelMeter m;
        m.setMeteringEnabled(true);
        m.setPeak(0.5f);
        QCOMPARE(m.level(), 0.5);
        QCOMPARE(m.peakHold(), 0.5);
        QVERIFY(!m.clip());
        m.setPeak(1.2f); // over full scale latches clip…
        QVERIFY(m.clip());

        // The master meter is the sink the 30 Hz drain feeds.
        ses::core::ProjectModel model;
        MixerPanel panel(&model);
        panel.setMeteringEnabled(true);
        QVERIFY(panel.masterMeter() != nullptr);
        QVERIFY(panel.masterMeter()->meteringEnabled());
        panel.setMasterPeak(0.25f);
        QCOMPARE(panel.masterMeter()->level(), 0.25);

        // Silence decays to zero — the bar never freezes at a stale level…
        QTRY_VERIFY_WITH_TIMEOUT(m.level() == 0.0 && m.peakHold() == 0.0, 5000);
        QVERIFY(!m.clip()); // …and the clip latch clears with the signal
        panel.setMeteringEnabled(false);
        QCOMPARE(panel.masterMeter()->level(), 0.0);
    }

    void telemetryDrainRunsAt30Hz()
    {
        // End-to-end plumbing: null-backend device → SpscRing → the 30 Hz
        // Transport drain → telemetryUpdated (the signal meters subscribe to).
        ses::core::ProjectModel model;
        ses::engine::DeviceEngine engine;
        if (!engine.openOnNullBackend())
            QSKIP("null audio backend unavailable");
        Transport transport(&model, &engine);
        int updates = 0;
        connect(&transport, &Transport::telemetryUpdated,
                [&updates](const ses::engine::Telemetry&) { ++updates; });
        transport.play();
        QTRY_VERIFY_WITH_TIMEOUT(updates >= 3, 2000); // several 30 Hz ticks
        QVERIFY(transport.playing());
        transport.stop();
    }

    void deviceRecoveryCoalescesEventsAndRecovers()
    {
        int opens = 0;
        DeviceRecovery recovery([&opens] { return ++opens == 3; },
                                { 10, 10, 10, 10 });
        QSignalSpy interruptedSpy(&recovery, &DeviceRecovery::interrupted);
        QSignalSpy attemptSpy(&recovery, &DeviceRecovery::attemptStarted);
        QSignalSpy retrySpy(&recovery, &DeviceRecovery::retryScheduled);
        QSignalSpy recoveredSpy(&recovery, &DeviceRecovery::recovered);
        QSignalSpy failedSpy(&recovery, &DeviceRecovery::failed);

        recovery.handleEvent({ ses::engine::DeviceEvent::Rerouted });
        QCOMPARE(opens, 1); // the first reopen is immediate
        QVERIFY(recovery.active());
        QCOMPARE(recovery.attempt(), 1);
        recovery.handleEvent({ ses::engine::DeviceEvent::Stopped });
        recovery.handleEvent({ ses::engine::DeviceEvent::InterruptionBegan });
        QCOMPARE(opens, 1); // repeated notifications do not reset the backoff
        QCOMPARE(interruptedSpy.count(), 1);

        QTRY_COMPARE_WITH_TIMEOUT(opens, 3, 2000);
        QVERIFY(!recovery.active());
        QCOMPARE(attemptSpy.count(), 3);
        QCOMPARE(retrySpy.count(), 2);
        QCOMPARE(retrySpy.at(0).at(0).toInt(), 2);
        QCOMPARE(retrySpy.at(0).at(1).toInt(), 5);
        QCOMPARE(retrySpy.at(0).at(2).toInt(), 10);
        QCOMPARE(retrySpy.at(1).at(0).toInt(), 3);
        QCOMPARE(recoveredSpy.count(), 1);
        QCOMPARE(failedSpy.count(), 0);

        recovery.handleEvent({ ses::engine::DeviceEvent::Started });
        recovery.handleEvent({ ses::engine::DeviceEvent::InterruptionEnded });
        QCOMPARE(opens, 3); // non-loss events never trigger reopen
    }

    void deviceRecoveryIgnoresRequestedStop()
    {
        // miniaudio reports a user-requested Stop with the same notification as
        // a loss; only real losses while playback is expected trigger retries.
        int opens = 0;
        bool playing = false;
        DeviceRecovery recovery([&opens] { ++opens; return true; }, { 1, 1, 1, 1 });
        recovery.setPlaybackActive([&playing] { return playing; });
        QSignalSpy interruptedSpy(&recovery, &DeviceRecovery::interrupted);

        recovery.handleEvent({ ses::engine::DeviceEvent::Stopped });
        QCOMPARE(opens, 0);
        QCOMPARE(interruptedSpy.count(), 0);
        QVERIFY(!recovery.active());

        // A reroute is a real device change even while parked.
        recovery.handleEvent({ ses::engine::DeviceEvent::Rerouted });
        QCOMPARE(opens, 1);
        QCOMPARE(interruptedSpy.count(), 1);
        QVERIFY(!recovery.active());

        // While playing, Stopped is a genuine loss.
        playing = true;
        recovery.handleEvent({ ses::engine::DeviceEvent::Stopped });
        QCOMPARE(opens, 2);
        QCOMPARE(interruptedSpy.count(), 2);
        QVERIFY(!recovery.active());
    }

    void deviceRecoveryStopsAfterFiveFailures()
    {
        int opens = 0;
        DeviceRecovery recovery([&opens] { ++opens; return false; },
                                { 1, 1, 1, 1 });
        QSignalSpy attemptSpy(&recovery, &DeviceRecovery::attemptStarted);
        QSignalSpy failedSpy(&recovery, &DeviceRecovery::failed);
        recovery.handleEvent({ ses::engine::DeviceEvent::Stopped });
        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 2000);
        QCOMPARE(opens, 5);
        QCOMPARE(attemptSpy.count(), 5);
        QCOMPARE(attemptSpy.last().at(0).toInt(), 5);
        QCOMPARE(attemptSpy.last().at(1).toInt(), 5);
        QVERIFY(!recovery.active());
        QTest::qWait(15);
        QCOMPARE(opens, 5); // no unbounded background retry loop
    }

    void transportFreezesDuringDeviceLoss()
    {
        ses::core::ProjectModel model;
        ses::core::Clip clip;
        clip.length = model.project().sampleRate * 4;
        ses::core::Track track;
        track.clips.append(clip);
        model.project().tracks.append(track);

        FakeEngine engine;
        engine.st.playhead = 48000;
        Transport transport(&model, &engine);
        transport.play();
        QTRY_COMPARE_WITH_TIMEOUT(transport.playhead(), int64_t(48000), 1000);
        QVERIFY(!transport.previewMode());
        QVERIFY(transport.playing());

        engine.st.playhead = 52000;
        transport.preserveEngineState(); // last authoritative head before close
        QCOMPARE(transport.playhead(), int64_t(52000));
        engine.online = false;
        QTest::qWait(100);
        QCOMPARE(transport.playhead(), int64_t(52000)); // no silent preview clock
        QVERIFY(!transport.previewMode());
        QVERIFY(transport.playing());

        transport.play(); // idempotent while the user still expects playback
        transport.stop(); // user cancellation is honored even with no device
        QVERIFY(!transport.playing());
        transport.play();
        transport.usePreviewMode(); // only permanent failure enters preview
        QVERIFY(transport.previewMode());
        QTest::qWait(100);
        QVERIFY(transport.playhead() > 52000);
        transport.stop();
        QVERIFY(!transport.playing());
    }

    void appIconIsEmbeddedAndRenders()
    {
        // The WM shows its generic placeholder when this resource is missing or
        // the SVG engine can't render it (regression guard for the app icon).
        QVERIFY(QFile::exists(QStringLiteral(":/icon.svg")));
        const QIcon icon = ses::ui::appIcon();
        QVERIFY(!icon.isNull());
        const QImage img = icon.pixmap(QSize(64, 64)).toImage();
        QVERIFY(!img.isNull());
        bool anyVisible = false;
        for (int y = 0; y < img.height() && !anyVisible; ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 0) {
                    anyVisible = true;
                    break;
                }
        QVERIFY(anyVisible); // real pixels, not a blank placeholder
    }

    void smokeJourney()
    {
        MainWindow w;
        const SmokeResult r = runSmoke(w);
        QVERIFY2(r.ok, qPrintable(r.detail));
    }

    void unreadableFileRejectedHonestly()
    {
        MainWindow w;
        w.importFiles({ QStringLiteral("/nonexistent/song.mp3") });
        QCOMPARE(w.model().project().clipCount(), 0); // no guessed clip
    }

    void shortcutGuard()
    {
        MainWindow w;
        w.show();
        qApp->processEvents();
        QLineEdit edit(&w);
        edit.show();
        edit.setFocus();
        qApp->processEvents();
        QVERIFY(QApplication::focusWidget() == &edit);
        QKeyEvent override1(QEvent::ShortcutOverride, Qt::Key_S, Qt::NoModifier);
        QApplication::sendEvent(qApp, &override1);
        QVERIFY(override1.isAccepted()); // text field keeps the key

        edit.clearFocus();
        qApp->processEvents();
        QKeyEvent override2(QEvent::ShortcutOverride, Qt::Key_S, Qt::NoModifier);
        QApplication::sendEvent(qApp, &override2);
        QVERIFY(!override2.isAccepted()); // app shortcut may fire
    }

    void waveformDrawsFromDecodedPeaks()
    {
        MainWindow w;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/wave.wav");
        QVERIFY(ses::core::writeTestWav(wav, 1.0));
        w.importFiles({ wav });
        w.decodePool().waitForDone();
        QVERIFY(w.mediaCache().contains(wav));

        TimelineWidget* tl = w.timeline();
        const QSize size = tl->viewport()->size();
        QVERIFY(size.width() > 0);
        QVERIFY(size.height() > 0);
        const auto render = [&] {
            // grab() runs the widget's paint path (viewport()->render() skips
            // QAbstractScrollArea's paint forwarding).
            return tl->grab().toImage();
        };

        // Without decoded peaks the same clip paints no waveform ...
        w.mediaCache().clear();
        const QImage withoutWave = render();
        // ... and the decoded peak LODs draw it (computeWavPeaks is gone).
        w.decodePool().request(wav);
        w.decodePool().waitForDone();
        const QImage withWave = render();
        QCOMPARE(withWave.size(), withoutWave.size());

        int changed = 0;
        for (int y = 0; y < withoutWave.height(); ++y)
            for (int x = 0; x < withoutWave.width(); ++x)
                if (withoutWave.pixel(x, y) != withWave.pixel(x, y))
                    ++changed;
        QVERIFY2(changed > 20,
                 qPrintable(QStringLiteral("waveform changed only %1 pixels").arg(changed)));
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"