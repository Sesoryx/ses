#pragma once
// TimelineWidget — custom QPainter NLE surface (DESIGN.md §10.9/§10.3).
// Not QGraphicsView, not QML: sample-accurate zoom, virtualized drawing,
// waveform LOD blitting, full hit-test control. Every gesture commits one
// undoable command on release (drag coalescing).

#include "core/media/MediaProbe.h"
#include "core/model/ProjectModel.h"
#include "ui/shell/Transport.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QMap>

namespace ses::ui {

enum class Tool { Select, Razor, Fade, Automation };

class TimelineWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    static constexpr int kRulerHeight = 30;

    TimelineWidget(core::ProjectModel* model, Transport* transport, QWidget* parent = nullptr);

    Tool tool() const { return tool_; }
    void setTool(Tool t);

    double zoom() const { return pxPerSec_; }
    void setZoom(double pxPerSec); // anchored at viewport center
    void zoomAt(double factor, QPoint viewportPos);
    void zoomToFit();

    int trackHeight() const { return trackH_; }
    void setTrackHeight(int h);

    bool snap() const { return snap_; }
    void setSnap(bool on) { snap_ = on; }

    QStringList selectedClips() const { return selection_; }
    void selectClips(const QStringList& ids);
    void clearSelection();
    void selectAll();

    int64_t sampleAtX(int viewportX) const;
    int xOfSample(int64_t sample) const;
    int laneTop(int trackIndex) const;

signals:
    void selectionChanged(const QStringList& clipIds);
    void seekRequested(int64_t sample);
    void splitRequested(const QString& clipId, int64_t sample);
    void filesDropped(const QStringList& paths);
    void zoomChanged(double pxPerSec);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    enum class Drag { None, Move, TrimLeft, TrimRight, Slip, FadeIn, FadeOut, Scrub, Marquee };
    struct Hit {
        Drag kind = Drag::None;
        QString clipId;
        int track = -1;
        QRectF rect;
    };

    void updateScrollbars();
    void drawRuler(QPainter& p);
    void drawClip(QPainter& p, const core::Track& track, const core::Clip& clip, int laneY);
    const core::Peaks& peaksFor(const QString& mediaId, const QString& path, int64_t frames);
    Hit hitTest(const QPoint& pos) const;
    int trackAtY(int y) const;
    double niceStepSec() const;
    int64_t snapSample(int64_t sample, const QString& excludeClip) const;
    void applyHover(const QPoint& pos);
    void commitDrag();
    QString timeLabel(double sec) const;

    core::ProjectModel* model_;
    Transport* transport_;
    Tool tool_ = Tool::Select;
    double pxPerSec_ = 100.0;
    int trackH_ = 84;
    bool snap_ = true;
    QStringList selection_;

    Drag drag_ = Drag::None;
    QString dragClip_;
    QPoint pressPos_;
    QMap<QString, core::ClipTrim> origTrims_;
    core::Fade origFade_;
    QMap<QString, core::ClipTrim> preview_;
    QMap<QString, core::Fade> previewFades_; // key: clipId + "|in" / "|out"
    QRect marquee_;
    bool marqueeAdditive_ = false;

    QString hoverClip_;
    Drag hoverKind_ = Drag::None;
    QHash<QString, core::Peaks> peaksCache_;
};

} // namespace ses::ui