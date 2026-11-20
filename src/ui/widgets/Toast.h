#pragma once
// Toast — non-blocking success/error feedback (§10.7). Quiet by default.

#include <QFrame>

class QLabel;
class QTimer;

namespace ses::ui {

class Toast : public QFrame {
    Q_OBJECT
public:
    explicit Toast(QWidget* parent = nullptr);
    void show(const QString& message, bool error = false);

private:
    QLabel* label_ = nullptr;
    QTimer* timer_ = nullptr;
};

} // namespace ses::ui