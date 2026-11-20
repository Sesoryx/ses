#pragma once
// Shortcut cheat sheet (§10.12) — opened with ? / F1.

#include <QDialog>

namespace ses::ui {

class CheatSheet : public QDialog {
    Q_OBJECT
public:
    explicit CheatSheet(QWidget* parent = nullptr);
};

} // namespace ses::ui