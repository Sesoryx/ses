#pragma once
// Undo command catalog (DESIGN.md §6.1). Every command stores minimal
// before/after value copies; interactive drags merge into one undo step.

#include "core/model/ProjectModel.h"

#include <QMap>
#include <QStringList>
#include <QUndoCommand>

namespace ses::core {

class AddTrackCommand : public QUndoCommand {
public:
    AddTrackCommand(ProjectModel* m, Track t, int index = -1);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    Track track_;
    int index_;
};

class RemoveTrackCommand : public QUndoCommand {
public:
    RemoveTrackCommand(ProjectModel* m, QString trackId);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString id_;
    Track track_;
    int index_ = 0;
};

class AddClipCommand : public QUndoCommand {
public:
    AddClipCommand(ProjectModel* m, QString trackId, Clip c, int index = -1, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString trackId_;
    Clip clip_;
    int index_;
};

class RemoveClipsCommand : public QUndoCommand {
public:
    RemoveClipsCommand(ProjectModel* m, QStringList clipIds);
    void redo() override;
    void undo() override;

private:
    struct Removed {
        QString trackId;
        int index;
        Clip clip;
    };
    ProjectModel* m_;
    QVector<Removed> removed_;
};

class MoveClipsCommand : public QUndoCommand {
public:
    enum { Id = 1002 };
    MoveClipsCommand(ProjectModel* m, QMap<QString, ClipTrim> after);
    int id() const override { return Id; }
    bool mergeWith(const QUndoCommand* other) override;
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QMap<QString, ClipTrim> before_, after_;
};

class TrimClipCommand : public QUndoCommand {
public:
    enum { Id = 1001 };
    TrimClipCommand(ProjectModel* m, QString clipId, ClipTrim after);
    int id() const override { return Id; }
    bool mergeWith(const QUndoCommand* other) override;
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString clipId_;
    ClipTrim before_, after_;
};

class SetClipGainCommand : public QUndoCommand {
public:
    enum { Id = 1003 };
    SetClipGainCommand(ProjectModel* m, QString clipId, double gain);
    int id() const override { return Id; }
    bool mergeWith(const QUndoCommand* other) override;
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString clipId_;
    double before_, after_;
};

class SetFadeCommand : public QUndoCommand {
public:
    enum { Id = 1005 };
    SetFadeCommand(ProjectModel* m, QString clipId, bool fadeIn, Fade f);
    int id() const override { return Id; }
    bool mergeWith(const QUndoCommand* other) override;
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString clipId_;
    bool fadeIn_;
    Fade before_, after_;
};

class SplitClipsCommand : public QUndoCommand {
public:
    // Splits at absolute timeline sample s; no-op when s is outside the clip.
    SplitClipsCommand(ProjectModel* m, QString clipId, int64_t s, QUndoCommand* parent = nullptr);
    bool isValid() const { return valid_; }
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString trackId_;
    Clip original_;
    Clip left_;
    Clip right_;
    int index_ = 0;
    bool valid_ = false;
};

class SetTrackParamCommand : public QUndoCommand {
public:
    enum { IdBase = 1100 };
    SetTrackParamCommand(ProjectModel* m, QString trackId, TrackField f, QVariant after);
    int id() const override { return IdBase + int(field_); }
    bool mergeWith(const QUndoCommand* other) override;
    void redo() override;
    void undo() override;

private:
    QVariant read() const;
    ProjectModel* m_;
    QString trackId_;
    TrackField field_;
    QVariant before_, after_;
};

class SetAbFaderCommand : public QUndoCommand {
public:
    enum { Id = 1200 };
    SetAbFaderCommand(ProjectModel* m, double value);
    int id() const override { return Id; }
    bool mergeWith(const QUndoCommand* other) override;
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    double before_, after_;
};

class SetAbModeCommand : public QUndoCommand {
public:
    SetAbModeCommand(ProjectModel* m, QString mode);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString before_, after_;
};

class SetAbRoutingCommand : public QUndoCommand {
public:
    SetAbRoutingCommand(ProjectModel* m, QString trackId, QString routing);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString trackId_;
    QString before_, after_;
};

// Copy A→B, Swap A/B, Reset — one undoable bus-state change (§4.5).
class AbBusCommand : public QUndoCommand {
public:
    enum Kind { CopyAtoB, Swap, Reset };
    AbBusCommand(ProjectModel* m, Kind kind);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    AbState before_, after_;
};

// Import: new MediaItem + clip (+ track when the project has none). Atomic.
class ImportMediaCommand : public QUndoCommand {
public:
    ImportMediaCommand(ProjectModel* m, MediaItem media, Clip clip, QString trackId);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    MediaItem media_;
    Clip clip_;
    QString trackId_;
    Track newTrack_;
    bool createdTrack_ = false;
};

class SetProjectNameCommand : public QUndoCommand {
public:
    SetProjectNameCommand(ProjectModel* m, QString name);
    void redo() override;
    void undo() override;

private:
    ProjectModel* m_;
    QString before_, after_;
};

} // namespace ses::core