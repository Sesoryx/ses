#include "core/model/ProjectModel.h"

#include <algorithm>

namespace ses::core {

ProjectModel::ProjectModel(QObject* parent)
    : QObject(parent)
{
    connect(&stack_, &QUndoStack::cleanChanged, this, [this](bool clean) {
        emit dirtyChanged(!clean);
    });
}

void ProjectModel::insertTrack(int index, const Track& t)
{
    index = std::clamp(index, 0, int(project_.tracks.size()));
    project_.tracks.insert(index, t);
    emit structureChanged();
    emit changed();
}

void ProjectModel::removeTrack(const QString& id)
{
    for (int i = 0; i < project_.tracks.size(); ++i)
        if (project_.tracks[i].id == id) {
            project_.tracks.remove(i);
            emit structureChanged();
            emit changed();
            return;
        }
}

int ProjectModel::trackIndex(const QString& id) const
{
    for (int i = 0; i < project_.tracks.size(); ++i)
        if (project_.tracks[i].id == id)
            return i;
    return -1;
}

void ProjectModel::setTrackField(const QString& id, TrackField f, const QVariant& v)
{
    Track* t = project_.trackById(id);
    if (!t)
        return;
    switch (f) {
    case TrackField::Name: t->name = v.toString(); break;
    case TrackField::Color: t->color = v.toString(); break;
    case TrackField::Gain: t->gain = std::clamp(v.toDouble(), 0.0, 4.0); break;
    case TrackField::Pan: t->pan = std::clamp(v.toDouble(), -1.0, 1.0); break;
    case TrackField::Mute: t->mute = v.toBool(); break;
    case TrackField::Solo: t->solo = v.toBool(); break;
    case TrackField::AbRouting: {
        const QString r = v.toString();
        t->abRouting = (r == "A" || r == "B") ? r : QStringLiteral("both");
        break;
    }
    case TrackField::Order: t->order = v.toInt(); break;
    }
    emit changed();
}

void ProjectModel::insertClip(const QString& trackId, int index, const Clip& c)
{
    Track* t = project_.trackById(trackId);
    if (!t)
        return;
    index = std::clamp(index, 0, int(t->clips.size()));
    t->clips.insert(index, c);
    emit structureChanged();
    emit changed();
}

void ProjectModel::removeClip(const QString& clipId)
{
    for (auto& t : project_.tracks)
        for (int i = 0; i < t.clips.size(); ++i)
            if (t.clips[i].id == clipId) {
                t.clips.remove(i);
                emit structureChanged();
                emit changed();
                return;
            }
}

void ProjectModel::replaceClip(const QString& clipId, const Clip& c)
{
    if (Clip* clip = project_.clipById(clipId)) {
        *clip = c;
        emit changed();
    }
}

void ProjectModel::setClipTrim(const QString& clipId, const ClipTrim& t)
{
    Track* track = nullptr;
    Clip* c = project_.clipById(clipId, &track);
    if (!c)
        return;
    ClipTrim v = t;
    v.timelineStart = std::max<int64_t>(0, v.timelineStart);
    v.sourceOffset = std::max<int64_t>(0, v.sourceOffset);
    v.length = std::max<int64_t>(0, v.length);
    if (const MediaItem* m = project_.mediaById(c->mediaId); m && m->frames > 0) {
        v.sourceOffset = std::min(v.sourceOffset, m->frames);
        v.length = std::min(v.length, m->frames - v.sourceOffset);
    }
    c->timelineStart = v.timelineStart;
    c->sourceOffset = v.sourceOffset;
    c->length = v.length;
    // Fades cannot exceed the clip.
    c->fadeIn.length = std::min(c->fadeIn.length, c->length);
    c->fadeOut.length = std::min(c->fadeOut.length, c->length);
    emit changed();
}

void ProjectModel::setClipGain(const QString& clipId, double g)
{
    if (Clip* c = project_.clipById(clipId)) {
        c->gain = std::clamp(g, 0.0, 4.0);
        emit changed();
    }
}

void ProjectModel::setClipFade(const QString& clipId, bool fadeIn, const Fade& f)
{
    if (Clip* c = project_.clipById(clipId)) {
        Fade v = f;
        v.length = std::clamp<int64_t>(v.length, 0, c->length);
        if (fadeIn)
            c->fadeIn = v;
        else
            c->fadeOut = v;
        emit changed();
    }
}

void ProjectModel::setClipMuted(const QString& clipId, bool muted)
{
    if (Clip* c = project_.clipById(clipId)) {
        c->muted = muted;
        emit changed();
    }
}

void ProjectModel::addMedia(const MediaItem& m)
{
    project_.mediaPool.append(m);
    emit changed();
}

void ProjectModel::removeMedia(const QString& id)
{
    for (int i = 0; i < project_.mediaPool.size(); ++i)
        if (project_.mediaPool[i].id == id) {
            project_.mediaPool.remove(i);
            emit changed();
            return;
        }
}

void ProjectModel::setAbFader(double v)
{
    project_.ab.fader = std::clamp(v, 0.0, 1.0);
    emit changed();
}

void ProjectModel::setAbMode(const QString& mode)
{
    project_.ab.mode = (mode == "split") ? QStringLiteral("split") : QStringLiteral("crossfade");
    emit changed();
}

void ProjectModel::setAbState(const AbState& s)
{
    project_.ab = s;
    emit changed();
}

void ProjectModel::setProjectName(const QString& name)
{
    project_.name = name;
    emit changed();
}

void ProjectModel::addMarker(const Marker& m)
{
    project_.markers.append(m);
    std::sort(project_.markers.begin(), project_.markers.end(),
              [](const Marker& a, const Marker& b) { return a.sample < b.sample; });
    emit changed();
}

void ProjectModel::removeMarker(const QString& id)
{
    for (int i = 0; i < project_.markers.size(); ++i)
        if (project_.markers[i].id == id) {
            project_.markers.remove(i);
            emit changed();
            return;
        }
}

ClipTrim ProjectModel::clipTrim(const QString& clipId) const
{
    if (const Clip* c = project_.clipById(clipId))
        return { c->sourceOffset, c->length, c->timelineStart };
    return {};
}

Clip ProjectModel::clipCopy(const QString& clipId) const
{
    if (const Clip* c = project_.clipById(clipId))
        return *c;
    return {};
}

QString ProjectModel::trackIdOfClip(const QString& clipId) const
{
    const Track* t = nullptr;
    project_.clipById(clipId, &t);
    return t ? t->id : QString();
}

int ProjectModel::clipIndex(const QString& clipId) const
{
    const Track* t = nullptr;
    const Clip* c = project_.clipById(clipId, &t);
    if (!c || !t)
        return -1;
    return int(c - t->clips.data());
}

} // namespace ses::core