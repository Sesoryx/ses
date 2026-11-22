#pragma once
// DeviceRecovery — GUI-side device-loss retry policy (ROADMAP §2.6).
// miniaudio notifications arrive as POD events through Transport; all device
// close/open calls and all user feedback stay on the GUI thread.

#include "engine/io/DeviceTypes.h"

#include <QObject>
#include <QTimer>
#include <QVector>

#include <functional>

namespace ses::ui {

class DeviceRecovery : public QObject {
    Q_OBJECT
public:
    using ReopenDevice = std::function<bool()>;

    // Five attempts: immediate, then 0.5 s, 1 s, 2 s and 4 s backoff.
    explicit DeviceRecovery(ReopenDevice reopen, QObject* parent = nullptr);
    DeviceRecovery(ReopenDevice reopen, QVector<int> backoffMs,
                   QObject* parent = nullptr);

    // miniaudio emits Stopped for an ordinary transport stop too. Only treat
    // it as a loss while playback is expected; Rerouted and interruption
    // events always indicate a real device change.
    void setPlaybackActive(std::function<bool()> probe) { playbackActive_ = std::move(probe); }

    bool active() const { return active_; }
    int attempt() const { return attempt_; }
    int maxAttempts() const { return int(backoffMs_.size()) + 1; } // immediate + delays

    // Ordinary member functions (not MOC slots): DeviceEvent's Qt metatype
    // declaration stays in Transport.h, and engine/io remains Qt-free.
    void handleEvent(ses::engine::DeviceEvent event);
    void cancel();

signals:
    void interrupted();
    void attemptStarted(int attempt, int maxAttempts);
    void retryScheduled(int nextAttempt, int maxAttempts, int delayMs);
    void recovered();
    void failed();

private:
    void tryOpen();

    ReopenDevice reopen_;
    std::function<bool()> playbackActive_;
    QVector<int> backoffMs_;
    QTimer timer_;
    bool active_ = false;
    int attempt_ = 0;
};

} // namespace ses::ui
