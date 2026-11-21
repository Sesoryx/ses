// ses — local-first audio workstation. Entry point + wiring (DESIGN.md §3/app).
// UI-first slice: --smoke and --screenshot gates; single-instance gate.

#include "core/media/MediaProbe.h"
#include "core/persistence/Settings.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Smoke.h"
#include "ui/shell/Transport.h"
#include "ui/theme/ThemeManager.h"
#include "ui/timeline/TimelineWidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

QFile* g_log = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const char* level = "INFO";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "CRIT"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }
    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate), QLatin1String(level), msg);
    if (g_log) {
        g_log->write(line.toUtf8());
        g_log->flush();
    }
    if (qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING"))
        std::fputs(line.toUtf8().constData(), stderr);
    if (type == QtFatalMsg)
        std::abort();
}

QString instanceName()
{
    return QStringLiteral("ses-instance-%1").arg(QString::fromLocal8Bit(qgetenv("USER")));
}

} // namespace

int main(int argc, char** argv)
{
    Q_INIT_RESOURCE(assets); // pull the qrc object out of the static lib
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ses"));
    QApplication::setOrganizationName(QStringLiteral("ses"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ses — local-first audio workstation (UI-first slice)"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeOpt(QStringLiteral("smoke"), QStringLiteral("Run the headless self-check and exit"));
    const QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Save dark/light screenshots to <dir> and exit"), QStringLiteral("dir"));
    parser.addOption(smokeOpt);
    parser.addOption(shotOpt);
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files to import"), QStringLiteral("[files...]"));
    parser.process(app);

    // Rotating-ish file log (full rotation in P6).
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/logs");
    QDir().mkpath(logDir);
    g_log = new QFile(logDir + QStringLiteral("/ses.log"));
    if (!g_log->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete g_log;
        g_log = nullptr; // Logging is best-effort; startup must still work.
    }
    qInstallMessageHandler(messageHandler);

    const bool headlessGate = parser.isSet(smokeOpt) || parser.isSet(shotOpt);

    // Single-instance gate (skipped for headless gates).
    QLocalServer server;
    if (!headlessGate) {
        QLocalSocket probe;
        probe.connectToServer(instanceName());
        if (probe.waitForConnected(150)) {
            probe.write("raise");
            probe.waitForBytesWritten(150);
            std::fprintf(stderr, "ses is already running — raising existing window.\n");
            return 0;
        }
        QLocalServer::removeServer(instanceName());
        if (!server.listen(instanceName()))
            qWarning("single-instance server failed to listen: %s", qPrintable(server.errorString()));
    }

    ses::ui::MainWindow w;
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&w, &server] {
        QLocalSocket* sock = server.nextPendingConnection();
        if (sock) {
            sock->readAll();
            sock->deleteLater();
        }
        w.showNormal();
        w.raise();
        w.activateWindow();
    });

    w.show();

    if (parser.isSet(smokeOpt)) {
        QTimer::singleShot(250, &app, [&] {
            const ses::ui::SmokeResult r = ses::ui::runSmoke(w);
            std::fprintf(stderr, "%s\n", qPrintable(r.detail));
            std::fprintf(stderr, "SMOKE %s\n", r.ok ? "OK" : "FAILED");
            app.exit(r.ok ? 0 : 1);
        });
    } else if (parser.isSet(shotOpt)) {
        const QString dir = parser.value(shotOpt);
        QDir().mkpath(dir);
        // Demo content so the screenshots show the real surface.
        QTemporaryDir tmp;
        const QString a = tmp.path() + QStringLiteral("/vox-take.wav");
        const QString b = tmp.path() + QStringLiteral("/music-bed.wav");
        ses::core::writeTestWav(a, 8.0);
        ses::core::writeTestWav(b, 6.0);
        w.importFiles({ a, b });
        w.transport()->seek(120000);
        const auto& tracks = w.model().project().tracks;
        if (!tracks.isEmpty() && !tracks.first().clips.isEmpty())
            w.timeline()->selectClips({ tracks.first().clips.first().id });
        QTimer::singleShot(500, &app, [&] {
            w.applyThemeMode(QStringLiteral("dark"));
            qApp->processEvents();
            w.grab().save(dir + QStringLiteral("/ses-dark.png"));
            w.applyThemeMode(QStringLiteral("light"));
            qApp->processEvents();
            w.grab().save(dir + QStringLiteral("/ses-light.png"));
            std::fprintf(stderr, "screenshots saved to %s\n", qPrintable(dir));
            app.exit(0);
        });
    } else {
        const QStringList files = parser.positionalArguments();
        if (!files.isEmpty())
            w.importFiles(files);
    }

    return app.exec();
}