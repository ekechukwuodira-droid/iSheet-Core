#include "sheetcore/func_stats/register.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace sheetcore::func_stats {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInvSqrt2Pi = 0.39894228040143267794;
constexpr double kSqrt2 = 1.41421356237309504880;

std::vector<double> collectNumbers(const std::vector<formula::EvalArg>& args,
                                   const formula::EvalContext& ctx,
                                   std::size_t startIdx = 0) {
    std::vector<double> out;
    for (std::size_t i = startIdx; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a.isRange() && ctx.values) {
            for (const auto& v : ctx.values->getRangeValues(a.rangeRef->sheetId, a.rangeRef->range)) {
                if (isNumber(v)) out.push_back(std::get<double>(v));
            }
        } else if (a.isArray()) {
            for (const auto& v : a.array->elements) {
                if (isNumber(v)) out.push_back(std::get<double>(v));
            }
        } else if (auto n = toNumber(a.scalar)) {
            out.push_back(*n);
        }
    }
    return out;
}

double normPdf(double z) { return kInvSqrt2Pi * std::exp(-0.5 * z * z); }
double normCdf(double z) { return 0.5 * std::erfc(-z / kSqrt2); }

// Peter John Acklam's rational approximation for inverse standard normal CDF
double normSInv(double p) {
    static constexpr double a[] = {
        -3.969683028665376e+01,  2.209460984245205e+02, -2.759285104469687e+02,
         1.383577518672690e+02, -3.066479806614716e+01,  2.506628277459239e+00
    };
    static constexpr double b[] = {
        -5.447609879822406e+01,  1.615858368580409e+02, -1.556989798598866e+02,
         6.680131188771972e+01, -1.328068155288572e+01
    };
    static constexpr double c[] = {
        -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
        -2.549732539343734e+00,  4.374664141464968e+00,  2.938163982698783e+00
    };
    static constexpr double d[] = {
         7.784695709041462e-03,  3.224671290700398e-01,  2.445134137142996e+00,
         3.754408661907416e+00
    };
    constexpr double pLow = 0.02425;
    constexpr double pHigh = 1.0 - pLow;
    if (p < pLow) {
        double q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if (p <= pHigh) {
        double q = p - 0.5;
        double r = q * q;
        return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
               (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    }
    double q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
}

CellValue fnVarImpl(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx, bool population) {
    auto nums = collectNumbers(args, ctx);
    std::size_t minN = population ? 1 : 2;
    if (nums.size() < minN) return ErrorValue{ErrorType::DivZero};
    double mean = std::accumulate(nums.begin(), nums.end(), 0.0) / static_cast<double>(nums.size());
    double sq = 0.0;
    for (double x : nums) { double d = x - mean; sq += d * d; }
    return sq / static_cast<double>(population ? nums.size() : (nums.size() - 1));
}

CellValue fnStdevImpl(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx, bool population) {
    CellValue v = fnVarImpl(args, ctx, population);
    return isError(v) ? v : CellValue{std::sqrt(std::get<double>(v))};
}

CellValue fnAveDev(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.empty()) return ErrorValue{ErrorType::Num};
    double mean = std::accumulate(nums.begin(), nums.end(), 0.0) / static_cast<double>(nums.size());
    double acc = 0.0;
    for (double x : nums) acc += std::fabs(x - mean);
    return acc / static_cast<double>(nums.size());
}

CellValue fnDevSq(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.empty()) return ErrorValue{ErrorType::Num};
    double mean = std::accumulate(nums.begin(), nums.end(), 0.0) / static_cast<double>(nums.size());
    double acc = 0.0;
    for (double x : nums) { double d = x - mean; acc += d * d; }
    return acc;
}

CellValue fnGeoMean(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.empty()) return ErrorValue{ErrorType::Num};
    double logSum = 0.0;
    for (double x : nums) {
        if (x <= 0.0) return ErrorValue{ErrorType::Num};
        logSum += std::log(x);
    }
    return std::exp(logSum / static_cast<double>(nums.size()));
}

CellValue fnHarMean(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.empty()) return ErrorValue{ErrorType::Num};
    double invSum = 0.0;
    for (double x : nums) {
        if (x <= 0.0) return ErrorValue{ErrorType::Num};
        invSum += 1.0 / x;
    }
    return static_cast<double>(nums.size()) / invSum;
}

