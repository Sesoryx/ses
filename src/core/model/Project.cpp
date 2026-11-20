#include "core/model/Project.h"

#include <QJsonArray>
#include <QUuid>

namespace ses::core {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

namespace {

// Returns o minus the known keys — the forward-compat remainder (§5.6).
QJsonObject remainder(QJsonObject o, std::initializer_list<const char*> known)
{
    for (const char* k : known)
        o.remove(QLatin1String(k));
    return o;
}

void mergeExtras(QJsonObject& target, const QJsonObject& extras)
{
    for (auto it = extras.begin(); it != extras.end(); ++it)
        if (!target.contains(it.key()))
            target.insert(it.key(), it.value());
}

QJsonArray toJsonArray(const auto& items)
{
    QJsonArray a;
    for (const auto& item : items)
        a.append(item.toJson());
    return a;
}

QString str(const QJsonObject& o, const char* key, const QString& def = {})
{
    const auto v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : def;
}
double dbl(const QJsonObject& o, const char* key, double def)
{
    const auto v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : def;
}
int64_t i64(const QJsonObject& o, const char* key, int64_t def = 0)
{
    const auto v = o.value(QLatin1String(key));
    return v.isDouble() ? int64_t(v.toDouble()) : def;
}
bool boolean(const QJsonObject& o, const char* key, bool def)
{
    const auto v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : def;
}

} // namespace

// ---- Fade -------------------------------------------------------------------

QJsonObject Fade::toJson() const
{
    QJsonObject o{ { QStringLiteral("length"), double(length) }, { QStringLiteral("curve"), curve } };
    mergeExtras(o, extras);
    return o;
}

Fade Fade::fromJson(const QJsonObject& o)
{
    Fade f;
    f.length = i64(o, "length");
    f.curve = str(o, "curve", QStringLiteral("linear"));
    f.extras = remainder(o, { "length", "curve" });
    return f;
}

// ---- Clip -------------------------------------------------------------------

QJsonObject Clip::toJson() const
{
    QJsonObject o{
        { QStringLiteral("id"), id },
        { QStringLiteral("mediaId"), mediaId },
        { QStringLiteral("name"), name },
        { QStringLiteral("color"), color },
        { QStringLiteral("muted"), muted },
        { QStringLiteral("timelineStart"), double(timelineStart) },
        { QStringLiteral("sourceOffset"), double(sourceOffset) },
        { QStringLiteral("length"), double(length) },
        { QStringLiteral("gain"), gain },
        { QStringLiteral("fadeIn"), fadeIn.toJson() },
        { QStringLiteral("fadeOut"), fadeOut.toJson() },
        { QStringLiteral("loopEnabled"), loopEnabled },
    };
    if (!aiMeta.isEmpty())
        o.insert(QStringLiteral("aiMeta"), aiMeta);
    mergeExtras(o, extras);
    return o;
}

Clip Clip::fromJson(const QJsonObject& o)
{
    Clip c;
    c.id = str(o, "id", newId());
    c.mediaId = str(o, "mediaId");
    c.name = str(o, "name");
    c.color = str(o, "color");
    c.muted = boolean(o, "muted", false);
    c.timelineStart = i64(o, "timelineStart");
    c.sourceOffset = i64(o, "sourceOffset");
    c.length = i64(o, "length");
    c.gain = dbl(o, "gain", 1.0);
    c.fadeIn = Fade::fromJson(o.value(QStringLiteral("fadeIn")).toObject());
    c.fadeOut = Fade::fromJson(o.value(QStringLiteral("fadeOut")).toObject());
    c.loopEnabled = boolean(o, "loopEnabled", false);
    c.aiMeta = o.value(QStringLiteral("aiMeta")).toObject();
    c.extras = remainder(o, { "id", "mediaId", "name", "color", "muted", "timelineStart",
                              "sourceOffset", "length", "gain", "fadeIn", "fadeOut",
                              "loopEnabled", "aiMeta" });
    return c;
}

// ---- Automation ---------------------------------------------------------------

QJsonObject AutomationPoint::toJson() const
{
    return { { QStringLiteral("t"), double(t) }, { QStringLiteral("value"), value },
             { QStringLiteral("curve"), curve } };
}

AutomationPoint AutomationPoint::fromJson(const QJsonObject& o)
{
    AutomationPoint p;
    p.t = i64(o, "t");
    p.value = dbl(o, "value", 0.0);
    p.curve = str(o, "curve", QStringLiteral("linear"));
    return p;
}

QJsonObject AutomationLane::toJson() const
{
    QJsonArray pts;
    for (const auto& p : points)
        pts.append(p.toJson());
    QJsonObject o{ { QStringLiteral("id"), id }, { QStringLiteral("target"), target },
                   { QStringLiteral("defaultValue"), defaultValue },
                   { QStringLiteral("points"), pts } };
    mergeExtras(o, extras);
    return o;
}

AutomationLane AutomationLane::fromJson(const QJsonObject& o)
{
    AutomationLane l;
    l.id = str(o, "id", newId());
    l.target = str(o, "target");
    l.defaultValue = dbl(o, "defaultValue", 1.0);
    for (const auto& v : o.value(QStringLiteral("points")).toArray())
        l.points.append(AutomationPoint::fromJson(v.toObject()));
    l.extras = remainder(o, { "id", "target", "defaultValue", "points" });
    return l;
}

// ---- FX / buses ----------------------------------------------------------------

QJsonObject FxInstance::toJson() const
{
    QJsonObject o{ { QStringLiteral("id"), id }, { QStringLiteral("type"), type },
                   { QStringLiteral("enabled"), enabled }, { QStringLiteral("params"), params } };
    mergeExtras(o, extras);
    return o;
}

FxInstance FxInstance::fromJson(const QJsonObject& o)
{
    FxInstance f;
    f.id = str(o, "id", newId());
    f.type = str(o, "type");
    f.enabled = boolean(o, "enabled", true);
    f.params = o.value(QStringLiteral("params")).toObject();
    f.extras = remainder(o, { "id", "type", "enabled", "params" });
    return f;
}

QJsonObject BusChain::toJson() const
{
    QJsonObject o{ { QStringLiteral("gain"), gain }, { QStringLiteral("fxChain"), toJsonArray(fxChain) } };
    mergeExtras(o, extras);
    return o;
}

BusChain BusChain::fromJson(const QJsonObject& o)
{
    BusChain b;
    b.gain = dbl(o, "gain", 1.0);
    for (const auto& v : o.value(QStringLiteral("fxChain")).toArray())
        b.fxChain.append(FxInstance::fromJson(v.toObject()));
    b.extras = remainder(o, { "gain", "fxChain" });
    return b;
}

// ---- Track ---------------------------------------------------------------------

QJsonObject Track::toJson() const
{
    QJsonObject o{
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), name },
        { QStringLiteral("kind"), kind },
        { QStringLiteral("color"), color },
        { QStringLiteral("order"), order },
        { QStringLiteral("mute"), mute },
        { QStringLiteral("solo"), solo },
        { QStringLiteral("gain"), gain },
        { QStringLiteral("pan"), pan },
        { QStringLiteral("abRouting"), abRouting },
        { QStringLiteral("fxChain"), toJsonArray(fxChain) },
        { QStringLiteral("clips"), toJsonArray(clips) },
        { QStringLiteral("automation"), toJsonArray(automation) },
    };
    mergeExtras(o, extras);
    return o;
}

