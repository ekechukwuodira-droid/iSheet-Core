#include "sheetcore/formula/array_utils.hpp"
#include "sheetcore/formula/functions.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace sheetcore::func_stats {

namespace {

constexpr double kPi = 3.14159265358979323846;

double normPdf(double x) {
    return std::exp(-0.5 * x * x) / std::sqrt(2.0 * kPi);
}

double normCdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

double normInv(double p) {
    // Acklam's rational approximation + 2 Halley refinement steps
    static constexpr double a[] = {
        -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
        1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00
    };
    static constexpr double b[] = {
        -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
        6.680131188771972e+01, -1.328068155288572e+01
    };
    static constexpr double c[] = {
        -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
        -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00
    };
    static constexpr double d[] = {
        7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
        3.754408661907416e+00
    };
    double x = 0.0;
    if (p < 0.02425) {
        double q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else if (p > 1.0 - 0.02425) {
        double q = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else {
        double q = p - 0.5;
        double r = q * q;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    }
    for (int i = 0; i < 2; ++i) {
        double err = normCdf(x) - p;
        double pdf = normPdf(x);
        if (pdf > 0.0) x -= err / pdf;
    }
    return x;
}

double logChoose(double n, double k) {
    return std::lgamma(n + 1.0) - std::lgamma(k + 1.0) - std::lgamma(n - k + 1.0);
}

// Regularized lower incomplete gamma P(a, x)
double gammaincP(double a, double x) {
    if (x <= 0.0) return 0.0;
    if (x < a + 1.0) {
        double ap = a;
        double sum = 1.0 / a;
        double del = sum;
        for (int n = 1; n <= 300; ++n) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * 1e-14) break;
        }
        return std::clamp(sum * std::exp(-x + a * std::log(x) - std::lgamma(a)), 0.0, 1.0);
    }
    // Continued fraction for Q(a, x) = 1 - P(a, x)
    double b = x + 1.0 - a;
    double c = 1.0 / 1e-30;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i <= 300; ++i) {
        double an = -static_cast<double>(i) * (static_cast<double>(i) - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < 1e-30) d = 1e-30;
        c = b + an / c;
        if (std::fabs(c) < 1e-30) c = 1e-30;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-14) break;
    }
    double q = std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
    return std::clamp(1.0 - q, 0.0, 1.0);
}

double gammaincQ(double a, double x) {
    return std::clamp(1.0 - gammaincP(a, x), 0.0, 1.0);
}

// Regularized incomplete beta I_x(a, b) via Lentz's continued fraction
double betacf(double a, double b, double x) {
    constexpr double kEps = 1e-14;
    constexpr double kFpMin = 1e-30;
    double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < kFpMin) d = kFpMin;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= 300; ++m) {
        int m2 = 2 * m;
        double aa = static_cast<double>(m) * (b - static_cast<double>(m)) * x /
                    ((qam + static_cast<double>(m2)) * (a + static_cast<double>(m2)));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + static_cast<double>(m)) * (qab + static_cast<double>(m)) * x /
             ((a + static_cast<double>(m2)) * (qap + static_cast<double>(m2)));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < kEps) break;
    }
    return h;
}

double betainc(double a, double b, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double bt = std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
                         a * std::log(x) + b * std::log(1.0 - x));
    if (x < (a + 1.0) / (a + b + 2.0)) {
        return std::clamp(bt * betacf(a, b, x) / a, 0.0, 1.0);
    }
    return std::clamp(1.0 - bt * betacf(b, a, 1.0 - x) / b, 0.0, 1.0);
}

// Generic strictly increasing CDF inverter on [lo, hi] (with optional upper expansion if unbounded)
template <typename CdfFn>
double invertIncreasingCdf(CdfFn&& cdf, double targetP, double lo, double hi, bool expandHi, bool expandLo = false) {
    if (expandHi) {
        while (cdf(hi) < targetP && hi < 1e100) hi *= 2.0;
    }
    if (expandLo) {
        while (cdf(lo) > targetP && lo > -1e100) lo *= 2.0;
    }
    for (int i = 0; i < 100; ++i) {
        double mid = 0.5 * (lo + hi);
        if (mid == lo || mid == hi) break;
        if (cdf(mid) < targetP) lo = mid;
        else hi = mid;
        if (std::fabs(hi - lo) <= 1e-12 * std::max(1.0, std::fabs(mid))) break;
    }
    return 0.5 * (lo + hi);
}

