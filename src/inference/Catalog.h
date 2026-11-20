#pragma once
// ses::inference — model catalog data (DESIGN.md §7.6, verified sizes/licenses).
// UI-first slice: pure catalog data, no audio.cpp linkage yet (P5). The UI
// renders honest install/state cards from this; nothing here runs inference.

#include <QString>
#include <QVector>

namespace ses::inference {

enum class Capability { Vad, Asr, Tts, Enhance, SuperRes, VoiceConversion, Music };

struct CatalogEntry {
    Capability capability;
    QString name;        // human label shown in the UI
    QString packageId;   // audio.cpp package id
    QString params;      // verified parameter count, as published
    QString license;     // weights license
    QString sizeNote;    // honest size framing ("≈6 MB f32", "small (enhancer-only)")
    QString note;        // caveats: language limits, conditional licenses, opt-in
    bool bundled = false;
    bool optIn = false;  // heavier-than-million-scale exceptions (§7.6.1)
};

// The default light catalog + opt-in heavier alternates, per DESIGN.md §7.6.
const QVector<CatalogEntry>& catalog();

QString capabilityName(Capability c);

} // namespace ses::inference