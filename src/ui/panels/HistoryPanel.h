#pragma once
// History panel (§10.6) — undo/redo timeline. Backed by QUndoStack today;
// the content-addressed snapshot journal + time-travel restore land in P3.

#include "core/model/ProjectModel.h"

#include <QWidget>

namespace ses::ui {

class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(core::ProjectModel* model, QWidget* parent = nullptr);
};

} // namespace ses::ui