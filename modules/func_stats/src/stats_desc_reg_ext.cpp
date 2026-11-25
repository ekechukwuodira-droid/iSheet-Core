#include "sheetcore/formula/array_utils.hpp"
#include "sheetcore/formula/functions.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace sheetcore::func_stats {

namespace {

std::vector<double> collectNums(const formula::EvalArg& arg, const formula::EvalContext& ctx) {
    std::vector<double> out;
    for (const auto& v : formula::flatten(arg, ctx)) {
        if (isNumber(v)) out.push_back(std::get<double>(v));
    }
    return out;
}

std::optional<ErrorValue> collectAllA(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx,
                                      std::vector<double>& out) {
    for (const auto& arg : args) {
        if (arg.isRange() || arg.isArray()) {
            for (const auto& v : formula::flatten(arg, ctx)) {
                if (isError(v)) return std::get<ErrorValue>(v);
                if (isEmpty(v)) continue;
                if (isNumber(v)) out.push_back(std::get<double>(v));
                else if (isDateTime(v)) out.push_back(std::get<DateTimeValue>(v).serial);
                else if (isBool(v)) out.push_back(std::get<bool>(v) ? 1.0 : 0.0);
                else if (isText(v)) out.push_back(0.0);
            }
        } else {
            if (isError(arg.scalar)) return std::get<ErrorValue>(arg.scalar);
            if (isEmpty(arg.scalar)) continue;
            if (isBool(arg.scalar)) out.push_back(std::get<bool>(arg.scalar) ? 1.0 : 0.0);
            else if (auto n = toNumber(arg.scalar)) out.push_back(*n);
            else return ErrorValue{ErrorType::Value};
        }
    }
    return std::nullopt;
}

// ---- PERCENTILE.INC / PERCENTILE.EXC / QUARTILE.INC / QUARTILE.EXC ----
CellValue percentileCalc(std::vector<double> vals, double k, bool exclusive) {
    if (vals.empty()) return ErrorValue{ErrorType::Num};
    std::sort(vals.begin(), vals.end());
    std::size_t n = vals.size();
    if (!exclusive) {
        if (k < 0.0 || k > 1.0) return ErrorValue{ErrorType::Num};
        double pos = k * static_cast<double>(n - 1);
        std::size_t lo = static_cast<std::size_t>(std::floor(pos));
        std::size_t hi = std::min(lo + 1, n - 1);
        return vals[lo] + (pos - static_cast<double>(lo)) * (vals[hi] - vals[lo]);
    }
    if (k <= 0.0 || k >= 1.0) return ErrorValue{ErrorType::Num};
    double pos = k * static_cast<double>(n + 1);
    if (pos < 1.0 || pos > static_cast<double>(n)) return ErrorValue{ErrorType::Num};
    std::size_t lo = static_cast<std::size_t>(std::floor(pos)) - 1;
    std::size_t hi = std::min(lo + 1, n - 1);
    return vals[lo] + (pos - std::floor(pos)) * (vals[hi] - vals[lo]);
}

CellValue fnPercentile(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool exc) {
    auto k = toNumber(a[1].scalar);
    if (!k) return ErrorValue{ErrorType::Value};
    return percentileCalc(collectNums(a[0], ctx), *k, exc);
}

CellValue fnQuartile(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool exc) {
    auto qD = toNumber(a[1].scalar);
    if (!qD) return ErrorValue{ErrorType::Value};
    int q = static_cast<int>(std::trunc(*qD));
    if (exc ? (q < 1 || q > 3) : (q < 0 || q > 4)) return ErrorValue{ErrorType::Num};
    return percentileCalc(collectNums(a[0], ctx), static_cast<double>(q) * 0.25, exc);
}

// ---- PERCENTRANK / PERCENTRANK.INC / PERCENTRANK.EXC ----
CellValue fnPercentRank(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool exc) {
    auto vals = collectNums(a[0], ctx);
    auto x = toNumber(a[1].scalar);
    int sig = (a.size() >= 3 && !a[2].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[2].scalar).value_or(0.0))) : 3;
    if (!x) return ErrorValue{ErrorType::Value};
    if (vals.empty() || sig < 1) return ErrorValue{ErrorType::Num};
    std::sort(vals.begin(), vals.end());
    if (*x < vals.front() || *x > vals.back()) return ErrorValue{ErrorType::NA};
    std::size_t n = vals.size();
    double raw = 0.0;
    if (n == 1) {
        raw = exc ? 0.5 : 1.0;
    } else {
        auto it = std::lower_bound(vals.begin(), vals.end(), *x);
        std::size_t idx = static_cast<std::size_t>(it - vals.begin());
        double rankPos = 0.0;
        if (idx < n && std::fabs(vals[idx] - *x) <= 1e-12) {
            rankPos = static_cast<double>(idx);
        } else {
            double lo = vals[idx - 1], hi = vals[idx];
            rankPos = static_cast<double>(idx - 1) + ((*x - lo) / (hi - lo));
        }
        raw = exc ? ((rankPos + 1.0) / static_cast<double>(n + 1))
                  : (rankPos / static_cast<double>(n - 1));
    }
    double scale = std::pow(10.0, sig);
    return std::trunc(raw * scale) / scale;
}

