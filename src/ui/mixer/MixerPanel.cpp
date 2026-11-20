#include "ui/mixer/MixerPanel.h"

#include "core/commands/Commands.h"
#include "ui/theme/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace ses::ui {

namespace {

// Honest idle meter: groove only, no fake signal. Real peaks arrive with
// engine telemetry in P1.
class IdleMeter : public QWidget {
public:
    explicit IdleMeter(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedWidth(8);
        setMinimumHeight(90);
        setToolTip(QStringLiteral("Meter — telemetry arrives with the audio engine (P1)"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        ThemeManager* th = ThemeManager::instance();
        p.fillRect(rect(), th->color(QStringLiteral("bgAlt")));
        p.setPen(th->color(QStringLiteral("border")));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        p.setPen(th->color(QStringLiteral("textSecondary")));
        QFont f = font();
        f.setPixelSize(8);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("–"));
    }
};

QString dbLabel(double gain)
{
    if (gain <= 0.0001)
        return QStringLiteral("-∞ dB");
    const double db = 20.0 * std::log10(gain);
    return QStringLiteral("%1%2 dB").arg(db >= 0 ? "+" : "").arg(db, 0, 'f', 1);
}

} // namespace

MixerPanel::MixerPanel(core::ProjectModel* model, QWidget* parent)
    : QWidget(parent), model_(model)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(210);
    container_ = new QWidget(scroll);
    layout_ = new QHBoxLayout(container_);
    layout_->setContentsMargins(8, 8, 8, 8);
    layout_->setSpacing(8);
    scroll->setWidget(container_);
    outer->addWidget(scroll);

    connect(model_, &core::ProjectModel::structureChanged, this, &MixerPanel::rebuild);
    connect(model_, &core::ProjectModel::changed, this, &MixerPanel::refresh);
    rebuild();
}

QWidget* MixerPanel::makeStrip(const core::Track& t)
{
    auto* frame = new QFrame(container_);
    frame->setObjectName(QStringLiteral("Card"));
    frame->setFixedWidth(96);
    auto* vl = new QVBoxLayout(frame);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(4);

    auto* name = new QLabel(t.name, frame);
    name->setAlignment(Qt::AlignCenter);
    name->setToolTip(t.name);
    QFont nf = name->font();
    nf.setPixelSize(11);
    nf.setBold(true);
    name->setFont(nf);

    auto* mid = new QHBoxLayout;
    mid->setSpacing(6);
    auto* meter = new IdleMeter(frame);
    auto* gain = new QSlider(Qt::Vertical, frame);
    gain->setRange(0, 200);
    gain->setValue(int(t.gain * 100));
    gain->setToolTip(QStringLiteral("Track gain"));
    gain->setFocusPolicy(Qt::ClickFocus);
    connect(gain, &QSlider::valueChanged, this, [this, id = t.id](int v) {
        model_->push(new core::SetTrackParamCommand(model_, id, core::TrackField::Gain, v / 100.0));
    });
    mid->addWidget(meter);
    mid->addWidget(gain, 1);

    auto* db = new QLabel(dbLabel(t.gain), frame);
    db->setAlignment(Qt::AlignCenter);
    db->setProperty("role", QStringLiteral("secondary"));

    auto* pan = new QSlider(Qt::Horizontal, frame);
    pan->setRange(-100, 100);
    pan->setValue(int(t.pan * 100));
    pan->setToolTip(QStringLiteral("Pan"));
    pan->setFocusPolicy(Qt::ClickFocus);
    connect(pan, &QSlider::valueChanged, this, [this, id = t.id](int v) {
        model_->push(new core::SetTrackParamCommand(model_, id, core::TrackField::Pan, v / 100.0));
    });

    auto* btns = new QHBoxLayout;
    btns->setSpacing(3);
    auto* mute = new QToolButton(frame);
    mute->setProperty("variant", QStringLiteral("mini"));
    mute->setProperty("kind", QStringLiteral("mute"));
    mute->setText(QStringLiteral("M"));
    mute->setCheckable(true);
    connect(mute, &QToolButton::toggled, this, [this, id = t.id](bool on) {
        model_->push(new core::SetTrackParamCommand(model_, id, core::TrackField::Mute, on));
    });
    auto* solo = new QToolButton(frame);
    solo->setProperty("variant", QStringLiteral("mini"));
    solo->setProperty("kind", QStringLiteral("solo"));
    solo->setText(QStringLiteral("S"));
    solo->setCheckable(true);
    connect(solo, &QToolButton::toggled, this, [this, id = t.id](bool on) {
        model_->push(new core::SetTrackParamCommand(model_, id, core::TrackField::Solo, on));
    });
    auto* chip = new QToolButton(frame);
    chip->setProperty("variant", QStringLiteral("chip"));
    chip->setToolTip(QStringLiteral("A/B routing"));
    connect(chip, &QToolButton::clicked, this, [this, id = t.id] {
        const core::Track* tr = model_->project().trackById(id);
        if (!tr)
            return;
        const QString next = tr->abRouting == "both" ? "A" : (tr->abRouting == "A" ? "B" : "both");
        model_->push(new core::SetAbRoutingCommand(model_, id, next));
    });
    btns->addWidget(mute);
    btns->addWidget(solo);
    btns->addWidget(chip);

    vl->addWidget(name);
    vl->addLayout(mid, 1);
    vl->addWidget(db);
    vl->addWidget(pan);
    vl->addLayout(btns);

    strips_.insert(t.id, Strip{ name, db, gain, pan, mute, solo, chip });
    return frame;
}

