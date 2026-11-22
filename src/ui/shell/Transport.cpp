#include "ui/shell/Transport.h"

#include <algorithm>

namespace ses::ui {

Transport::Transport(core::ProjectModel* model, engine::Engine* engine, QObject* parent)
    : QObject(parent), model_(model), engine_(engine), previewMode_(!engine->available())
{
    qRegisterMetaType<engine::Telemetry>();
    qRegisterMetaType<engine::DeviceEvent>();
    timer_.setInterval(33); // 30 Hz: telemetry drain + playhead pull (DESIGN.md §2)
    connect(&timer_, &QTimer::timeout, this, &Transport::tick);
    // The drain starts with the pump (play()) and outlives stop() in engine
    // mode (meters decay, device events arrive). Before first play the callback
    // has never run, so the rings are provably empty — nothing to drain.
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
    stopPending_ = false;
    setPlaying(true);
    timer_.start();
}

void Transport::stop()
{
    if (!previewMode()) {
        engine_->stop();
        // Stop is the user's intent; it also makes the Stopped notification
        // distinguishable from a real device loss (DeviceRecovery probe).
        stopPending_ = true;
        setPlaying(false);
        // Keep the drain alive: the pump still emits silence telemetry (meters
        // decay) and device events can arrive any time (P1 task 7).
        return;
    }
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

void Transport::preserveEngineState()
{
    if (previewMode_ || !engine_->available())
        return;
    const auto st = engine_->state();
    if (st.playhead == playhead_)
        return;
    playhead_ = st.playhead;
    clock_.restart();
    emit playheadChanged(playhead_);
}

void Transport::usePreviewMode()
{
    if (previewMode_)
        return;
    previewMode_ = true;
    clock_.restart(); // do not count the device-outage interval as preview time
    if (!playing_)
        timer_.stop();
}

void Transport::drainEngine()
{
    // Aggregate one 30 Hz window per signal: latest playhead/counters, max peak.
    // Full rings drop pushes (never block); a dropped batch is a meter tick.
    engine::Telemetry batch {};
    engine::Telemetry t {};
    float peak = 0.0f;
    bool any = false;
    while (engine_->nextTelemetry(t)) {
        batch = t;
        peak = std::max(peak, t.masterPeak);
        any = true;
    }
    if (any) {
        batch.masterPeak = peak;
        emit telemetryUpdated(batch);
    }
    engine::DeviceEvent e {};
    while (engine_->nextDeviceEvent(e))
        emit deviceEvent(e);
}

void Transport::tick()
{
    if (!previewMode()) {
        drainEngine(); // P1 task 4: rings → meters (30 Hz)
        // A device loss is not preview playback. Freeze both state and head
        // while DeviceRecovery retries; resume from the preserved state.
        if (!engine_->available())
            return;
        // Real engine: pull authoritative state (playhead/playing atomics).
        const auto st = engine_->state();
        if (st.playhead != playhead_) {
            playhead_ = st.playhead;
            emit playheadChanged(playhead_);
        }
        if (stopPending_ && !st.playing)
            stopPending_ = false; // RT acknowledged the requested stop
        if (st.playing != playing_ && !stopPending_)
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