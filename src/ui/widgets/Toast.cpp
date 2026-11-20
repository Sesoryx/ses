#include "ui/widgets/Toast.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>

namespace ses::ui {

Toast::Toast(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("Toast"));
    auto* hl = new QHBoxLayout(this);
    hl->setContentsMargins(14, 8, 14, 8);
    label_ = new QLabel(this);
    label_->setWordWrap(true);
    label_->setMaximumWidth(360);
    hl->addWidget(label_);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(3500);
    connect(timer_, &QTimer::timeout, this, &QWidget::hide);
    hide();
}

void Toast::show(const QString& message, bool error)
{
    label_->setText(message);
    label_->setProperty("role", error ? QStringLiteral("danger") : QString());
    style()->unpolish(label_);
    style()->polish(label_);
    adjustSize();
    if (parentWidget()) {
        const QSize ps = parentWidget()->size();
        move(ps.width() - width() - 16, ps.height() - height() - 40);
    }
    QFrame::show();
    raise();
    timer_->start();
}

} // namespace ses::ui