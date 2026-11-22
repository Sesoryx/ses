#include "ui/timeline/TimelineWidget.h"

#include "core/commands/Commands.h"
#include "core/render/FadeCurve.h"
#include "ui/theme/ThemeManager.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ses::ui {

using core::Clip;
using core::ClipTrim;
using core::Track;

namespace {
QColor blend(const QColor& a, const QColor& b, double t)
{
    return QColor(int(a.red() * (1 - t) + b.red() * t),
                  int(a.green() * (1 - t) + b.green() * t),
                  int(a.blue() * (1 - t) + b.blue() * t));
}
QColor alpha(QColor c, int a)
{
    c.setAlpha(a);
    return c;
}
} // namespace

TimelineWidget::TimelineWidget(core::ProjectModel* model, Transport* transport, QWidget* parent)
    : QAbstractScrollArea(parent), model_(model), transport_(transport)
{
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->setAcceptDrops(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    horizontalScrollBar()->setSingleStep(24);
    verticalScrollBar()->setSingleStep(24);

    connect(model_, &core::ProjectModel::changed, this, [this] {
        updateScrollbars();
        viewport()->update();
    });
    connect(model_, &core::ProjectModel::structureChanged, this, [this] {
        // Prune selection of clips that no longer exist.
        QStringList alive;
        for (const auto& id : selection_)
            if (model_->project().clipById(id))
                alive.append(id);
        if (alive.size() != selection_.size()) {
            selection_ = alive;
            emit selectionChanged(selection_);
        }
        updateScrollbars();
        viewport()->update();
    });
    connect(transport_, &Transport::playheadChanged, this, [this](int64_t) { viewport()->update(); });
    connect(transport_, &Transport::loopChanged, this, [this](bool) { viewport()->update(); });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { viewport()->update(); });

    updateScrollbars();
}

void TimelineWidget::setTool(Tool t)
{
    tool_ = t;
    viewport()->setCursor(t == Tool::Razor ? Qt::CrossCursor : Qt::ArrowCursor);
    viewport()->update();
}

// ---- coordinates ---------------------------------------------------------------

int64_t TimelineWidget::sampleAtX(int viewportX) const
{
    const double sec = (viewportX + horizontalScrollBar()->value()) / pxPerSec_;
    return std::max<int64_t>(0, int64_t(sec * model_->project().sampleRate));
}

int TimelineWidget::xOfSample(int64_t sample) const
{
    return int(double(sample) / model_->project().sampleRate * pxPerSec_) - horizontalScrollBar()->value();
}

int TimelineWidget::laneTop(int trackIndex) const
{
    return kRulerHeight + trackIndex * trackH_ - verticalScrollBar()->value();
}

int TimelineWidget::trackAtY(int y) const
{
    if (y < kRulerHeight)
        return -1;
    const int i = (y - kRulerHeight + verticalScrollBar()->value()) / trackH_;
    return (i >= 0 && i < model_->project().tracks.size()) ? i : -1;
}

double TimelineWidget::niceStepSec() const
{
    static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                    1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
    for (double s : steps)
        if (s * pxPerSec_ >= 80)
            return s;
    return 1200;
}

QString TimelineWidget::timeLabel(double sec) const
{
    if (pxPerSec_ > 4000) {
        return QString::number(int64_t(sec * model_->project().sampleRate));
    }
    const int64_t ms = int64_t(std::llround(sec * 1000.0));
    const int64_t m = ms / 60000;
    const int64_t s = (ms / 1000) % 60;
    const int64_t mm = ms % 1000;
    if (m > 0)
        return QStringLiteral("%1:%2.%3").arg(m).arg(s, 2, 10, QLatin1Char('0')).arg(mm, 3, 10, QLatin1Char('0'));
    return QStringLiteral("%1.%2").arg(s).arg(mm, 3, 10, QLatin1Char('0'));
}

