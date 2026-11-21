#include "ui/shell/MainWindow.h"

#include "core/commands/Commands.h"
#include "core/media/MediaProbe.h"
#include "ui/mixer/AbFader.h"
#include "ui/mixer/MixerPanel.h"
#include "ui/panels/AiPanel.h"
#include "ui/panels/HistoryPanel.h"
#include "ui/panels/ModelManagerPanel.h"
#include "ui/shell/Transport.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"
#include "ui/timeline/TimelineWidget.h"
#include "ui/timeline/TrackHeaders.h"
#include "ui/widgets/CheatSheet.h"
#include "ui/widgets/CoachMarks.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/Toast.h"

#include <QActionGroup>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace ses::ui {

namespace {

QString sha256File(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd())
        hash.addData(f.read(65536));
    return QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex());
}

QToolButton* toolButton(const QString& icon, const QString& tip, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setIcon(sesIcon(icon));
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::ClickFocus);
    return b;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("MainWindow"));
    resize(1280, 800);
    setAcceptDrops(true);

    settings_ = core::Settings::load();
    ThemeManager::instance()->setMode(ThemeManager::modeFromString(settings_.theme));

    // Real RT engine (P1 task 3): the default output device at the project
    // rate. If no device opens, Transport honestly stays in preview mode.
    engine_.open(model_.project().sampleRate);
    transport_ = new Transport(&model_, &engine_, this);
    toast_ = new Toast(this);

    auto* central = new QWidget(this);
    auto* vl = new QVBoxLayout(central);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    setCentralWidget(central);

    buildTransportBar(central);
    coach_ = new CoachMarks(&settings_, central);
    vl->addWidget(coach_);
    buildEditor(central);
    buildDock();
    buildStatusBar();
    buildActions();
    buildMenus();
    wireModel();

    qApp->installEventFilter(this);
    refreshTitle();
    refreshTransport();
    positionEmptyState();
    updateThemeIcon();
}

// ---- transport bar ---------------------------------------------------------------