// ---- RANK.EQ / RANK.AVG ----
CellValue fnRank(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool avgTies) {
    auto num = toNumber(a[0].scalar);
    if (!num) return ErrorValue{ErrorType::Value};
    bool asc = (a.size() >= 3 && !a[2].isOmitted()) ? (toNumber(a[2].scalar).value_or(0.0) != 0.0) : false;
    auto vals = collectNums(a[1], ctx);
    std::size_t before = 0, eq = 0;
    for (double v : vals) {
        if (std::fabs(v - *num) <= 1e-12) ++eq;
        else if (asc ? (v < *num) : (v > *num)) ++before;
    }
    if (eq == 0) return ErrorValue{ErrorType::NA};
    if (!avgTies) return static_cast<double>(before + 1);
    return static_cast<double>(before + 1) + 0.5 * static_cast<double>(eq - 1);
}

// ---- MODE.SNGL / MODE.MULT ----
std::vector<double> computeModes(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::vector<double> vals;
    for (const auto& a : args) {
        auto part = collectNums(a, ctx);
        vals.insert(vals.end(), part.begin(), part.end());
    }
    std::unordered_map<double, std::size_t> freq;
    std::vector<double> order;
    std::size_t maxFreq = 0;
    for (double v : vals) {
        auto& f = freq[v];
        if (f == 0) order.push_back(v);
        ++f;
        if (f > maxFreq) maxFreq = f;
    }
    std::vector<double> modes;
    if (maxFreq < 2) return modes;
    for (double v : order) {
        if (freq[v] == maxFreq) modes.push_back(v);
    }
    return modes;
}

CellValue fnModeSngl(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto modes = computeModes(a, ctx);
    return modes.empty() ? CellValue{ErrorValue{ErrorType::NA}} : CellValue{modes.front()};
}

formula::EvalArg fnModeMult(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto modes = computeModes(a, ctx);
    if (modes.empty()) return formula::makeScalar(ErrorValue{ErrorType::NA});
    formula::ArrayValue out(modes.size(), 1);
    for (std::size_t i = 0; i < modes.size(); ++i) out.at(i, 0) = modes[i];
    return formula::makeArray(std::move(out));
}

// ---- PROB(x_range, prob_range, lower_limit, [upper_limit]) ----
CellValue fnProb(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto xs = collectNums(a[0], ctx);
    auto ps = collectNums(a[1], ctx);
    auto lo = toNumber(a[2].scalar);
    double hi = (a.size() >= 4 && !a[3].isOmitted()) ? toNumber(a[3].scalar).value_or(std::nan("")) : lo.value_or(std::nan(""));
    if (!lo || std::isnan(hi)) return ErrorValue{ErrorType::Value};
    if (xs.size() != ps.size() || xs.empty()) return ErrorValue{ErrorType::NA};
    double pSum = 0.0;
    for (double p : ps) {
        if (p < 0.0 || p > 1.0) return ErrorValue{ErrorType::Num};
        pSum += p;
    }
    if (std::fabs(pSum - 1.0) > 1e-6) return ErrorValue{ErrorType::Num};
    double acc = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (xs[i] >= *lo && xs[i] <= hi) acc += ps[i];
    }
    return acc;
}

