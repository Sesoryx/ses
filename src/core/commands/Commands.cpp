#include "core/commands/Commands.h"

#include <algorithm>
#include <utility>

namespace ses::core {

// ---- tracks -----------------------------------------------------------------

AddTrackCommand::AddTrackCommand(ProjectModel* m, Track t, int index)
    : m_(m), track_(std::move(t)), index_(index)
{
    if (track_.id.isEmpty())
        track_.id = newId();
    setText(QObject::tr("Add track"));
}

void AddTrackCommand::redo()
{
    m_->insertTrack(index_ < 0 ? int(m_->project().tracks.size()) : index_, track_);
}

void AddTrackCommand::undo()
{
    m_->removeTrack(track_.id);
}

RemoveTrackCommand::RemoveTrackCommand(ProjectModel* m, QString trackId)
    : m_(m), id_(std::move(trackId))
{
    index_ = m_->trackIndex(id_);
    if (const Track* t = m_->project().trackById(id_))
        track_ = *t;
    setText(QObject::tr("Remove track"));
}

void RemoveTrackCommand::redo()
{
    m_->removeTrack(id_);
}

void RemoveTrackCommand::undo()
{
    m_->insertTrack(index_, track_);
}

// ---- clips ------------------------------------------------------------------

AddClipCommand::AddClipCommand(ProjectModel* m, QString trackId, Clip c, int index, QUndoCommand* parent)
    : QUndoCommand(parent), m_(m), trackId_(std::move(trackId)), clip_(std::move(c)), index_(index)
{
    if (clip_.id.isEmpty())
        clip_.id = newId();
    setText(QObject::tr("Add clip"));
}

void AddClipCommand::redo()
{
    m_->insertClip(trackId_, index_ < 0 ? 1000000 : index_, clip_);
}

void AddClipCommand::undo()
{
    m_->removeClip(clip_.id);
}

RemoveClipsCommand::RemoveClipsCommand(ProjectModel* m, QStringList clipIds)
    : m_(m)
{
    for (const auto& id : clipIds) {
        const Track* t = nullptr;
        const Clip* c = m_->project().clipById(id, &t);
        if (c && t)
            removed_.append({ t->id, m_->clipIndex(id), *c });
    }
    // Remove in reverse document order so indices stay valid during redo.
    std::sort(removed_.begin(), removed_.end(), [](const Removed& a, const Removed& b) {
        if (a.trackId != b.trackId)
            return a.trackId < b.trackId;
        return a.index > b.index;
    });
    setText(QObject::tr("Remove clips"));
}

void RemoveClipsCommand::redo()
{
    for (const auto& r : removed_)
        m_->removeClip(r.clip.id);
}

void RemoveClipsCommand::undo()
{
    // Re-insert in ascending order per track.
    QVector<Removed> asc = removed_;
    std::sort(asc.begin(), asc.end(), [](const Removed& a, const Removed& b) {
        if (a.trackId != b.trackId)
            return a.trackId < b.trackId;
        return a.index < b.index;
    });
    for (const auto& r : asc)
        m_->insertClip(r.trackId, r.index, r.clip);
}

MoveClipsCommand::MoveClipsCommand(ProjectModel* m, QMap<QString, ClipTrim> after)
    : m_(m), after_(std::move(after))
{
    for (auto it = after_.begin(); it != after_.end(); ++it)
        before_.insert(it.key(), m_->clipTrim(it.key()));
    setText(QObject::tr("Move clips"));
}

bool MoveClipsCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != Id)
        return false;
    const auto* o = static_cast<const MoveClipsCommand*>(other);
    if (o->after_.keys() != after_.keys())
        return false;
    after_ = o->after_; // keep original before_
    return true;
}

void MoveClipsCommand::redo()
{
    for (auto it = after_.begin(); it != after_.end(); ++it)
        m_->setClipTrim(it.key(), it.value());
}

void MoveClipsCommand::undo()
{
    for (auto it = before_.begin(); it != before_.end(); ++it)
        m_->setClipTrim(it.key(), it.value());
}

TrimClipCommand::TrimClipCommand(ProjectModel* m, QString clipId, ClipTrim after)
    : m_(m), clipId_(std::move(clipId)), after_(after)
{
    before_ = m_->clipTrim(clipId_);
    setText(QObject::tr("Trim clip"));
}

