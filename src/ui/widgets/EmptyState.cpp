#include "ui/widgets/EmptyState.h"

#include "ui/theme/Icons.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ses::ui {

EmptyState::EmptyState(QWidget* parent)
    : QWidget(parent)
{
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("EmptyState"));
    auto* fl = new QVBoxLayout(frame);
    fl->setContentsMargins(36, 28, 36, 28);
    fl->setSpacing(10);

    auto* icon = new QLabel(frame);
    icon->setPixmap(sesIcon(QStringLiteral("wave")).pixmap(40, 40));
    icon->setAlignment(Qt::AlignCenter);
    fl->addWidget(icon);

    auto* title = new QLabel(QStringLiteral("Drop audio here"), frame);
    title->setProperty("role", QStringLiteral("title"));
    title->setAlignment(Qt::AlignCenter);
    QFont tf = title->font();
    tf.setPixelSize(17);
    title->setFont(tf);
    fl->addWidget(title);

    auto* sub = new QLabel(QStringLiteral("WAV imports now · FLAC/MP3 decode arrives with the audio engine (P1).\nOpen → drop → Space to play. No setup, no account."), frame);
    sub->setProperty("role", QStringLiteral("secondary"));
    sub->setAlignment(Qt::AlignCenter);
    sub->setWordWrap(true);
    fl->addWidget(sub);

    auto* btns = new QHBoxLayout;
    btns->setSpacing(8);
    auto* import = new QPushButton(sesIcon(QStringLiteral("import")), QStringLiteral(" Import audio"), frame);
    import->setProperty("variant", QStringLiteral("primary"));
    connect(import, &QPushButton::clicked, this, &EmptyState::importRequested);
    auto* open = new QPushButton(sesIcon(QStringLiteral("folder")), QStringLiteral(" Open project"), frame);
    open->setEnabled(false);
    open->setToolTip(QStringLiteral("Project bundles (.ego) arrive with persistence (P3)"));
    connect(open, &QPushButton::clicked, this, &EmptyState::openRequested);
    auto* recent = new QPushButton(QStringLiteral("Recent"), frame);
    recent->setEnabled(false);
    recent->setToolTip(QStringLiteral("Recent projects appear here once persistence lands (P3)"));
    connect(recent, &QPushButton::clicked, this, &EmptyState::recentRequested);
    btns->addStretch(1);
    btns->addWidget(import);
    btns->addWidget(open);
    btns->addWidget(recent);
    btns->addStretch(1);
    fl->addLayout(btns);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(frame);
    setFixedSize(frame->sizeHint().expandedTo(QSize(420, 200)));
}

void EmptyState::setRecentAvailable(bool available)
{
    // Recent list is wired in P3; kept for API parity.
    Q_UNUSED(available);
}

} // namespace ses::ui