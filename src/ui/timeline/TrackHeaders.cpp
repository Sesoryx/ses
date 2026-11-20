#include "ui/timeline/TrackHeaders.h"

#include "core/commands/Commands.h"
#include "ui/theme/Icons.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace ses::ui {

TrackHeaders::TrackHeaders(core::ProjectModel* model, QWidget* parent)
    : QScrollArea(parent), model_(model)
{
    setFixedWidth(190);
    setWidgetResizable(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    container_ = new QWidget(this);
    layout_ = new QVBoxLayout(container_);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    setWidget(container_);

    connect(model_, &core::ProjectModel::structureChanged, this, &TrackHeaders::rebuild);
    connect(model_, &core::ProjectModel::changed, this, &TrackHeaders::refresh);
    rebuild();
}

void TrackHeaders::setTrackHeight(int h)
{
    trackH_ = h;
    rebuild();
}

void TrackHeaders::syncVertical(int value)
{
    verticalScrollBar()->setValue(value);
}

void TrackHeaders::wheelEvent(QWheelEvent* ev)
{
    emit verticalWheel(ev->angleDelta().y());
    ev->accept();
}

bool TrackHeaders::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::MouseButtonDblClick) {
        const QString id = obj->property("trackId").toString();
        if (!id.isEmpty()) {
            emit renameRequested(id);
            return true;
        }
    }
    return QScrollArea::eventFilter(obj, ev);
}

void TrackHeaders::rebuild()
{
    // Clear old rows.
    while (QLayoutItem* item = layout_->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    rows_.clear();

    const auto& tracks = model_->project().tracks;
    for (const auto& t : tracks) {
        auto* row = new QFrame(container_);
        row->setObjectName(QStringLiteral("TrackRow"));
        row->setFixedHeight(trackH_);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(10, 6, 8, 6);
        hl->setSpacing(5);

        auto* dot = new QWidget(row);
        dot->setFixedSize(10, 10);
        QPalette pal = dot->palette();
        pal.setColor(QPalette::Window, QColor(t.color));
        dot->setPalette(pal);
        dot->setAutoFillBackground(true);

        auto* name = new QLabel(t.name, row);
        name->setProperty("trackId", t.id);
        name->installEventFilter(this);
        name->setToolTip(t.name + QStringLiteral(" — double-click to rename"));
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setMinimumWidth(20);

        auto* mute = new QToolButton(row);
        mute->setProperty("variant", QStringLiteral("mini"));
        mute->setProperty("kind", QStringLiteral("mute"));
        mute->setText(QStringLiteral("M"));
        mute->setCheckable(true);
        mute->setChecked(t.mute);
        mute->setToolTip(QStringLiteral("Mute (M)"));
        connect(mute, &QToolButton::toggled, this, [this, id = t.id](bool on) {
            model_->push(new core::SetTrackParamCommand(model_, id, core::TrackField::Mute, on));
        });

        auto* solo = new QToolButton(row);
        solo->setProperty("variant", QStringLiteral("mini"));
        solo->setProperty("kind", QStringLiteral("solo"));
        solo->setText(QStringLiteral("S"));
        solo->setCheckable(true);
        solo->setChecked(t.solo);
        solo->setToolTip(QStringLiteral("Solo (Shift+S)"));
        connect(solo, &QToolButton::toggled, this, [this, id = t.id](bool on) {
            model_->push(new core::SetTrackParamCommand(model_, id, core::TrackField::Solo, on));
        });

        auto* chip = new QToolButton(row);
        chip->setProperty("variant", QStringLiteral("chip"));
        chip->setToolTip(QStringLiteral("A/B routing — click to cycle"));
        connect(chip, &QToolButton::clicked, this, [this, id = t.id] {
            const core::Track* t = model_->project().trackById(id);
            if (!t)
                return;
            const QString next = t->abRouting == "both" ? "A" : (t->abRouting == "A" ? "B" : "both");
            model_->push(new core::SetAbRoutingCommand(model_, id, next));
        });

        hl->addWidget(dot);
        hl->addWidget(name, 1);
        hl->addWidget(mute);
        hl->addWidget(solo);
        hl->addWidget(chip);
        layout_->addWidget(row);
        rows_.insert(t.id, Row{ name, mute, solo, chip, dot });
    }

    auto* add = new QToolButton(container_);
    add->setIcon(sesIcon(QStringLiteral("plus")));
    add->setText(QStringLiteral(" Add track"));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setToolTip(QStringLiteral("Add track (Ctrl+Shift+N)"));
    connect(add, &QToolButton::clicked, this, &TrackHeaders::addTrackRequested);
    layout_->addWidget(add);
    layout_->addStretch(1);

    refresh();
}

void TrackHeaders::refresh()
{
    for (const auto& t : model_->project().tracks) {
        auto it = rows_.find(t.id);
        if (it == rows_.end())
            continue;
        const Row& r = it.value();
        r.name->setText(t.name);
        r.name->setToolTip(t.name + QStringLiteral(" — double-click to rename"));
        QPalette pal = r.dot->palette();
        pal.setColor(QPalette::Window, QColor(t.color));
        r.dot->setPalette(pal);
        r.mute->blockSignals(true);
        r.mute->setChecked(t.mute);
        r.mute->blockSignals(false);
        r.solo->blockSignals(true);
        r.solo->setChecked(t.solo);
        r.solo->blockSignals(false);
        const QString label = t.abRouting == "both" ? QStringLiteral("AB") : t.abRouting;
        r.chip->setText(label);
        r.chip->setProperty("route", t.abRouting);
        r.chip->style()->unpolish(r.chip);
        r.chip->style()->polish(r.chip);
    }
}

} // namespace ses::ui