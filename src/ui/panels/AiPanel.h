#pragma once
// AI panel (§10.5) — honest capability cards. UI-first slice: catalog data is
// real (verified sizes/licenses), but inference is not connected yet (P5), so
// every Run control explains its state instead of faking progress.

#include "core/model/ProjectModel.h"

#include <QWidget>

class QLabel;

namespace ses::ui {

class AiPanel : public QWidget {
    Q_OBJECT
public:
    explicit AiPanel(core::ProjectModel* model, QWidget* parent = nullptr);
    void setSelectionScope(const QStringList& clipIds);

private:
    core::ProjectModel* model_;
    QLabel* scope_ = nullptr;
};

} // namespace ses::ui