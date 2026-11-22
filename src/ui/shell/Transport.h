#pragma once
// Transport — UI-side playback controller. Binds to engine::Engine when one is
// available (P1); until then it runs an explicitly-labelled PREVIEW transport
// (30 Hz playhead animation, no audio). The status bar always says which mode
// is active — never pretend sound is coming out.

#include "core/model/ProjectModel.h"
#include "engine/Engine.h"

#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QTimer>

namespace ses::ui {

class Transport : public QObject {
    Q_OBJECT
public:
    Transport(core::ProjectModel* model, engine::Engine* engine, QObject* parent = nullptr);

    // Preview is an explicit no-device mode, not a temporary device loss.
    // During recovery the transport freezes and later resumes (ROADMAP §2.6).
    bool previewMode() const { return previewMode_; }
    bool playing() const { return playing_; }
    int64_t playhead() const { return playhead_; }
    bool loop() const { return loop_; }
    int64_t loopStart() const { return loopStart_; }
    int64_t loopEnd() const { return loopEnd_; }

public slots:
    void play();
    void stop();
    void toggle();
    void seek(int64_t sample);
    void toStart();
    void nudge(int64_t deltaSamples);
    void setLoop(bool on);
    void preserveEngineState(); // snapshot the authoritative head before close
    void usePreviewMode();      // permanent recovery failure: honestly no audio

signals:
    void playheadChanged(int64_t sample);
    void playingChanged(bool playing);
    void loopChanged(bool on);
    // P1 task 4: the 30 Hz engine drain (ROADMAP §7) — telemetry feeds the
    // meters; device events are the hook for the task-7 recovery path.
    void telemetryUpdated(const ses::engine::Telemetry& t);
    void deviceEvent(ses::engine::DeviceEvent e);

private:
    void tick();
    void drainEngine(); // P1 task 4: SpscRing drains → signals, GUI thread
    void setPlaying(bool on);

    core::ProjectModel* model_;
    engine::Engine* engine_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool previewMode_ = false;
    bool stopPending_ = false; // user Stop sent, RT has not acknowledged yet
    bool playing_ = false;
    bool loop_ = false;
    int64_t playhead_ = 0;
    int64_t loopStart_ = 0;
    int64_t loopEnd_ = 0;
};

} // namespace ses::ui

// The drain signals may also be connected queued (worker-side consumers later).
Q_DECLARE_METATYPE(ses::engine::Telemetry)
Q_DECLARE_METATYPE(ses::engine::DeviceEvent)