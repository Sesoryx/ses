#pragma once
// EmptyState — first-run drop zone (§10.2 J1, §10.7). Three actions, no wizard.

#include <QWidget>

namespace ses::ui {

class EmptyState : public QWidget {
    Q_OBJECT
public:
    explicit EmptyState(QWidget* parent = nullptr);
    void setRecentAvailable(bool available);

signals:
    void importRequested();
    void openRequested();
    void recentRequested();
};

} // namespace ses::ui