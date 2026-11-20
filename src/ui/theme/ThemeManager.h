#pragma once
// ThemeManager — single token source for the whole UI (DESIGN.md §10.10).
// tokens.{dark,light}.json → QSS generation + QPalette + timeline palette +
// icon tint. One shared apply path for startup/toggle/system-change, memoized.

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>

namespace ses::ui {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum Mode { Dark, Light, System };

    static ThemeManager* instance();

    void setMode(Mode mode);
    Mode mode() const { return mode_; }
    bool isDark() const { return resolvedDark_; }
    QString modeString() const;
    static Mode modeFromString(const QString& s);

    QColor color(const QString& token) const;
    QString token(const QString& key) const;
    QStringList tokenKeys() const;

    // Bumped on every applied theme change; icon caches key on it.
    int generation() const { return generation_; }

    // WCAG relative-luminance contrast ratio (used by the token contrast gate).
    static double contrastRatio(const QColor& a, const QColor& b);

signals:
    void themeChanged();

private:
    explicit ThemeManager(QObject* parent = nullptr);
    void apply();
    void loadTokens();

    Mode mode_ = System;
    bool resolvedDark_ = true;
    bool applied_ = false;
    int generation_ = 0;
    QHash<QString, QString> dark_, light_;
};

} // namespace ses::ui