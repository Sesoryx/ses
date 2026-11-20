#pragma once
// Model manager (§10.5/§11.4) — catalog with real sizes/licenses, install
// state, models root. Download/verify/remove land with inference (P5);
// controls explain that instead of being silently dead.

#include <QWidget>

namespace ses::ui {

class ModelManagerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ModelManagerPanel(QWidget* parent = nullptr);
};

} // namespace ses::ui