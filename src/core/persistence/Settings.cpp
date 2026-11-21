#include "core/persistence/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace ses::core {

QString Settings::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/settings.json");
}

Settings Settings::load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return {};
    return fromJson(doc.object());
}

bool Settings::save() const
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return f.commit();
}

QJsonObject Settings::toJson() const
{
    QJsonArray recent, dismissed;
    for (const auto& s : recentProjects)
        recent.append(s);
    for (const auto& s : dismissedCoachMarks)
        dismissed.append(s);
    QJsonObject o{
        { QStringLiteral("schemaVersion"), schemaVersion },
        { QStringLiteral("theme"), theme },
        { QStringLiteral("snap"), snap },
        { QStringLiteral("reducedMotion"), reducedMotion },
        { QStringLiteral("mixerVisible"), mixerVisible },
        { QStringLiteral("dockVisible"), dockVisible },
        { QStringLiteral("uiScale"), uiScale },
        { QStringLiteral("recentProjects"), recent },
        { QStringLiteral("dismissedCoachMarks"), dismissed },
    };
    for (auto it = extras.begin(); it != extras.end(); ++it)
        if (!o.contains(it.key()))
            o.insert(it.key(), it.value());
    return o;
}

Settings Settings::fromJson(const QJsonObject& o)
{
    Settings s;
    s.schemaVersion = o.value(QStringLiteral("schemaVersion")).toInt(kSchemaVersion);
    const auto theme = o.value(QStringLiteral("theme")).toString();
    s.theme = (theme == "dark" || theme == "light") ? theme : QStringLiteral("system");
    s.snap = o.value(QStringLiteral("snap")).toBool(true);
    s.reducedMotion = o.value(QStringLiteral("reducedMotion")).toBool(false);
    s.mixerVisible = o.value(QStringLiteral("mixerVisible")).toBool(false);
    s.dockVisible = o.value(QStringLiteral("dockVisible")).toBool(true);
    const double scale = o.value(QStringLiteral("uiScale")).toDouble(1.0);
    s.uiScale = std::clamp(scale, 0.8, 1.6);
    for (const auto& v : o.value(QStringLiteral("recentProjects")).toArray())
        if (v.isString())
            s.recentProjects.append(v.toString());
    for (const auto& v : o.value(QStringLiteral("dismissedCoachMarks")).toArray())
        if (v.isString())
            s.dismissedCoachMarks.append(v.toString());

    QJsonObject rest = o;
    for (const char* k : { "schemaVersion", "theme", "snap", "reducedMotion", "mixerVisible",
                           "dockVisible", "uiScale", "recentProjects", "dismissedCoachMarks" })
        rest.remove(QLatin1String(k));
    s.extras = rest;
    return s;
}

} // namespace ses::core