CellValue fnTrimMean(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers({args[0]}, ctx);
    auto pct = toNumber(args[1].scalar);
    if (nums.empty() || !pct || *pct < 0.0 || *pct >= 1.0) return ErrorValue{ErrorType::Num};
    std::sort(nums.begin(), nums.end());
    std::size_t trimEach = static_cast<std::size_t>(std::floor(static_cast<double>(nums.size()) * (*pct) / 2.0));
    double sum = 0.0;
    std::size_t count = nums.size() - 2 * trimEach;
    for (std::size_t i = trimEach; i < nums.size() - trimEach; ++i) sum += nums[i];
    return sum / static_cast<double>(count);
}

CellValue fnSkew(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx, bool pop) {
    auto nums = collectNumbers(args, ctx);
    if (nums.size() < 3) return ErrorValue{ErrorType::DivZero};
    double n = static_cast<double>(nums.size());
    double mean = std::accumulate(nums.begin(), nums.end(), 0.0) / n;
    double m2 = 0.0, m3 = 0.0;
    for (double x : nums) {
        double d = x - mean;
        m2 += d * d;
        m3 += d * d * d;
    }
    if (pop) {
        double s = std::sqrt(m2 / n);
        if (s == 0.0) return ErrorValue{ErrorType::DivZero};
        return (m3 / n) / (s * s * s);
    }
    double s = std::sqrt(m2 / (n - 1.0));
    if (s == 0.0) return ErrorValue{ErrorType::DivZero};
    return (n / ((n - 1.0) * (n - 2.0))) * (m3 / (s * s * s));
}

CellValue fnKurt(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args, ctx);
    if (nums.size() < 4) return ErrorValue{ErrorType::DivZero};
    double n = static_cast<double>(nums.size());
    double mean = std::accumulate(nums.begin(), nums.end(), 0.0) / n;
    double m2 = 0.0, m4 = 0.0;
    for (double x : nums) { double d = x - mean; double d2 = d * d; m2 += d2; m4 += d2 * d2; }
    double s = std::sqrt(m2 / (n - 1.0));
    if (s == 0.0) return ErrorValue{ErrorType::DivZero};
    double term1 = (n * (n + 1.0)) / ((n - 1.0) * (n - 2.0) * (n - 3.0)) * (m4 / (s * s * s * s));
    double term2 = (3.0 * (n - 1.0) * (n - 1.0)) / ((n - 2.0) * (n - 3.0));
    return term1 - term2;
}

CellValue fnCovar(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx, bool pop) {
    auto x = collectNumbers({args[0]}, ctx);
    auto y = collectNumbers({args[1]}, ctx);
    std::size_t minN = pop ? 1 : 2;
    if (x.size() != y.size() || x.size() < minN) return ErrorValue{ErrorType::DivZero};
    double mx = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
    double my = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(y.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) sum += (x[i] - mx) * (y[i] - my);
    return sum / static_cast<double>(pop ? x.size() : (x.size() - 1));
}

struct LinReg {
    bool valid = false;
    double slope = 0.0;
    double intercept = 0.0;
    double r = 0.0;
    double steyx = 0.0;
};

LinReg computeLinReg(const std::vector<double>& y, const std::vector<double>& x) {
    LinReg lr;
    if (x.size() != y.size() || x.size() < 2) return lr;
    double n = static_cast<double>(x.size());
    double mx = std::accumulate(x.begin(), x.end(), 0.0) / n;
    double my = std::accumulate(y.begin(), y.end(), 0.0) / n;
    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        double dx = x[i] - mx, dy = y[i] - my;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
    }
    if (sxx == 0.0) return lr;
    lr.valid = true;
    lr.slope = sxy / sxx;
    lr.intercept = my - lr.slope * mx;
    lr.r = (syy > 0.0) ? (sxy / std::sqrt(sxx * syy)) : 0.0;
    if (x.size() > 2) {
        double sse = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            double err = y[i] - (lr.intercept + lr.slope * x[i]);
            sse += err * err;
        }
        lr.steyx = std::sqrt(sse / (n - 2.0));
    }
    return lr;
}

CellValue fnCountBlank(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[0].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    std::size_t cnt = 0;
    for (const auto& v : ctx.values->getRangeValues(args[0].rangeRef->sheetId, args[0].rangeRef->range)) {
        if (isEmpty(v) || (isText(v) && std::get<std::string>(v).empty())) ++cnt;
    }
    return static_cast<double>(cnt);
}

