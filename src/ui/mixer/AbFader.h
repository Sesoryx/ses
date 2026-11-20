#pragma once
// AbFader — the always-visible split A/B audition fader (DESIGN.md §4.5/§10.4).
// A on the left, B on the right, center detent, 80 ms eased solo snaps,
// mode popover (Crossfade/Split) and Copy/Swap/Reset iteration actions.
// Fader moves are parameter commands (merged), not plan rebuilds.

#include <QWidget>

class QToolButton;
class QVariantAnimation;

namespace ses::ui {

class AbFader : public QWidget {
    Q_OBJECT
public:
    explicit AbFader(QWidget* parent = nullptr);

    double value() const { return value_; }
    void setValue(double v, bool animate = false);
    void setReducedMotion(bool on) { reducedMotion_ = on; }
    void setMode(const QString& mode); // "crossfade" | "split"

    static double snapDetent(double v);
    QSize sizeHint() const override { return { 250, 34 }; }

signals:
    void valueChanged(double v);          // user/animation driven → command
    void modeChanged(const QString& mode);
    void busAction(int kind);             // AbBusCommand::Kind

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QRect grooveRect() const;
    void layoutButtons();
    void dragTo(int x);

    double value_ = 0.5;
    bool dragging_ = false;
    bool reducedMotion_ = false;
    QString mode_ = QStringLiteral("crossfade");
    QToolButton* soloA_ = nullptr;
    QToolButton* soloB_ = nullptr;
    QToolButton* modeBtn_ = nullptr;
    QToolButton* moreBtn_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
};

} // namespace ses::ui