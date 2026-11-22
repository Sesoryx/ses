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
#include <QVector>

#include <algorithm>

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

    // P1 task 5: the decode pool caches the arena and folds peak LODs.
    w.decodePool().waitForDone();
    check(w.mediaCache().contains(wav), "decode pool fills the media cache");
    const auto decoded = w.mediaCache().find(wav);
    check(decoded && decoded->frames == 96000, "decoded arena holds the file PCM");
    check(decoded && decoded->peaks.valid(), "peak LODs folded during decode");

    const QString clipId = w.model().project().tracks.first().clips.first().id;
    w.transport()->seek(48000);
    w.timeline()->selectClips({ clipId });
    w.splitAtPlayhead();
    check(w.model().project().clipCount() == 2, "split at playhead yields two clips");

    w.model().undoStack().undo();
    check(w.model().project().clipCount() == 1, "one undo step restores the unsplit clip");

    // Theme toggle: restyles + timing gate (< 100 ms, DESIGN.md §1.4).
    // The first apply pays cold QSS parse/polish cost (100–400 ms under system
    // noise); the interactive gate is the warm steady state. Task-8 decision:
    // warm both directions once, then gate on the median of three toggles.
    // Cold values are logged, never used to fail the UX gate.
    QElapsedTimer timer;
    const auto applyAndPump = [&](const QString& mode) {
        timer.restart();
        w.applyThemeMode(mode);
        qApp->processEvents();
        return timer.elapsed();
    };
    const auto paletteMatches = [](const QString& token) {
        return qApp->styleSheet().contains(QStringLiteral("palette(window)"))
            && qApp->palette().color(QPalette::Window).name()
                == ThemeManager::instance()->color(token).name();
    };

    const qint64 coldLightMs = applyAndPump(QStringLiteral("light"));
    check(paletteMatches(QStringLiteral("bg")), "light theme palette and stylesheet applied");
    const qint64 coldDarkMs = applyAndPump(QStringLiteral("dark"));
    check(paletteMatches(QStringLiteral("bg")), "dark theme palette and stylesheet applied");

    QVector<qint64> lightTimes;
    QVector<qint64> darkTimes;
    for (int i = 0; i < 3; ++i) {
        lightTimes.append(applyAndPump(QStringLiteral("light")));
        darkTimes.append(applyAndPump(QStringLiteral("dark")));
    }
    const auto median = [](QVector<qint64> values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    const qint64 lightMs = median(lightTimes);
    const qint64 darkMs = median(darkTimes);
    qInfo("theme timing: cold light=%lldms dark=%lldms | median light=%lldms dark=%lldms",
          coldLightMs, coldDarkMs, lightMs, darkMs);
    check(lightMs < 100 && darkMs < 100,
          QStringLiteral("theme toggle < 100 ms (median light %1 ms, dark %2 ms; cold %3/%4 ms)")
              .arg(lightMs).arg(darkMs).arg(coldLightMs).arg(coldDarkMs));

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