bool TrimClipCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != Id)
        return false;
    const auto* o = static_cast<const TrimClipCommand*>(other);
    if (o->clipId_ != clipId_)
        return false;
    after_ = o->after_;
    return true;
}

void TrimClipCommand::redo()
{
    m_->setClipTrim(clipId_, after_);
}

void TrimClipCommand::undo()
{
    m_->setClipTrim(clipId_, before_);
}

SetClipGainCommand::SetClipGainCommand(ProjectModel* m, QString clipId, double gain)
    : m_(m), clipId_(std::move(clipId)), after_(gain)
{
    before_ = m_->clipCopy(clipId_).gain;
    setText(QObject::tr("Set clip gain"));
}

bool SetClipGainCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != Id)
        return false;
    const auto* o = static_cast<const SetClipGainCommand*>(other);
    if (o->clipId_ != clipId_)
        return false;
    after_ = o->after_;
    return true;
}

void SetClipGainCommand::redo()
{
    m_->setClipGain(clipId_, after_);
}

void SetClipGainCommand::undo()
{
    m_->setClipGain(clipId_, before_);
}

SetFadeCommand::SetFadeCommand(ProjectModel* m, QString clipId, bool fadeIn, Fade f)
    : m_(m), clipId_(std::move(clipId)), fadeIn_(fadeIn), after_(std::move(f))
{
    const Clip c = m_->clipCopy(clipId_);
    before_ = fadeIn_ ? c.fadeIn : c.fadeOut;
    setText(QObject::tr("Set fade"));
}

bool SetFadeCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != Id)
        return false;
    const auto* o = static_cast<const SetFadeCommand*>(other);
    if (o->clipId_ != clipId_ || o->fadeIn_ != fadeIn_)
        return false;
    after_ = o->after_;
    return true;
}

void SetFadeCommand::redo()
{
    m_->setClipFade(clipId_, fadeIn_, after_);
}

void SetFadeCommand::undo()
{
    m_->setClipFade(clipId_, fadeIn_, before_);
}

SplitClipsCommand::SplitClipsCommand(ProjectModel* m, QString clipId, int64_t s, QUndoCommand* parent)
    : QUndoCommand(parent), m_(m)
{
    const Track* t = nullptr;
    const Clip* c = m_->project().clipById(clipId, &t);
    if (!c || !t)
        return;
    if (s <= c->timelineStart || s >= c->end())
        return;
    original_ = *c;
    trackId_ = t->id;
    index_ = m_->clipIndex(clipId);

    left_ = original_;
    left_.length = s - original_.timelineStart;
    left_.fadeOut.length = std::min(left_.fadeOut.length, left_.length);

    right_ = original_;
    right_.id = newId();
    right_.timelineStart = s;
    right_.sourceOffset = original_.sourceOffset + (s - original_.timelineStart);
    right_.length = original_.length - (s - original_.timelineStart);
    right_.fadeIn.length = std::min(right_.fadeIn.length, right_.length);

    valid_ = true;
    setText(QObject::tr("Split clip"));
}

void SplitClipsCommand::redo()
{
    if (!valid_)
        return;
    m_->replaceClip(original_.id, left_);
    m_->insertClip(trackId_, index_ + 1, right_);
}

void SplitClipsCommand::undo()
{
    if (!valid_)
        return;
    m_->removeClip(right_.id);
    m_->replaceClip(original_.id, original_);
}

// ---- track params / AB --------------------------------------------------------

SetTrackParamCommand::SetTrackParamCommand(ProjectModel* m, QString trackId, TrackField f, QVariant after)
    : m_(m), trackId_(std::move(trackId)), field_(f), after_(std::move(after))
{
    before_ = read();
    setText(QObject::tr("Set track parameter"));
}

QVariant SetTrackParamCommand::read() const
{
    const Track* t = m_->project().trackById(trackId_);
    if (!t)
        return {};
    switch (field_) {
    case TrackField::Name: return t->name;
    case TrackField::Color: return t->color;
    case TrackField::Gain: return t->gain;
    case TrackField::Pan: return t->pan;
    case TrackField::Mute: return t->mute;
    case TrackField::Solo: return t->solo;
    case TrackField::AbRouting: return t->abRouting;
    case TrackField::Order: return t->order;
    }
    return {};
}

