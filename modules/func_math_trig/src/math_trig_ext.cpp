#include "sheetcore/formula/array_utils.hpp"
#include "sheetcore/formula/functions.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace sheetcore::func_math_trig {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string toUpperAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}

bool matchesCriteria(const CellValue& val, const CellValue& crit) {
    if (isText(crit)) {
        const std::string& cs = std::get<std::string>(crit);
        if (cs.size() >= 2 && (cs[0] == '>' || cs[0] == '<')) {
            if (cs[0] == '<' && cs[1] == '>') {
                std::string_view rhs = std::string_view(cs).substr(2);
                if (auto n = toNumber(std::string(rhs)); n && isNumber(val)) {
                    return std::fabs(std::get<double>(val) - *n) > 1e-12;
                }
                return toUpperAscii(toDisplayString(val)) != toUpperAscii(rhs);
            }
            bool isGt = cs[0] == '>';
            bool isEq = cs[1] == '=';
            std::string_view numStr = std::string_view(cs).substr(isEq ? 2 : 1);
            if (auto target = toNumber(std::string(numStr)); target && isNumber(val)) {
                double d = std::get<double>(val);
                return isGt ? (isEq ? d >= *target : d > *target)
                            : (isEq ? d <= *target : d < *target);
            }
        } else if (!cs.empty() && cs[0] == '=') {
            std::string_view rhs = std::string_view(cs).substr(1);
            if (auto n = toNumber(std::string(rhs)); n && isNumber(val)) {
                return std::fabs(std::get<double>(val) - *n) <= 1e-12;
            }
            return toUpperAscii(toDisplayString(val)) == toUpperAscii(rhs);
        }
        if (isText(val)) {
            return toUpperAscii(std::get<std::string>(val)) == toUpperAscii(cs);
        }
    }
    return val == crit;
}

// ---- ACOT / ACOTH ----
CellValue fnAcot(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar);
    if (!x) return ErrorValue{ErrorType::Value};
    return (*x == 0.0) ? (kPi / 2.0) : ((*x > 0.0) ? std::atan(1.0 / *x) : (kPi + std::atan(1.0 / *x)));
}

CellValue fnAcoth(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto x = toNumber(a[0].scalar);
    if (!x) return ErrorValue{ErrorType::Value};
    if (std::fabs(*x) <= 1.0) return ErrorValue{ErrorType::Num};
    return 0.5 * std::log((*x + 1.0) / (*x - 1.0));
}

// ---- CEILING.MATH / CEILING.PRECISE / ISO.CEILING / FLOOR.MATH / FLOOR.PRECISE ----
CellValue fnCeilingMath(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto num = toNumber(a[0].scalar);
    if (!num) return ErrorValue{ErrorType::Value};
    double sig = (a.size() >= 2 && !a[1].isOmitted()) ? toNumber(a[1].scalar).value_or(std::nan("")) : 1.0;
    double mode = (a.size() >= 3 && !a[2].isOmitted()) ? toNumber(a[2].scalar).value_or(std::nan("")) : 0.0;
    if (std::isnan(sig) || std::isnan(mode)) return ErrorValue{ErrorType::Value};
    if (sig == 0.0 || *num == 0.0) return 0.0;
    double step = std::fabs(sig);
    if (*num < 0.0 && mode != 0.0) {
        return -std::ceil(std::fabs(*num) / step) * step;
    }
    return std::ceil(*num / step) * step;
}

CellValue fnCeilingPrecise(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto num = toNumber(a[0].scalar);
    if (!num) return ErrorValue{ErrorType::Value};
    double sig = (a.size() >= 2 && !a[1].isOmitted()) ? toNumber(a[1].scalar).value_or(std::nan("")) : 1.0;
    if (std::isnan(sig)) return ErrorValue{ErrorType::Value};
    if (sig == 0.0 || *num == 0.0) return 0.0;
    double step = std::fabs(sig);
    return std::ceil(*num / step) * step;
}

