#pragma once
// ses::core — decoded PCM arena + waveform peak LODs (DESIGN.md §4.3/§5.2).
// An AudioBuffer is the GUI-side decode result, owned by MediaCache through a
// std::shared_ptr; RenderPlans hold raw const float* pointers into `pcm` (P2).
// Peaks are folded while the file is decoded — one pass, no second scan
// (ROADMAP §2.7) — into a doubling-span level pyramid the timeline blits
// waveforms from at any zoom. Qt6::Core only: pure data, no GUI, no audio I/O.

#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ses::core {

// One waveform LOD level: mono-mix min/max per bucket.
struct PeakLevel {
    int64_t samplesPerBucket = 0;
    QVector<float> min;
    QVector<float> max;
    bool valid() const { return samplesPerBucket > 0 && !min.isEmpty() && min.size() == max.size(); }
};

// Levels double the bucket span (kBaseSpan, 2*kBaseSpan, ...). pickLevel()
// returns the coarsest level whose buckets still fit inside one pixel column;
// the caller folds the few buckets a column spans into its exact min/max.
struct PeakPyramid {
    static constexpr int64_t kBaseSpan = 256;
    QVector<PeakLevel> levels;

    bool valid() const { return !levels.isEmpty() && levels.first().valid(); }
    const PeakLevel& pickLevel(int64_t framesPerPixel) const;
};

// Single-pass folder used by the decode pool: feed frames as they are read.
// Mono-mix semantics match the old computeWavPeaks exactly — the average of
// the channels, non-finite results sanitized to 0, clamped to [-1, 1].
class PeakBuilder {
public:
    explicit PeakBuilder(int64_t baseSpan = PeakPyramid::kBaseSpan)
        : baseSpan_(baseSpan > 0 ? baseSpan : PeakPyramid::kBaseSpan)
    {
    }

    void pushFrame(float left, float right);
    PeakPyramid finish();

private:
    struct Running {
        float mn = 0.0f;
        float mx = 0.0f;
        int64_t n = 0;
    };

    void ensureLevel(int level);
    void foldUp(int level, const Running& bucket);

    static constexpr int kMaxLevels = 32;
    int64_t baseSpan_;
    QVector<Running> running_;
    QVector<QVector<float>> min_;
    QVector<QVector<float>> max_;
};

// The decoded arena: interleaved f32 at the project rate, folded to stereo.
// Stored flat (std::vector) because it is raw PCM storage, not UI state.
struct AudioBuffer {
    std::vector<float> pcm; // interleaved stereo f32 @ sampleRate
    int sampleRate = 0;
    int channels = 2;
    int64_t frames = 0;
    PeakPyramid peaks;
};

} // namespace ses::core