void MainWindow::buildTransportBar(QWidget* central)
{
    auto* bar = new QFrame(central);
    bar->setObjectName(QStringLiteral("TransportBar"));
    bar->setFixedHeight(56);
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(10, 6, 10, 6);
    hl->setSpacing(6);

    playBtn_ = toolButton(QStringLiteral("play"), QStringLiteral("Play / stop (Space)"), bar);
    playBtn_->setProperty("variant", QStringLiteral("transport"));
    playBtn_->setCheckable(true);
    connect(playBtn_, &QToolButton::toggled, this, [this](bool on) {
        on ? transport_->play() : transport_->stop();
    });
    auto* stopBtn = toolButton(QStringLiteral("stop"), QStringLiteral("Stop"), bar);
    stopBtn->setProperty("variant", QStringLiteral("transport"));
    connect(stopBtn, &QToolButton::clicked, transport_, &Transport::stop);
    auto* toStartBtn = toolButton(QStringLiteral("tostart"), QStringLiteral("To start (Enter)"), bar);
    toStartBtn->setProperty("variant", QStringLiteral("transport"));
    connect(toStartBtn, &QToolButton::clicked, transport_, &Transport::toStart);
    loopBtn_ = toolButton(QStringLiteral("loop"), QStringLiteral("Loop (L)"), bar);
    loopBtn_->setProperty("variant", QStringLiteral("transport"));
    loopBtn_->setCheckable(true);
    connect(loopBtn_, &QToolButton::toggled, transport_, &Transport::setLoop);

    auto* sep = new QFrame(bar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(26);

    timeDisplay_ = new QLabel(QStringLiteral("00:00:00.000 / 00:00:00.000"), bar);
    timeDisplay_->setObjectName(QStringLiteral("TimeDisplay"));

    abFader_ = new AbFader(bar);
    abFader_->setReducedMotion(settings_.reducedMotion);
    abFader_->setMode(model_.project().ab.mode);

    themeBtn_ = toolButton(QStringLiteral("moon"), QStringLiteral("Toggle theme (T)"), bar);
    connect(themeBtn_, &QToolButton::clicked, this, &MainWindow::toggleTheme);

    engineBadge_ = new QLabel(bar);
    engineBadge_->setObjectName(QStringLiteral("Badge"));
    engineBadge_->setText(transport_->previewMode()
                              ? QStringLiteral("Preview · no audio")
                              : QStringLiteral("CPU · engine"));
    engineBadge_->setToolTip(QStringLiteral("Audio engine: miniaudio device + RT mixer (P1). Without an output device the transport runs in labelled preview mode — no audio."));

    hl->addWidget(playBtn_);
    hl->addWidget(stopBtn);
    hl->addWidget(toStartBtn);
    hl->addWidget(loopBtn_);
    hl->addWidget(sep);
    hl->addWidget(timeDisplay_);
    hl->addStretch(1);
    hl->addWidget(abFader_);
    hl->addWidget(themeBtn_);
    hl->addWidget(engineBadge_);

    static_cast<QVBoxLayout*>(central->layout())->addWidget(bar);
}

// ---- editor ------------------------------------------------------------------------

void MainWindow::buildEditor(QWidget* central)
{
    mainSplit_ = new QSplitter(Qt::Horizontal, central);
    editorSplit_ = new QSplitter(Qt::Vertical, mainSplit_);

    auto* editorTop = new QWidget(editorSplit_);
    auto* ev = new QVBoxLayout(editorTop);
    ev->setContentsMargins(0, 0, 0, 0);
    ev->setSpacing(0);

    // Tool row.
    auto* toolRow = new QFrame(editorTop);
    toolRow->setObjectName(QStringLiteral("TransportBar"));
    auto* tl = new QHBoxLayout(toolRow);
    tl->setContentsMargins(8, 4, 8, 4);
    tl->setSpacing(4);

    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);
    const struct ToolDef { const char* icon; const char* name; Tool tool; const char* key; } tools[] = {
        { "cursor", "Select (V)", Tool::Select, "V" },
        { "razor", "Razor (R)", Tool::Razor, "R" },
        { "fade", "Fade (F)", Tool::Fade, "F" },
        { "automation", "Automation (Shift+A) — editor lands in P4", Tool::Automation, "Shift+A" },
    };
    for (const auto& td : tools) {
        auto* act = new QAction(sesIcon(QLatin1String(td.icon)), QString::fromUtf8(td.name), this);
        act->setCheckable(true);
        act->setShortcut(QKeySequence(QLatin1String(td.key)));
        toolGroup_->addAction(act);
        addAction(act);
        auto* btn = new QToolButton(toolRow);
        btn->setDefaultAction(act);
        btn->setFocusPolicy(Qt::ClickFocus);
        tl->addWidget(btn);
        connect(act, &QAction::triggered, this, [this, t = td.tool] { timeline_->setTool(t); });
        if (td.tool == Tool::Select)
            act->setChecked(true);
    }

    auto* sep = new QFrame(toolRow);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(20);
    tl->addWidget(sep);

    auto* magnet = new QAction(sesIcon(QStringLiteral("magnet")), QStringLiteral("Magnetic snap (hold Ctrl to bypass)"), this);
    magnet->setCheckable(true);
    magnet->setChecked(settings_.snap);
    addAction(magnet);
    auto* magnetBtn = new QToolButton(toolRow);
    magnetBtn->setDefaultAction(magnet);
    tl->addWidget(magnetBtn);
    connect(magnet, &QAction::toggled, this, [this](bool on) {
        timeline_->setSnap(on);
        settings_.snap = on;
        settings_.save();
    });

    auto* zoomOut = toolButton(QStringLiteral("minus"), QStringLiteral("Zoom out (−)"), toolRow);
    connect(zoomOut, &QToolButton::clicked, this, [this] { timeline_->zoomAt(0.8, QPoint(timeline_->viewport()->width() / 2, 0)); });
    auto* zoomIn = toolButton(QStringLiteral("plus"), QStringLiteral("Zoom in (+)"), toolRow);
    connect(zoomIn, &QToolButton::clicked, this, [this] { timeline_->zoomAt(1.25, QPoint(timeline_->viewport()->width() / 2, 0)); });
    auto* fit = toolButton(QStringLiteral("fit"), QStringLiteral("Zoom to fit (Ctrl+0)"), toolRow);
    connect(fit, &QToolButton::clicked, this, [this] { timeline_->zoomToFit(); });
    tl->addWidget(zoomOut);
    tl->addWidget(zoomIn);
    tl->addWidget(fit);
    tl->addStretch(1);

    selectionLabel_ = new QLabel(QStringLiteral("no selection"), toolRow);
    selectionLabel_->setProperty("role", QStringLiteral("secondary"));
    tl->addWidget(selectionLabel_);

    auto* mixerToggle = toolButton(QStringLiteral("sliders"), QStringLiteral("Show / hide mixer"), toolRow);
    mixerToggle->setCheckable(true);
    mixerToggle->setChecked(settings_.mixerVisible);
    connect(mixerToggle, &QToolButton::toggled, this, [this](bool on) {
        mixer_->setVisible(on);
        settings_.mixerVisible = on;
        settings_.save();
    });
    auto* dockToggle = toolButton(QStringLiteral("sparkles"), QStringLiteral("Show / hide dock"), toolRow);
    dockToggle->setCheckable(true);
    dockToggle->setChecked(settings_.dockVisible);
    connect(dockToggle, &QToolButton::toggled, this, [this](bool on) {
        dock_->setVisible(on);
        settings_.dockVisible = on;
        settings_.save();
    });
    tl->addWidget(mixerToggle);
    tl->addWidget(dockToggle);
    ev->addWidget(toolRow);

    // Headers + timeline.
    auto* content = new QWidget(editorTop);
    auto* ch = new QHBoxLayout(content);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->setSpacing(0);
    headers_ = new TrackHeaders(&model_, content);
    timeline_ = new TimelineWidget(&model_, transport_, content);
    timeline_->setSnap(settings_.snap);
    ch->addWidget(headers_);
    ch->addWidget(timeline_, 1);
    ev->addWidget(content, 1);

    empty_ = new EmptyState(timeline_->viewport());
    connect(empty_, &EmptyState::importRequested, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import audio"), QString(),
                                                          QStringLiteral("WAV audio (*.wav);;All files (*)"));
        if (!path.isEmpty())
            importFiles({ path });
    });

    mixer_ = new MixerPanel(&model_, editorSplit_);
    mixer_->setVisible(settings_.mixerVisible);

    editorSplit_->addWidget(editorTop);
    editorSplit_->addWidget(mixer_);
    editorSplit_->setStretchFactor(0, 1);
    editorSplit_->setSizes({ 700, 190 });

    mainSplit_->addWidget(editorSplit_);
    static_cast<QVBoxLayout*>(central->layout())->addWidget(mainSplit_, 1);
}

