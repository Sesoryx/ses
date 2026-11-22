#pragma once
// Minimal WAV probe + test fixtures for the UI-first slice. Real decoding
// (all formats) lives in the engine decode pool (engine/io/Decode, P1 task 5);
// probeWav stays as the cheap import pre-check for RIFF/WAVE PCM + float files
// and reports other cases honestly instead of guessing. Waveform peak LODs
// are computed during decode (core/media/AudioBuffer) — this file no longer
// computes peaks. Qt6::Core only — no GUI, no audio I/O.

#include <QString>
#include <cstdint>

namespace ses::core {

struct MediaInfo {
    bool ok = false;
    QString format; // "wav"
    int sampleRate = 0;
    int channels = 0;
    int64_t frames = 0;
    int bitsPerSample = 0;
    bool isFloat = false;
    QString error;

    double seconds() const { return sampleRate > 0 ? double(frames) / sampleRate : 0.0; }
};

MediaInfo probeWav(const QString& path);

// Writes a small sine-sweep WAV fixture (PCM16). Used by --smoke and tests.
bool writeTestWav(const QString& path, double seconds, int sampleRate = 48000, int channels = 2);

} // namespace ses::core