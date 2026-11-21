// tst_ui — offscreen gate for the shell (DESIGN.md §13): theme, contrast,
// A/B fader, timeline math, the smoke journey, shortcut guard.
#include "core/media/MediaProbe.h"
#include "core/persistence/Settings.h"
#include "ui/mixer/AbFader.h"
#include "ui/mixer/MixerPanel.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Smoke.h"
#include "ui/shell/Transport.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"
#include "ui/timeline/TimelineWidget.h"

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryDir>
#include <QtTest>

using namespace ses::ui;

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

    void nonWavRejectedHonestly()
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
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"