void MainWindow::buildDock()
{
    dock_ = new QTabWidget(mainSplit_);
    ai_ = new AiPanel(&model_, dock_);
    history_ = new HistoryPanel(&model_, dock_);
    models_ = new ModelManagerPanel(dock_);
    dock_->addTab(ai_, sesIcon(QStringLiteral("sparkles")), QStringLiteral("AI"));
    dock_->addTab(history_, sesIcon(QStringLiteral("history")), QStringLiteral("History"));
    dock_->addTab(models_, sesIcon(QStringLiteral("cube")), QStringLiteral("Models"));
    dock_->setMinimumWidth(300);
    dock_->setVisible(settings_.dockVisible);
    mainSplit_->addWidget(dock_);
    mainSplit_->setStretchFactor(0, 1);
    mainSplit_->setSizes({ 940, 340 });
}

void MainWindow::buildStatusBar()
{
    auto* offline = new QLabel(QStringLiteral("● Offline — local only"), this);
    offline->setObjectName(QStringLiteral("Badge"));
    offline->setProperty("status", QStringLiteral("ok"));
    offline->setToolTip(QStringLiteral("No telemetry, no cloud. The only network path is an explicit model download (P5)."));
    statusBar()->addWidget(offline);
    auto* sr = new QLabel(QStringLiteral("%1 kHz").arg(model_.project().sampleRate / 1000.0), this);
    sr->setToolTip(QStringLiteral("Project sample rate (fixed 48 kHz in v1)"));
    statusBar()->addPermanentWidget(sr);
    cursorLabel_ = new QLabel(QStringLiteral("00:00:00.000"), this);
    statusBar()->addPermanentWidget(cursorLabel_);
}

