#pragma once
// MainWindow — the ses shell (DESIGN.md §10.1/§10.11): persistent transport
// bar, track headers + timeline, collapsible mixer, right dock (AI/History/
// Models), quiet status bar. Widgets-only, styled entirely by QSS.

#include "core/media/MediaCache.h"
#include "core/model/ProjectModel.h"
#include "core/persistence/Settings.h"
#include "engine/Engine.h"
#include "engine/io/Decode.h"

#include <QMainWindow>

class QLabel;
class QToolButton;
class QSplitter;
class QTabWidget;
class QAction;
class QActionGroup;

namespace ses::ui {

class Transport;
class TimelineWidget;
class TrackHeaders;
class AbFader;
class MixerPanel;
class HistoryPanel;
class AiPanel;
class ModelManagerPanel;
class EmptyState;
class Toast;
class CoachMarks;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    core::ProjectModel& model() { return model_; }
    core::MediaCache& mediaCache() { return mediaCache_; }
    engine::DecodePool& decodePool() { return decodePool_; }
    Transport* transport() const { return transport_; }
    TimelineWidget* timeline() const { return timeline_; }
    AbFader* abFader() const { return abFader_; }
    core::Settings& settings() { return settings_; }

    void importFiles(const QStringList& paths);
    void splitAtPlayhead();
    void toggleTheme();
    void applyThemeMode(const QString& mode); // dark | light | system (persisted)
    QString timeString(int64_t samples) const;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;
    void closeEvent(QCloseEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    void buildTransportBar(QWidget* central);
    void buildEditor(QWidget* central);
    void buildDock();
    void buildStatusBar();
    void buildActions();
    void buildMenus();
    void wireModel();
    void refreshTransport();
    void refreshTitle();
    void positionEmptyState();
    void updateThemeIcon();
    QString selectedTrackId() const;
    void routeSelectedTrack(const QString& routing);
    void toggleSelectedTrackField(core::TrackField f);
    void removeSelection();
    void duplicateSelection();

    core::Settings settings_;
    core::ProjectModel model_;
    engine::DeviceEngine engine_;
    core::MediaCache mediaCache_; // decoded arenas (GUI-side, DESIGN §4.3)
    engine::DecodePool decodePool_;
    Transport* transport_ = nullptr;

    TimelineWidget* timeline_ = nullptr;
    TrackHeaders* headers_ = nullptr;
    MixerPanel* mixer_ = nullptr;
    AbFader* abFader_ = nullptr;
    QTabWidget* dock_ = nullptr;
    AiPanel* ai_ = nullptr;
    HistoryPanel* history_ = nullptr;
    ModelManagerPanel* models_ = nullptr;
    EmptyState* empty_ = nullptr;
    Toast* toast_ = nullptr;
    CoachMarks* coach_ = nullptr;
    QSplitter* mainSplit_ = nullptr;
    QSplitter* editorSplit_ = nullptr;

    QToolButton* playBtn_ = nullptr;
    QToolButton* loopBtn_ = nullptr;
    QToolButton* themeBtn_ = nullptr;
    QLabel* timeDisplay_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    QLabel* cursorLabel_ = nullptr;
    QLabel* engineBadge_ = nullptr;

    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
    QAction* deleteAct_ = nullptr;
    QActionGroup* toolGroup_ = nullptr;
};

} // namespace ses::ui