std::vector<double> collectNumericList(const formula::EvalArg& arg, const formula::EvalContext& ctx) {
    std::vector<double> out;
    for (const auto& v : formula::flatten(arg, ctx)) {
        if (isNumber(v)) out.push_back(std::get<double>(v));
    }
    return out;
}

// ---- BETA.DIST / BETADIST / BETA.INV / BETAINV ----
CellValue fnBetaDistModern(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), alpha = toNumber(a[1].scalar), beta = toNumber(a[2].scalar);
    auto cum = toBool(a[3].scalar);
    double A = (a.size() >= 5 && !a[4].isOmitted()) ? toNumber(a[4].scalar).value_or(std::nan("")) : 0.0;
    double B = (a.size() >= 6 && !a[5].isOmitted()) ? toNumber(a[5].scalar).value_or(std::nan("")) : 1.0;
    if (!x || !alpha || !beta || !cum || std::isnan(A) || std::isnan(B)) return ErrorValue{ErrorType::Value};
    if (*alpha <= 0 || *beta <= 0 || *x < A || *x > B || A >= B) return ErrorValue{ErrorType::Num};
    double t = (*x - A) / (B - A);
    if (*cum) return betainc(*alpha, *beta, t);
    if (t == 0.0 || t == 1.0) return 0.0;
    double logPdf = (*alpha - 1.0) * std::log(t) + (*beta - 1.0) * std::log(1.0 - t) -
                    (std::lgamma(*alpha) + std::lgamma(*beta) - std::lgamma(*alpha + *beta)) - std::log(B - A);
    return std::exp(logPdf);
}

CellValue fnBetaDistLegacy(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), alpha = toNumber(a[1].scalar), beta = toNumber(a[2].scalar);
    double A = (a.size() >= 4 && !a[3].isOmitted()) ? toNumber(a[3].scalar).value_or(std::nan("")) : 0.0;
    double B = (a.size() >= 5 && !a[4].isOmitted()) ? toNumber(a[4].scalar).value_or(std::nan("")) : 1.0;
    if (!x || !alpha || !beta || std::isnan(A) || std::isnan(B)) return ErrorValue{ErrorType::Value};
    if (*alpha <= 0 || *beta <= 0 || *x < A || *x > B || A >= B) return ErrorValue{ErrorType::Num};
    return betainc(*alpha, *beta, (*x - A) / (B - A));
}

CellValue fnBetaInv(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto p = toNumber(a[0].scalar), alpha = toNumber(a[1].scalar), beta = toNumber(a[2].scalar);
    double A = (a.size() >= 4 && !a[3].isOmitted()) ? toNumber(a[3].scalar).value_or(std::nan("")) : 0.0;
    double B = (a.size() >= 5 && !a[4].isOmitted()) ? toNumber(a[4].scalar).value_or(std::nan("")) : 1.0;
    if (!p || !alpha || !beta || std::isnan(A) || std::isnan(B)) return ErrorValue{ErrorType::Value};
    if (*alpha <= 0 || *beta <= 0 || *p <= 0 || *p >= 1 || A >= B) return ErrorValue{ErrorType::Num};
    double t = invertIncreasingCdf([&](double u) { return betainc(*alpha, *beta, u); }, *p, 0.0, 1.0, false);
    return A + t * (B - A);
}

// ---- BINOM.DIST.RANGE / BINOM.INV / CRITBINOM / HYPGEOM / NEGBINOM ----
double binomPmf(int k, int n, double p) {
    if (k < 0 || k > n) return 0.0;
    if (p == 0.0) return k == 0 ? 1.0 : 0.0;
    if (p == 1.0) return k == n ? 1.0 : 0.0;
    return std::exp(logChoose(n, k) + k * std::log(p) + (n - k) * std::log(1.0 - p));
}

