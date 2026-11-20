#pragma once
// Monochrome SVG icons tinted at runtime from the active theme (DESIGN.md
// §10.10). No per-theme raster sets; the engine reads current theme colors at
// paint time, so a theme switch re-tints automatically.

#include <QIcon>
#include <QIconEngine>
#include <QString>

namespace ses::ui {

class TintedIconEngine : public QIconEngine {
public:
    explicit TintedIconEngine(QString name);
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override;
    QIconEngine* clone() const override;

private:
    QString name_;
};

QIcon sesIcon(const QString& name);

} // namespace ses::ui