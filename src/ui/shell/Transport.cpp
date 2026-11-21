#include "ui/shell/Transport.h"

#include <algorithm>

namespace ses::ui {

Transport::Transport(core::ProjectModel* model, engine::Engine* engine, QObject* parent)
    : QObject(parent), model_(model), engine_(engine)
{
    timer_.setInterval(33); // ~30 Hz telemetry/pull rate (DESIGN.md §2)
    connect(&timer_, &QTimer::timeout, this, &Transport::tick);
}

void Transport::setPlaying(bool on)
{
    if (playing_ == on)
        return;
    playing_ = on;
    if (on)
        clock_.restart();
    emit playingChanged(on);
}

void Transport::play()
{
    if (playing_)
        return;
    if (!previewMode()) {
        engine_->play();
    }
    if (playhead_ >= model_->project().duration()) {
        playhead_ = loop_ ? loopStart_ : 0;
        if (!previewMode())
            engine_->seek(playhead_); // keep the RT playhead in sync with the UI
    }
    setPlaying(true);
    timer_.start();
}

void Transport::stop()
{
    if (!previewMode())
        engine_->stop();
    setPlaying(false);
    timer_.stop();
}

void Transport::toggle()
{
    playing_ ? stop() : play();
}

void Transport::seek(int64_t sample)
{
    const int64_t dur = model_->project().duration();
    playhead_ = std::clamp<int64_t>(sample, 0, std::max<int64_t>(0, dur));
    if (!previewMode())
        engine_->seek(playhead_);
    if (playing_)
        clock_.restart();
    emit playheadChanged(playhead_);
}

void Transport::toStart()
{
    seek(loop_ ? loopStart_ : 0);
}

void Transport::nudge(int64_t deltaSamples)
{
    seek(playhead_ + deltaSamples);
}

void Transport::setLoop(bool on)
{
    loop_ = on;
    if (on && loopEnd_ <= loopStart_) {
        loopStart_ = 0;
        loopEnd_ = model_->project().duration();
    }
    if (!previewMode())
        engine_->setLoop(loop_, loopStart_, loopEnd_);
    emit loopChanged(on);
}

void Transport::tick()
{
    if (!previewMode()) {
        // Real engine: pull authoritative state (telemetry ring drain in P1).
        const auto st = engine_->state();
        if (st.playhead != playhead_) {
            playhead_ = st.playhead;
            emit playheadChanged(playhead_);
        }
        if (st.playing != playing_)
            setPlaying(st.playing);
        return;
    }
    if (!playing_)
        return;
    const int64_t rate = model_->project().sampleRate;
    const int64_t elapsed = clock_.restart();
    int64_t next = playhead_ + elapsed * rate / 1000;
    const int64_t dur = model_->project().duration();
    if (loop_ && loopEnd_ > loopStart_ && next >= loopEnd_) {
        next = loopStart_ + (next - loopEnd_) % std::max<int64_t>(1, loopEnd_ - loopStart_);
    } else if (next >= dur) {
        next = dur;
        setPlaying(false);
        timer_.stop();
    }
    playhead_ = next;
    emit playheadChanged(playhead_);
}

} // namespace ses::ui