void TimelineWidget::updateScrollbars()
{
    const auto& proj = model_->project();
    const int64_t dur = std::max<int64_t>(proj.duration(), proj.sampleRate * 10);
    const int contentW = int(double(dur) / proj.sampleRate * pxPerSec_) + 240;
    horizontalScrollBar()->setRange(0, std::max(0, contentW - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    const int contentH = int(proj.tracks.size()) * trackH_ + 60;
    verticalScrollBar()->setRange(0, std::max(0, contentH - (viewport()->height() - kRulerHeight)));
    verticalScrollBar()->setPageStep(std::max(1, viewport()->height() - kRulerHeight));
}

// ---- zoom ------------------------------------------------------------------------

void TimelineWidget::setZoom(double pxPerSec)
{
    zoomAt(pxPerSec / pxPerSec_, QPoint(viewport()->width() / 2, 0));
}

void TimelineWidget::zoomAt(double factor, QPoint viewportPos)
{
    const int64_t anchor = sampleAtX(viewportPos.x());
    pxPerSec_ = std::clamp(pxPerSec_ * factor, 2.0, 20000.0);
    updateScrollbars();
    const int target = int(double(anchor) / model_->project().sampleRate * pxPerSec_) - viewportPos.x();
    horizontalScrollBar()->setValue(std::clamp(target, 0, horizontalScrollBar()->maximum()));
    emit zoomChanged(pxPerSec_);
    viewport()->update();
}

void TimelineWidget::zoomToFit()
{
    const int64_t dur = model_->project().duration();
    if (dur <= 0)
        return;
    const double sec = double(dur) / model_->project().sampleRate;
    pxPerSec_ = std::clamp((viewport()->width() - 48) / sec, 2.0, 20000.0);
    horizontalScrollBar()->setValue(0);
    updateScrollbars();
    emit zoomChanged(pxPerSec_);
    viewport()->update();
}

void TimelineWidget::setTrackHeight(int h)
{
    trackH_ = std::clamp(h, 56, 160);
    updateScrollbars();
    viewport()->update();
}

// ---- selection ---------------------------------------------------------------------

void TimelineWidget::selectClips(const QStringList& ids)
{
    selection_ = ids;
    emit selectionChanged(selection_);
    viewport()->update();
}

void TimelineWidget::clearSelection()
{
    if (!selection_.isEmpty()) {
        selection_.clear();
        emit selectionChanged(selection_);
        viewport()->update();
    }
}

void TimelineWidget::selectAll()
{
    QStringList all;
    for (const auto& t : model_->project().tracks)
        for (const auto& c : t.clips)
            all.append(c.id);
    selectClips(all);
}

// ---- painting ------------------------------------------------------------------------

void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    ThemeManager* th = ThemeManager::instance();
    const auto& proj = model_->project();
    const int W = viewport()->width();
    const int H = viewport()->height();

    p.fillRect(viewport()->rect(), th->color(QStringLiteral("bg")));

    // Lane backgrounds + grid (under clips).
    const double step = niceStepSec();
    const double startSec = horizontalScrollBar()->value() / pxPerSec_;
    for (int i = 0; i < proj.tracks.size(); ++i) {
        const int y = laneTop(i);
        if (y + trackH_ < kRulerHeight || y > H)
            continue;
        if (i % 2 == 1)
            p.fillRect(QRect(0, y, W, trackH_), th->color(QStringLiteral("bgAlt")));
        p.setPen(th->color(QStringLiteral("border")));
        p.drawLine(0, y + trackH_ - 1, W, y + trackH_ - 1);
    }
    p.setPen(QPen(th->color(QStringLiteral("grid")), 1));
    for (double t = std::floor(startSec / step) * step; t * pxPerSec_ - horizontalScrollBar()->value() < W; t += step) {
        const int x = int(t * pxPerSec_) - horizontalScrollBar()->value();
        if (x >= 0)
            p.drawLine(x, kRulerHeight, x, H);
    }

    // Clips.
    const int64_t s0 = sampleAtX(0);
    const int64_t s1 = sampleAtX(W);
    for (int i = 0; i < proj.tracks.size(); ++i) {
        const int y = laneTop(i);
        if (y + trackH_ < kRulerHeight || y > H)
            continue;
        const Track& track = proj.tracks[i];
        for (const auto& clip : track.clips) {
            const ClipTrim tr = preview_.value(clip.id, ClipTrim{ clip.sourceOffset, clip.length, clip.timelineStart });
            if (tr.timelineStart + tr.length < s0 || tr.timelineStart > s1)
                continue;
            drawClip(p, track, clip, y);
        }
    }

    // Loop band in ruler.
    if (transport_->loop() && transport_->loopEnd() > transport_->loopStart()) {
        const int lx = xOfSample(transport_->loopStart());
        const int rx = xOfSample(transport_->loopEnd());
        p.fillRect(QRect(lx, 0, rx - lx, kRulerHeight), alpha(th->color(QStringLiteral("accent")), 36));
        p.setPen(QPen(th->color(QStringLiteral("accent")), 1));
        p.drawLine(lx, 0, lx, kRulerHeight);
        p.drawLine(rx, 0, rx, kRulerHeight);
    }

    drawRuler(p);

    // Playhead (glides at the 30 Hz telemetry rate).
    const int px = xOfSample(transport_->playhead());
    if (px >= 0 && px <= W) {
        const QColor ph = th->color(QStringLiteral("playhead"));
        p.setPen(QPen(ph, 1.5));
        p.drawLine(QPointF(px + 0.5, kRulerHeight), QPointF(px + 0.5, H));
        p.setPen(Qt::NoPen);
        p.setBrush(ph);
        const QPointF tri[] = { QPointF(px - 5, kRulerHeight - 9), QPointF(px + 5, kRulerHeight - 9), QPointF(px, kRulerHeight) };
        p.drawPolygon(tri, 3);
    }

    // Marquee.
    if (drag_ == Drag::Marquee && !marquee_.isNull()) {
        p.setBrush(alpha(th->color(QStringLiteral("selection")), 40));
        p.setPen(QPen(th->color(QStringLiteral("selection")), 1));
        p.drawRect(marquee_);
    }
}

void TimelineWidget::drawRuler(QPainter& p)
{
    ThemeManager* th = ThemeManager::instance();
    const int W = viewport()->width();
    p.fillRect(QRect(0, 0, W, kRulerHeight), th->color(QStringLiteral("panel")));
    p.setPen(th->color(QStringLiteral("border")));
    p.drawLine(0, kRulerHeight - 1, W, kRulerHeight - 1);

    const double step = niceStepSec();
    const double startSec = horizontalScrollBar()->value() / pxPerSec_;
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    for (double t = std::floor(startSec / step) * step; t * pxPerSec_ - horizontalScrollBar()->value() < W; t += step) {
        const int x = int(t * pxPerSec_) - horizontalScrollBar()->value();
        if (x < 0)
            continue;
        p.setPen(th->color(QStringLiteral("ruler")));
        p.drawLine(x, kRulerHeight - 7, x, kRulerHeight - 1);
        p.drawText(x + 4, 12, timeLabel(t));
        // Minor ticks.
        for (int k = 1; k < 5; ++k) {
            const int mx = int((t + step * k / 5.0) * pxPerSec_) - horizontalScrollBar()->value();
            if (mx >= 0 && mx < W)
                p.drawLine(mx, kRulerHeight - 4, mx, kRulerHeight - 1);
        }
    }
}

void TimelineWidget::drawClip(QPainter& p, const Track& track, const Clip& clip, int laneY)
{
    ThemeManager* th = ThemeManager::instance();
    const auto& proj = model_->project();
    const double rate = proj.sampleRate;
    const ClipTrim tr = preview_.value(clip.id, ClipTrim{ clip.sourceOffset, clip.length, clip.timelineStart });
    const double x = xOfSample(tr.timelineStart);
    const double w = std::max(2.0, double(tr.length) / rate * pxPerSec_);
    const QRectF r(x, laneY + 5, w, trackH_ - 10);

    QColor fill = blend(th->color(QStringLiteral("clipFill")), QColor(track.color), 0.16);
    if (clip.muted)
        fill = blend(fill, th->color(QStringLiteral("bg")), 0.55);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(r, 4, 4);

    // Track-color identity strip.
    p.fillRect(QRectF(x, laneY + 5, w, 3), alpha(QColor(track.color), 170));

    // Waveform (decoded peak-LOD blit, P1 task 5): pick the pyramid level
    // whose buckets fit one pixel column and fold the few it spans per column.
    const core::MediaItem* media = proj.mediaById(clip.mediaId);
    const double cy = r.center().y();
    const double halfH = (r.height() - 12) / 2.0;
    if (media && media->frames > 0) {
        const std::shared_ptr<const core::AudioBuffer> buffer =
            mediaCache_ ? mediaCache_->find(media->path) : nullptr;
        if (buffer && buffer->peaks.valid()) {
            const int64_t spb = std::max<int64_t>(1, int64_t(rate / pxPerSec_));
            const core::PeakLevel& level = buffer->peaks.pickLevel(spb);
            p.setPen(QPen(th->color(QStringLiteral("waveform")), 1));
            const int c0 = int(std::max(r.left(), 0.0));
            const int c1 = int(std::min(r.right(), double(viewport()->width())));
            const int64_t span = level.samplesPerBucket;
            for (int col = c0; col < c1; ++col) {
                const int64_t s0 = tr.sourceOffset + int64_t((col - x) / pxPerSec_ * rate);
                const int64_t s1 = std::max(
                    s0 + 1, tr.sourceOffset + int64_t((col + 1 - x) / pxPerSec_ * rate));
                int64_t b0 = s0 / span;
                int64_t b1 = (s1 - 1) / span;
                if (b1 < 0 || b0 >= level.min.size())
                    continue;
                b0 = std::max<int64_t>(b0, 0);
                b1 = std::min<int64_t>(b1, level.min.size() - 1);
                float mn = level.min[int(b0)];
                float mx = level.max[int(b0)];
                for (int64_t b = b0 + 1; b <= b1; ++b) {
                    mn = std::min(mn, level.min[int(b)]);
                    mx = std::max(mx, level.max[int(b)]);
                }
                p.drawLine(QPointF(col + 0.5, cy - mx * halfH), QPointF(col + 0.5, cy - mn * halfH));
            }
        }
    } else {
        p.setPen(th->color(QStringLiteral("textSecondary")));
        p.drawText(r, Qt::AlignCenter, QStringLiteral("decode pending"));
    }

    // Fades: curve + shaded region + corner handle.
    const auto drawFade = [&](bool in) {
        const QString key = clip.id + (in ? QStringLiteral("|in") : QStringLiteral("|out"));
        const core::Fade fade = previewFades_.value(key, in ? clip.fadeIn : clip.fadeOut);
        if (fade.length <= 0)
            return;
        const double fw = std::min(w, double(fade.length) / rate * pxPerSec_);
        const auto curve = core::fadeCurveFromString(fade.curve);
        QPainterPath path;
        const int N = 16;
        for (int i = 0; i <= N; ++i) {
            const double t = double(i) / N;
            const double g = in ? core::fadeInGain(curve, t) : core::fadeOutGain(curve, t);
            const double px = in ? x + t * fw : x + w - t * fw;
            const double py = r.bottom() - g * r.height();
            if (i == 0)
                path.moveTo(px, py);
            else
                path.lineTo(px, py);
        }
        QPainterPath shade = path;
        if (in) {
            shade.lineTo(x + fw, r.top());
            shade.lineTo(x, r.top());
        } else {
            shade.lineTo(x + w - fw, r.top());
            shade.lineTo(x + w, r.top());
        }
        shade.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(th->color(QStringLiteral("bg")), 130));
        p.drawPath(shade);
        p.setPen(QPen(th->color(QStringLiteral("waveformPeak")), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        // Corner handle.
        const bool hot = hoverClip_ == clip.id && ((in && hoverKind_ == Drag::FadeIn) || (!in && hoverKind_ == Drag::FadeOut));
        p.setBrush(hot || selection_.contains(clip.id) ? th->color(QStringLiteral("accent")) : alpha(th->color(QStringLiteral("textSecondary")), 160));
        QPointF h[3];
        if (in) {
            h[0] = QPointF(x, r.top());
            h[1] = QPointF(x + 10, r.top());
            h[2] = QPointF(x, r.top() + 10);
        } else {
            h[0] = QPointF(x + w, r.top());
            h[1] = QPointF(x + w - 10, r.top());
            h[2] = QPointF(x + w, r.top() + 10);
        }
        p.drawPolygon(h, 3);
    };
    drawFade(true);
    drawFade(false);

    // Muted hatch.
    if (clip.muted) {
        p.setPen(QPen(alpha(th->color(QStringLiteral("border")), 160), 1));
        for (double hx = r.left() - r.height(); hx < r.right(); hx += 8)
            p.drawLine(QPointF(std::max(hx, r.left()), r.bottom()), QPointF(std::min(hx + r.height(), r.right()), r.top()));
    }

    // Name.
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(clip.muted ? th->color(QStringLiteral("textSecondary")) : th->color(QStringLiteral("text")));
    const QString name = clip.name.isEmpty() ? QStringLiteral("(unnamed)") : clip.name;
    p.drawText(QRectF(x + 5, laneY + 9, w - 24, 14), Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(name, Qt::ElideRight, int(w - 24)));

    // A/B routing chip (color + letter, never color alone).
    if (track.abRouting != QLatin1String("both")) {
        const bool isA = track.abRouting == QLatin1String("A");
        p.setPen(isA ? th->color(QStringLiteral("accent")) : th->color(QStringLiteral("warning")));
        QFont bf = font();
        bf.setPixelSize(10);
        bf.setBold(true);
        p.setFont(bf);
        p.drawText(QRectF(x + w - 18, laneY + 9, 14, 14), Qt::AlignCenter, isA ? QStringLiteral("A") : QStringLiteral("B"));
    }

    // Selection outline.
    if (selection_.contains(clip.id)) {
        p.setPen(QPen(th->color(QStringLiteral("selection")), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 4, 4);
    }
}

// ---- hit testing ---------------------------------------------------------------------

TimelineWidget::Hit TimelineWidget::hitTest(const QPoint& pos) const
{
    Hit h;
    if (pos.y() < kRulerHeight) {
        h.kind = Drag::Scrub;
        return h;
    }
    const int ti = trackAtY(pos.y());
    if (ti < 0)
        return h;
    h.track = ti;
    const Track& track = model_->project().tracks[ti];
    for (int i = int(track.clips.size()) - 1; i >= 0; --i) {
        const Clip& c = track.clips[i];
        const double x = xOfSample(c.timelineStart);
        const double w = std::max(2.0, double(c.length) / model_->project().sampleRate * pxPerSec_);
        const QRectF r(x, laneTop(ti) + 5, w, trackH_ - 10);
        if (!r.adjusted(-2, 0, 2, 0).contains(pos))
            continue;
        h.clipId = c.id;
        h.rect = r;
        const bool nearTop = pos.y() < r.top() + 13;
        if (nearTop && pos.x() < r.left() + 12 && c.fadeIn.length > 0)
            h.kind = Drag::FadeIn;
        else if (nearTop && pos.x() > r.right() - 12 && c.fadeOut.length > 0)
            h.kind = Drag::FadeOut;
        else if (pos.x() <= r.left() + 5)
            h.kind = Drag::TrimLeft;
        else if (pos.x() >= r.right() - 5)
            h.kind = Drag::TrimRight;
        else
            h.kind = Drag::Move;
        return h;
    }
    return h;
}

int64_t TimelineWidget::snapSample(int64_t sample, const QString& excludeClip) const
{
    const int64_t rate = model_->project().sampleRate;
    const int64_t threshold = int64_t(8.0 / pxPerSec_ * rate);
    QVector<int64_t> targets{ 0, transport_->playhead() };
    for (const auto& t : model_->project().tracks)
        for (const auto& c : t.clips) {
            if (c.id == excludeClip)
                continue;
            targets.append(c.timelineStart);
            targets.append(c.end());
        }
    const double step = niceStepSec();
    targets.append(int64_t(std::llround(sample / (step * rate)) * step * rate));
    int64_t best = sample;
    int64_t bestDist = threshold + 1;
    for (int64_t t : targets) {
        const int64_t d = std::abs(t - sample);
        if (d < bestDist) {
            bestDist = d;
            best = t;
        }
    }
    return bestDist <= threshold ? best : sample;
}

void TimelineWidget::applyHover(const QPoint& pos)
{
    const Hit h = hitTest(pos);
    QString clip;
    Drag kind = Drag::None;
    Qt::CursorShape cursor = Qt::ArrowCursor;
    if (tool_ == Tool::Razor) {
        cursor = Qt::CrossCursor;
    } else if (h.kind == Drag::Scrub) {
        cursor = Qt::PointingHandCursor;
    } else if (!h.clipId.isEmpty()) {
        clip = h.clipId;
        kind = h.kind;
        switch (h.kind) {
        case Drag::TrimLeft:
        case Drag::TrimRight: cursor = Qt::SizeHorCursor; break;
        case Drag::FadeIn:
        case Drag::FadeOut: cursor = Qt::SizeBDiagCursor; break;
        default: cursor = Qt::SizeAllCursor; break;
        }
    }
    if (clip != hoverClip_ || kind != hoverKind_) {
        hoverClip_ = clip;
        hoverKind_ = kind;
        viewport()->update();
    }
    viewport()->setCursor(cursor);
}

// ---- mouse ---------------------------------------------------------------------------

void TimelineWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton)
        return;
    const QPoint pos = ev->position().toPoint();
    pressPos_ = pos;
    const Hit h = hitTest(pos);

    if (h.kind == Drag::Scrub) {
        drag_ = Drag::Scrub;
        emit seekRequested(sampleAtX(pos.x()));
        return;
    }
    if (tool_ == Tool::Razor && !h.clipId.isEmpty()) {
        emit splitRequested(h.clipId, sampleAtX(pos.x()));
        return;
    }
    if (h.clipId.isEmpty()) {
        marqueeAdditive_ = ev->modifiers() & Qt::ShiftModifier;
        if (!marqueeAdditive_)
            clearSelection();
        drag_ = Drag::Marquee;
        marquee_ = QRect(pos, pos);
        viewport()->update();
        return;
    }

    // Selection semantics.
    if (ev->modifiers() & Qt::ShiftModifier) {
        if (selection_.contains(h.clipId))
            selection_.removeAll(h.clipId);
        else
            selection_.append(h.clipId);
        emit selectionChanged(selection_);
    } else if (!selection_.contains(h.clipId)) {
        selection_ = { h.clipId };
        emit selectionChanged(selection_);
    }

    dragClip_ = h.clipId;
    origTrims_.clear();
    for (const auto& id : selection_)
        origTrims_.insert(id, model_->clipTrim(id));
    const Clip c = model_->clipCopy(h.clipId);
    origFade_ = (h.kind == Drag::FadeOut) ? c.fadeOut : c.fadeIn;

    if (tool_ == Tool::Fade)
        drag_ = pos.x() < h.rect.center().x() ? Drag::FadeIn : Drag::FadeOut;
    else if (h.kind == Drag::Move && (ev->modifiers() & Qt::AltModifier))
        drag_ = Drag::Slip;
    else
        drag_ = h.kind;
    viewport()->update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* ev)
{
    const QPoint pos = ev->position().toPoint();
    if (drag_ == Drag::None) {
        applyHover(pos);
        return;
    }
    const double rate = model_->project().sampleRate;
    const int64_t delta = int64_t((pos.x() - pressPos_.x()) / pxPerSec_ * rate);
    const bool noSnap = ev->modifiers() & Qt::ControlModifier;

    switch (drag_) {
    case Drag::Scrub:
        emit seekRequested(sampleAtX(pos.x()));
        break;
    case Drag::Marquee:
        marquee_ = QRect(pressPos_, pos).normalized();
        viewport()->update();
        break;
    case Drag::Move: {
        preview_.clear();
        for (auto it = origTrims_.begin(); it != origTrims_.end(); ++it) {
            int64_t ns = std::max<int64_t>(0, it.value().timelineStart + delta);
            if (snap_ && !noSnap)
                ns = std::max<int64_t>(0, snapSample(ns, it.key()));
            preview_.insert(it.key(), ClipTrim{ it.value().sourceOffset, it.value().length, ns });
        }
        viewport()->update();
        break;
    }
    case Drag::TrimLeft: {
        const ClipTrim o = origTrims_.value(dragClip_);
        const int64_t end = o.timelineStart + o.length;
        const int64_t minStart = std::max<int64_t>(0, o.timelineStart - o.sourceOffset);
        int64_t ns = std::clamp<int64_t>(o.timelineStart + delta, minStart, end - 1);
        if (snap_ && !noSnap)
            ns = std::clamp<int64_t>(snapSample(ns, dragClip_), minStart, end - 1);
        preview_.insert(dragClip_, ClipTrim{ o.sourceOffset + (ns - o.timelineStart), end - ns, ns });
        viewport()->update();
        break;
    }
    case Drag::TrimRight: {
        const ClipTrim o = origTrims_.value(dragClip_);
        int64_t maxLen = o.length;
        if (const core::MediaItem* m = model_->project().mediaById(model_->clipCopy(dragClip_).mediaId))
            maxLen = m->frames - o.sourceOffset;
        int64_t nl = std::clamp<int64_t>(o.length + delta, 1, maxLen);
        if (snap_ && !noSnap) {
            const int64_t snappedEnd = snapSample(o.timelineStart + nl, dragClip_);
            nl = std::clamp<int64_t>(snappedEnd - o.timelineStart, 1, maxLen);
        }
        preview_.insert(dragClip_, ClipTrim{ o.sourceOffset, nl, o.timelineStart });
        viewport()->update();
        break;
    }
    case Drag::Slip: {
        const ClipTrim o = origTrims_.value(dragClip_);
        int64_t maxOff = o.sourceOffset;
        if (const core::MediaItem* m = model_->project().mediaById(model_->clipCopy(dragClip_).mediaId))
            maxOff = m->frames - o.length;
        const int64_t no = std::clamp<int64_t>(o.sourceOffset + delta, 0, std::max<int64_t>(0, maxOff));
        preview_.insert(dragClip_, ClipTrim{ no, o.length, o.timelineStart });
        viewport()->update();
        break;
    }
    case Drag::FadeIn:
    case Drag::FadeOut: {
        const bool in = drag_ == Drag::FadeIn;
        const int64_t nl = std::clamp<int64_t>(origFade_.length + (in ? delta : -delta), 0, origTrims_.value(dragClip_).length);
        previewFades_.insert(dragClip_ + (in ? QStringLiteral("|in") : QStringLiteral("|out")),
                             core::Fade{ nl, origFade_.curve, {} });
        viewport()->update();
        break;
    }
    default:
        break;
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton || drag_ == Drag::None)
        return;
    commitDrag();
    drag_ = Drag::None;
    preview_.clear();
    previewFades_.clear();
    marquee_ = QRect();
    applyHover(ev->position().toPoint());
    viewport()->update();
}

void TimelineWidget::commitDrag()
{
    switch (drag_) {
    case Drag::Move: {
        if (preview_.isEmpty())
            break;
        bool changed = false;
        for (auto it = preview_.begin(); it != preview_.end(); ++it)
            if (!(it.value() == origTrims_.value(it.key())))
                changed = true;
        if (changed)
            model_->push(new core::MoveClipsCommand(model_, preview_));
        break;
    }
    case Drag::TrimLeft:
    case Drag::TrimRight:
    case Drag::Slip: {
        const ClipTrim after = preview_.value(dragClip_);
        if (!(after == origTrims_.value(dragClip_)))
            model_->push(new core::TrimClipCommand(model_, dragClip_, after));
        break;
    }
    case Drag::FadeIn:
    case Drag::FadeOut: {
        const bool in = drag_ == Drag::FadeIn;
        const QString key = dragClip_ + (in ? QStringLiteral("|in") : QStringLiteral("|out"));
        if (previewFades_.contains(key))
            model_->push(new core::SetFadeCommand(model_, dragClip_, in, previewFades_.value(key)));
        break;
    }
    case Drag::Marquee: {
        if (marquee_.width() < 3 && marquee_.height() < 3)
            break;
        QStringList hits;
        const auto& proj = model_->project();
        for (int i = 0; i < proj.tracks.size(); ++i) {
            const int y = laneTop(i);
            if (y + trackH_ < marquee_.top() || y > marquee_.bottom())
                continue;
            for (const auto& c : proj.tracks[i].clips) {
                const int cx = xOfSample(c.timelineStart);
                const int cw = int(double(c.length) / proj.sampleRate * pxPerSec_);
                if (cx + cw >= marquee_.left() && cx <= marquee_.right())
                    hits.append(c.id);
            }
        }
        if (marqueeAdditive_) {
            for (const auto& id : hits)
                if (!selection_.contains(id))
                    selection_.append(id);
            emit selectionChanged(selection_);
        } else {
            selectClips(hits);
        }
        break;
    }
    default:
        break;
    }
}

void TimelineWidget::wheelEvent(QWheelEvent* ev)
{
    const QPoint pos = ev->position().toPoint();
    const int dy = ev->angleDelta().y();
    if (ev->modifiers() & Qt::ControlModifier) {
        setTrackHeight(trackH_ + (dy > 0 ? 8 : -8));
    } else if (ev->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dy);
    } else {
        zoomAt(std::pow(1.15, -dy / 120.0), pos);
    }
    ev->accept();
}

void TimelineWidget::resizeEvent(QResizeEvent*)
{
    updateScrollbars();
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* ev)
{
    if (ev->mimeData()->hasUrls())
        ev->acceptProposedAction();
}

void TimelineWidget::dropEvent(QDropEvent* ev)
{
    QStringList paths;
    for (const auto& url : ev->mimeData()->urls())
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        ev->acceptProposedAction();
    }
}

} // namespace ses::ui