// ---- VARA / VARPA / STDEVA / STDEVPA ----
CellValue fnVarStdevA(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx,
                      bool sample, bool isStdev) {
    std::vector<double> vals;
    if (auto e = collectAllA(a, ctx, vals)) return *e;
    if (vals.size() < (sample ? 2u : 1u)) return ErrorValue{ErrorType::DivZero};
    double m = std::accumulate(vals.begin(), vals.end(), 0.0) / static_cast<double>(vals.size());
    double ss = 0.0;
    for (double v : vals) ss += (v - m) * (v - m);
    double var = ss / static_cast<double>(sample ? (vals.size() - 1) : vals.size());
    return isStdev ? std::sqrt(var) : var;
}

// ---- FREQUENCY(data_array, bins_array) ----
formula::EvalArg fnFrequency(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto data = collectNums(a[0], ctx);
    auto bins = collectNums(a[1], ctx);
    std::size_t m = bins.size();
    std::vector<std::size_t> order(m);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t i, std::size_t j) { return bins[i] < bins[j]; });

    std::vector<std::size_t> counts(m + 1, 0);
    for (double v : data) {
        bool placed = false;
        for (std::size_t k = 0; k < m; ++k) {
            if (v <= bins[order[k]]) {
                ++counts[order[k]];
                placed = true;
                break;
            }
        }
        if (!placed) ++counts[m];
    }
    formula::ArrayValue out(m + 1, 1);
    for (std::size_t i = 0; i <= m; ++i) out.at(i, 0) = static_cast<double>(counts[i]);
    return formula::makeArray(std::move(out));
}

// ---- Multiple OLS solver (normal equations with Gaussian elimination + partial pivoting) ----
bool solveLinearSystem(std::vector<double> A, std::vector<double> b, std::size_t n, std::vector<double>& x) {
    x.assign(n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t piv = k;
        double maxV = std::fabs(A[k * n + k]);
        for (std::size_t i = k + 1; i < n; ++i) {
            double v = std::fabs(A[i * n + k]);
            if (v > maxV) { maxV = v; piv = i; }
        }
        if (maxV < 1e-15) return false;
        if (piv != k) {
            for (std::size_t j = k; j < n; ++j) std::swap(A[k * n + j], A[piv * n + j]);
            std::swap(b[k], b[piv]);
        }
        double diag = A[k * n + k];
        for (std::size_t i = k + 1; i < n; ++i) {
            double f = A[i * n + k] / diag;
            for (std::size_t j = k + 1; j < n; ++j) A[i * n + j] -= f * A[k * n + j];
            b[i] -= f * b[k];
        }
    }
    for (std::size_t i = n; i-- > 0;) {
        double s = b[i];
        for (std::size_t j = i + 1; j < n; ++j) s -= A[i * n + j] * x[j];
        x[i] = s / A[i * n + i];
    }
    return true;
}

bool invertMatrix(std::vector<double> A, std::size_t n, std::vector<double>& inv) {
    inv.assign(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) inv[i * n + i] = 1.0;
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t piv = k;
        double maxV = std::fabs(A[k * n + k]);
        for (std::size_t i = k + 1; i < n; ++i) {
            double v = std::fabs(A[i * n + k]);
            if (v > maxV) { maxV = v; piv = i; }
        }
        if (maxV < 1e-15) return false;
        if (piv != k) {
            for (std::size_t j = 0; j < n; ++j) {
                std::swap(A[k * n + j], A[piv * n + j]);
                std::swap(inv[k * n + j], inv[piv * n + j]);
            }
        }
        double diag = A[k * n + k];
        for (std::size_t j = 0; j < n; ++j) { A[k * n + j] /= diag; inv[k * n + j] /= diag; }
        for (std::size_t i = 0; i < n; ++i) {
            if (i == k) continue;
            double f = A[i * n + k];
            for (std::size_t j = 0; j < n; ++j) {
                A[i * n + j] -= f * A[k * n + j];
                inv[i * n + j] -= f * inv[k * n + j];
            }
        }
    }
    return true;
}

