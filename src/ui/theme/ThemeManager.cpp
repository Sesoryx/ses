#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace ses::ui {

ThemeManager* ThemeManager::instance()
{
    static ThemeManager* mgr = new ThemeManager(qApp);
    return mgr;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    loadTokens();
    if (qApp) {
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            if (mode_ == System)
                apply();
        });
    }
}

void ThemeManager::loadTokens()
{
    const auto load = [](const QString& path, QHash<QString, QString>& out) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return;
        const auto obj = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            out.insert(it.key(), it.value().toString());
    };
    load(QStringLiteral(":/theme/tokens.dark.json"), dark_);
    load(QStringLiteral(":/theme/tokens.light.json"), light_);
}

ThemeManager::Mode ThemeManager::modeFromString(const QString& s)
{
    if (s == QLatin1String("dark"))
        return Dark;
    if (s == QLatin1String("light"))
        return Light;
    return System;
}

QString ThemeManager::modeString() const
{
    switch (mode_) {
    case Dark: return QStringLiteral("dark");
    case Light: return QStringLiteral("light");
    case System: break;
    }
    return QStringLiteral("system");
}

void ThemeManager::setMode(Mode mode)
{
    mode_ = mode;
    apply();
}

void ThemeManager::apply()
{
    bool dark = true;
    switch (mode_) {
    case Dark: dark = true; break;
    case Light: dark = false; break;
    case System:
        dark = qApp ? qApp->styleHints()->colorScheme() != Qt::ColorScheme::Light : true;
        break;
    }
    if (applied_ && dark == resolvedDark_)
        return; // memoized: skip redundant re-applies
    resolvedDark_ = dark;
    applied_ = true;
    ++generation_;

    const QHash<QString, QString>& tokens = dark ? dark_ : light_;

    // QSS: substitute @tokens, longest keys first so @bg never eats @bgAlt.
    QFile tf(QStringLiteral(":/theme/app.qss.template"));
    QString qss;
    if (tf.open(QIODevice::ReadOnly))
        qss = QString::fromUtf8(tf.readAll());
    QStringList keys = tokens.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        return a.size() > b.size();
    });
    static const QHash<QString, QString> paletteRoles = {
        { QStringLiteral("radius"), QStringLiteral("8px") },
        { QStringLiteral("radiusSmall"), QStringLiteral("5px") },
        { QStringLiteral("spacing"), QStringLiteral("8px") },
        { QStringLiteral("fontBase"), QStringLiteral("13px") },
        { QStringLiteral("fontSmall"), QStringLiteral("11px") },
        { QStringLiteral("fontTitle"), QStringLiteral("15px") },
        { QStringLiteral("bg"), QStringLiteral("palette(window)") },
        { QStringLiteral("bgAlt"), QStringLiteral("palette(alternatebase)") },
        { QStringLiteral("panel"), QStringLiteral("palette(base)") },
        { QStringLiteral("panelAlt"), QStringLiteral("palette(midlight)") },
        { QStringLiteral("border"), QStringLiteral("palette(mid)") },
        { QStringLiteral("text"), QStringLiteral("palette(windowtext)") },
        { QStringLiteral("textSecondary"), QStringLiteral("palette(placeholdertext)") },
        { QStringLiteral("accent"), QStringLiteral("palette(highlight)") },
        { QStringLiteral("accentHover"), QStringLiteral("palette(link)") },
        { QStringLiteral("accentText"), QStringLiteral("palette(highlightedtext)") },
        { QStringLiteral("danger"), QStringLiteral("palette(brighttext)") },
        { QStringLiteral("warning"), QStringLiteral("palette(linkvisited)") },
        { QStringLiteral("ok"), QStringLiteral("palette(light)") },
        { QStringLiteral("waveform"), QStringLiteral("palette(dark)") },
        { QStringLiteral("waveformPeak"), QStringLiteral("palette(shadow)") },
        { QStringLiteral("clipFill"), QStringLiteral("palette(button)") },
        { QStringLiteral("clipFillAlt"), QStringLiteral("palette(tooltipbase)") },
        { QStringLiteral("automation"), QStringLiteral("palette(tooltiptext)") },
        { QStringLiteral("selection"), QStringLiteral("palette(highlight)") },
        { QStringLiteral("playhead"), QStringLiteral("palette(brighttext)") },
        { QStringLiteral("ruler"), QStringLiteral("palette(buttontext)") },
        { QStringLiteral("grid"), QStringLiteral("palette(alternatebase)") },
    };
    for (const auto& k : keys)
        qss.replace(QLatin1Char('@') + k, paletteRoles.value(k, tokens.value(k)));

    // QPalette for native dialogs / fallback. Secondary text uses the
    // contrast-checked textSecondary role — never QPalette::Mid.
    if (qApp) {
        QPalette pal;
        const QColor bg = color(QStringLiteral("bg"));
        const QColor panel = color(QStringLiteral("panel"));
        const QColor text = color(QStringLiteral("text"));
        const QColor secondary = color(QStringLiteral("textSecondary"));
        const QColor accent = color(QStringLiteral("accent"));
        pal.setColor(QPalette::Window, bg);
        pal.setColor(QPalette::WindowText, text);
        pal.setColor(QPalette::Base, panel);
        pal.setColor(QPalette::AlternateBase, color(QStringLiteral("bgAlt")));
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::Button, color(QStringLiteral("clipFill")));
        pal.setColor(QPalette::ButtonText, color(QStringLiteral("ruler")));
        pal.setColor(QPalette::BrightText, color(QStringLiteral("danger")));
        pal.setColor(QPalette::Light, color(QStringLiteral("ok")));
        pal.setColor(QPalette::Midlight, color(QStringLiteral("panelAlt")));
        pal.setColor(QPalette::Dark, color(QStringLiteral("waveform")));
        pal.setColor(QPalette::Mid, color(QStringLiteral("border")));
        pal.setColor(QPalette::Shadow, color(QStringLiteral("waveformPeak")));
        pal.setColor(QPalette::Highlight, accent);
        pal.setColor(QPalette::HighlightedText, color(QStringLiteral("accentText")));
        pal.setColor(QPalette::ToolTipBase, color(QStringLiteral("clipFillAlt")));
        pal.setColor(QPalette::ToolTipText, color(QStringLiteral("automation")));
        pal.setColor(QPalette::PlaceholderText, secondary);
        pal.setColor(QPalette::Link, accent);
        pal.setColor(QPalette::LinkVisited, color(QStringLiteral("warning")));
        pal.setColor(QPalette::Disabled, QPalette::Text, secondary);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, secondary);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, secondary);
        qApp->setPalette(pal);
        if (!styleSheetApplied_) {
            qApp->setStyleSheet(qss);
            styleSheetApplied_ = true;
        } else {
            // Refresh existing widgets against the changed palette without
            // reparsing the entire app stylesheet on every theme toggle.
            const auto widgets = QApplication::allWidgets();
            for (QWidget* widget : widgets) {
                widget->style()->unpolish(widget);
                widget->style()->polish(widget);
                widget->update();
            }
        }
    }

    emit themeChanged();
}

QColor ThemeManager::color(const QString& key) const
{
    const auto& tokens = resolvedDark_ ? dark_ : light_;
    return QColor(tokens.value(key, QStringLiteral("#ff00ff")));
}

QString ThemeManager::token(const QString& key) const
{
    const auto& tokens = resolvedDark_ ? dark_ : light_;
    return tokens.value(key);
}

QStringList ThemeManager::tokenKeys() const
{
    return (resolvedDark_ ? dark_ : light_).keys();
}

double ThemeManager::contrastRatio(const QColor& a, const QColor& b)
{
    const auto lum = [](const QColor& c) {
        const auto lin = [](double v) {
            v /= 255.0;
            return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * lin(c.red()) + 0.7152 * lin(c.green()) + 0.0722 * lin(c.blue());
    };
    const double l1 = lum(a), l2 = lum(b);
    const double hi = std::max(l1, l2), lo = std::min(l1, l2);
    return (hi + 0.05) / (lo + 0.05);
}

} // namespace ses::ui