CellValue fnFloorMath(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto num = toNumber(a[0].scalar);
    if (!num) return ErrorValue{ErrorType::Value};
    double sig = (a.size() >= 2 && !a[1].isOmitted()) ? toNumber(a[1].scalar).value_or(std::nan("")) : 1.0;
    double mode = (a.size() >= 3 && !a[2].isOmitted()) ? toNumber(a[2].scalar).value_or(std::nan("")) : 0.0;
    if (std::isnan(sig) || std::isnan(mode)) return ErrorValue{ErrorType::Value};
    if (sig == 0.0 || *num == 0.0) return 0.0;
    double step = std::fabs(sig);
    if (*num < 0.0 && mode != 0.0) {
        return -std::floor(std::fabs(*num) / step) * step;
    }
    return std::floor(*num / step) * step;
}

CellValue fnFloorPrecise(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto num = toNumber(a[0].scalar);
    if (!num) return ErrorValue{ErrorType::Value};
    double sig = (a.size() >= 2 && !a[1].isOmitted()) ? toNumber(a[1].scalar).value_or(std::nan("")) : 1.0;
    if (std::isnan(sig)) return ErrorValue{ErrorType::Value};
    if (sig == 0.0 || *num == 0.0) return 0.0;
    double step = std::fabs(sig);
    return std::floor(*num / step) * step;
}

// ---- SERIESSUM(x, n, m, coefficients) ----
CellValue fnSeriesSum(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto x = toNumber(a[0].scalar);
    auto n = toNumber(a[1].scalar);
    auto m = toNumber(a[2].scalar);
    if (!x || !n || !m) return ErrorValue{ErrorType::Value};
    auto coeffs = formula::flatten(a[3], ctx);
    double sum = 0.0;
    for (std::size_t i = 0; i < coeffs.size(); ++i) {
        if (isError(coeffs[i])) return coeffs[i];
        auto c = toNumber(coeffs[i]);
        if (!c) return ErrorValue{ErrorType::Value};
        double p = std::pow(*x, *n + static_cast<double>(i) * (*m));
        if (std::isnan(p) || std::isinf(p)) return ErrorValue{ErrorType::Num};
        sum += (*c) * p;
    }
    return sum;
}

// ---- EUROCONVERT(number, source, target, [full_precision], [triangulation_precision]) ----
struct EuroCurrency {
    double rate; // units per 1 EUR
    int decimals;
};

const std::unordered_map<std::string, EuroCurrency>& euroRates() {
    static const std::unordered_map<std::string, EuroCurrency> kRates = {
        {"EUR", {1.0, 2}},
        {"ATS", {13.7603, 2}},
        {"BEF", {40.3399, 0}},
        {"CYP", {0.585274, 2}},
        {"DEM", {1.95583, 2}},
        {"EEK", {15.6466, 2}},
        {"ESP", {166.386, 0}},
        {"FIM", {5.94573, 2}},
        {"FRF", {6.55957, 2}},
        {"GRD", {340.750, 0}},
        {"HRK", {7.53450, 2}},
        {"IEP", {0.787564, 2}},
        {"ITL", {1936.27, 0}},
        {"LTL", {3.45280, 2}},
        {"LUF", {40.3399, 0}},
        {"LVL", {0.702804, 2}},
        {"MTL", {0.429300, 2}},
        {"NLG", {2.20371, 2}},
        {"PTE", {200.482, 0}},
        {"SIT", {239.640, 2}},
        {"SKK", {30.1260, 2}},
    };
    return kRates;
}

double roundToDigits(double v, int digits) {
    double f = std::pow(10.0, digits);
    return std::round(v * f) / f;
}

CellValue fnEuroConvert(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto num = toNumber(a[0].scalar);
    if (!num) return ErrorValue{ErrorType::Value};
    std::string src = toUpperAscii(toDisplayString(a[1].scalar));
    std::string dst = toUpperAscii(toDisplayString(a[2].scalar));
    const auto& rates = euroRates();
    auto itS = rates.find(src);
    auto itD = rates.find(dst);
    if (itS == rates.end() || itD == rates.end()) return ErrorValue{ErrorType::Value};
    bool fullPrec = (a.size() >= 4 && !a[3].isOmitted()) ? toBool(a[3].scalar).value_or(false) : false;
    int triPrec = -1;
    if (a.size() >= 5 && !a[4].isOmitted()) {
        auto tp = toNumber(a[4].scalar);
        if (!tp || *tp < 3.0) return ErrorValue{ErrorType::Value};
        triPrec = static_cast<int>(std::trunc(*tp));
    }
    if (src == dst) return *num;
    double eur = *num / itS->second.rate;
    if (triPrec >= 3) {
        // Round intermediate EUR to triPrec significant digits
        if (eur != 0.0) {
            int mag = static_cast<int>(std::floor(std::log10(std::fabs(eur))));
            double scale = std::pow(10.0, triPrec - 1 - mag);
            eur = std::round(eur * scale) / scale;
        }
    }
    double out = eur * itD->second.rate;
    if (!fullPrec) {
        out = roundToDigits(out, itD->second.decimals);
    }
    return out;
}