CellValue fnBinomDistRange(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto nD = toNumber(a[0].scalar), pD = toNumber(a[1].scalar), s1D = toNumber(a[2].scalar);
    if (!nD || !pD || !s1D) return ErrorValue{ErrorType::Value};
    int n = static_cast<int>(std::trunc(*nD));
    int s1 = static_cast<int>(std::trunc(*s1D));
    int s2 = (a.size() >= 4 && !a[3].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[3].scalar).value_or(-1.0))) : s1;
    if (n < 0 || *pD < 0.0 || *pD > 1.0 || s1 < 0 || s1 > n || s2 < s1 || s2 > n) return ErrorValue{ErrorType::Num};
    double sum = 0.0;
    for (int k = s1; k <= s2; ++k) sum += binomPmf(k, n, *pD);
    return std::clamp(sum, 0.0, 1.0);
}

CellValue fnBinomInv(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto nD = toNumber(a[0].scalar), pD = toNumber(a[1].scalar), alpha = toNumber(a[2].scalar);
    if (!nD || !pD || !alpha) return ErrorValue{ErrorType::Value};
    int n = static_cast<int>(std::trunc(*nD));
    if (n < 0 || *pD < 0.0 || *pD > 1.0 || *alpha <= 0.0 || *alpha >= 1.0) return ErrorValue{ErrorType::Num};
    double cdf = 0.0;
    for (int k = 0; k <= n; ++k) {
        cdf += binomPmf(k, n, *pD);
        if (cdf >= *alpha) return static_cast<double>(k);
    }
    return static_cast<double>(n);
}

double hypgeomPmf(int k, int n, int M, int N) {
    if (k < std::max(0, n + M - N) || k > std::min(n, M)) return 0.0;
    return std::exp(logChoose(M, k) + logChoose(N - M, n - k) - logChoose(N, n));
}

CellValue fnHypgeomDist(const std::vector<formula::EvalArg>& a, const formula::EvalContext&, bool hasCumArg) {
    auto sD = toNumber(a[0].scalar), nsD = toNumber(a[1].scalar),
         psD = toNumber(a[2].scalar), npD = toNumber(a[3].scalar);
    bool cum = hasCumArg ? toBool(a[4].scalar).value_or(false) : false;
    if (!sD || !nsD || !psD || !npD) return ErrorValue{ErrorType::Value};
    int k = static_cast<int>(std::trunc(*sD));
    int n = static_cast<int>(std::trunc(*nsD));
    int M = static_cast<int>(std::trunc(*psD));
    int N = static_cast<int>(std::trunc(*npD));
    if (k < 0 || n <= 0 || M <= 0 || N <= 0 || n > N || M > N || k > n || k > M) return ErrorValue{ErrorType::Num};
    if (!cum) return hypgeomPmf(k, n, M, N);
    double sum = 0.0;
    for (int i = std::max(0, n + M - N); i <= k; ++i) sum += hypgeomPmf(i, n, M, N);
    return std::clamp(sum, 0.0, 1.0);
}

double negbinomPmf(int f, int s, double p) {
    return std::exp(logChoose(f + s - 1, s - 1) + s * std::log(p) + f * std::log(1.0 - p));
}

CellValue fnNegBinomDist(const std::vector<formula::EvalArg>& a, const formula::EvalContext&, bool hasCumArg) {
    auto fD = toNumber(a[0].scalar), sD = toNumber(a[1].scalar), pD = toNumber(a[2].scalar);
    bool cum = hasCumArg ? toBool(a[3].scalar).value_or(false) : false;
    if (!fD || !sD || !pD) return ErrorValue{ErrorType::Value};
    int f = static_cast<int>(std::trunc(*fD));
    int s = static_cast<int>(std::trunc(*sD));
    if (f < 0 || s < 1 || *pD <= 0.0 || *pD >= 1.0) return ErrorValue{ErrorType::Num};
    if (!cum) return negbinomPmf(f, s, *pD);
    double sum = 0.0;
    for (int i = 0; i <= f; ++i) sum += negbinomPmf(i, s, *pD);
    return std::clamp(sum, 0.0, 1.0);
}

// ---- CHISQ.DIST / CHISQ.DIST.RT / CHIDIST / CHISQ.INV / CHISQ.INV.RT / CHIINV / CHISQ.TEST / CHITEST ----
double chisqCdf(double x, double df) { return gammaincP(0.5 * df, 0.5 * x); }
double chisqPdf(double x, double df) {
    if (x <= 0.0) return 0.0;
    double k = 0.5 * df;
    return std::exp((k - 1.0) * std::log(x) - 0.5 * x - k * std::log(2.0) - std::lgamma(k));
}

