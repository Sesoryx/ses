#include "core/render/FadeCurve.h"

namespace ses::core {

FadeCurveType fadeCurveFromString(const QString& s)
{
    if (s == QLatin1String("equalPower"))
        return FadeCurveType::EqualPower;
    if (s == QLatin1String("exp"))
        return FadeCurveType::Exp;
    if (s == QLatin1String("sCurve"))
        return FadeCurveType::SCurve;
    return FadeCurveType::Linear;
}

QString fadeCurveToString(FadeCurveType c)
{
    switch (c) {
    case FadeCurveType::EqualPower: return QStringLiteral("equalPower");
    case FadeCurveType::Exp: return QStringLiteral("exp");
    case FadeCurveType::SCurve: return QStringLiteral("sCurve");
    case FadeCurveType::Linear: break;
    }
    return QStringLiteral("linear");
}

} // namespace ses::core