#pragma once
// Mixer — collapsible bottom strip panel (§10.1/§10.11). Gain fader, pan,
// M/S, A/B routing chip per track + master strip. The master meter is fed by
// the 30 Hz engine telemetry drain (P1 task 4); per-track meters stay honestly
// idle until per-track telemetry exists (P4).

#include "core/model/ProjectModel.h"

#include <QHash>
#include <QTimer>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;
class QHBoxLayout;

namespace ses::ui {

// Vertical peak meter. Honest by construction: with metering disabled it draws
// an idle groove and ignores peaks; when telemetry stops the bar decays on its
// own timer, so a frozen "live" meter is impossible.
class LevelMeter : public QWidget {
public:
    explicit LevelMeter(QWidget* parent = nullptr);

    void setMeteringEnabled(bool on);
    bool meteringEnabled() const { return metering_; }

    void setPeak(float linearPeak); // GUI thread, from engine telemetry
    double level() const { return level_; } // displayed level (>1 = over full scale)
    double peakHold() const { return hold_; }
    bool clip() const { return clip_; }

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    void decayTick();

    QTimer decay_;
    double level_ = 0.0;
    double hold_ = 0.0;
    int holdAge_ = 0;
    bool metering_ = false;
    bool clip_ = false;
};

class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(core::ProjectModel* model, QWidget* parent = nullptr);

    LevelMeter* masterMeter() const { return masterMeter_; }

public slots:
    void setMasterPeak(float linearPeak); // engine telemetry (30 Hz, via Transport)
    void setMeteringEnabled(bool on);     // real engine attached → live meters

private:
    struct Strip {
        QLabel* name = nullptr;
        QLabel* db = nullptr;
        QSlider* gain = nullptr;
        QSlider* pan = nullptr;
        QToolButton* mute = nullptr;
        QToolButton* solo = nullptr;
        QToolButton* chip = nullptr;
    };
    void rebuild();
    void refresh();
    QWidget* makeStrip(const core::Track& t);
    QWidget* makeMasterStrip();

    core::ProjectModel* model_;
    QWidget* container_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
    QHash<QString, Strip> strips_;
    LevelMeter* masterMeter_ = nullptr;
};

} // namespace ses::ui