CellValue fnChisqDist(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    auto cum = toBool(a[2].scalar);
    if (!x || !df || !cum) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (*x < 0.0 || d < 1.0) return ErrorValue{ErrorType::Num};
    return *cum ? chisqCdf(*x, d) : chisqPdf(*x, d);
}

CellValue fnChisqDistRt(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    if (!x || !df) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (*x < 0.0 || d < 1.0) return ErrorValue{ErrorType::Num};
    return gammaincQ(0.5 * d, 0.5 * (*x));
}

CellValue fnChisqInv(const std::vector<formula::EvalArg>& a, const formula::EvalContext&, bool rightTail) {
    auto p = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    if (!p || !df) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (*p <= 0.0 || *p >= 1.0 || d < 1.0) return ErrorValue{ErrorType::Num};
    double target = rightTail ? (1.0 - *p) : *p;
    return invertIncreasingCdf([&](double x) { return chisqCdf(x, d); }, target, 0.0, std::max(2.0 * d, 10.0), true);
}

CellValue fnChisqTest(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    formula::ArrayValue act = formula::toArray(a[0], ctx);
    formula::ArrayValue exp = formula::toArray(a[1], ctx);
    if (act.rows != exp.rows || act.cols != exp.cols || act.elements.empty()) return ErrorValue{ErrorType::NA};
    double stat = 0.0;
    for (std::size_t i = 0; i < act.elements.size(); ++i) {
        auto av = toNumber(act.elements[i]), ev = toNumber(exp.elements[i]);
        if (!av || !ev) continue;
        if (*ev == 0.0) return ErrorValue{ErrorType::DivZero};
        double diff = *av - *ev;
        stat += (diff * diff) / *ev;
    }
    std::size_t r = act.rows, c = act.cols;
    double df = (r > 1 && c > 1) ? static_cast<double>((r - 1) * (c - 1))
                                 : static_cast<double>(std::max(r, c) - 1);
    if (df < 1.0) return ErrorValue{ErrorType::NA};
    return gammaincQ(0.5 * df, 0.5 * stat);
}

// ---- F.DIST / F.DIST.RT / FDIST / F.INV / F.INV.RT / FINV / F.TEST / FTEST ----
double fCdf(double x, double d1, double d2) {
    if (x <= 0.0) return 0.0;
    return betainc(0.5 * d1, 0.5 * d2, (d1 * x) / (d1 * x + d2));
}

double fPdf(double x, double d1, double d2) {
    if (x <= 0.0) return 0.0;
    double logNum = 0.5 * d1 * std::log(d1) + 0.5 * d2 * std::log(d2) + (0.5 * d1 - 1.0) * std::log(x);
    double logDen = 0.5 * (d1 + d2) * std::log(d1 * x + d2) +
                    std::lgamma(0.5 * d1) + std::lgamma(0.5 * d2) - std::lgamma(0.5 * (d1 + d2));
    return std::exp(logNum - logDen);
}

CellValue fnFDist(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), d1 = toNumber(a[1].scalar), d2 = toNumber(a[2].scalar);
    auto cum = toBool(a[3].scalar);
    if (!x || !d1 || !d2 || !cum) return ErrorValue{ErrorType::Value};
    double df1 = std::trunc(*d1), df2 = std::trunc(*d2);
    if (*x < 0.0 || df1 < 1.0 || df2 < 1.0) return ErrorValue{ErrorType::Num};
    return *cum ? fCdf(*x, df1, df2) : fPdf(*x, df1, df2);
}

CellValue fnFDistRt(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), d1 = toNumber(a[1].scalar), d2 = toNumber(a[2].scalar);
    if (!x || !d1 || !d2) return ErrorValue{ErrorType::Value};
    double df1 = std::trunc(*d1), df2 = std::trunc(*d2);
    if (*x < 0.0 || df1 < 1.0 || df2 < 1.0) return ErrorValue{ErrorType::Num};
    return 1.0 - fCdf(*x, df1, df2);
}

