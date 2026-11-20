// tst_ui — offscreen gate for the shell (DESIGN.md §13): theme, contrast,
// A/B fader, timeline math, the smoke journey, shortcut guard.
#include "core/media/MediaProbe.h"
#include "core/persistence/Settings.h"
#include "ui/mixer/AbFader.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Smoke.h"
#include "ui/shell/Transport.h"
#include "ui/theme/ThemeManager.h"
#include "ui/timeline/TimelineWidget.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>
#include <QStandardPaths>
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
        QVERIFY(qApp->styleSheet().contains(darkBg));
        QCOMPARE(qApp->palette().color(QPalette::Window).name(), darkBg);
        QVERIFY(tm->isDark());

        w.applyThemeMode(QStringLiteral("light"));
        const QString lightBg = tm->color(QStringLiteral("bg")).name();
        QVERIFY(qApp->styleSheet().contains(lightBg));
        QCOMPARE(qApp->palette().color(QPalette::Window).name(), lightBg);
        QVERIFY(!tm->isDark());
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