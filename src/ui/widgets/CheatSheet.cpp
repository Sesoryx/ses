#include "ui/widgets/CheatSheet.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>

#include <iterator>

namespace ses::ui {

CheatSheet::CheatSheet(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Keyboard shortcuts"));
    resize(520, 460);
    auto* vl = new QVBoxLayout(this);

    auto* table = new QTableWidget(this);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({ QStringLiteral("Group"), QStringLiteral("Keys"), QStringLiteral("Action") });
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    const struct Row { const char* g; const char* k; const char* a; } rows[] = {
        { "Transport", "Space", "Play / stop (preview transport until P1)" },
        { "Transport", "Enter", "To start" },
        { "Transport", "L", "Loop" },
        { "Transport", ", / .", "Nudge playhead ∓1 s" },
        { "Tools", "V / R / F / Shift+A", "Select / razor / fade / automation" },
        { "Edit", "S", "Split selected clip at playhead" },
        { "Edit", "Del", "Remove selected clips" },
        { "Edit", "Ctrl+Z / Ctrl+Shift+Z", "Undo / redo" },
        { "Edit", "Ctrl+D", "Duplicate clips" },
        { "Edit", "Ctrl+A", "Select all clips" },
        { "Audition", "A / B / \\", "Solo A / solo B / toggle" },
        { "Audition", "Shift+1 / Shift+2", "Route track to A / B" },
        { "Track", "M / Shift+S", "Mute / solo track" },
        { "Track", "Ctrl+Shift+N", "Add track" },
        { "View", "+ / − / Ctrl+0", "Zoom in / out / fit" },
        { "View", "T", "Toggle dark / light theme" },
        { "View", "Ctrl+Tab", "Next dock panel" },
        { "View", "Ctrl+I / Ctrl+Q", "Import audio / quit" },
        { "Help", "? / F1", "This cheat sheet" },
        { "Timeline", "Wheel / Ctrl+Wheel", "Zoom at cursor / track height" },
        { "Timeline", "Alt+drag / Ctrl held", "Slip content / disable snap" },
    };
    table->setRowCount(int(std::size(rows)));
    for (int i = 0; i < int(std::size(rows)); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(QString::fromUtf8(rows[i].g)));
        table->setItem(i, 1, new QTableWidgetItem(QString::fromUtf8(rows[i].k)));
        table->setItem(i, 2, new QTableWidgetItem(QString::fromUtf8(rows[i].a)));
    }
    vl->addWidget(table);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::clicked, this, &QDialog::accept);
    vl->addWidget(box);
}

} // namespace ses::ui