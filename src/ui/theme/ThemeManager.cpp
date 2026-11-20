#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QRegularExpression>
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
    for (const auto& k : keys)
        qss.replace(QLatin1Char('@') + k, tokens.value(k));
    QElapsedTimer stage;
    stage.start();
    if (qApp)
        qApp->setStyleSheet(qss);
    qInfo("theme apply: setStyleSheet=%lldms", stage.elapsed());
    // TEMP PROBE: isolate propagation vs rule-matching cost.
    stage.restart();
    qApp->setStyleSheet(QStringLiteral("QWidget { background: %1; color: %2; }").arg(tokens.value("bg"), tokens.value("text")));
    qInfo("theme probe: trivial setStyleSheet=%lldms", stage.elapsed());
    stage.restart();
    QString noFonts = qss;
    noFonts.remove(QRegularExpression(QStringLiteral("font-[^;]+;")));
    qApp->setStyleSheet(noFonts);
    qInfo("theme probe: no-font rules=%lldms", stage.elapsed());
    stage.restart();
    qApp->setStyleSheet(qss);
    qInfo("theme probe: full re-apply=%lldms", stage.elapsed());
    // TEMP PROBE: widget count + empty-window baseline.
    qInfo("theme probe: widget count=%d", qApp->allWidgets().size());
    stage.restart();
    const auto tops = qApp->topLevelWidgets();
    for (QWidget* t : tops)
        t->setUpdatesEnabled(false);
    qApp->setStyleSheet(QStringLiteral("QWidget { background: #123456; }"));
    for (QWidget* t : tops) {
        t->setUpdatesEnabled(true);
        t->update();
    }
    qInfo("theme probe: updates-disabled trivial=%lldms", stage.elapsed());
    stage.restart();
    qApp->setStyleSheet(qss);
    qInfo("theme probe: final full=%lldms", stage.elapsed());

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
        pal.setColor(QPalette::AlternateBase, color(QStringLiteral("panelAlt")));
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::Button, color(QStringLiteral("bgAlt")));
        pal.setColor(QPalette::ButtonText, text);
        pal.setColor(QPalette::BrightText, text);
        pal.setColor(QPalette::Highlight, accent);
        pal.setColor(QPalette::HighlightedText, color(QStringLiteral("accentText")));
        pal.setColor(QPalette::ToolTipBase, panel);
        pal.setColor(QPalette::ToolTipText, text);
        pal.setColor(QPalette::PlaceholderText, secondary);
        pal.setColor(QPalette::Link, accent);
        pal.setColor(QPalette::Disabled, QPalette::Text, secondary);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, secondary);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, secondary);
        stage.restart();
        qApp->setPalette(pal);
        qInfo("theme apply: setPalette=%lldms", stage.elapsed());
    }

    stage.restart();
    emit themeChanged();
    qInfo("theme apply: themeChanged=%lldms", stage.elapsed());
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