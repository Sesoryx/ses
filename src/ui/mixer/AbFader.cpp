#include "ui/mixer/AbFader.h"

#include "core/commands/Commands.h"
#include "ui/theme/ThemeManager.h"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace ses::ui {

AbFader::AbFader(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(220, 34);
    setToolTip(QStringLiteral("A/B crossfader — A / B / \\ keys, center detent"));

    soloA_ = new QToolButton(this);
    soloA_->setText(QStringLiteral("A"));
    soloA_->setProperty("variant", QStringLiteral("chip"));
    soloA_->setProperty("route", QStringLiteral("A"));
    soloA_->setToolTip(QStringLiteral("Solo A (key A)"));
    connect(soloA_, &QToolButton::clicked, this, [this] { setValue(0.0, true); });

    soloB_ = new QToolButton(this);
    soloB_->setText(QStringLiteral("B"));
    soloB_->setProperty("variant", QStringLiteral("chip"));
    soloB_->setProperty("route", QStringLiteral("B"));
    soloB_->setToolTip(QStringLiteral("Solo B (key B)"));
    connect(soloB_, &QToolButton::clicked, this, [this] { setValue(1.0, true); });

    modeBtn_ = new QToolButton(this);
    modeBtn_->setProperty("variant", QStringLiteral("chip"));
    modeBtn_->setToolTip(QStringLiteral("A/B mode: Crossfade (blend) or Split (A→left ear, B→right ear)"));
    connect(modeBtn_, &QToolButton::clicked, this, [this] {
        QMenu menu(this);
        auto* xf = menu.addAction(QStringLiteral("Crossfade — constant-power blend"));
        auto* sp = menu.addAction(QStringLiteral("Split — A left ear, B right ear"));
        xf->setCheckable(true);
        sp->setCheckable(true);
        xf->setChecked(mode_ == QLatin1String("crossfade"));
        sp->setChecked(mode_ == QLatin1String("split"));
        QAction* picked = menu.exec(modeBtn_->mapToGlobal(QPoint(0, modeBtn_->height())));
        if (picked == xf)
            emit modeChanged(QStringLiteral("crossfade"));
        else if (picked == sp)
            emit modeChanged(QStringLiteral("split"));
    });

    moreBtn_ = new QToolButton(this);
    moreBtn_->setText(QStringLiteral("⋯"));
    moreBtn_->setProperty("variant", QStringLiteral("chip"));
    moreBtn_->setToolTip(QStringLiteral("Copy A→B · Swap A/B · Reset"));
    connect(moreBtn_, &QToolButton::clicked, this, [this] {
        QMenu menu(this);
        auto* copy = menu.addAction(QStringLiteral("Copy A → B"));
        auto* swap = menu.addAction(QStringLiteral("Swap A/B"));
        auto* reset = menu.addAction(QStringLiteral("Reset"));
        QAction* picked = menu.exec(moreBtn_->mapToGlobal(QPoint(0, moreBtn_->height())));
        if (picked == copy)
            emit busAction(core::AbBusCommand::CopyAtoB);
        else if (picked == swap)
            emit busAction(core::AbBusCommand::Swap);
        else if (picked == reset)
            emit busAction(core::AbBusCommand::Reset);
    });

    anim_ = new QVariantAnimation(this);
    anim_->setDuration(80);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        value_ = v.toDouble();
        update();
        emit valueChanged(value_);
    });

    layoutButtons();
}

double AbFader::snapDetent(double v)
{
    return std::abs(v - 0.5) < 0.025 ? 0.5 : std::clamp(v, 0.0, 1.0);
}

void AbFader::setMode(const QString& mode)
{
    mode_ = mode;
    modeBtn_->setText(mode == QLatin1String("split") ? QStringLiteral("SPLIT") : QStringLiteral("XFADE"));
}

void AbFader::setValue(double v, bool animate)
{
    v = std::clamp(v, 0.0, 1.0);
    anim_->stop();
    if (animate && !reducedMotion_ && !qFuzzyCompare(v, value_)) {
        anim_->setStartValue(value_);
        anim_->setEndValue(v);
        anim_->start();
        return;
    }
    if (qFuzzyCompare(v, value_))
        return;
    value_ = v;
    update();
    emit valueChanged(value_);
}

QRect AbFader::grooveRect() const
{
    return rect().adjusted(34, 0, -88, 0);
}

void AbFader::layoutButtons()
{
    const int h = height();
    soloA_->setGeometry(0, (h - 22) / 2, 26, 22);
    const QRect g = grooveRect();
    soloB_->setGeometry(g.right() + 6, (h - 22) / 2, 26, 22);
    modeBtn_->setGeometry(g.right() + 36, (h - 22) / 2, 46, 22);
    moreBtn_->setGeometry(width() - 24, (h - 22) / 2, 22, 22);
}

void AbFader::resizeEvent(QResizeEvent*)
{
    layoutButtons();
}

void AbFader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    ThemeManager* th = ThemeManager::instance();
    const QRect g = grooveRect();
    const int cy = g.center().y();

    // Groove with A/B tinted halves.
    p.setPen(Qt::NoPen);
    p.setBrush(th->color(QStringLiteral("border")));
    p.drawRoundedRect(QRect(g.left(), cy - 2, g.width(), 4), 2, 2);
    const int hx = g.left() + int(value_ * g.width());
    p.setBrush(QColor(th->color(QStringLiteral("accent"))));
    p.drawRoundedRect(QRect(g.left(), cy - 2, hx - g.left(), 4), 2, 2);

    // Center detent tick.
    p.setPen(QPen(th->color(QStringLiteral("textSecondary")), 1));
    const int cx = g.left() + g.width() / 2;
    p.drawLine(cx, cy - 7, cx, cy + 7);

    // Handle.
    p.setPen(QPen(th->color(QStringLiteral("border")), 1));
    p.setBrush(th->color(QStringLiteral("accent")));
    p.drawEllipse(QPointF(hx, cy), 8, 8);
    p.setPen(th->color(QStringLiteral("accentText")));
    QFont f = font();
    f.setPixelSize(8);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(hx - 8, cy - 8, 16, 16), Qt::AlignCenter,
               value_ < 0.34 ? QStringLiteral("A") : (value_ > 0.66 ? QStringLiteral("B") : QStringLiteral("·")));
}

void AbFader::dragTo(int x)
{
    const QRect g = grooveRect();
    const double v = double(x - g.left()) / std::max(1, g.width());
    const double snapped = snapDetent(v);
    if (!qFuzzyCompare(snapped, value_)) {
        value_ = snapped;
        update();
        emit valueChanged(value_);
    }
}

void AbFader::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton && grooveRect().adjusted(-8, -12, 8, 12).contains(ev->position().toPoint())) {
        dragging_ = true;
        anim_->stop();
        dragTo(int(ev->position().x()));
    }
}

void AbFader::mouseMoveEvent(QMouseEvent* ev)
{
    if (dragging_)
        dragTo(int(ev->position().x()));
}

void AbFader::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = false;
}

} // namespace ses::ui