struct OlsResult {
    std::vector<double> slopes; // length p (in input column order)
    double intercept = 0.0;
    std::vector<double> seSlopes;
    double seIntercept = 0.0;
    double r2 = 0.0;
    double seY = 0.0;
    double fStat = 0.0;
    double df = 0.0;
    double ssReg = 0.0;
    double ssRes = 0.0;
    bool ok = false;
};

// X is n x p (row-major observations x predictors), y is length n
OlsResult fitOls(const std::vector<double>& y, const std::vector<double>& X,
                 std::size_t n, std::size_t p, bool includeConst) {
    OlsResult res;
    std::size_t k = p + (includeConst ? 1 : 0);
    if (n == 0 || k == 0 || n < k) return res;

    // Design matrix D: cols 0..p-1 are X, col p (if includeConst) is 1.0
    std::vector<double> XtX(k * k, 0.0);
    std::vector<double> Xty(k, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t a = 0; a < k; ++a) {
            double da = (a < p) ? X[i * p + a] : 1.0;
            Xty[a] += da * y[i];
            for (std::size_t b = 0; b < k; ++b) {
                double db = (b < p) ? X[i * p + b] : 1.0;
                XtX[a * k + b] += da * db;
            }
        }
    }
    std::vector<double> beta;
    if (!solveLinearSystem(XtX, Xty, k, beta)) return res;

    res.slopes.assign(beta.begin(), beta.begin() + static_cast<std::ptrdiff_t>(p));
    res.intercept = includeConst ? beta[p] : 0.0;

    double yMean = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(n);
    double ssTot = 0.0, ssRes = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double yhat = res.intercept;
        for (std::size_t j = 0; j < p; ++j) yhat += res.slopes[j] * X[i * p + j];
        double err = y[i] - yhat;
        ssRes += err * err;
        double dev = includeConst ? (y[i] - yMean) : y[i];
        ssTot += dev * dev;
    }
    res.ssRes = ssRes;
    res.ssReg = std::max(0.0, ssTot - ssRes);
    res.r2 = (ssTot > 0.0) ? (res.ssReg / ssTot) : 1.0;
    res.df = static_cast<double>(n - k);
    double mse = (res.df > 0.0) ? (ssRes / res.df) : 0.0;
    res.seY = std::sqrt(mse);
    res.fStat = (mse > 0.0) ? ((res.ssReg / static_cast<double>(p)) / mse) : 0.0;

    std::vector<double> invXtX;
    res.seSlopes.assign(p, 0.0);
    if (res.df > 0.0 && invertMatrix(XtX, k, invXtX)) {
        for (std::size_t j = 0; j < p; ++j) res.seSlopes[j] = std::sqrt(std::max(0.0, mse * invXtX[j * k + j]));
        if (includeConst) res.seIntercept = std::sqrt(std::max(0.0, mse * invXtX[p * k + p]));
    }
    res.ok = true;
    return res;
}

bool prepareRegressionData(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx,
                           bool logY, std::vector<double>& y, std::vector<double>& X,
                           std::size_t& n, std::size_t& p, bool& transposeX) {
    formula::ArrayValue ya = formula::toArray(a[0], ctx);
    y.clear();
    for (const auto& v : ya.elements) {
        auto d = toNumber(v);
        if (!d) return false;
        if (logY && *d <= 0.0) return false;
        y.push_back(logY ? std::log(*d) : *d);
    }
    n = y.size();
    if (n == 0) return false;

    transposeX = false;
    if (a.size() >= 2 && !a[1].isOmitted()) {
        formula::ArrayValue xa = formula::toArray(a[1], ctx);
        if (xa.rows == n) {
            p = xa.cols;
            X.resize(n * p);
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = 0; j < p; ++j) {
                    auto d = toNumber(xa.at(i, j));
                    if (!d) return false;
                    X[i * p + j] = *d;
                }
            }
        } else if (xa.cols == n) {
            transposeX = true;
            p = xa.rows;
            X.resize(n * p);
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = 0; j < p; ++j) {
                    auto d = toNumber(xa.at(j, i));
                    if (!d) return false;
                    X[i * p + j] = *d;
                }
            }
        } else {
            return false;
        }
    } else {
        p = 1;
        X.resize(n);
        for (std::size_t i = 0; i < n; ++i) X[i] = static_cast<double>(i + 1);
    }
    return true;
}

