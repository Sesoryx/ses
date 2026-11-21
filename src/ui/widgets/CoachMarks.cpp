#include "ui/widgets/CoachMarks.h"

#include "core/persistence/Settings.h"
#include "ui/theme/Icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace ses::ui {

CoachMarks::CoachMarks(core::Settings* settings, QWidget* parent)
    : QWidget(parent), settings_(settings)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 2, 8, 0);
    outer->setSpacing(0);
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    outer->addLayout(row);

    const struct Tip { const char* id; const char* text; } tips[] = {
        { "drop", "Drop a WAV to import" },
        { "split", "S to split at playhead" },
        { "ab", "A / B to compare" },
    };
    QStringList visibleTips;
    for (const auto& tip : tips)
        if (!settings_->dismissedCoachMarks.contains(QLatin1String(tip.id)))
            visibleTips.append(QString::fromUtf8(tip.text));

    auto* label = new QLabel(visibleTips.join(QStringLiteral("   ·   ")), this);
    label->setProperty("role", QStringLiteral("secondary"));
    row->addWidget(label);
    row->addStretch(1);

    auto* close = new QToolButton(this);
    close->setIcon(sesIcon(QStringLiteral("close")));
    close->setIconSize(QSize(12, 12));
    close->setToolTip(QStringLiteral("Dismiss tips"));
    connect(close, &QToolButton::clicked, this, [this] {
        for (const QString& id : { QStringLiteral("drop"), QStringLiteral("split"), QStringLiteral("ab") })
            if (!settings_->dismissedCoachMarks.contains(id))
                settings_->dismissedCoachMarks.append(id);
        settings_->save();
        refresh();
    });
    row->addWidget(close);
    refresh();
}

void CoachMarks::refresh()
{
    // Hide the strip entirely once every tip is dismissed.
    const bool anyLeft = !settings_->dismissedCoachMarks.contains(QStringLiteral("drop"))
        || !settings_->dismissedCoachMarks.contains(QStringLiteral("split"))
        || !settings_->dismissedCoachMarks.contains(QStringLiteral("ab"));
    setVisible(anyLeft);
}

} // namespace ses::ui