#include "ui/panels/HistoryPanel.h"

#include "ui/theme/Icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QToolButton>
#include <QUndoView>
#include <QVBoxLayout>

namespace ses::ui {

HistoryPanel::HistoryPanel(core::ProjectModel* model, QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(4);
    auto* undo = new QToolButton(this);
    undo->setIcon(sesIcon(QStringLiteral("undo")));
    undo->setToolTip(QStringLiteral("Undo (Ctrl+Z)"));
    connect(undo, &QToolButton::clicked, &model->undoStack(), &QUndoStack::undo);
    auto* redo = new QToolButton(this);
    redo->setIcon(sesIcon(QStringLiteral("redo")));
    redo->setToolTip(QStringLiteral("Redo (Ctrl+Shift+Z)"));
    connect(redo, &QToolButton::clicked, &model->undoStack(), &QUndoStack::redo);
    auto* clear = new QToolButton(this);
    clear->setIcon(sesIcon(QStringLiteral("close")));
    clear->setToolTip(QStringLiteral("Clear history (guarded)"));
    connect(clear, &QToolButton::clicked, this, [this, model] {
        if (QMessageBox::question(this, QStringLiteral("Clear history"),
                                  QStringLiteral("Clear the undo history? Your audio stays as it is; only the ability to step back is removed."),
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            model->undoStack().clear();
    });
    auto* title = new QLabel(QStringLiteral("History"), this);
    title->setProperty("role", QStringLiteral("title"));
    bar->addWidget(title);
    bar->addStretch(1);
    bar->addWidget(undo);
    bar->addWidget(redo);
    bar->addWidget(clear);
    vl->addLayout(bar);

    auto* view = new QUndoView(&model->undoStack(), this);
    view->setEmptyLabel(QStringLiteral("No edits yet — every change lands here."));
    view->setAlternatingRowColors(true);
    vl->addWidget(view, 1);

    auto* note = new QLabel(QStringLiteral("Click an entry to step back. Snapshot journal + time-travel arrive with persistence (P3)."), this);
    note->setProperty("role", QStringLiteral("secondary"));
    note->setWordWrap(true);
    vl->addWidget(note);

    const auto refresh = [undo, redo, model] {
        undo->setEnabled(model->undoStack().canUndo());
        redo->setEnabled(model->undoStack().canRedo());
    };
    connect(&model->undoStack(), &QUndoStack::indexChanged, this, refresh);
    refresh();
}

} // namespace ses::ui