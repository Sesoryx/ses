#pragma once
// Mixer — collapsible bottom strip panel (§10.1/§10.11). Gain fader, pan,
// M/S, A/B routing chip per track + master strip. Meters stay honestly idle
// until the engine delivers telemetry (P1).

#include "core/model/ProjectModel.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;
class QHBoxLayout;

namespace ses::ui {

class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(core::ProjectModel* model, QWidget* parent = nullptr);

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
};

} // namespace ses::ui