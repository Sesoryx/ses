#pragma once
// ses::core — project data model (DESIGN.md §5). Pure data + JSON, Qt6::Core only.
// All times are int64 samples at the project sample rate. Unknown JSON keys are
// preserved on load and re-emitted on save (forward-only compatibility).

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cstdint>

namespace ses::core {

QString newId();

struct Fade {
    int64_t length = 0;
    QString curve = QStringLiteral("linear"); // linear | equalPower | exp | sCurve
    QJsonObject extras;
    QJsonObject toJson() const;
    static Fade fromJson(const QJsonObject& o);
    bool operator==(const Fade&) const = default;
};

struct Clip {
    QString id;
    QString mediaId;
    QString name;
    QString color;
    bool muted = false;
    int64_t timelineStart = 0;
    int64_t sourceOffset = 0;
    int64_t length = 0;
    double gain = 1.0;
    Fade fadeIn;
    Fade fadeOut;
    bool loopEnabled = false; // phase 2; v1 always false
    QJsonObject aiMeta;       // prompt/seed/model for AI-generated clips (§7.7)
    QJsonObject extras;

    int64_t end() const { return timelineStart + length; }
    QJsonObject toJson() const;
    static Clip fromJson(const QJsonObject& o);
};

struct ClipTrim {
    int64_t sourceOffset = 0;
    int64_t length = 0;
    int64_t timelineStart = 0;
    bool operator==(const ClipTrim&) const = default;
};

struct AutomationPoint {
    int64_t t = 0;
    double value = 0.0;
    QString curve = QStringLiteral("linear"); // linear | exp | step
    QJsonObject toJson() const;
    static AutomationPoint fromJson(const QJsonObject& o);
};

struct AutomationLane {
    QString id;
    QString target; // ParamPath, e.g. "track.gain"
    double defaultValue = 1.0;
    QVector<AutomationPoint> points; // sorted by t
    QJsonObject extras;
    QJsonObject toJson() const;
    static AutomationLane fromJson(const QJsonObject& o);
};

struct FxInstance {
    QString id;
    QString type; // gain | eq3 | comp | limiter
    bool enabled = true;
    QJsonObject params;
    QJsonObject extras;
    QJsonObject toJson() const;
    static FxInstance fromJson(const QJsonObject& o);
};

struct BusChain {
    double gain = 1.0;
    QVector<FxInstance> fxChain;
    QJsonObject extras;
    QJsonObject toJson() const;
    static BusChain fromJson(const QJsonObject& o);
};

struct Track {
    QString id;
    QString name;
    QString kind = QStringLiteral("audio");
    QString color = QStringLiteral("#7aa2f7");
    int order = 0;
    bool mute = false;
    bool solo = false;
    double gain = 1.0;
    double pan = 0.0; // -1..1
    QString abRouting = QStringLiteral("both"); // A | B | both
    QVector<FxInstance> fxChain;
    QVector<Clip> clips;
    QVector<AutomationLane> automation;
    QJsonObject extras;

    QJsonObject toJson() const;
    static Track fromJson(const QJsonObject& o);
};

struct MediaItem {
    QString id;
    QString path;
    QString contentHash;
    QString format;
    int sampleRate = 48000;
    int channels = 2;
    int64_t frames = 0;
    QJsonObject extras;

    double seconds() const { return sampleRate > 0 ? double(frames) / sampleRate : 0.0; }
    QJsonObject toJson() const;
    static MediaItem fromJson(const QJsonObject& o);
};

struct Marker {
    QString id;
    QString label;
    QString color;
    int64_t sample = 0;
    QJsonObject extras;
    QJsonObject toJson() const;
    static Marker fromJson(const QJsonObject& o);
};

struct AbState {
    double fader = 0.5; // 0 = A only, 1 = B only
    QString mode = QStringLiteral("crossfade"); // crossfade | split
    BusChain busA;
    BusChain busB;
    QJsonObject extras;
    QJsonObject toJson() const;
    static AbState fromJson(const QJsonObject& o);
};

struct MasterState {
    double gain = 1.0;
    double limiterCeiling = -0.3;
    double limiterRelease = 0.05;
    QJsonObject extras;
    QJsonObject toJson() const;
    static MasterState fromJson(const QJsonObject& o);
};

struct ViewState {
    double zoom = 100.0; // pixels per second
    double scrollX = 0;
    double scrollY = 0;
    QString theme;
    QJsonObject extras;
    QJsonObject toJson() const;
    static ViewState fromJson(const QJsonObject& o);
};

struct Project {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    QString id = newId();
    QString name = QStringLiteral("Untitled");
    int sampleRate = 48000;
    int bitDepthExport = 24;
    qint64 createdAt = 0;
    qint64 modifiedAt = 0;
    double tempo = 120.0; // display only in v1
    QString timeSignature = QStringLiteral("4/4");
    QVector<MediaItem> mediaPool;
    QVector<Track> tracks;
    QVector<Marker> markers;
    AbState ab;
    MasterState master;
    ViewState view;
    QJsonObject extras;

    Track* trackById(const QString& id);
    const Track* trackById(const QString& id) const;
    MediaItem* mediaById(const QString& id);
    const MediaItem* mediaById(const QString& id) const;
    // Finds a clip anywhere; returns nullptr when absent. trackOut may be null.
    Clip* clipById(const QString& id, Track** trackOut = nullptr);
    const Clip* clipById(const QString& id, const Track** trackOut = nullptr) const;

    int64_t duration() const; // max clip end, samples
    int clipCount() const;

    QJsonObject toJson() const;
    static Project fromJson(const QJsonObject& o);
};

} // namespace ses::core