CellValue fnNormDist(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto x = toNumber(args[0].scalar), mean = toNumber(args[1].scalar), sd = toNumber(args[2].scalar);
    bool cum = toBool(args[3].scalar).value_or(true);
    if (!x || !mean || !sd || *sd <= 0.0) return ErrorValue{ErrorType::Num};
    double z = (*x - *mean) / (*sd);
    return cum ? normCdf(z) : (normPdf(z) / (*sd));
}

CellValue fnNormInv(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto p = toNumber(args[0].scalar), mean = toNumber(args[1].scalar), sd = toNumber(args[2].scalar);
    if (!p || !mean || !sd || *p <= 0.0 || *p >= 1.0 || *sd <= 0.0) return ErrorValue{ErrorType::Num};
    return (*mean) + (*sd) * normSInv(*p);
}

CellValue fnExponDist(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto x = toNumber(args[0].scalar), lam = toNumber(args[1].scalar);
    bool cum = toBool(args[2].scalar).value_or(true);
    if (!x || !lam || *x < 0.0 || *lam <= 0.0) return ErrorValue{ErrorType::Num};
    return cum ? (1.0 - std::exp(-(*lam) * (*x))) : ((*lam) * std::exp(-(*lam) * (*x)));
}

CellValue fnPoissonDist(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto xOpt = toNumber(args[0].scalar), mean = toNumber(args[1].scalar);
    bool cum = toBool(args[2].scalar).value_or(true);
    if (!xOpt || !mean || *xOpt < 0.0 || *mean < 0.0) return ErrorValue{ErrorType::Num};
    int k = static_cast<int>(std::trunc(*xOpt));
    auto pmf = [&](int i) {
        return std::exp(-(*mean) + static_cast<double>(i) * std::log(*mean) - std::lgamma(static_cast<double>(i + 1)));
    };
    if (!cum) return *mean == 0.0 ? (k == 0 ? 1.0 : 0.0) : pmf(k);
    double sum = 0.0;
    for (int i = 0; i <= k; ++i) sum += (*mean == 0.0 ? (i == 0 ? 1.0 : 0.0) : pmf(i));
    return std::min(1.0, sum);
}

CellValue fnBinomDist(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto kOpt = toNumber(args[0].scalar), nOpt = toNumber(args[1].scalar), p = toNumber(args[2].scalar);
    bool cum = toBool(args[3].scalar).value_or(false);
    if (!kOpt || !nOpt || !p || *kOpt < 0 || *nOpt < 0 || *kOpt > *nOpt || *p < 0.0 || *p > 1.0) {
        return ErrorValue{ErrorType::Num};
    }
    int k = static_cast<int>(std::trunc(*kOpt)), n = static_cast<int>(std::trunc(*nOpt));
    auto pmf = [&](int i) {
        if (*p == 0.0) return i == 0 ? 1.0 : 0.0;
        if (*p == 1.0) return i == n ? 1.0 : 0.0;
        double logC = std::lgamma(n + 1.0) - std::lgamma(i + 1.0) - std::lgamma(n - i + 1.0);
        return std::exp(logC + i * std::log(*p) + (n - i) * std::log(1.0 - *p));
    };
    if (!cum) return pmf(k);
    double s = 0.0;
    for (int i = 0; i <= k; ++i) s += pmf(i);
    return std::min(1.0, s);
}

CellValue fnWeibullDist(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto x = toNumber(args[0].scalar), alpha = toNumber(args[1].scalar), beta = toNumber(args[2].scalar);
    bool cum = toBool(args[3].scalar).value_or(true);
    if (!x || !alpha || !beta || *x < 0.0 || *alpha <= 0.0 || *beta <= 0.0) return ErrorValue{ErrorType::Num};
    double ratio = (*x) / (*beta);
    if (cum) return 1.0 - std::exp(-std::pow(ratio, *alpha));
    return ((*alpha) / (*beta)) * std::pow(ratio, *alpha - 1.0) * std::exp(-std::pow(ratio, *alpha));
}

CellValue fnConfidenceNorm(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto alpha = toNumber(args[0].scalar), sd = toNumber(args[1].scalar), sz = toNumber(args[2].scalar);
    if (!alpha || !sd || !sz || *alpha <= 0.0 || *alpha >= 1.0 || *sd <= 0.0 || *sz < 1.0) {
        return ErrorValue{ErrorType::Num};
    }
    double z = normSInv(1.0 - (*alpha) / 2.0);
    return z * (*sd) / std::sqrt(std::trunc(*sz));
}

} // namespace

void registerStatsDistExtFunctions(formula::FunctionRegistry& registry);
void registerStatsDescRegExtFunctions(formula::FunctionRegistry& registry);

void registerStatsFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"STDEV.S", 1, 255, [](const auto& a, const auto& c) { return fnStdevImpl(a, c, false); }});
    registry.registerFunction({"STDEV.P", 1, 255, [](const auto& a, const auto& c) { return fnStdevImpl(a, c, true); }});
    registry.registerFunction({"STDEVP", 1, 255, [](const auto& a, const auto& c) { return fnStdevImpl(a, c, true); }});
    registry.registerFunction({"VAR.S", 1, 255, [](const auto& a, const auto& c) { return fnVarImpl(a, c, false); }});
    registry.registerFunction({"VAR.P", 1, 255, [](const auto& a, const auto& c) { return fnVarImpl(a, c, true); }});
    registry.registerFunction({"VARP", 1, 255, [](const auto& a, const auto& c) { return fnVarImpl(a, c, true); }});
    registry.registerFunction({"AVEDEV", 1, 255, fnAveDev});
    registry.registerFunction({"DEVSQ", 1, 255, fnDevSq});
    registry.registerFunction({"GEOMEAN", 1, 255, fnGeoMean});
    registry.registerFunction({"HARMEAN", 1, 255, fnHarMean});
    registry.registerFunction({"TRIMMEAN", 2, 2, fnTrimMean});
    registry.registerFunction({"SKEW", 1, 255, [](const auto& a, const auto& c) { return fnSkew(a, c, false); }});
    registry.registerFunction({"SKEW.P", 1, 255, [](const auto& a, const auto& c) { return fnSkew(a, c, true); }});
    registry.registerFunction({"KURT", 1, 255, fnKurt});
    registry.registerFunction({"COUNTBLANK", 1, 1, fnCountBlank});

    registry.registerFunction({"STANDARDIZE", 3, 3, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar), m = toNumber(a[1].scalar), s = toNumber(a[2].scalar);
        if (!x || !m || !s || *s <= 0.0) return ErrorValue{ErrorType::Num};
        return (*x - *m) / (*s);
    }});
    registry.registerFunction({"FISHER", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar);
        if (!x || *x <= -1.0 || *x >= 1.0) return ErrorValue{ErrorType::Num};
        return 0.5 * std::log((1.0 + *x) / (1.0 - *x));
    }});
    registry.registerFunction({"FISHERINV", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto y = toNumber(a[0].scalar);
        if (!y) return ErrorValue{ErrorType::Value};
        double e2y = std::exp(2.0 * (*y));
        return (e2y - 1.0) / (e2y + 1.0);
    }});

    registry.registerFunction({"COVARIANCE.P", 2, 2, [](const auto& a, const auto& c) { return fnCovar(a, c, true); }});
    registry.registerFunction({"COVAR", 2, 2, [](const auto& a, const auto& c) { return fnCovar(a, c, true); }});
    registry.registerFunction({"COVARIANCE.S", 2, 2, [](const auto& a, const auto& c) { return fnCovar(a, c, false); }});

    registry.registerFunction({"SLOPE", 2, 2, [](const auto& a, const auto& c) -> CellValue {
        auto lr = computeLinReg(collectNumbers({a[0]}, c), collectNumbers({a[1]}, c));
        return lr.valid ? CellValue{lr.slope} : CellValue{ErrorValue{ErrorType::DivZero}};
    }});
    registry.registerFunction({"INTERCEPT", 2, 2, [](const auto& a, const auto& c) -> CellValue {
        auto lr = computeLinReg(collectNumbers({a[0]}, c), collectNumbers({a[1]}, c));
        return lr.valid ? CellValue{lr.intercept} : CellValue{ErrorValue{ErrorType::DivZero}};
    }});
    registry.registerFunction({"PEARSON", 2, 2, [](const auto& a, const auto& c) -> CellValue {
        auto lr = computeLinReg(collectNumbers({a[0]}, c), collectNumbers({a[1]}, c));
        return lr.valid ? CellValue{lr.r} : CellValue{ErrorValue{ErrorType::DivZero}};
    }});
    registry.registerFunction({"RSQ", 2, 2, [](const auto& a, const auto& c) -> CellValue {
        auto lr = computeLinReg(collectNumbers({a[0]}, c), collectNumbers({a[1]}, c));
        return lr.valid ? CellValue{lr.r * lr.r} : CellValue{ErrorValue{ErrorType::DivZero}};
    }});
    registry.registerFunction({"STEYX", 2, 2, [](const auto& a, const auto& c) -> CellValue {
        auto lr = computeLinReg(collectNumbers({a[0]}, c), collectNumbers({a[1]}, c));
        return lr.valid ? CellValue{lr.steyx} : CellValue{ErrorValue{ErrorType::DivZero}};
    }});
    auto forecastFn = [](const std::vector<formula::EvalArg>& a, const formula::EvalContext& c) -> CellValue {
        auto xVal = toNumber(a[0].scalar);
        if (!xVal) return ErrorValue{ErrorType::Value};
        auto lr = computeLinReg(collectNumbers({a[1]}, c), collectNumbers({a[2]}, c));
        return lr.valid ? CellValue{lr.intercept + lr.slope * (*xVal)} : CellValue{ErrorValue{ErrorType::DivZero}};
    };
    registry.registerFunction({"FORECAST", 3, 3, forecastFn});
    registry.registerFunction({"FORECAST.LINEAR", 3, 3, forecastFn});

    registry.registerFunction({"NORM.DIST", 4, 4, fnNormDist});
    registry.registerFunction({"NORMDIST", 4, 4, fnNormDist});
    registry.registerFunction({"NORM.S.DIST", 1, 2, [](const auto& a, const auto&) -> CellValue {
        auto z = toNumber(a[0].scalar);
        if (!z) return ErrorValue{ErrorType::Value};
        bool cum = a.size() >= 2 ? toBool(a[1].scalar).value_or(true) : true;
        return cum ? normCdf(*z) : normPdf(*z);
    }});
    registry.registerFunction({"NORMSDIST", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto z = toNumber(a[0].scalar);
        return z ? CellValue{normCdf(*z)} : CellValue{ErrorValue{ErrorType::Value}};
    }});
    registry.registerFunction({"NORM.INV", 3, 3, fnNormInv});
    registry.registerFunction({"NORMINV", 3, 3, fnNormInv});
    registry.registerFunction({"NORM.S.INV", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto p = toNumber(a[0].scalar);
        if (!p || *p <= 0.0 || *p >= 1.0) return ErrorValue{ErrorType::Num};
        return normSInv(*p);
    }});
    registry.registerFunction({"NORMSINV", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto p = toNumber(a[0].scalar);
        if (!p || *p <= 0.0 || *p >= 1.0) return ErrorValue{ErrorType::Num};
        return normSInv(*p);
    }});
    registry.registerFunction({"PHI", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto z = toNumber(a[0].scalar);
        return z ? CellValue{normPdf(*z)} : CellValue{ErrorValue{ErrorType::Value}};
    }});
    registry.registerFunction({"GAUSS", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto z = toNumber(a[0].scalar);
        return z ? CellValue{normCdf(*z) - 0.5} : CellValue{ErrorValue{ErrorType::Value}};
    }});
    registry.registerFunction({"EXPON.DIST", 3, 3, fnExponDist});
    registry.registerFunction({"EXPONDIST", 3, 3, fnExponDist});
    registry.registerFunction({"POISSON.DIST", 3, 3, fnPoissonDist});
    registry.registerFunction({"POISSON", 3, 3, fnPoissonDist});
    registry.registerFunction({"BINOM.DIST", 4, 4, fnBinomDist});
    registry.registerFunction({"BINOMDIST", 4, 4, fnBinomDist});
    registry.registerFunction({"WEIBULL.DIST", 4, 4, fnWeibullDist});
    registry.registerFunction({"GAMMA", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar);
        if (!x || (*x <= 0.0 && std::floor(*x) == *x)) return ErrorValue{ErrorType::Num};
        return std::tgamma(*x);
    }});
    registry.registerFunction({"GAMMALN", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar);
        if (!x || *x <= 0.0) return ErrorValue{ErrorType::Num};
        return std::lgamma(*x);
    }});
    registry.registerFunction({"GAMMALN.PRECISE", 1, 1, [](const auto& a, const auto&) -> CellValue {
        auto x = toNumber(a[0].scalar);
        if (!x || *x <= 0.0) return ErrorValue{ErrorType::Num};
        return std::lgamma(*x);
    }});
    registry.registerFunction({"CONFIDENCE.NORM", 3, 3, fnConfidenceNorm});
    registry.registerFunction({"CONFIDENCE", 3, 3, fnConfidenceNorm});
    registerStatsDistExtFunctions(registry);
    registerStatsDescRegExtFunctions(registry);
}

} // namespace sheetcore::func_stats