// ---- LINEST / LOGEST ----
formula::EvalArg fnLinestLogest(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool isLog) {
    bool includeConst = (a.size() >= 3 && !a[2].isOmitted()) ? toBool(a[2].scalar).value_or(true) : true;
    bool fullStats = (a.size() >= 4 && !a[3].isOmitted()) ? toBool(a[3].scalar).value_or(false) : false;
    std::vector<double> y, X;
    std::size_t n = 0, p = 0;
    bool transposeX = false;
    if (!prepareRegressionData(a, ctx, isLog, y, X, n, p, transposeX)) {
        return formula::makeScalar(ErrorValue{ErrorType::Value});
    }
    OlsResult ols = fitOls(y, X, n, p, includeConst);
    if (!ols.ok) return formula::makeScalar(ErrorValue{ErrorType::Num});

    // Excel returns slopes in REVERSE order: [m_p, m_{p-1}, ..., m_1, b]
    std::size_t cols = p + 1;
    std::size_t rows = fullStats ? 5 : 1;
    formula::ArrayValue out(rows, cols, ErrorValue{ErrorType::NA});
    for (std::size_t j = 0; j < p; ++j) {
        double m = ols.slopes[p - 1 - j];
        out.at(0, j) = isLog ? std::exp(m) : m;
    }
    out.at(0, p) = isLog ? (includeConst ? std::exp(ols.intercept) : 1.0) : ols.intercept;

    if (fullStats) {
        for (std::size_t j = 0; j < p; ++j) out.at(1, j) = ols.seSlopes[p - 1 - j];
        out.at(1, p) = includeConst ? CellValue{ols.seIntercept} : CellValue{ErrorValue{ErrorType::NA}};
        out.at(2, 0) = ols.r2;
        out.at(2, 1) = ols.seY;
        out.at(3, 0) = ols.fStat;
        out.at(3, 1) = ols.df;
        out.at(4, 0) = ols.ssReg;
        out.at(4, 1) = ols.ssRes;
    }
    return formula::makeArray(std::move(out));
}

// ---- TREND / GROWTH ----
formula::EvalArg fnTrendGrowth(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx, bool isGrowth) {
    bool includeConst = (a.size() >= 4 && !a[3].isOmitted()) ? toBool(a[3].scalar).value_or(true) : true;
    std::vector<double> y, X;
    std::size_t n = 0, p = 0;
    bool transposeX = false;
    if (!prepareRegressionData(a, ctx, isGrowth, y, X, n, p, transposeX)) {
        return formula::makeScalar(ErrorValue{ErrorType::Value});
    }
    OlsResult ols = fitOls(y, X, n, p, includeConst);
    if (!ols.ok) return formula::makeScalar(ErrorValue{ErrorType::Num});

    if (a.size() >= 3 && !a[2].isOmitted()) {
        formula::ArrayValue nx = formula::toArray(a[2], ctx);
        if (p == 1) {
            formula::ArrayValue out(nx.rows, nx.cols);
            for (std::size_t i = 0; i < nx.elements.size(); ++i) {
                auto d = toNumber(nx.elements[i]);
                if (!d) return formula::makeScalar(ErrorValue{ErrorType::Value});
                double pred = ols.intercept + ols.slopes[0] * (*d);
                out.elements[i] = isGrowth ? std::exp(pred) : pred;
            }
            return formula::makeArray(std::move(out));
        }
        std::size_t mObs = transposeX ? nx.cols : nx.rows;
        formula::ArrayValue out(transposeX ? 1 : mObs, transposeX ? mObs : 1);
        for (std::size_t i = 0; i < mObs; ++i) {
            double pred = ols.intercept;
            for (std::size_t j = 0; j < p; ++j) {
                auto d = toNumber(transposeX ? nx.at(j, i) : nx.at(i, j));
                if (!d) return formula::makeScalar(ErrorValue{ErrorType::Value});
                pred += ols.slopes[j] * (*d);
            }
            out.elements[i] = isGrowth ? std::exp(pred) : pred;
        }
        return formula::makeArray(std::move(out));
    }

    // No new_x: return fitted values for known_x
    formula::ArrayValue out(transposeX ? 1 : n, transposeX ? n : 1);
    for (std::size_t i = 0; i < n; ++i) {
        double pred = ols.intercept;
        for (std::size_t j = 0; j < p; ++j) pred += ols.slopes[j] * X[i * p + j];
        out.elements[i] = isGrowth ? std::exp(pred) : pred;
    }
    return formula::makeArray(std::move(out));
}

