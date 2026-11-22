#include "ui/shell/DeviceRecovery.h"

namespace ses::ui {

DeviceRecovery::DeviceRecovery(ReopenDevice reopen, QObject* parent)
    : DeviceRecovery(std::move(reopen), { 500, 1000, 2000, 4000 }, parent)
{
}

DeviceRecovery::DeviceRecovery(ReopenDevice reopen, QVector<int> backoffMs,
                               QObject* parent)
    : QObject(parent)
    , reopen_(std::move(reopen))
    , backoffMs_(std::move(backoffMs))
{
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &DeviceRecovery::tryOpen);
}

void DeviceRecovery::handleEvent(engine::DeviceEvent event)
{
    using Kind = engine::DeviceEvent::Kind;
    // A requested stop parks the device; it is not a disconnected device. The
    // same notification also covers backend suspension, which only matters
    // while we expect audio to be flowing.
    if (event.kind == Kind::Stopped && playbackActive_ && !playbackActive_())
        return;
    const bool interrupted = event.kind == Kind::Stopped
        || event.kind == Kind::Rerouted
        || event.kind == Kind::InterruptionBegan;
    if (!interrupted || active_)
        return; // Started/InterruptionEnded need no reopen; coalesce repeated loss events.

    active_ = true;
    attempt_ = 0;
    emit this->interrupted();
    tryOpen();
}

void DeviceRecovery::cancel()
{
    timer_.stop();
    active_ = false;
    attempt_ = 0;
}

void DeviceRecovery::tryOpen()
{
    if (!active_)
        return;

    timer_.stop();
    ++attempt_;
    emit attemptStarted(attempt_, maxAttempts());
    if (reopen_ && reopen_()) {
        active_ = false;
        emit recovered();
        return;
    }

    if (attempt_ >= maxAttempts()) {
        active_ = false;
        emit failed();
        return;
    }

    const int delay = backoffMs_.at(attempt_ - 1);
    emit retryScheduled(attempt_ + 1, maxAttempts(), delay);
    timer_.start(delay);
}

} // namespace ses::ui
