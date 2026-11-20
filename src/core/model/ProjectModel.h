#pragma once
// ProjectModel — single source of truth (DESIGN.md §6.1). Mutated only by
// QUndoCommand::redo()/undo() on the GUI thread. Emits changed() after every
// committed mutation so the shell can rebuild plans / mark dirty / schedule saves.

#include "core/model/Project.h"

#include <QObject>
#include <QUndoStack>
#include <QVariant>

namespace ses::core {

enum class TrackField { Name, Color, Gain, Pan, Mute, Solo, AbRouting, Order };

class ProjectModel : public QObject {
    Q_OBJECT
public:
    explicit ProjectModel(QObject* parent = nullptr);

    Project& project() { return project_; }
    const Project& project() const { return project_; }
    QUndoStack& undoStack() { return stack_; }
    const QUndoStack& undoStack() const { return stack_; }
    void push(QUndoCommand* cmd) { stack_.push(cmd); }

    // ---- mutation API (called by commands; also emits change signals) ----
    void insertTrack(int index, const Track& t);
    void removeTrack(const QString& id);
    int trackIndex(const QString& id) const;
    void setTrackField(const QString& id, TrackField f, const QVariant& v);

    void insertClip(const QString& trackId, int index, const Clip& c);
    void removeClip(const QString& clipId);
    void replaceClip(const QString& clipId, const Clip& c);
    void setClipTrim(const QString& clipId, const ClipTrim& t); // clamped to media bounds
    void setClipGain(const QString& clipId, double g);
    void setClipFade(const QString& clipId, bool fadeIn, const Fade& f);
    void setClipMuted(const QString& clipId, bool muted);

    void addMedia(const MediaItem& m);
    void removeMedia(const QString& id);

    void setAbFader(double v);
    void setAbMode(const QString& mode);
    void setAbState(const AbState& s);

    void setProjectName(const QString& name);
    void addMarker(const Marker& m);
    void removeMarker(const QString& id);

    // ---- value reads used to capture command before-state ----
    ClipTrim clipTrim(const QString& clipId) const;
    Clip clipCopy(const QString& clipId) const;
    QString trackIdOfClip(const QString& clipId) const;
    int clipIndex(const QString& clipId) const;

signals:
    void changed();          // any committed mutation (incl. undo/redo)
    void structureChanged(); // tracks/clips added or removed
    void dirtyChanged(bool dirty);

private:
    Project project_;
    QUndoStack stack_;
};

} // namespace ses::core