Track Track::fromJson(const QJsonObject& o)
{
    Track t;
    t.id = str(o, "id", newId());
    t.name = str(o, "name", QStringLiteral("Track"));
    t.kind = str(o, "kind", QStringLiteral("audio"));
    t.color = str(o, "color", QStringLiteral("#7aa2f7"));
    t.order = int(i64(o, "order"));
    t.mute = boolean(o, "mute", false);
    t.solo = boolean(o, "solo", false);
    t.gain = dbl(o, "gain", 1.0);
    t.pan = dbl(o, "pan", 0.0);
    t.abRouting = str(o, "abRouting", QStringLiteral("both"));
    for (const auto& v : o.value(QStringLiteral("fxChain")).toArray())
        t.fxChain.append(FxInstance::fromJson(v.toObject()));
    for (const auto& v : o.value(QStringLiteral("clips")).toArray())
        t.clips.append(Clip::fromJson(v.toObject()));
    for (const auto& v : o.value(QStringLiteral("automation")).toArray())
        t.automation.append(AutomationLane::fromJson(v.toObject()));
    t.extras = remainder(o, { "id", "name", "kind", "color", "order", "mute", "solo", "gain",
                              "pan", "abRouting", "fxChain", "clips", "automation" });
    return t;
}

// ---- Media / markers -------------------------------------------------------------

