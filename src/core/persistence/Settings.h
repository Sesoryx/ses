#pragma once
// Versioned app settings (DESIGN.md §11.2/§11.3). Stored at
// ~/.config/ses/settings.json; invalid stored values normalize to safe
// defaults on read; unknown keys are preserved on round-trip.

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ses::core {

struct Settings {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    QString theme = QStringLiteral("system"); // dark | light | system
    bool snap = true;
    bool reducedMotion = false;
    bool mixerVisible = true;
    bool dockVisible = true;
    double uiScale = 1.0; // 0.8..1.6
    QStringList recentProjects;
    QStringList dismissedCoachMarks;
    QJsonObject extras;

    static QString filePath();
    static Settings load();
    bool save() const; // atomic (QSaveFile)

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject& o); // normalizes
};

} // namespace ses::core