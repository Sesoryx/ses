#pragma once
// Fade curve math (DESIGN.md §5.4) — pure functions shared by the RT mixer,
// the offline bounce renderer, and the timeline painter. Golden-tested.

#include <QString>
#include <cmath>

namespace ses::core {

enum class FadeCurveType { Linear, EqualPower, Exp, SCurve };

FadeCurveType fadeCurveFromString(const QString& s);
QString fadeCurveToString(FadeCurveType c);

// p in [0,1] is progress through the fade region.
// Fade-in:  p=0 at clip start (silent) → p=1 full level.
// Fade-out: p=0 at fade start (full)  → p=1 at clip end (silent).
inline double fadeInGain(FadeCurveType c, double p)
{
    p = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    switch (c) {
    case FadeCurveType::Linear: return p;
    case FadeCurveType::EqualPower: return std::sin(p * M_PI / 2.0);
    case FadeCurveType::Exp: return p * p;
    case FadeCurveType::SCurve: return p * p * (3.0 - 2.0 * p);
    }
    return p;
}

inline double fadeOutGain(FadeCurveType c, double p)
{
    p = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    switch (c) {
    case FadeCurveType::Linear: return 1.0 - p;
    case FadeCurveType::EqualPower: return std::cos(p * M_PI / 2.0);
    case FadeCurveType::Exp: return (1.0 - p) * (1.0 - p);
    case FadeCurveType::SCurve: {
        const double q = 1.0 - p;
        return q * q * (3.0 - 2.0 * q);
    }
    }
    return 1.0 - p;
}

} // namespace ses::core