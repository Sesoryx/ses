#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QCache>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

namespace ses::ui {

namespace {

// Tinted renders are cached per (name, size, dpr, mode, theme generation);
// without this every repolish re-renders + recolors every SVG icon.
QCache<QString, QImage>& iconCache()
{
    static QCache<QString, QImage> cache(8 * 1024 * 1024);
    return cache;
}

QColor tintFor(QIcon::Mode mode)
{
    ThemeManager* t = ThemeManager::instance();
    switch (mode) {
    case QIcon::Disabled: return t->color(QStringLiteral("textSecondary"));
    case QIcon::Active:
    case QIcon::Selected: return t->color(QStringLiteral("accent"));
    case QIcon::Normal: break;
    }
    return t->color(QStringLiteral("text"));
}

// SVG parse is expensive; keep one renderer per icon for the process lifetime.
QSvgRenderer* rendererFor(const QString& name)
{
    static QHash<QString, QSvgRenderer*> renderers;
    auto it = renderers.find(name);
    if (it != renderers.end())
        return it.value();
    auto* r = new QSvgRenderer(QStringLiteral(":/icons/%1.svg").arg(name));
    renderers.insert(name, r);
    return r;
}

QImage renderTinted(const QString& name, const QSize& size, qreal dpr, const QColor& tint)
{
    QImage img(size * dpr, QImage::Format_ARGB32);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QSvgRenderer* renderer = rendererFor(name);
    if (renderer->isValid()) {
        QPainter p(&img);
        renderer->render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    }
    // Recolor opaque pixels, preserve alpha (works for stroke and fill SVGs).
    const QRgb rgb = tint.rgb();
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a > 0)
                line[x] = qRgba(qRed(rgb), qGreen(rgb), qBlue(rgb), a);
        }
    }
    return img;
}

QImage cachedTinted(const QString& name, const QSize& size, qreal dpr, QIcon::Mode mode)
{
    const QString key = QStringLiteral("%1|%2x%3|%4|%5|%6")
                            .arg(name)
                            .arg(size.width())
                            .arg(size.height())
                            .arg(dpr)
                            .arg(int(mode))
                            .arg(ThemeManager::instance()->generation());
    if (const QImage* hit = iconCache().object(key))
        return *hit;
    QImage img = renderTinted(name, size, dpr, tintFor(mode));
    iconCache().insert(key, new QImage(img), int(img.sizeInBytes()));
    return img;
}

} // namespace

TintedIconEngine::TintedIconEngine(QString name)
    : name_(std::move(name))
{
}

void TintedIconEngine::paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State)
{
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QImage img = cachedTinted(name_, rect.size(), dpr, mode);
    painter->drawImage(rect, img);
}

QPixmap TintedIconEngine::pixmap(const QSize& size, QIcon::Mode mode, QIcon::State)
{
    const QImage img = cachedTinted(name_, size, 1.0, mode);
    return QPixmap::fromImage(img);
}

QIconEngine* TintedIconEngine::clone() const
{
    return new TintedIconEngine(name_);
}

QIcon sesIcon(const QString& name)
{
    return QIcon(new TintedIconEngine(name));
}

} // namespace ses::ui