#include "ui/widgets/CoachMarks.h"

#include "core/persistence/Settings.h"
#include "ui/theme/Icons.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace ses::ui {

CoachMarks::CoachMarks(core::Settings* settings, QWidget* parent)
    : QWidget(parent), settings_(settings)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 0);
    outer->setSpacing(0);
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    outer->addLayout(row);

    const struct Tip { const char* id; const char* text; } tips[] = {
        { "drop", "Drop a WAV anywhere to import it." },
        { "split", "S splits the selected clip at the playhead." },
        { "ab", "A / B / \\ compares two versions instantly." },
    };
    for (const auto& tip : tips) {
        if (settings_->dismissedCoachMarks.contains(QLatin1String(tip.id)))
            continue;
        auto* mark = new QFrame(this);
        mark->setObjectName(QStringLiteral("CoachMark"));
        auto* hl = new QHBoxLayout(mark);
        hl->setContentsMargins(10, 4, 6, 4);
        hl->setSpacing(6);
        auto* label = new QLabel(QString::fromUtf8(tip.text), mark);
        label->setProperty("role", QStringLiteral("secondary"));
        auto* close = new QToolButton(mark);
        close->setIcon(sesIcon(QStringLiteral("close")));
        close->setIconSize(QSize(12, 12));
        close->setToolTip(QStringLiteral("Dismiss"));
        const QString id = QLatin1String(tip.id);
        connect(close, &QToolButton::clicked, this, [this, id, mark] {
            if (!settings_->dismissedCoachMarks.contains(id))
                settings_->dismissedCoachMarks.append(id);
            settings_->save();
            mark->deleteLater();
            refresh();
        });
        hl->addWidget(label);
        hl->addWidget(close);
        row->addWidget(mark);
    }
    row->addStretch(1);
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