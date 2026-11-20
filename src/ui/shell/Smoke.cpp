#include "ui/shell/Smoke.h"

#include "core/media/MediaProbe.h"
#include "core/persistence/Settings.h"
#include "ui/mixer/AbFader.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Transport.h"
#include "ui/theme/ThemeManager.h"
#include "ui/timeline/TimelineWidget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

namespace ses::ui {

SmokeResult runSmoke(MainWindow& w)
{
    QStringList steps;
    bool allOk = true;
    const auto check = [&](bool cond, const QString& name) {
        steps << QStringLiteral("%1 %2").arg(cond ? QStringLiteral("PASS") : QStringLiteral("FAIL"), name);
        allOk = allOk && cond;
    };

    const QString originalTheme = w.settings().theme;

    QTemporaryDir dir;
    const QString wav = dir.path() + QStringLiteral("/fixture.wav");
    check(core::writeTestWav(wav, 2.0), "write fixture wav");
    if (!dir.isValid())
        return { false, steps.join(QLatin1Char('\n')) };

    w.importFiles({ wav });
    qApp->processEvents();
    check(w.model().project().clipCount() == 1, "import creates one clip");
    check(w.model().project().mediaPool.size() == 1, "import registers media item");
    check(!w.model().project().mediaPool.first().contentHash.isEmpty(), "media hashed on import");

    const QString clipId = w.model().project().tracks.first().clips.first().id;
    w.transport()->seek(48000);
    w.timeline()->selectClips({ clipId });
    w.splitAtPlayhead();
    check(w.model().project().clipCount() == 2, "split at playhead yields two clips");

    w.model().undoStack().undo();
    check(w.model().project().clipCount() == 1, "one undo step restores the unsplit clip");

    // Theme toggle: restyles + timing gate (< 100 ms, DESIGN.md §1.4).
    QElapsedTimer timer;
    timer.start();
    w.applyThemeMode(QStringLiteral("light"));
    const qint64 lightApplyMs = timer.elapsed();
    qApp->processEvents();
    const qint64 lightMs = timer.elapsed();
    const QString lightBg = ThemeManager::instance()->color(QStringLiteral("bg")).name();
    check(qApp->styleSheet().contains(lightBg), "light theme stylesheet applied");
    timer.restart();
    w.applyThemeMode(QStringLiteral("dark"));
    const qint64 darkApplyMs = timer.elapsed();
    qApp->processEvents();
    const qint64 darkMs = timer.elapsed();
    const QString darkBg = ThemeManager::instance()->color(QStringLiteral("bg")).name();
    check(qApp->styleSheet().contains(darkBg), "dark theme stylesheet applied");
    qInfo("theme timing: light apply=%lldms total=%lldms | dark apply=%lldms total=%lldms",
          lightApplyMs, lightMs, darkApplyMs, darkMs);
    check(lightMs < 100 && darkMs < 100, QStringLiteral("theme toggle < 100 ms (light %1 ms, dark %2 ms)").arg(lightMs).arg(darkMs));

    // A/B fader → undoable command.
    w.abFader()->setValue(1.0);
    check(qFuzzyCompare(w.model().project().ab.fader, 1.0), "fader move lands in model");
    w.model().undoStack().undo();
    check(qFuzzyCompare(w.model().project().ab.fader, 0.5), "fader move undoable in one step");

    check(core::Settings::load().theme == QStringLiteral("dark"), "theme choice persisted to settings");

    // Restore the user's theme preference.
    w.applyThemeMode(originalTheme.isEmpty() ? QStringLiteral("system") : originalTheme);

    return { allOk, steps.join(QLatin1Char('\n')) };
}

} // namespace ses::ui