// ---- PERCENTOF(data_subset, data_all) ----
CellValue fnPercentOf(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    auto sumNums = [&](const formula::EvalArg& arg, double& out) -> std::optional<ErrorValue> {
        out = 0.0;
        for (const auto& v : formula::flatten(arg, ctx)) {
            if (isError(v)) return std::get<ErrorValue>(v);
            if (isNumber(v)) out += std::get<double>(v);
        }
        return std::nullopt;
    };
    double sub = 0.0, all = 0.0;
    if (auto e = sumNums(a[0], sub)) return *e;
    if (auto e = sumNums(a[1], all)) return *e;
    if (all == 0.0) return ErrorValue{ErrorType::DivZero};
    return sub / all;
}

// ---- Matrix functions: MDETERM, MINVERSE, MMULT, MUNIT ----
bool extractNumericMatrix(const formula::EvalArg& arg, const formula::EvalContext& ctx,
                          std::size_t& rows, std::size_t& cols, std::vector<double>& data) {
    formula::ArrayValue arr = formula::toArray(arg, ctx);
    rows = arr.rows;
    cols = arr.cols;
    if (rows == 0 || cols == 0) return false;
    data.resize(rows * cols);
    for (std::size_t i = 0; i < arr.elements.size(); ++i) {
        if (isError(arr.elements[i]) || isEmpty(arr.elements[i]) || isText(arr.elements[i]) || isBool(arr.elements[i])) {
            return false;
        }
        auto n = toNumber(arr.elements[i]);
        if (!n) return false;
        data[i] = *n;
    }
    return true;
}

CellValue fnMDeterm(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::size_t rows = 0, cols = 0;
    std::vector<double> m;
    if (!extractNumericMatrix(a[0], ctx, rows, cols, m) || rows != cols) {
        return ErrorValue{ErrorType::Value};
    }
    std::size_t n = rows;
    double det = 1.0;
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t pivot = k;
        double maxVal = std::fabs(m[k * n + k]);
        for (std::size_t i = k + 1; i < n; ++i) {
            double v = std::fabs(m[i * n + k]);
            if (v > maxVal) { maxVal = v; pivot = i; }
        }
        if (maxVal < 1e-15) return 0.0;
        if (pivot != k) {
            for (std::size_t j = k; j < n; ++j) std::swap(m[k * n + j], m[pivot * n + j]);
            det = -det;
        }
        double diag = m[k * n + k];
        det *= diag;
        for (std::size_t i = k + 1; i < n; ++i) {
            double factor = m[i * n + k] / diag;
            for (std::size_t j = k + 1; j < n; ++j) {
                m[i * n + j] -= factor * m[k * n + j];
            }
        }
    }
    return det;
}