// ---- FORECAST.ETS family (Holt-Winters additive exponential smoothing) ----
struct EtsModel {
    double alpha = 0.3;
    double beta = 0.1;
    double gamma = 0.1;
    int season = 1;
    double level = 0.0;
    double trend = 0.0;
    std::vector<double> seasonals;
    double stepSize = 1.0;
    double lastTime = 0.0;
    double mase = 0.0;
    double smape = 0.0;
    double mae = 0.0;
    double rmse = 0.0;
    bool ok = false;
};

int detectSeasonality(const std::vector<double>& y) {
    std::size_t n = y.size();
    if (n < 6) return 1;
    double mean = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(n);
    double denom = 0.0;
    for (double v : y) denom += (v - mean) * (v - mean);
    if (denom <= 1e-12) return 1;

    int bestLag = 1;
    double bestR = 0.35; // require meaningful positive autocorrelation
    std::size_t maxLag = std::min<std::size_t>(n / 2, 24);
    for (std::size_t lag = 2; lag <= maxLag; ++lag) {
        double num = 0.0;
        for (std::size_t i = lag; i < n; ++i) num += (y[i] - mean) * (y[i - lag] - mean);
        double r = num / denom;
        if (r > bestR) { bestR = r; bestLag = static_cast<int>(lag); }
    }
    return bestLag;
}

EtsModel fitEts(const std::vector<double>& vals, const std::vector<double>& times, int requestedSeason) {
    EtsModel m;
    if (vals.size() != times.size() || vals.size() < 2) return m;
    std::size_t n = vals.size();
    std::vector<std::size_t> ord(n);
    std::iota(ord.begin(), ord.end(), 0);
    std::sort(ord.begin(), ord.end(), [&](std::size_t i, std::size_t j) { return times[i] < times[j]; });

    std::vector<double> y(n), t(n);
    for (std::size_t i = 0; i < n; ++i) { y[i] = vals[ord[i]]; t[i] = times[ord[i]]; }
    m.stepSize = (t.back() - t.front()) / static_cast<double>(n - 1);
    if (m.stepSize <= 0.0) return m;
    m.lastTime = t.back();

    int s = (requestedSeason == 1) ? detectSeasonality(y) : ((requestedSeason <= 0) ? 1 : requestedSeason);
    if (static_cast<std::size_t>(s) >= n) s = 1;
    m.season = s;

    // Initialize level, trend, seasonal components
    double L = y[0];
    double T = (y[1] - y[0]);
    std::vector<double> S(static_cast<std::size_t>(s), 0.0);
    if (s > 1 && n >= static_cast<std::size_t>(2 * s)) {
        double avg1 = 0.0, avg2 = 0.0;
        for (int i = 0; i < s; ++i) {
            avg1 += y[static_cast<std::size_t>(i)];
            avg2 += y[static_cast<std::size_t>(i + s)];
        }
        avg1 /= static_cast<double>(s);
        avg2 /= static_cast<double>(s);
        L = avg1;
        T = (avg2 - avg1) / static_cast<double>(s);
        for (int i = 0; i < s; ++i) S[static_cast<std::size_t>(i)] = y[static_cast<std::size_t>(i)] - avg1;
    }

    double sumAbsErr = 0.0, sumSqErr = 0.0, sumSmape = 0.0, sumNaive = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t si = i % static_cast<std::size_t>(s);
        double yhat = L + T + (s > 1 ? S[si] : 0.0);
        double err = y[i] - yhat;
        if (i > 0) {
            sumAbsErr += std::fabs(err);
            sumSqErr += err * err;
            double d = std::fabs(y[i]) + std::fabs(yhat);
            sumSmape += (d > 0.0) ? (2.0 * std::fabs(err) / d) : 0.0;
            sumNaive += std::fabs(y[i] - y[i - 1]);
        }
        double prevL = L;
        double adjY = y[i] - (s > 1 ? S[si] : 0.0);
        L = m.alpha * adjY + (1.0 - m.alpha) * (L + T);
        T = m.beta * (L - prevL) + (1.0 - m.beta) * T;
        if (s > 1) S[si] = m.gamma * (y[i] - L) + (1.0 - m.gamma) * S[si];
    }

    double denomN = static_cast<double>(n - 1);
    m.level = L;
    m.trend = T;
    m.seasonals = std::move(S);
    m.mae = sumAbsErr / denomN;
    m.rmse = std::sqrt(sumSqErr / denomN);
    m.smape = sumSmape / denomN;
    m.mase = (sumNaive > 0.0) ? (sumAbsErr / sumNaive) : 0.0;
    m.ok = true;
    return m;
}

