#pragma once
// Minimal WAV probe + peak LODs for the UI-first slice. The full decode pool
// (miniaudio/libsndfile, all formats) lands with the engine in P1; until then
// only RIFF/WAVE PCM + float files import, and other formats are reported
// honestly instead of guessed. Qt6::Core only — no GUI, no audio I/O.

#include <QString>
#include <QVector>
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

struct Peaks {
    int64_t samplesPerBucket = 0;
    QVector<float> min;
    QVector<float> max;
    bool valid() const { return !min.isEmpty() && min.size() == max.size(); }
};

// Mono-mixed min/max per bucket over the whole file. buckets is a hint; the
// actual bucket count may differ slightly. Returns invalid Peaks on failure.
Peaks computeWavPeaks(const QString& path, int buckets);

// Writes a small sine-sweep WAV fixture (PCM16). Used by --smoke and tests.
bool writeTestWav(const QString& path, double seconds, int sampleRate = 48000, int channels = 2);

} // namespace ses::core