formula::EvalArg fnMInverse(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::size_t rows = 0, cols = 0;
    std::vector<double> m;
    if (!extractNumericMatrix(a[0], ctx, rows, cols, m) || rows != cols) {
        return formula::makeScalar(ErrorValue{ErrorType::Value});
    }
    std::size_t n = rows;
    std::vector<double> inv(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) inv[i * n + i] = 1.0;

    for (std::size_t k = 0; k < n; ++k) {
        std::size_t pivot = k;
        double maxVal = std::fabs(m[k * n + k]);
        for (std::size_t i = k + 1; i < n; ++i) {
            double v = std::fabs(m[i * n + k]);
            if (v > maxVal) { maxVal = v; pivot = i; }
        }
        if (maxVal < 1e-15) return formula::makeScalar(ErrorValue{ErrorType::Num});
        if (pivot != k) {
            for (std::size_t j = 0; j < n; ++j) {
                std::swap(m[k * n + j], m[pivot * n + j]);
                std::swap(inv[k * n + j], inv[pivot * n + j]);
            }
        }
        double diag = m[k * n + k];
        for (std::size_t j = 0; j < n; ++j) {
            m[k * n + j] /= diag;
            inv[k * n + j] /= diag;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (i == k) continue;
            double factor = m[i * n + k];
            for (std::size_t j = 0; j < n; ++j) {
                m[i * n + j] -= factor * m[k * n + j];
                inv[i * n + j] -= factor * inv[k * n + j];
            }
        }
    }

    formula::ArrayValue out(n, n);
    for (std::size_t i = 0; i < n * n; ++i) out.elements[i] = inv[i];
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnMMult(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::size_t r1 = 0, c1 = 0, r2 = 0, c2 = 0;
    std::vector<double> m1, m2;
    if (!extractNumericMatrix(a[0], ctx, r1, c1, m1) ||
        !extractNumericMatrix(a[1], ctx, r2, c2, m2) ||
        c1 != r2) {
        return formula::makeScalar(ErrorValue{ErrorType::Value});
    }
    formula::ArrayValue out(r1, c2);
    for (std::size_t i = 0; i < r1; ++i) {
        for (std::size_t j = 0; j < c2; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < c1; ++k) {
                s += m1[i * c1 + k] * m2[k * c2 + j];
            }
            out.at(i, j) = s;
        }
    }
    return formula::makeArray(std::move(out));
}

formula::EvalArg fnMUnit(const std::vector<formula::EvalArg>& a, const formula::EvalContext&) {
    auto dn = toNumber(a[0].scalar);
    if (!dn) return formula::makeScalar(ErrorValue{ErrorType::Value});
    long long n = static_cast<long long>(std::trunc(*dn));
    if (n <= 0 || n > 1024) return formula::makeScalar(ErrorValue{ErrorType::Value});
    std::size_t sz = static_cast<std::size_t>(n);
    formula::ArrayValue out(sz, sz, 0.0);
    for (std::size_t i = 0; i < sz; ++i) out.at(i, i) = 1.0;
    return formula::makeArray(std::move(out));
}

// ---- AVERAGEA / MAXA / MINA ----
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

CellValue fnAverageA(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::vector<double> vals;
    if (auto e = collectAllA(a, ctx, vals)) return *e;
    if (vals.empty()) return ErrorValue{ErrorType::DivZero};
    double s = std::accumulate(vals.begin(), vals.end(), 0.0);
    return s / static_cast<double>(vals.size());
}

CellValue fnMaxA(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::vector<double> vals;
    if (auto e = collectAllA(a, ctx, vals)) return *e;
    if (vals.empty()) return 0.0;
    return *std::max_element(vals.begin(), vals.end());
}

CellValue fnMinA(const std::vector<formula::EvalArg>& a, const formula::EvalContext& ctx) {
    std::vector<double> vals;
    if (auto e = collectAllA(a, ctx, vals)) return *e;
    if (vals.empty()) return 0.0;
    return *std::min_element(vals.begin(), vals.end());
}

// ---- MAXIFS / MINIFS ----
CellValue fnExtremumIfs(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx, bool isMax) {
    if (args.size() < 3 || (args.size() % 2) == 0) return ErrorValue{ErrorType::Value};
    auto target = formula::flatten(args[0], ctx);
    std::vector<std::vector<CellValue>> critRanges;
    std::vector<CellValue> crits;
    for (std::size_t i = 1; i + 1 < args.size(); i += 2) {
        auto cr = formula::flatten(args[i], ctx);
        if (cr.size() != target.size()) return ErrorValue{ErrorType::Value};
        critRanges.push_back(std::move(cr));
        crits.push_back(args[i + 1].scalar);
    }
    bool found = false;
    double best = isMax ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    for (std::size_t idx = 0; idx < target.size(); ++idx) {
        bool ok = true;
        for (std::size_t c = 0; c < critRanges.size(); ++c) {
            if (!matchesCriteria(critRanges[c][idx], crits[c])) { ok = false; break; }
        }
        if (ok && isNumber(target[idx])) {
            double v = std::get<double>(target[idx]);
            if (!found || (isMax ? (v > best) : (v < best))) {
                best = v;
                found = true;
            }
        }
    }
    return found ? best : 0.0;
}