CellValue fnForecastEts(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto target = toNumber(a[0].scalar);
    if (!target) return ErrorValue{ErrorType::Value};
    auto vals = collectNums(a[1], ctx);
    auto times = collectNums(a[2], ctx);
    int reqSeason = (a.size() >= 4 && !a[3].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[3].scalar).value_or(1.0))) : 1;
    auto m = fitEts(vals, times, reqSeason);
    if (!m.ok) return ErrorValue{ErrorType::Num};
    double steps = std::max(0.0, (*target - m.lastTime) / m.stepSize);
    double pred = m.level + steps * m.trend;
    if (m.season > 1) {
        std::size_t idx = static_cast<std::size_t>(std::llround(steps)) % static_cast<std::size_t>(m.season);
        pred += m.seasonals[idx];
    }
    return pred;
}

CellValue fnForecastEtsConfint(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto target = toNumber(a[0].scalar);
    if (!target) return ErrorValue{ErrorType::Value};
    auto vals = collectNums(a[1], ctx);
    auto times = collectNums(a[2], ctx);
    double conf = (a.size() >= 4 && !a[3].isOmitted()) ? toNumber(a[3].scalar).value_or(0.95) : 0.95;
    int reqSeason = (a.size() >= 5 && !a[4].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[4].scalar).value_or(1.0))) : 1;
    if (conf <= 0.0 || conf >= 1.0) return ErrorValue{ErrorType::Num};
    auto m = fitEts(vals, times, reqSeason);
    if (!m.ok) return ErrorValue{ErrorType::Num};
    // Approximate z-score for two-sided confidence level via inverse erf
    double p = 0.5 + 0.5 * conf;
    // Quick bisection on normal CDF
    double lo = 0.0, hi = 6.0;
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        if (0.5 * std::erfc(-mid / std::sqrt(2.0)) < p) lo = mid; else hi = mid;
    }
    double z = 0.5 * (lo + hi);
    double steps = std::max(1.0, (*target - m.lastTime) / m.stepSize);
    return z * m.rmse * std::sqrt(steps);
}

CellValue fnForecastEtsSeasonality(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto vals = collectNums(a[0], ctx);
    auto times = collectNums(a[1], ctx);
    if (vals.size() != times.size() || vals.size() < 2) return ErrorValue{ErrorType::NA};
    return static_cast<double>(detectSeasonality(vals));
}