CellValue fnFInv(const std::vector<formula::EvalArg>& a, const formula::EvalContext&, bool rightTail) {
    auto p = toNumber(a[0].scalar), d1 = toNumber(a[1].scalar), d2 = toNumber(a[2].scalar);
    if (!p || !d1 || !d2) return ErrorValue{ErrorType::Value};
    double df1 = std::trunc(*d1), df2 = std::trunc(*d2);
    if (*p <= 0.0 || *p >= 1.0 || df1 < 1.0 || df2 < 1.0) return ErrorValue{ErrorType::Num};
    double target = rightTail ? (1.0 - *p) : *p;
    return invertIncreasingCdf([&](double x) { return fCdf(x, df1, df2); }, target, 0.0, 10.0, true);
}

CellValue fnFTest(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto v1 = collectNumericList(a[0], ctx);
    auto v2 = collectNumericList(a[1], ctx);
    if (v1.size() < 2 || v2.size() < 2) return ErrorValue{ErrorType::DivZero};
    auto varOf = [](const std::vector<double>& v) {
        double m = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
        double ss = 0.0;
        for (double d : v) { double diff = d - m; ss += diff * diff; }
        return ss / static_cast<double>(v.size() - 1);
    };
    double s1 = varOf(v1), s2 = varOf(v2);
    if (s1 == 0.0 || s2 == 0.0) return ErrorValue{ErrorType::DivZero};
    double f = s1 / s2;
    double df1 = static_cast<double>(v1.size() - 1);
    double df2 = static_cast<double>(v2.size() - 1);
    double cdf = fCdf(f, df1, df2);
    return std::min(1.0, 2.0 * std::min(cdf, 1.0 - cdf));
}

// ---- GAMMA.DIST / GAMMADIST / GAMMA.INV / GAMMAINV ----
CellValue fnGammaDist(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), alpha = toNumber(a[1].scalar), beta = toNumber(a[2].scalar);
    auto cum = toBool(a[3].scalar);
    if (!x || !alpha || !beta || !cum) return ErrorValue{ErrorType::Value};
    if (*x < 0.0 || *alpha <= 0.0 || *beta <= 0.0) return ErrorValue{ErrorType::Num};
    if (*cum) return gammaincP(*alpha, *x / *beta);
    if (*x == 0.0) return (*alpha == 1.0) ? (1.0 / *beta) : 0.0;
    return std::exp((*alpha - 1.0) * std::log(*x) - (*x / *beta) - (*alpha) * std::log(*beta) - std::lgamma(*alpha));
}

CellValue fnGammaInv(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto p = toNumber(a[0].scalar), alpha = toNumber(a[1].scalar), beta = toNumber(a[2].scalar);
    if (!p || !alpha || !beta) return ErrorValue{ErrorType::Value};
    if (*p <= 0.0 || *p >= 1.0 || *alpha <= 0.0 || *beta <= 0.0) return ErrorValue{ErrorType::Num};
    double u = invertIncreasingCdf([&](double x) { return gammaincP(*alpha, x); }, *p, 0.0, std::max(2.0 * (*alpha), 10.0), true);
    return u * (*beta);
}

// ---- LOGNORM.DIST / LOGNORMDIST / LOGNORM.INV / LOGINV ----
CellValue fnLogNormDist(const std::vector<formula::EvalArg>& a, const formula::EvalContext&, bool hasCumArg) {
    auto x = toNumber(a[0].scalar), mean = toNumber(a[1].scalar), sd = toNumber(a[2].scalar);
    bool cum = hasCumArg ? toBool(a[3].scalar).value_or(true) : true;
    if (!x || !mean || !sd) return ErrorValue{ErrorType::Value};
    if (*x <= 0.0 || *sd <= 0.0) return ErrorValue{ErrorType::Num};
    double z = (std::log(*x) - *mean) / *sd;
    return cum ? normCdf(z) : (normPdf(z) / (*x * (*sd)));
}

CellValue fnLogNormInv(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto p = toNumber(a[0].scalar), mean = toNumber(a[1].scalar), sd = toNumber(a[2].scalar);
    if (!p || !mean || !sd) return ErrorValue{ErrorType::Value};
    if (*p <= 0.0 || *p >= 1.0 || *sd <= 0.0) return ErrorValue{ErrorType::Num};
    return std::exp(*mean + (*sd) * normInv(*p));
}

