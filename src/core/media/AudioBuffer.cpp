#include "core/media/AudioBuffer.h"

namespace ses::core {

const PeakLevel& PeakPyramid::pickLevel(int64_t framesPerPixel) const
{
    static const PeakLevel empty{};
    if (levels.isEmpty())
        return empty;
    // Coarsest level whose bucket span still fits inside one pixel column.
    int i = 0;
    while (i + 1 < levels.size() && levels[i + 1].samplesPerBucket <= framesPerPixel)
        ++i;
    return levels[i];
}

void PeakBuilder::ensureLevel(int level)
{
    while (running_.size() <= level) {
        running_.append(Running{});
        min_.append(QVector<float>{});
        max_.append(QVector<float>{});
    }
}

void PeakBuilder::foldUp(int level, const Running& bucket)
{
    ensureLevel(level + 1);
    Running& up = running_[level + 1];
    if (up.n == 0) {
        up.mn = bucket.mn;
        up.mx = bucket.mx;
    } else {
        up.mn = std::min(up.mn, bucket.mn);
        up.mx = std::max(up.mx, bucket.mx);
    }
    ++up.n;
}

void PeakBuilder::pushFrame(float left, float right)
{
    float v = (left + right) * 0.5f;
    if (!std::isfinite(v))
        v = 0.0f;
    v = std::clamp(v, -1.0f, 1.0f);

    ensureLevel(0);
    Running& l0 = running_[0];
    if (l0.n == 0) {
        l0.mn = v;
        l0.mx = v;
    } else {
        l0.mn = std::min(l0.mn, v);
        l0.mx = std::max(l0.mx, v);
    }
    ++l0.n;

    // Commit full buckets and propagate them up the pyramid (each level holds
    // two of the level below; level 0 holds baseSpan frames).
    for (int k = 0; k < kMaxLevels; ++k) {
        const int64_t full = k == 0 ? baseSpan_ : 2;
        if (running_[k].n < full)
            break;
        const Running done = running_[k];
        running_[k] = Running{};
        min_[k].append(done.mn);
        max_[k].append(done.mx);
        if (k + 1 >= kMaxLevels)
            break;
        foldUp(k, done);
    }
}

PeakPyramid PeakBuilder::finish()
{
    // Flush partial buckets upward so every level covers the whole signal —
    // the tail of the file must not vanish at coarse LODs.
    for (int k = 0; k < running_.size(); ++k) {
        if (running_[k].n == 0)
            continue;
        const Running done = running_[k];
        running_[k] = Running{};
        min_[k].append(done.mn);
        max_[k].append(done.mx);
        // One bucket at this level already spans the whole signal: nothing
        // above it can add resolution.
        if (min_[k].size() == 1)
            break;
        if (k + 1 < kMaxLevels)
            foldUp(k, done);
    }

    PeakPyramid out;
    for (int k = 0; k < min_.size() && !min_[k].isEmpty(); ++k) {
        PeakLevel level;
        level.samplesPerBucket = baseSpan_ << k;
        level.min = min_[k];
        level.max = max_[k];
        out.levels.append(level);
    }
    return out;
}

} // namespace ses::core