CellValue fnForecastEtsStat(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto vals = collectNums(a[0], ctx);
    auto times = collectNums(a[1], ctx);
    auto statTypeD = toNumber(a[2].scalar);
    if (!statTypeD) return ErrorValue{ErrorType::Value};
    int statType = static_cast<int>(std::trunc(*statTypeD));
    if (statType < 1 || statType > 8) return ErrorValue{ErrorType::Num};
    int reqSeason = (a.size() >= 4 && !a[3].isOmitted()) ? static_cast<int>(std::trunc(toNumber(a[3].scalar).value_or(1.0))) : 1;
    auto m = fitEts(vals, times, reqSeason);
    if (!m.ok) return ErrorValue{ErrorType::Num};
    switch (statType) {
        case 1: return m.alpha;
        case 2: return m.beta;
        case 3: return m.gamma;
        case 4: return m.mase;
        case 5: return m.smape;
        case 6: return m.mae;
        case 7: return m.rmse;
        case 8: return m.stepSize;
        default: return ErrorValue{ErrorType::Num};
    }
}

} // namespace

void registerStatsDescRegExtFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"PERCENTILE.INC", 2, 2, [](const auto& a, const auto& c) { return fnPercentile(a, c, false); }});
    registry.registerFunction({"PERCENTILE.EXC", 2, 2, [](const auto& a, const auto& c) { return fnPercentile(a, c, true); }});
    registry.registerFunction({"PERCENTRANK", 2, 3, [](const auto& a, const auto& c) { return fnPercentRank(a, c, false); }});
    registry.registerFunction({"PERCENTRANK.INC", 2, 3, [](const auto& a, const auto& c) { return fnPercentRank(a, c, false); }});
    registry.registerFunction({"PERCENTRANK.EXC", 2, 3, [](const auto& a, const auto& c) { return fnPercentRank(a, c, true); }});
    registry.registerFunction({"QUARTILE.INC", 2, 2, [](const auto& a, const auto& c) { return fnQuartile(a, c, false); }});
    registry.registerFunction({"QUARTILE.EXC", 2, 2, [](const auto& a, const auto& c) { return fnQuartile(a, c, true); }});
    registry.registerFunction({"RANK.EQ", 2, 3, [](const auto& a, const auto& c) { return fnRank(a, c, false); }});
    registry.registerFunction({"RANK.AVG", 2, 3, [](const auto& a, const auto& c) { return fnRank(a, c, true); }});
    registry.registerFunction({"MODE.SNGL", 1, 255, fnModeSngl});
    registry.registerArrayFunction("MODE.MULT", 1, 255, fnModeMult);
    registry.registerFunction({"PROB", 3, 4, fnProb});
    registry.registerFunction({"STDEVA", 1, 255, [](const auto& a, const auto& c) { return fnVarStdevA(a, c, true, true); }});
    registry.registerFunction({"STDEVPA", 1, 255, [](const auto& a, const auto& c) { return fnVarStdevA(a, c, false, true); }});
    registry.registerFunction({"VARA", 1, 255, [](const auto& a, const auto& c) { return fnVarStdevA(a, c, true, false); }});
    registry.registerFunction({"VARPA", 1, 255, [](const auto& a, const auto& c) { return fnVarStdevA(a, c, false, false); }});

    registry.registerArrayFunction("FREQUENCY", 2, 2, fnFrequency);
    registry.registerArrayFunction("LINEST", 1, 4, [](const auto& a, const auto& c) { return fnLinestLogest(a, c, false); });
    registry.registerArrayFunction("LOGEST", 1, 4, [](const auto& a, const auto& c) { return fnLinestLogest(a, c, true); });
    registry.registerArrayFunction("TREND", 1, 4, [](const auto& a, const auto& c) { return fnTrendGrowth(a, c, false); });
    registry.registerArrayFunction("GROWTH", 1, 4, [](const auto& a, const auto& c) { return fnTrendGrowth(a, c, true); });

    registry.registerFunction({"FORECAST.ETS", 3, 6, fnForecastEts});
    registry.registerFunction({"FORECAST.ETS.CONFINT", 3, 7, fnForecastEtsConfint});
    registry.registerFunction({"FORECAST.ETS.SEASONALITY", 2, 4, fnForecastEtsSeasonality});
    registry.registerFunction({"FORECAST.ETS.STAT", 3, 6, fnForecastEtsStat});
}

} // namespace sheetcore::func_stats