// ---- Student-t: T.DIST, T.DIST.2T, T.DIST.RT, TDIST, T.INV, T.INV.2T, TINV, T.TEST, TTEST, CONFIDENCE.T ----
double tCdf(double x, double df) {
    double ib = betainc(0.5 * df, 0.5, df / (df + x * x));
    return (x >= 0.0) ? (1.0 - 0.5 * ib) : (0.5 * ib);
}

double tPdf(double x, double df) {
    return std::exp(std::lgamma(0.5 * (df + 1.0)) - std::lgamma(0.5 * df) -
                    0.5 * std::log(df * kPi) - 0.5 * (df + 1.0) * std::log(1.0 + (x * x) / df));
}

double tInvVal(double p, double df) {
    if (p == 0.5) return 0.0;
    return invertIncreasingCdf([&](double x) { return tCdf(x, df); }, p, -10.0, 10.0, true, true);
}

CellValue fnTDist(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    auto cum = toBool(a[2].scalar);
    if (!x || !df || !cum) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (d < 1.0) return ErrorValue{ErrorType::Num};
    return *cum ? tCdf(*x, d) : tPdf(*x, d);
}

CellValue fnTDistRt(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    if (!x || !df) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (d < 1.0) return ErrorValue{ErrorType::Num};
    return 1.0 - tCdf(*x, d);
}

CellValue fnTDist2T(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    if (!x || !df) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (*x < 0.0 || d < 1.0) return ErrorValue{ErrorType::Num};
    return 2.0 * (1.0 - tCdf(*x, d));
}

CellValue fnTDistLegacy(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), df = toNumber(a[1].scalar), tails = toNumber(a[2].scalar);
    if (!x || !df || !tails) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    int t = static_cast<int>(std::trunc(*tails));
    if (*x < 0.0 || d < 1.0 || (t != 1 && t != 2)) return ErrorValue{ErrorType::Num};
    return (t == 1) ? (1.0 - tCdf(*x, d)) : (2.0 * (1.0 - tCdf(*x, d)));
}

CellValue fnTInv(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto p = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    if (!p || !df) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (*p <= 0.0 || *p >= 1.0 || d < 1.0) return ErrorValue{ErrorType::Num};
    return tInvVal(*p, d);
}

CellValue fnTInv2T(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto p = toNumber(a[0].scalar), df = toNumber(a[1].scalar);
    if (!p || !df) return ErrorValue{ErrorType::Value};
    double d = std::trunc(*df);
    if (*p <= 0.0 || *p > 1.0 || d < 1.0) return ErrorValue{ErrorType::Num};
    if (*p == 1.0) return 0.0;
    return tInvVal(1.0 - 0.5 * (*p), d);
}

CellValue fnConfidenceT(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto alpha = toNumber(a[0].scalar), sd = toNumber(a[1].scalar), sz = toNumber(a[2].scalar);
    if (!alpha || !sd || !sz) return ErrorValue{ErrorType::Value};
    double n = std::trunc(*sz);
    if (*alpha <= 0.0 || *alpha >= 1.0 || *sd <= 0.0 || n < 2.0) return ErrorValue{ErrorType::Num};
    return tInvVal(1.0 - 0.5 * (*alpha), n - 1.0) * (*sd) / std::sqrt(n);
}