QJsonObject MediaItem::toJson() const
{
    QJsonObject o{
        { QStringLiteral("id"), id },
        { QStringLiteral("path"), path },
        { QStringLiteral("contentHash"), contentHash },
        { QStringLiteral("format"), format },
        { QStringLiteral("sampleRate"), sampleRate },
        { QStringLiteral("channels"), channels },
        { QStringLiteral("frames"), double(frames) },
    };
    mergeExtras(o, extras);
    return o;
}

MediaItem MediaItem::fromJson(const QJsonObject& o)
{
    MediaItem m;
    m.id = str(o, "id", newId());
    m.path = str(o, "path");
    m.contentHash = str(o, "contentHash");
    m.format = str(o, "format");
    m.sampleRate = int(i64(o, "sampleRate", 48000));
    m.channels = int(i64(o, "channels", 2));
    m.frames = i64(o, "frames");
    m.extras = remainder(o, { "id", "path", "contentHash", "format", "sampleRate", "channels", "frames" });
    return m;
}

QJsonObject Marker::toJson() const
{
    QJsonObject o{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
                   { QStringLiteral("color"), color }, { QStringLiteral("sample"), double(sample) } };
    mergeExtras(o, extras);
    return o;
}

Marker Marker::fromJson(const QJsonObject& o)
{
    Marker m;
    m.id = str(o, "id", newId());
    m.label = str(o, "label");
    m.color = str(o, "color");
    m.sample = i64(o, "sample");
    m.extras = remainder(o, { "id", "label", "color", "sample" });
    return m;
}

// ---- AB / master / view -----------------------------------------------------------

QJsonObject AbState::toJson() const
{
    QJsonObject o{ { QStringLiteral("fader"), fader }, { QStringLiteral("mode"), mode },
                   { QStringLiteral("busA"), busA.toJson() }, { QStringLiteral("busB"), busB.toJson() } };
    mergeExtras(o, extras);
    return o;
}

AbState AbState::fromJson(const QJsonObject& o)
{
    AbState a;
    a.fader = dbl(o, "fader", 0.5);
    a.mode = str(o, "mode", QStringLiteral("crossfade"));
    a.busA = BusChain::fromJson(o.value(QStringLiteral("busA")).toObject());
    a.busB = BusChain::fromJson(o.value(QStringLiteral("busB")).toObject());
    a.extras = remainder(o, { "fader", "mode", "busA", "busB" });
    return a;
}

QJsonObject MasterState::toJson() const
{
    QJsonObject o{
        { QStringLiteral("gain"), gain },
        { QStringLiteral("limiter"), QJsonObject{ { QStringLiteral("ceiling"), limiterCeiling },
                                                  { QStringLiteral("release"), limiterRelease } } },
    };
    mergeExtras(o, extras);
    return o;
}

MasterState MasterState::fromJson(const QJsonObject& o)
{
    MasterState m;
    m.gain = dbl(o, "gain", 1.0);
    const auto lim = o.value(QStringLiteral("limiter")).toObject();
    m.limiterCeiling = dbl(lim, "ceiling", -0.3);
    m.limiterRelease = dbl(lim, "release", 0.05);
    m.extras = remainder(o, { "gain", "limiter" });
    return m;
}

QJsonObject ViewState::toJson() const
{
    QJsonObject o{ { QStringLiteral("zoom"), zoom }, { QStringLiteral("scrollX"), scrollX },
                   { QStringLiteral("scrollY"), scrollY }, { QStringLiteral("theme"), theme } };
    mergeExtras(o, extras);
    return o;
}

ViewState ViewState::fromJson(const QJsonObject& o)
{
    ViewState v;
    v.zoom = dbl(o, "zoom", 100.0);
    v.scrollX = dbl(o, "scrollX", 0);
    v.scrollY = dbl(o, "scrollY", 0);
    v.theme = str(o, "theme");
    v.extras = remainder(o, { "zoom", "scrollX", "scrollY", "theme" });
    return v;
}

