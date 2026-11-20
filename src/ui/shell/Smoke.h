#pragma once
// Shared self-check used by `ses --smoke` and tst_ui. Exercises the real UI
// offscreen: import → split → undo, theme toggle timing, A/B fader commands,
// settings persistence. Returns honest pass/fail per step.

#include <QString>

namespace ses::ui {

class MainWindow;

struct SmokeResult {
    bool ok = false;
    QString detail;
};

SmokeResult runSmoke(MainWindow& w);

} // namespace ses::ui