#pragma once
// CoachMarks — three dismissible first-run tips (§10.2 J1). Never block.

#include <QWidget>

namespace ses::core { struct Settings; }

namespace ses::ui {

class CoachMarks : public QWidget {
    Q_OBJECT
public:
    explicit CoachMarks(core::Settings* settings, QWidget* parent = nullptr);
    void refresh();

private:
    core::Settings* settings_;
};

} // namespace ses::ui