// ---- AGGREGATE(function_num, options, ref1, [ref2 / k]) ----
CellValue fnAggregate(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto fnNumD = toNumber(args[0].scalar);
    auto optD = toNumber(args[1].scalar);
    if (!fnNumD || !optD) return ErrorValue{ErrorType::Value};
    int fnNum = static_cast<int>(std::trunc(*fnNumD));
    int opt = static_cast<int>(std::trunc(*optD));
    if (fnNum < 1 || fnNum > 19 || opt < 0 || opt > 7) return ErrorValue{ErrorType::Value};

    bool ignoreErrors = (opt == 2 || opt == 3 || opt == 6 || opt == 7);
    bool needsK = (fnNum >= 14 && fnNum <= 19);
    std::size_t dataEnd = needsK ? (args.size() - 1) : args.size();
    if (dataEnd <= 2) return ErrorValue{ErrorType::Value};

    double kVal = 0.0;
    if (needsK) {
        auto k = toNumber(args.back().scalar);
        if (!k) return ErrorValue{ErrorType::Value};
        kVal = *k;
    }

    std::vector<double> nums;
    std::size_t nonEmptyCount = 0;
    for (std::size_t i = 2; i < dataEnd; ++i) {
        for (const auto& v : formula::flatten(args[i], ctx)) {
            if (isError(v)) {
                if (ignoreErrors) continue;
                return v;
            }
            if (!isEmpty(v)) ++nonEmptyCount;
            if (isNumber(v)) nums.push_back(std::get<double>(v));
        }
    }

    auto meanOf = [&]() {
        return std::accumulate(nums.begin(), nums.end(), 0.0) / static_cast<double>(nums.size());
    };
    auto varOf = [&](bool sample) -> CellValue {
        if (nums.size() < (sample ? 2u : 1u)) return ErrorValue{ErrorType::DivZero};
        double m = meanOf();
        double ss = 0.0;
        for (double d : nums) { double diff = d - m; ss += diff * diff; }
        return ss / static_cast<double>(sample ? (nums.size() - 1) : nums.size());
    };
    auto percentileInc = [&](double p) -> CellValue {
        if (nums.empty() || p < 0.0 || p > 1.0) return ErrorValue{ErrorType::Num};
        std::sort(nums.begin(), nums.end());
        double pos = p * static_cast<double>(nums.size() - 1);
        std::size_t lo = static_cast<std::size_t>(std::floor(pos));
        std::size_t hi = std::min(lo + 1, nums.size() - 1);
        double frac = pos - static_cast<double>(lo);
        return nums[lo] + frac * (nums[hi] - nums[lo]);
    };
    auto percentileExc = [&](double p) -> CellValue {
        std::size_t n = nums.size();
        if (n == 0 || p <= 0.0 || p >= 1.0) return ErrorValue{ErrorType::Num};
        double pos = p * static_cast<double>(n + 1);
        if (pos < 1.0 || pos > static_cast<double>(n)) return ErrorValue{ErrorType::Num};
        std::sort(nums.begin(), nums.end());
        std::size_t lo = static_cast<std::size_t>(std::floor(pos)) - 1;
        std::size_t hi = std::min(lo + 1, n - 1);
        double frac = pos - std::floor(pos);
        return nums[lo] + frac * (nums[hi] - nums[lo]);
    };

    switch (fnNum) {
        case 1: return nums.empty() ? CellValue{ErrorValue{ErrorType::DivZero}} : CellValue{meanOf()};
        case 2: return static_cast<double>(nums.size());
        case 3: return static_cast<double>(nonEmptyCount);
        case 4: return nums.empty() ? 0.0 : *std::max_element(nums.begin(), nums.end());
        case 5: return nums.empty() ? 0.0 : *std::min_element(nums.begin(), nums.end());
        case 6: {
            if (nums.empty()) return 0.0;
            double p = 1.0;
            for (double d : nums) p *= d;
            return p;
        }
        case 7: { auto v = varOf(true); return isNumber(v) ? CellValue{std::sqrt(std::get<double>(v))} : v; }
        case 8: { auto v = varOf(false); return isNumber(v) ? CellValue{std::sqrt(std::get<double>(v))} : v; }
        case 9: return std::accumulate(nums.begin(), nums.end(), 0.0);
        case 10: return varOf(true);
        case 11: return varOf(false);
        case 12: return percentileInc(0.5);
        case 13: {
            if (nums.empty()) return ErrorValue{ErrorType::NA};
            std::unordered_map<double, std::size_t> freq;
            std::size_t bestCnt = 0;
            double bestVal = nums[0];
            for (double d : nums) {
                std::size_t c = ++freq[d];
                if (c > bestCnt) { bestCnt = c; bestVal = d; }
            }
            return bestCnt >= 2 ? CellValue{bestVal} : CellValue{ErrorValue{ErrorType::NA}};
        }
        case 14: {
            long long k = static_cast<long long>(std::trunc(kVal));
            if (k < 1 || static_cast<std::size_t>(k) > nums.size()) return ErrorValue{ErrorType::Num};
            std::sort(nums.begin(), nums.end(), std::greater<double>());
            return nums[static_cast<std::size_t>(k - 1)];
        }
        case 15: {
            long long k = static_cast<long long>(std::trunc(kVal));
            if (k < 1 || static_cast<std::size_t>(k) > nums.size()) return ErrorValue{ErrorType::Num};
            std::sort(nums.begin(), nums.end());
            return nums[static_cast<std::size_t>(k - 1)];
        }
        case 16: return percentileInc(kVal);
        case 17: {
            int q = static_cast<int>(std::trunc(kVal));
            if (q < 0 || q > 4) return ErrorValue{ErrorType::Num};
            return percentileInc(static_cast<double>(q) * 0.25);
        }
        case 18: return percentileExc(kVal);
        case 19: {
            int q = static_cast<int>(std::trunc(kVal));
            if (q < 1 || q > 3) return ErrorValue{ErrorType::Num};
            return percentileExc(static_cast<double>(q) * 0.25);
        }
        default: return ErrorValue{ErrorType::Value};
    }
}

} // namespace

void registerMathTrigExtFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"ACOT", 1, 1, fnAcot});
    registry.registerFunction({"ACOTH", 1, 1, fnAcoth});
    registry.registerFunction({"AGGREGATE", 3, 255, fnAggregate});
    registry.registerFunction({"CEILING.MATH", 1, 3, fnCeilingMath});
    registry.registerFunction({"CEILING.PRECISE", 1, 2, fnCeilingPrecise});
    registry.registerFunction({"ISO.CEILING", 1, 2, fnCeilingPrecise});
    registry.registerFunction({"FLOOR.MATH", 1, 3, fnFloorMath});
    registry.registerFunction({"FLOOR.PRECISE", 1, 2, fnFloorPrecise});
    registry.registerFunction({"SERIESSUM", 4, 4, fnSeriesSum});
    registry.registerFunction({"EUROCONVERT", 3, 5, fnEuroConvert});
    registry.registerFunction({"PERCENTOF", 2, 2, fnPercentOf});

    registry.registerFunction({"MDETERM", 1, 1, fnMDeterm});
    registry.registerArrayFunction("MINVERSE", 1, 1, fnMInverse);
    registry.registerArrayFunction("MMULT", 2, 2, fnMMult);
    registry.registerArrayFunction("MUNIT", 1, 1, fnMUnit);

    registry.registerFunction({"AVERAGEA", 1, 255, fnAverageA});
    registry.registerFunction({"MAXA", 1, 255, fnMaxA});
    registry.registerFunction({"MINA", 1, 255, fnMinA});
    registry.registerFunction({"MAXIFS", 3, 255, [](const auto& a, const auto& c) { return fnExtremumIfs(a, c, true); }});
    registry.registerFunction({"MINIFS", 3, 255, [](const auto& a, const auto& c) { return fnExtremumIfs(a, c, false); }});
}

} // namespace sheetcore::func_math_trig