// ---- actions / menus ------------------------------------------------------------------

void MainWindow::buildActions()
{
    const auto make = [this](const QString& text, const QKeySequence& seq, auto slot) {
        auto* act = new QAction(text, this);
        if (!seq.isEmpty())
            act->setShortcut(seq);
        connect(act, &QAction::triggered, this, slot);
        addAction(act);
        return act;
    };

    make(QStringLiteral("Import audio…"), QKeySequence(Qt::CTRL | Qt::Key_I), [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Import audio"), QString(),
                                                                QStringLiteral("WAV audio (*.wav);;All files (*)"));
        if (!paths.isEmpty())
            importFiles(paths);
    });
    auto* openAct = make(QStringLiteral("Open project…"), QKeySequence(Qt::CTRL | Qt::Key_O), [] {});
    openAct->setEnabled(false);
    openAct->setToolTip(QStringLiteral("Project bundles (.ego) arrive with persistence (P3)"));
    auto* saveAct = make(QStringLiteral("Save"), QKeySequence(Qt::CTRL | Qt::Key_S), [] {});
    saveAct->setEnabled(false);
    saveAct->setToolTip(QStringLiteral("Project persistence arrives in P3"));
    auto* exportAct = make(QStringLiteral("Export…"), QKeySequence(Qt::CTRL | Qt::Key_E), [] {});
    exportAct->setEnabled(false);
    exportAct->setToolTip(QStringLiteral("Offline bounce arrives in P4"));
    make(QStringLiteral("Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { close(); });

    undoAct_ = make(QStringLiteral("Undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), [this] { model_.undoStack().undo(); });
    redoAct_ = make(QStringLiteral("Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] { model_.undoStack().redo(); });
    deleteAct_ = make(QStringLiteral("Remove clips"), QKeySequence(Qt::Key_Delete), [this] { removeSelection(); });
    make(QStringLiteral("Split at playhead"), QKeySequence(Qt::Key_S), [this] { splitAtPlayhead(); });
    make(QStringLiteral("Duplicate clips"), QKeySequence(Qt::CTRL | Qt::Key_D), [this] { duplicateSelection(); });
    make(QStringLiteral("Select all clips"), QKeySequence(Qt::CTRL | Qt::Key_A), [this] { timeline_->selectAll(); });

    make(QStringLiteral("Play / stop"), QKeySequence(Qt::Key_Space), [this] { transport_->toggle(); });
    auto* toStart = make(QStringLiteral("To start"), QKeySequence(Qt::Key_Return), [this] { transport_->toStart(); });
    toStart->setShortcuts({ QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter) });
    make(QStringLiteral("Loop"), QKeySequence(Qt::Key_L), [this] { transport_->setLoop(!transport_->loop()); });
    make(QStringLiteral("Nudge back"), QKeySequence(Qt::Key_Comma), [this] { transport_->nudge(-model_.project().sampleRate); });
    make(QStringLiteral("Nudge forward"), QKeySequence(Qt::Key_Period), [this] { transport_->nudge(model_.project().sampleRate); });

    make(QStringLiteral("Solo A"), QKeySequence(Qt::Key_A), [this] { abFader_->setValue(0.0, true); });
    make(QStringLiteral("Solo B"), QKeySequence(Qt::Key_B), [this] { abFader_->setValue(1.0, true); });
    make(QStringLiteral("Toggle A/B"), QKeySequence(Qt::Key_Backslash), [this] {
        abFader_->setValue(abFader_->value() < 0.5 ? 1.0 : 0.0, true);
    });
    make(QStringLiteral("Route track to A"), QKeySequence(Qt::SHIFT | Qt::Key_1), [this] { routeSelectedTrack(QStringLiteral("A")); });
    make(QStringLiteral("Route track to B"), QKeySequence(Qt::SHIFT | Qt::Key_2), [this] { routeSelectedTrack(QStringLiteral("B")); });

    make(QStringLiteral("Mute track"), QKeySequence(Qt::Key_M), [this] { toggleSelectedTrackField(core::TrackField::Mute); });
    make(QStringLiteral("Solo track"), QKeySequence(Qt::SHIFT | Qt::Key_S), [this] { toggleSelectedTrackField(core::TrackField::Solo); });
    make(QStringLiteral("Add track"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] {
        core::Track t;
        t.id = core::newId();
        t.name = QStringLiteral("Audio %1").arg(model_.project().tracks.size() + 1);
        model_.push(new core::AddTrackCommand(&model_, t));
    });

    auto* zin = make(QStringLiteral("Zoom in"), QKeySequence(Qt::Key_Plus), [this] { timeline_->zoomAt(1.25, QPoint(timeline_->viewport()->width() / 2, 0)); });
    zin->setShortcuts({ QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal) });
    make(QStringLiteral("Zoom out"), QKeySequence(Qt::Key_Minus), [this] { timeline_->zoomAt(0.8, QPoint(timeline_->viewport()->width() / 2, 0)); });
    make(QStringLiteral("Zoom to fit"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { timeline_->zoomToFit(); });
    make(QStringLiteral("Toggle theme"), QKeySequence(Qt::Key_T), [this] { toggleTheme(); });
    make(QStringLiteral("Next dock panel"), QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] {
        dock_->setCurrentIndex((dock_->currentIndex() + 1) % dock_->count());
    });
    auto* cheat = make(QStringLiteral("Shortcut cheat sheet"), QKeySequence(Qt::Key_Question), [this] {
        CheatSheet dlg(this);
        dlg.exec();
    });
    cheat->setShortcuts({ QKeySequence(Qt::Key_Question), QKeySequence(Qt::Key_F1) });
    make(QStringLiteral("About ses"), QKeySequence(), [this] {
        QMessageBox::about(this, QStringLiteral("ses"),
                           QStringLiteral("<b>ses 0.1.0</b> — local-first audio workstation.<br><br>"
                                          "UI-first slice: editing shell, theme system, undo history.<br>"
                                          "Audio engine: miniaudio device + RT mixer (P1). Inference (P5) "
                                          "is not connected yet.<br><br>"
                                          "No cloud. No telemetry. Your audio stays on this machine."));
    });

    const auto refreshUndo = [this] {
        undoAct_->setEnabled(model_.undoStack().canUndo());
        redoAct_->setEnabled(model_.undoStack().canRedo());
        undoAct_->setText(model_.undoStack().canUndo()
                              ? QStringLiteral("Undo %1").arg(model_.undoStack().undoText())
                              : QStringLiteral("Undo"));
        redoAct_->setText(model_.undoStack().canRedo()
                              ? QStringLiteral("Redo %1").arg(model_.undoStack().redoText())
                              : QStringLiteral("Redo"));
    };
    connect(&model_.undoStack(), &QUndoStack::indexChanged, this, refreshUndo);
    refreshUndo();
}

void MainWindow::buildMenus()
{
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    auto* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    auto* track = menuBar()->addMenu(QStringLiteral("&Track"));
    auto* transportMenu = menuBar()->addMenu(QStringLiteral("T&ransport"));
    auto* view = menuBar()->addMenu(QStringLiteral("&View"));
    auto* help = menuBar()->addMenu(QStringLiteral("&Help"));

    const auto acts = actions();
    const auto find = [&acts](const QString& text) -> QAction* {
        for (auto* a : acts)
            if (a->text() == text)
                return a;
        return nullptr;
    };
    const auto add = [](QMenu* m, QAction* a) { if (a) m->addAction(a); };

    add(file, find(QStringLiteral("Import audio…")));
    add(file, find(QStringLiteral("Open project…")));
    add(file, find(QStringLiteral("Save")));
    add(file, find(QStringLiteral("Export…")));
    file->addSeparator();
    add(file, find(QStringLiteral("Quit")));

    add(edit, undoAct_);
    add(edit, redoAct_);
    edit->addSeparator();
    add(edit, find(QStringLiteral("Split at playhead")));
    add(edit, find(QStringLiteral("Duplicate clips")));
    add(edit, deleteAct_);
    add(edit, find(QStringLiteral("Select all clips")));

    add(track, find(QStringLiteral("Add track")));
    add(track, find(QStringLiteral("Mute track")));
    add(track, find(QStringLiteral("Solo track")));
    track->addSeparator();
    add(track, find(QStringLiteral("Route track to A")));
    add(track, find(QStringLiteral("Route track to B")));

    add(transportMenu, find(QStringLiteral("Play / stop")));
    add(transportMenu, find(QStringLiteral("To start")));
    add(transportMenu, find(QStringLiteral("Loop")));
    add(transportMenu, find(QStringLiteral("Nudge back")));
    add(transportMenu, find(QStringLiteral("Nudge forward")));
    transportMenu->addSeparator();
    add(transportMenu, find(QStringLiteral("Solo A")));
    add(transportMenu, find(QStringLiteral("Solo B")));
    add(transportMenu, find(QStringLiteral("Toggle A/B")));

    add(view, find(QStringLiteral("Zoom in")));
    add(view, find(QStringLiteral("Zoom out")));
    add(view, find(QStringLiteral("Zoom to fit")));
    view->addSeparator();
    add(view, find(QStringLiteral("Toggle theme")));
    add(view, find(QStringLiteral("Next dock panel")));

    add(help, find(QStringLiteral("Shortcut cheat sheet")));
    add(help, find(QStringLiteral("About ses")));
}

// ---- wiring ---------------------------------------------------------------------------

void MainWindow::wireModel()
{
    connect(&model_, &core::ProjectModel::changed, this, [this] {
        refreshTitle();
        refreshTransport();
        positionEmptyState();
        // Sync A/B fader + mode from the model (undo, bus commands).
        abFader_->blockSignals(true);
        abFader_->setValue(model_.project().ab.fader);
        abFader_->blockSignals(false);
        abFader_->setMode(model_.project().ab.mode);
    });
    connect(transport_, &Transport::playheadChanged, this, [this](int64_t) { refreshTransport(); });
    connect(transport_, &Transport::playingChanged, this, [this](bool on) {
        playBtn_->blockSignals(true);
        playBtn_->setChecked(on);
        playBtn_->blockSignals(false);
    });
    connect(transport_, &Transport::loopChanged, this, [this](bool on) {
        loopBtn_->blockSignals(true);
        loopBtn_->setChecked(on);
        loopBtn_->blockSignals(false);
    });

    // P1 task 4: the 30 Hz telemetry drain (Transport) feeds the master meter.
    mixer_->setMeteringEnabled(!transport_->previewMode());
    connect(transport_, &Transport::telemetryUpdated, this,
            [this](const engine::Telemetry& t) { mixer_->setMasterPeak(t.masterPeak); });
    // Transport::deviceEvent is the P1 task 7 hook (device-loss recovery +
    // status bar); its ring drain already runs on the same 30 Hz timer.

    connect(timeline_, &TimelineWidget::selectionChanged, this, [this](const QStringList& ids) {
        ai_->setSelectionScope(ids);
        selectionLabel_->setText(ids.isEmpty() ? QStringLiteral("no selection")
                                               : QStringLiteral("%1 clip(s) selected").arg(ids.size()));
        deleteAct_->setEnabled(!ids.isEmpty());
    });
    deleteAct_->setEnabled(false);
    connect(timeline_, &TimelineWidget::seekRequested, transport_, &Transport::seek);
    connect(timeline_, &TimelineWidget::splitRequested, this, [this](const QString& id, int64_t s) {
        auto* cmd = new core::SplitClipsCommand(&model_, id, s);
        if (cmd->isValid())
            model_.push(cmd);
        else
            delete cmd;
    });
    connect(timeline_, &TimelineWidget::filesDropped, this, &MainWindow::importFiles);

    connect(headers_, &TrackHeaders::addTrackRequested, this, [this] {
        core::Track t;
        t.id = core::newId();
        t.name = QStringLiteral("Audio %1").arg(model_.project().tracks.size() + 1);
        model_.push(new core::AddTrackCommand(&model_, t));
    });
    connect(headers_, &TrackHeaders::renameRequested, this, [this](const QString& id) {
        const core::Track* t = model_.project().trackById(id);
        if (!t)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename track"),
                                                   QStringLiteral("Track name:"), QLineEdit::Normal, t->name, &ok);
        if (ok && !name.isEmpty() && name != t->name)
            model_.push(new core::SetTrackParamCommand(&model_, id, core::TrackField::Name, name));
    });
    connect(headers_, &TrackHeaders::verticalWheel, this, [this](int dy) {
        timeline_->verticalScrollBar()->setValue(timeline_->verticalScrollBar()->value() - dy);
    });
    connect(timeline_->verticalScrollBar(), &QScrollBar::valueChanged, headers_, &TrackHeaders::syncVertical);
    connect(timeline_, &TimelineWidget::zoomChanged, this, [this](double) { positionEmptyState(); });

    connect(abFader_, &AbFader::valueChanged, this, [this](double v) {
        model_.push(new core::SetAbFaderCommand(&model_, v));
    });
    connect(abFader_, &AbFader::modeChanged, this, [this](const QString& m) {
        model_.push(new core::SetAbModeCommand(&model_, m));
    });
    connect(abFader_, &AbFader::busAction, this, [this](int kind) {
        model_.push(new core::AbBusCommand(&model_, static_cast<core::AbBusCommand::Kind>(kind)));
    });

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { updateThemeIcon(); });
}

// ---- helpers ---------------------------------------------------------------------------

QString MainWindow::timeString(int64_t samples) const
{
    const int64_t rate = model_.project().sampleRate;
    const int64_t ms = samples * 1000 / std::max<int64_t>(1, rate);
    const int64_t h = ms / 3600000;
    const int64_t m = (ms / 60000) % 60;
    const int64_t s = (ms / 1000) % 60;
    const int64_t mm = ms % 1000;
    if (h == 0) {
        return QStringLiteral("%1:%2.%3")
            .arg(ms / 60000, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'))
            .arg(mm, 3, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(mm, 3, 10, QLatin1Char('0'));
}

void MainWindow::refreshTransport()
{
    timeDisplay_->setText(QStringLiteral("%1 / %2")
                              .arg(timeString(transport_->playhead()), timeString(model_.project().duration())));
    cursorLabel_->setText(timeString(transport_->playhead()));
}

void MainWindow::refreshTitle()
{
    const bool dirty = !model_.undoStack().isClean();
    setWindowTitle(QStringLiteral("%1%2 — ses").arg(model_.project().name, dirty ? QStringLiteral(" ●") : QString()));
}

void MainWindow::positionEmptyState()
{
    const bool empty = model_.project().clipCount() == 0;
    empty_->setVisible(empty);
    if (empty) {
        QWidget* vp = timeline_->viewport();
        empty_->move(std::max(0, (vp->width() - empty_->width()) / 2),
                     std::max(0, (vp->height() - empty_->height()) / 2));
        empty_->raise();
    }
}

void MainWindow::updateThemeIcon()
{
    themeBtn_->setIcon(sesIcon(ThemeManager::instance()->isDark() ? QStringLiteral("moon") : QStringLiteral("sun")));
}

QString MainWindow::selectedTrackId() const
{
    const QStringList sel = timeline_->selectedClips();
    if (!sel.isEmpty()) {
        const QString tid = model_.trackIdOfClip(sel.first());
        if (!tid.isEmpty())
            return tid;
    }
    const auto& tracks = model_.project().tracks;
    return tracks.isEmpty() ? QString() : tracks.first().id;
}

void MainWindow::routeSelectedTrack(const QString& routing)
{
    const QString tid = selectedTrackId();
    if (!tid.isEmpty())
        model_.push(new core::SetAbRoutingCommand(&model_, tid, routing));
}

void MainWindow::toggleSelectedTrackField(core::TrackField f)
{
    const QString tid = selectedTrackId();
    const core::Track* t = model_.project().trackById(tid);
    if (!t)
        return;
    const bool cur = f == core::TrackField::Mute ? t->mute : t->solo;
    model_.push(new core::SetTrackParamCommand(&model_, tid, f, !cur));
}

void MainWindow::removeSelection()
{
    const QStringList sel = timeline_->selectedClips();
    if (sel.isEmpty())
        return;
    model_.push(new core::RemoveClipsCommand(&model_, sel));
    toast_->show(QStringLiteral("Removed %1 clip(s) — Ctrl+Z to undo").arg(sel.size()));
}

void MainWindow::duplicateSelection()
{
    const QStringList sel = timeline_->selectedClips();
    if (sel.isEmpty())
        return;
    auto* macro = new QUndoCommand(QStringLiteral("Duplicate clips"));
    for (const auto& id : sel) {
        const core::Track* t = nullptr;
        const core::Clip* c = model_.project().clipById(id, &t);
        if (!c || !t)
            continue;
        core::Clip copy = *c;
        copy.id = core::newId();
        copy.timelineStart = c->end();
        new core::AddClipCommand(&model_, t->id, copy, -1, macro);
    }
    model_.push(macro);
}

void MainWindow::splitAtPlayhead()
{
    const int64_t s = transport_->playhead();
    const QStringList sel = timeline_->selectedClips();
    if (sel.isEmpty()) {
        toast_->show(QStringLiteral("Select a clip first, then press S to split at the playhead."), true);
        return;
    }
    auto* macro = new QUndoCommand(QStringLiteral("Split clips"));
    int valid = 0;
    for (const auto& id : sel) {
        auto* cmd = new core::SplitClipsCommand(&model_, id, s, macro);
        if (cmd->isValid())
            ++valid;
    }
    if (valid == 0) {
        delete macro;
        toast_->show(QStringLiteral("Playhead is outside the selected clip(s)."), true);
        return;
    }
    if (valid == 1)
        macro->setText(QStringLiteral("Split clip"));
    model_.push(macro);
}

void MainWindow::importFiles(const QStringList& paths)
{
    const bool firstImport = model_.project().clipCount() == 0;
    int imported = 0;
    for (const auto& path : paths) {
        const QFileInfo fi(path);
        if (fi.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) != 0) {
            toast_->show(QStringLiteral("%1: FLAC/MP3/OGG decode arrives with the audio engine (P1). WAV imports now.").arg(fi.fileName()), true);
            continue;
        }
        const core::MediaInfo info = core::probeWav(path);
        if (!info.ok) {
            toast_->show(QStringLiteral("%1: %2").arg(fi.fileName(), info.error), true);
            continue;
        }
        core::MediaItem m;
        m.id = core::newId();
        m.path = path;
        m.format = QStringLiteral("wav");
        m.sampleRate = info.sampleRate;
        m.channels = info.channels;
        m.frames = info.frames;
        m.contentHash = sha256File(path);

        core::Clip c;
        c.id = core::newId();
        c.name = fi.completeBaseName();
        c.length = info.frames;
        c.timelineStart = model_.project().duration();

        model_.push(new core::ImportMediaCommand(&model_, m, c, selectedTrackId()));
        ++imported;
        if (info.sampleRate != model_.project().sampleRate)
            toast_->show(QStringLiteral("%1 is %2 Hz — resampling to the 48 kHz project rate arrives with the engine (P1).")
                             .arg(fi.fileName()).arg(info.sampleRate));
    }
    if (imported > 0) {
        toast_->show(QStringLiteral("Imported %1 file(s) — Ctrl+Z to undo").arg(imported));
        if (firstImport)
            timeline_->zoomToFit();
    }
}

void MainWindow::toggleTheme()
{
    applyThemeMode(ThemeManager::instance()->isDark() ? QStringLiteral("light") : QStringLiteral("dark"));
}

void MainWindow::applyThemeMode(const QString& mode)
{
    settings_.theme = mode;
    settings_.save();
    ThemeManager::instance()->setMode(ThemeManager::modeFromString(mode));
}

// ---- events ---------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    // Global shortcuts must not steal keys from text fields (§10.12).
    if (obj == qApp && ev->type() == QEvent::ShortcutOverride) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw)
                   || qobject_cast<QPlainTextEdit*>(fw) || qobject_cast<QAbstractSpinBox*>(fw))) {
            ev->accept();
            return true;
        }
    }
    if (timeline_ && obj == timeline_->viewport() && ev->type() == QEvent::Resize)
        positionEmptyState();
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* ev)
{
    if (ev->mimeData()->hasUrls())
        ev->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* ev)
{
    QStringList paths;
    for (const auto& url : ev->mimeData()->urls())
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    if (!paths.isEmpty()) {
        importFiles(paths);
        ev->acceptProposedAction();
    }
}

void MainWindow::closeEvent(QCloseEvent* ev)
{
    settings_.save();
    ev->accept();
}

void MainWindow::resizeEvent(QResizeEvent* ev)
{
    QMainWindow::resizeEvent(ev);
    positionEmptyState();
}

} // namespace ses::ui