CellValue fnTTest(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto v1 = collectNumericList(a[0], ctx);
    auto v2 = collectNumericList(a[1], ctx);
    auto tailsD = toNumber(a[2].scalar), typeD = toNumber(a[3].scalar);
    if (!tailsD || !typeD) return ErrorValue{ErrorType::Value};
    int tails = static_cast<int>(std::trunc(*tailsD));
    int type = static_cast<int>(std::trunc(*typeD));
    if ((tails != 1 && tails != 2) || type < 1 || type > 3) return ErrorValue{ErrorType::Num};

    double tStat = 0.0, df = 0.0;
    if (type == 1) { // Paired
        if (v1.size() != v2.size() || v1.size() < 2) return ErrorValue{ErrorType::NA};
        std::size_t n = v1.size();
        double meanD = 0.0;
        for (std::size_t i = 0; i < n; ++i) meanD += (v1[i] - v2[i]);
        meanD /= static_cast<double>(n);
        double ss = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double d = (v1[i] - v2[i]) - meanD;
            ss += d * d;
        }
        double sd = std::sqrt(ss / static_cast<double>(n - 1));
        if (sd == 0.0) return ErrorValue{ErrorType::DivZero};
        tStat = std::fabs(meanD / (sd / std::sqrt(static_cast<double>(n))));
        df = static_cast<double>(n - 1);
    } else {
        if (v1.size() < 2 || v2.size() < 2) return ErrorValue{ErrorType::DivZero};
        double n1 = static_cast<double>(v1.size()), n2 = static_cast<double>(v2.size());
        double m1 = std::accumulate(v1.begin(), v1.end(), 0.0) / n1;
        double m2 = std::accumulate(v2.begin(), v2.end(), 0.0) / n2;
        double ss1 = 0.0, ss2 = 0.0;
        for (double x : v1) ss1 += (x - m1) * (x - m1);
        for (double x : v2) ss2 += (x - m2) * (x - m2);
        double s1 = ss1 / (n1 - 1.0), s2 = ss2 / (n2 - 1.0);
        if (type == 2) { // Equal variance
            double sp2 = (ss1 + ss2) / (n1 + n2 - 2.0);
            double se = std::sqrt(sp2 * (1.0 / n1 + 1.0 / n2));
            if (se == 0.0) return ErrorValue{ErrorType::DivZero};
            tStat = std::fabs((m1 - m2) / se);
            df = n1 + n2 - 2.0;
        } else { // Welch unequal variance
            double v1n = s1 / n1, v2n = s2 / n2;
            double se = std::sqrt(v1n + v2n);
            if (se == 0.0) return ErrorValue{ErrorType::DivZero};
            tStat = std::fabs((m1 - m2) / se);
            df = (v1n + v2n) * (v1n + v2n) / ((v1n * v1n) / (n1 - 1.0) + (v2n * v2n) / (n2 - 1.0));
        }
    }
    double p1 = 1.0 - tCdf(tStat, df);
    return (tails == 1) ? p1 : std::min(1.0, 2.0 * p1);
}

// ---- WEIBULL (legacy alias for WEIBULL.DIST) & Z.TEST / ZTEST ----
CellValue fnWeibullLegacy(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar), alpha = toNumber(a[1].scalar), beta = toNumber(a[2].scalar);
    auto cum = toBool(a[3].scalar);
    if (!x || !alpha || !beta || !cum) return ErrorValue{ErrorType::Value};
    if (*x < 0.0 || *alpha <= 0.0 || *beta <= 0.0) return ErrorValue{ErrorType::Num};
    if (*cum) return 1.0 - std::exp(-std::pow(*x / *beta, *alpha));
    return (*alpha / std::pow(*beta, *alpha)) * std::pow(*x, *alpha - 1.0) * std::exp(-std::pow(*x / *beta, *alpha));
}

CellValue fnZTest(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto vals = collectNumericList(a[0], ctx);
    auto x0 = toNumber(a[1].scalar);
    if (!x0) return ErrorValue{ErrorType::Value};
    if (vals.empty()) return ErrorValue{ErrorType::NA};
    double n = static_cast<double>(vals.size());
    double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / n;
    double sigma = 0.0;
    if (a.size() >= 3 && !a[2].isOmitted()) {
        auto s = toNumber(a[2].scalar);
        if (!s || *s <= 0.0) return ErrorValue{ErrorType::Num};
        sigma = *s;
    } else {
        if (vals.size() < 2) return ErrorValue{ErrorType::DivZero};
        double ss = 0.0;
        for (double v : vals) ss += (v - mean) * (v - mean);
        sigma = std::sqrt(ss / (n - 1.0));
        if (sigma == 0.0) return ErrorValue{ErrorType::DivZero};
    }
    double z = (mean - *x0) / (sigma / std::sqrt(n));
    return 1.0 - normCdf(z);
}

} // namespace

void registerStatsDistExtFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"BETA.DIST", 4, 6, fnBetaDistModern});
    registry.registerFunction({"BETADIST", 3, 5, fnBetaDistLegacy});
    registry.registerFunction({"BETA.INV", 3, 5, fnBetaInv});
    registry.registerFunction({"BETAINV", 3, 5, fnBetaInv});

    registry.registerFunction({"BINOM.DIST.RANGE", 3, 4, fnBinomDistRange});
    registry.registerFunction({"BINOM.INV", 3, 3, fnBinomInv});
    registry.registerFunction({"CRITBINOM", 3, 3, fnBinomInv});
    registry.registerFunction({"HYPGEOM.DIST", 5, 5, [](const auto& a, const auto& c) { return fnHypgeomDist(a, c, true); }});
    registry.registerFunction({"HYPGEOMDIST", 4, 4, [](const auto& a, const auto& c) { return fnHypgeomDist(a, c, false); }});
    registry.registerFunction({"NEGBINOM.DIST", 4, 4, [](const auto& a, const auto& c) { return fnNegBinomDist(a, c, true); }});
    registry.registerFunction({"NEGBINOMDIST", 3, 3, [](const auto& a, const auto& c) { return fnNegBinomDist(a, c, false); }});

    registry.registerFunction({"CHISQ.DIST", 3, 3, fnChisqDist});
    registry.registerFunction({"CHISQ.DIST.RT", 2, 2, fnChisqDistRt});
    registry.registerFunction({"CHIDIST", 2, 2, fnChisqDistRt});
    registry.registerFunction({"CHISQ.INV", 2, 2, [](const auto& a, const auto& c) { return fnChisqInv(a, c, false); }});
    registry.registerFunction({"CHISQ.INV.RT", 2, 2, [](const auto& a, const auto& c) { return fnChisqInv(a, c, true); }});
    registry.registerFunction({"CHIINV", 2, 2, [](const auto& a, const auto& c) { return fnChisqInv(a, c, true); }});
    registry.registerFunction({"CHISQ.TEST", 2, 2, fnChisqTest});
    registry.registerFunction({"CHITEST", 2, 2, fnChisqTest});

    registry.registerFunction({"F.DIST", 4, 4, fnFDist});
    registry.registerFunction({"F.DIST.RT", 3, 3, fnFDistRt});
    registry.registerFunction({"FDIST", 3, 3, fnFDistRt});
    registry.registerFunction({"F.INV", 3, 3, [](const auto& a, const auto& c) { return fnFInv(a, c, false); }});
    registry.registerFunction({"F.INV.RT", 3, 3, [](const auto& a, const auto& c) { return fnFInv(a, c, true); }});
    registry.registerFunction({"FINV", 3, 3, [](const auto& a, const auto& c) { return fnFInv(a, c, true); }});
    registry.registerFunction({"F.TEST", 2, 2, fnFTest});
    registry.registerFunction({"FTEST", 2, 2, fnFTest});

    registry.registerFunction({"GAMMA.DIST", 4, 4, fnGammaDist});
    registry.registerFunction({"GAMMADIST", 4, 4, fnGammaDist});
    registry.registerFunction({"GAMMA.INV", 3, 3, fnGammaInv});
    registry.registerFunction({"GAMMAINV", 3, 3, fnGammaInv});

    registry.registerFunction({"LOGNORM.DIST", 4, 4, [](const auto& a, const auto& c) { return fnLogNormDist(a, c, true); }});
    registry.registerFunction({"LOGNORMDIST", 3, 3, [](const auto& a, const auto& c) { return fnLogNormDist(a, c, false); }});
    registry.registerFunction({"LOGNORM.INV", 3, 3, fnLogNormInv});
    registry.registerFunction({"LOGINV", 3, 3, fnLogNormInv});

    registry.registerFunction({"T.DIST", 3, 3, fnTDist});
    registry.registerFunction({"T.DIST.2T", 2, 2, fnTDist2T});
    registry.registerFunction({"T.DIST.RT", 2, 2, fnTDistRt});
    registry.registerFunction({"TDIST", 3, 3, fnTDistLegacy});
    registry.registerFunction({"T.INV", 2, 2, fnTInv});
    registry.registerFunction({"T.INV.2T", 2, 2, fnTInv2T});
    registry.registerFunction({"TINV", 2, 2, fnTInv2T});
    registry.registerFunction({"T.TEST", 4, 4, fnTTest});
    registry.registerFunction({"TTEST", 4, 4, fnTTest});
    registry.registerFunction({"CONFIDENCE.T", 3, 3, fnConfidenceT});

    registry.registerFunction({"WEIBULL", 4, 4, fnWeibullLegacy});
    registry.registerFunction({"Z.TEST", 2, 3, fnZTest});
    registry.registerFunction({"ZTEST", 2, 3, fnZTest});
}

} // namespace sheetcore::func_stats