QWidget* MixerPanel::makeMasterStrip()
{
    auto* frame = new QFrame(container_);
    frame->setObjectName(QStringLiteral("Card"));
    frame->setFixedWidth(96);
    auto* vl = new QVBoxLayout(frame);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(4);

    auto* name = new QLabel(QStringLiteral("MASTER"), frame);
    name->setAlignment(Qt::AlignCenter);
    QFont nf = name->font();
    nf.setPixelSize(11);
    nf.setBold(true);
    name->setFont(nf);

    auto* mid = new QHBoxLayout;
    mid->setSpacing(6);
    mid->addWidget(new IdleMeter(frame));
    auto* gain = new QSlider(Qt::Vertical, frame);
    gain->setRange(0, 200);
    gain->setValue(100);
    gain->setEnabled(false);
    gain->setToolTip(QStringLiteral("Master chain + limiter arrive with P4"));
    mid->addWidget(gain, 1);

    auto* lim = new QLabel(QStringLiteral("lim –0.3 dB"), frame);
    lim->setAlignment(Qt::AlignCenter);
    lim->setProperty("role", QStringLiteral("secondary"));
    lim->setToolTip(QStringLiteral("Master limiter (P4)"));

    auto* note = new QLabel(QStringLiteral("bus A/B"), frame);
    note->setAlignment(Qt::AlignCenter);
    note->setProperty("role", QStringLiteral("secondary"));

    vl->addWidget(name);
    vl->addLayout(mid, 1);
    vl->addWidget(lim);
    vl->addWidget(note);
    return frame;
}

void MixerPanel::rebuild()
{
    while (QLayoutItem* item = layout_->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    strips_.clear();
    for (const auto& t : model_->project().tracks)
        layout_->addWidget(makeStrip(t));
    layout_->addWidget(makeMasterStrip());
    layout_->addStretch(1);
    refresh();
}

void MixerPanel::refresh()
{
    for (const auto& t : model_->project().tracks) {
        auto it = strips_.find(t.id);
        if (it == strips_.end())
            continue;
        const Strip& s = it.value();
        s.name->setText(t.name);
        s.db->setText(dbLabel(t.gain));
        s.gain->blockSignals(true);
        s.gain->setValue(int(t.gain * 100));
        s.gain->blockSignals(false);
        s.pan->blockSignals(true);
        s.pan->setValue(int(t.pan * 100));
        s.pan->blockSignals(false);
        s.mute->blockSignals(true);
        s.mute->setChecked(t.mute);
        s.mute->blockSignals(false);
        s.solo->blockSignals(true);
        s.solo->setChecked(t.solo);
        s.solo->blockSignals(false);
        s.chip->setText(t.abRouting == "both" ? QStringLiteral("AB") : t.abRouting);
        s.chip->setProperty("route", t.abRouting);
        s.chip->style()->unpolish(s.chip);
        s.chip->style()->polish(s.chip);
    }
}

} // namespace ses::ui