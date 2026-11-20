#pragma once
// Transport — UI-side playback controller. Binds to engine::Engine when one is
// available (P1); until then it runs an explicitly-labelled PREVIEW transport
// (30 Hz playhead animation, no audio). The status bar always says which mode
// is active — never pretend sound is coming out.

#include "core/model/ProjectModel.h"
#include "engine/Engine.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace ses::ui {

class Transport : public QObject {
    Q_OBJECT
public:
    Transport(core::ProjectModel* model, engine::Engine* engine, QObject* parent = nullptr);

    bool previewMode() const { return !engine_->available(); }
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

signals:
    void playheadChanged(int64_t sample);
    void playingChanged(bool playing);
    void loopChanged(bool on);

private:
    void tick();
    void setPlaying(bool on);

    core::ProjectModel* model_;
    engine::Engine* engine_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool playing_ = false;
    bool loop_ = false;
    int64_t playhead_ = 0;
    int64_t loopStart_ = 0;
    int64_t loopEnd_ = 0;
};

} // namespace ses::ui