// ---- Project ----------------------------------------------------------------------

Track* Project::trackById(const QString& tid)
{
    for (auto& t : tracks)
        if (t.id == tid)
            return &t;
    return nullptr;
}

const Track* Project::trackById(const QString& tid) const
{
    for (const auto& t : tracks)
        if (t.id == tid)
            return &t;
    return nullptr;
}

MediaItem* Project::mediaById(const QString& mid)
{
    for (auto& m : mediaPool)
        if (m.id == mid)
            return &m;
    return nullptr;
}

const MediaItem* Project::mediaById(const QString& mid) const
{
    for (const auto& m : mediaPool)
        if (m.id == mid)
            return &m;
    return nullptr;
}

Clip* Project::clipById(const QString& cid, Track** trackOut)
{
    for (auto& t : tracks)
        for (auto& c : t.clips)
            if (c.id == cid) {
                if (trackOut)
                    *trackOut = &t;
                return &c;
            }
    if (trackOut)
        *trackOut = nullptr;
    return nullptr;
}

const Clip* Project::clipById(const QString& cid, const Track** trackOut) const
{
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            if (c.id == cid) {
                if (trackOut)
                    *trackOut = &t;
                return &c;
            }
    if (trackOut)
        *trackOut = nullptr;
    return nullptr;
}

int64_t Project::duration() const
{
    int64_t d = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            d = std::max(d, c.end());
    return d;
}

int Project::clipCount() const
{
    int n = 0;
    for (const auto& t : tracks)
        n += int(t.clips.size());
    return n;
}

QJsonObject Project::toJson() const
{
    QJsonObject o{
        { QStringLiteral("schemaVersion"), schemaVersion },
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), name },
        { QStringLiteral("sampleRate"), sampleRate },
        { QStringLiteral("bitDepthExport"), bitDepthExport },
        { QStringLiteral("createdAt"), double(createdAt) },
        { QStringLiteral("modifiedAt"), double(modifiedAt) },
        { QStringLiteral("tempo"), tempo },
        { QStringLiteral("timeSignature"), timeSignature },
        { QStringLiteral("mediaPool"), toJsonArray(mediaPool) },
        { QStringLiteral("tracks"), toJsonArray(tracks) },
        { QStringLiteral("markers"), toJsonArray(markers) },
        { QStringLiteral("ab"), ab.toJson() },
        { QStringLiteral("master"), master.toJson() },
        { QStringLiteral("view"), view.toJson() },
    };
    mergeExtras(o, extras);
    return o;
}

Project Project::fromJson(const QJsonObject& o)
{
    Project p;
    p.schemaVersion = int(i64(o, "schemaVersion", kSchemaVersion));
    p.id = str(o, "id", newId());
    p.name = str(o, "name", QStringLiteral("Untitled"));
    p.sampleRate = int(i64(o, "sampleRate", 48000));
    p.bitDepthExport = int(i64(o, "bitDepthExport", 24));
    p.createdAt = i64(o, "createdAt");
    p.modifiedAt = i64(o, "modifiedAt");
    p.tempo = dbl(o, "tempo", 120.0);
    p.timeSignature = str(o, "timeSignature", QStringLiteral("4/4"));
    for (const auto& v : o.value(QStringLiteral("mediaPool")).toArray())
        p.mediaPool.append(MediaItem::fromJson(v.toObject()));
    for (const auto& v : o.value(QStringLiteral("tracks")).toArray())
        p.tracks.append(Track::fromJson(v.toObject()));
    for (const auto& v : o.value(QStringLiteral("markers")).toArray())
        p.markers.append(Marker::fromJson(v.toObject()));
    p.ab = AbState::fromJson(o.value(QStringLiteral("ab")).toObject());
    p.master = MasterState::fromJson(o.value(QStringLiteral("master")).toObject());
    p.view = ViewState::fromJson(o.value(QStringLiteral("view")).toObject());
    p.extras = remainder(o, { "schemaVersion", "id", "name", "sampleRate", "bitDepthExport",
                              "createdAt", "modifiedAt", "tempo", "timeSignature", "mediaPool",
                              "tracks", "markers", "ab", "master", "view" });
    return p;
}

} // namespace ses::core