bool SetTrackParamCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;
    const auto* o = static_cast<const SetTrackParamCommand*>(other);
    if (o->trackId_ != trackId_ || o->field_ != field_)
        return false;
    after_ = o->after_;
    return true;
}

void SetTrackParamCommand::redo()
{
    m_->setTrackField(trackId_, field_, after_);
}

void SetTrackParamCommand::undo()
{
    m_->setTrackField(trackId_, field_, before_);
}

SetAbFaderCommand::SetAbFaderCommand(ProjectModel* m, double value)
    : m_(m), after_(value)
{
    before_ = m_->project().ab.fader;
    setText(QObject::tr("A/B fader"));
}

bool SetAbFaderCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != Id)
        return false;
    after_ = static_cast<const SetAbFaderCommand*>(other)->after_;
    return true;
}

void SetAbFaderCommand::redo()
{
    m_->setAbFader(after_);
}

void SetAbFaderCommand::undo()
{
    m_->setAbFader(before_);
}

SetAbModeCommand::SetAbModeCommand(ProjectModel* m, QString mode)
    : m_(m), after_(std::move(mode))
{
    before_ = m_->project().ab.mode;
    setText(QObject::tr("A/B mode"));
}

void SetAbModeCommand::redo()
{
    m_->setAbMode(after_);
}

void SetAbModeCommand::undo()
{
    m_->setAbMode(before_);
}

SetAbRoutingCommand::SetAbRoutingCommand(ProjectModel* m, QString trackId, QString routing)
    : m_(m), trackId_(std::move(trackId)), after_(std::move(routing))
{
    if (const Track* t = m_->project().trackById(trackId_))
        before_ = t->abRouting;
    setText(QObject::tr("A/B routing"));
}

void SetAbRoutingCommand::redo()
{
    m_->setTrackField(trackId_, TrackField::AbRouting, after_);
}

void SetAbRoutingCommand::undo()
{
    m_->setTrackField(trackId_, TrackField::AbRouting, before_);
}

AbBusCommand::AbBusCommand(ProjectModel* m, Kind kind)
    : m_(m)
{
    before_ = m_->project().ab;
    after_ = before_;
    switch (kind) {
    case CopyAtoB:
        after_.busB = before_.busA;
        setText(QObject::tr("Copy A → B"));
        break;
    case Swap:
        std::swap(after_.busA, after_.busB);
        setText(QObject::tr("Swap A/B"));
        break;
    case Reset:
        after_.busA = BusChain{};
        after_.busB = BusChain{};
        after_.fader = 0.5;
        setText(QObject::tr("Reset A/B"));
        break;
    }
}

void AbBusCommand::redo()
{
    m_->setAbState(after_);
}

void AbBusCommand::undo()
{
    m_->setAbState(before_);
}

// ---- import / misc --------------------------------------------------------------

ImportMediaCommand::ImportMediaCommand(ProjectModel* m, MediaItem media, Clip clip, QString trackId)
    : m_(m), media_(std::move(media)), clip_(std::move(clip)), trackId_(std::move(trackId))
{
    if (media_.id.isEmpty())
        media_.id = newId();
    if (clip_.id.isEmpty())
        clip_.id = newId();
    clip_.mediaId = media_.id;
    if (trackId_.isEmpty() || !m_->project().trackById(trackId_)) {
        createdTrack_ = true;
        newTrack_.id = newId();
        newTrack_.name = QObject::tr("Audio 1");
        trackId_ = newTrack_.id;
    }
    setText(QObject::tr("Import %1").arg(clip_.name));
}

void ImportMediaCommand::redo()
{
    m_->addMedia(media_);
    if (createdTrack_)
        m_->insertTrack(int(m_->project().tracks.size()), newTrack_);
    m_->insertClip(trackId_, 1000000, clip_);
}

void ImportMediaCommand::undo()
{
    m_->removeClip(clip_.id);
    if (createdTrack_)
        m_->removeTrack(trackId_);
    m_->removeMedia(media_.id);
}

SetProjectNameCommand::SetProjectNameCommand(ProjectModel* m, QString name)
    : m_(m), after_(std::move(name))
{
    before_ = m_->project().name;
    setText(QObject::tr("Rename project"));
}

void SetProjectNameCommand::redo()
{
    m_->setProjectName(after_);
}

void SetProjectNameCommand::undo()
{
    m_->setProjectName(before_);
}

} // namespace ses::core