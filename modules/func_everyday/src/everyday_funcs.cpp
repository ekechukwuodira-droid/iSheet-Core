#include "sheetcore/func_everyday/register.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>

namespace sheetcore::func_everyday {

namespace {

bool matchesCriteria(const CellValue& val, const CellValue& crit) {
    if (isText(crit)) {
        const std::string& cs = std::get<std::string>(crit);
        if (cs.size() >= 2 && (cs[0] == '>' || cs[0] == '<')) {
            if (cs[0] == '<' && cs[1] == '>') {
                std::string_view rhs = std::string_view(cs).substr(2);
                if (auto n = toNumber(std::string(rhs)); n && isNumber(val)) {
                    return std::fabs(std::get<double>(val) - *n) > 1e-12;
                }
                return toDisplayString(val) != rhs;
            }
            bool isGt = cs[0] == '>';
            bool isEq = cs[1] == '=';
            std::string_view numStr = std::string_view(cs).substr(isEq ? 2 : 1);
            if (auto target = toNumber(std::string(numStr)); target && isNumber(val)) {
                double d = std::get<double>(val);
                return isGt ? (isEq ? d >= *target : d > *target)
                            : (isEq ? d <= *target : d < *target);
            }
        }
    }
    return val == crit;
}

std::vector<double> collectNumbers(const formula::EvalArg& arg, const formula::EvalContext& ctx) {
    std::vector<double> out;
    if (arg.isRange() && ctx.values) {
        for (const auto& v : ctx.values->getRangeValues(arg.rangeRef->sheetId, arg.rangeRef->range)) {
            if (isNumber(v)) out.push_back(std::get<double>(v));
        }
    } else if (arg.isArray()) {
        for (const auto& v : arg.array->elements) {
            if (isNumber(v)) out.push_back(std::get<double>(v));
        }
    } else if (auto n = toNumber(arg.scalar)) {
        out.push_back(*n);
    }
    return out;
}

double civilToSerial(long long y, int m, int d) {
    while (m > 12) { m -= 12; ++y; }
    while (m < 1)  { m += 12; --y; }
    y -= (m <= 2);
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153u * static_cast<unsigned>(m > 2 ? m - 3 : m + 9) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long daysSince1970 = era * 146097 + static_cast<long long>(doe) - 719468;
    return static_cast<double>(daysSince1970 + 25569LL);
}

void serialToCivil(double serial, int& year, int& month, int& day) {
    long long daysSince1970 = static_cast<long long>(std::floor(serial)) - 25569LL;
    long long z = daysSince1970 + 719468LL;
    long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    unsigned doe = static_cast<unsigned>(z - era * 146097LL);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = static_cast<long long>(yoe) + era * 400LL;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    year = static_cast<int>(y);
    month = static_cast<int>(m);
    day = static_cast<int>(d);
}

int daysInMonth(int y, int m) {
    static const int kDays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) return 29;
    return kDays[std::clamp(m - 1, 0, 11)];
}

// 1=Sun .. 7=Sat (Excel default return_type=1)
int weekdayFromSerial(double serial) {
    // 1899-12-30 was Saturday (7), so serial 1 (1899-12-31) is Sunday (1)
    long long d = static_cast<long long>(std::floor(serial));
    int wd = static_cast<int>(((d - 1) % 7 + 7) % 7) + 1;
    return wd;
}

// ---- Logic & Lookup ----
CellValue fnIfs(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (args.size() < 2 || (args.size() % 2) != 0) return ErrorValue{ErrorType::Value};
    for (std::size_t i = 0; i < args.size(); i += 2) {
        if (isError(args[i].scalar)) return args[i].scalar;
        auto b = toBool(args[i].scalar);
        if (!b) return ErrorValue{ErrorType::Value};
        if (*b) return args[i + 1].scalar;
    }
    return ErrorValue{ErrorType::NA};
}

CellValue fnIfNa(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar) && std::get<ErrorValue>(args[0].scalar).type == ErrorType::NA) {
        return args[1].scalar;
    }
    return args[0].scalar;
}

CellValue fnSwitch(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (args.size() < 3) return ErrorValue{ErrorType::Value};
    const CellValue& expr = args[0].scalar;
    std::size_t i = 1;
    while (i + 1 < args.size()) {
        if (expr == args[i].scalar) return args[i + 1].scalar;
        i += 2;
    }
    if (i < args.size()) return args[i].scalar; // default
    return ErrorValue{ErrorType::NA};
}

CellValue fnSumProduct(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::vector<std::vector<double>> vecs;
    std::size_t len = 0;
    for (const auto& a : args) {
        std::vector<double> v;
        if (a.isRange() && ctx.values) {
            for (const auto& cell : ctx.values->getRangeValues(a.rangeRef->sheetId, a.rangeRef->range)) {
                v.push_back(toNumber(cell).value_or(0.0));
            }
        } else if (a.isArray()) {
            for (const auto& cell : a.array->elements) v.push_back(toNumber(cell).value_or(0.0));
        } else {
            v.push_back(toNumber(a.scalar).value_or(0.0));
        }
        if (vecs.empty()) len = v.size();
        else if (v.size() != len) return ErrorValue{ErrorType::Value};
        vecs.push_back(std::move(v));
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < len; ++i) {
        double prod = 1.0;
        for (const auto& v : vecs) prod *= v[i];
        sum += prod;
    }
    return sum;
}

CellValue fnRow(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (args.empty()) return static_cast<double>(ctx.currentCell.row + 1);
    if (args[0].isRange()) return static_cast<double>(args[0].rangeRef->range.normalized().start.row + 1);
    return static_cast<double>(ctx.currentCell.row + 1);
}

CellValue fnColumn(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (args.empty()) return static_cast<double>(ctx.currentCell.col + 1);
    if (args[0].isRange()) return static_cast<double>(args[0].rangeRef->range.normalized().start.col + 1);
    return static_cast<double>(ctx.currentCell.col + 1);
}

CellValue fnRows(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (args[0].isRange()) return static_cast<double>(args[0].rangeRef->range.rowCount());
    if (args[0].isArray()) return static_cast<double>(args[0].array->rows);
    return 1.0;
}

CellValue fnColumns(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (args[0].isRange()) return static_cast<double>(args[0].rangeRef->range.colCount());
    if (args[0].isArray()) return static_cast<double>(args[0].array->cols);
    return 1.0;
}

CellValue fnAverageIf(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[0].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    auto critVals = ctx.values->getRangeValues(args[0].rangeRef->sheetId, args[0].rangeRef->range);
    auto avgVals = (args.size() >= 3 && args[2].isRange())
        ? ctx.values->getRangeValues(args[2].rangeRef->sheetId, args[2].rangeRef->range)
        : critVals;
    std::size_t n = std::min(critVals.size(), avgVals.size());
    double sum = 0.0;
    std::size_t cnt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (matchesCriteria(critVals[i], args[1].scalar) && isNumber(avgVals[i])) {
            sum += std::get<double>(avgVals[i]);
            ++cnt;
        }
    }
    return cnt > 0 ? CellValue{sum / static_cast<double>(cnt)} : CellValue{ErrorValue{ErrorType::DivZero}};
}

CellValue fnAverageIfs(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (args.size() < 3 || (args.size() % 2) == 0 || !args[0].isRange() || !ctx.values) {
        return ErrorValue{ErrorType::Value};
    }
    auto avgVals = ctx.values->getRangeValues(args[0].rangeRef->sheetId, args[0].rangeRef->range);
    std::vector<std::vector<CellValue>> critRanges;
    std::vector<CellValue> crits;
    for (std::size_t i = 1; i < args.size(); i += 2) {
        if (!args[i].isRange()) return ErrorValue{ErrorType::Value};
        critRanges.push_back(ctx.values->getRangeValues(args[i].rangeRef->sheetId, args[i].rangeRef->range));
        crits.push_back(args[i + 1].scalar);
    }
    double sum = 0.0;
    std::size_t cnt = 0;
    for (std::size_t r = 0; r < avgVals.size(); ++r) {
        bool ok = true;
        for (std::size_t k = 0; k < critRanges.size(); ++k) {
            if (r >= critRanges[k].size() || !matchesCriteria(critRanges[k][r], crits[k])) { ok = false; break; }
        }
        if (ok && isNumber(avgVals[r])) { sum += std::get<double>(avgVals[r]); ++cnt; }
    }
    return cnt > 0 ? CellValue{sum / static_cast<double>(cnt)} : CellValue{ErrorValue{ErrorType::DivZero}};
}

// ---- Math & Stats ----
CellValue fnRoundUp(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar), d = toNumber(args[1].scalar);
    if (!n || !d) return ErrorValue{ErrorType::Value};
    double f = std::pow(10.0, std::trunc(*d));
    return (*n >= 0.0 ? std::ceil(*n * f) : std::floor(*n * f)) / f;
}

CellValue fnRoundDown(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar), d = toNumber(args[1].scalar);
    if (!n || !d) return ErrorValue{ErrorType::Value};
    double f = std::pow(10.0, std::trunc(*d));
    return std::trunc(*n * f) / f;
}

CellValue fnInt(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    return n ? CellValue{std::floor(*n)} : CellValue{ErrorValue{ErrorType::Value}};
}

CellValue fnMod(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto a = toNumber(args[0].scalar), b = toNumber(args[1].scalar);
    if (!a || !b) return ErrorValue{ErrorType::Value};
    if (*b == 0.0) return ErrorValue{ErrorType::DivZero};
    return *a - (*b) * std::floor(*a / *b);
}

CellValue fnCeiling(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar), sig = toNumber(args[1].scalar);
    if (!n || !sig || *sig == 0.0) return ErrorValue{ErrorType::Num};
    return std::ceil(*n / *sig) * (*sig);
}

CellValue fnFloor(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar), sig = toNumber(args[1].scalar);
    if (!n || !sig || *sig == 0.0) return ErrorValue{ErrorType::Num};
    return std::floor(*n / *sig) * (*sig);
}

CellValue fnSign(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    if (!n) return ErrorValue{ErrorType::Value};
    return (*n > 0.0) ? 1.0 : ((*n < 0.0) ? -1.0 : 0.0);
}

CellValue fnRand(const std::vector<formula::EvalArg>&, const formula::EvalContext&) {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

CellValue fnRandBetween(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto lo = toNumber(args[0].scalar), hi = toNumber(args[1].scalar);
    if (!lo || !hi || *lo > *hi) return ErrorValue{ErrorType::Num};
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<long long> dist(static_cast<long long>(std::ceil(*lo)),
                                                  static_cast<long long>(std::floor(*hi)));
    return static_cast<double>(dist(rng));
}

CellValue fnLarge(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args[0], ctx);
    auto k = toNumber(args[1].scalar);
    if (!k || *k < 1 || static_cast<std::size_t>(*k) > nums.size()) return ErrorValue{ErrorType::Num};
    std::sort(nums.begin(), nums.end(), std::greater<double>());
    return nums[static_cast<std::size_t>(*k) - 1];
}

CellValue fnSmall(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args[0], ctx);
    auto k = toNumber(args[1].scalar);
    if (!k || *k < 1 || static_cast<std::size_t>(*k) > nums.size()) return ErrorValue{ErrorType::Num};
    std::sort(nums.begin(), nums.end());
    return nums[static_cast<std::size_t>(*k) - 1];
}

CellValue fnRank(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto val = toNumber(args[0].scalar);
    if (!val) return ErrorValue{ErrorType::Value};
    auto nums = collectNumbers(args[1], ctx);
    bool ascending = args.size() >= 3 ? (toNumber(args[2].scalar).value_or(0.0) != 0.0) : false;
    std::size_t rank = 1;
    bool found = false;
    for (double x : nums) {
        if (std::fabs(x - *val) <= 1e-12) found = true;
        else if (ascending ? (x < *val) : (x > *val)) ++rank;
    }
    return found ? CellValue{static_cast<double>(rank)} : CellValue{ErrorValue{ErrorType::NA}};
}

CellValue fnPercentile(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto nums = collectNumbers(args[0], ctx);
    auto k = toNumber(args[1].scalar);
    if (nums.empty() || !k || *k < 0.0 || *k > 1.0) return ErrorValue{ErrorType::Num};
    std::sort(nums.begin(), nums.end());
    double pos = (*k) * static_cast<double>(nums.size() - 1);
    std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    std::size_t hi = std::min(lo + 1, nums.size() - 1);
    double frac = pos - static_cast<double>(lo);
    return nums[lo] + frac * (nums[hi] - nums[lo]);
}

CellValue fnQuartile(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto q = toNumber(args[1].scalar);
    if (!q || *q < 0.0 || *q > 4.0) return ErrorValue{ErrorType::Num};
    std::vector<formula::EvalArg> pArgs{args[0], formula::EvalArg{(*q) * 0.25, std::nullopt, std::nullopt}};
    return fnPercentile(pArgs, ctx);
}

CellValue fnCorrel(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto x = collectNumbers(args[0], ctx);
    auto y = collectNumbers(args[1], ctx);
    if (x.size() != y.size() || x.size() < 2) return ErrorValue{ErrorType::DivZero};
    double mx = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
    double my = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(y.size());
    double num = 0.0, dx2 = 0.0, dy2 = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        double dx = x[i] - mx, dy = y[i] - my;
        num += dx * dy;
        dx2 += dx * dx;
        dy2 += dy * dy;
    }
    double den = std::sqrt(dx2 * dy2);
    return den == 0.0 ? CellValue{ErrorValue{ErrorType::DivZero}} : CellValue{num / den};
}

// ---- Text ----
CellValue fnTextJoin(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::string delim = toDisplayString(args[0].scalar);
    bool ignoreEmpty = toBool(args[1].scalar).value_or(true);
    std::string out;
    bool first = true;
    auto appendVal = [&](const CellValue& v) {
        std::string s = toDisplayString(v);
        if (ignoreEmpty && s.empty()) return;
        if (!first) out += delim;
        out += s;
        first = false;
    };
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i].isRange() && ctx.values) {
            for (const auto& v : ctx.values->getRangeValues(args[i].rangeRef->sheetId, args[i].rangeRef->range)) {
                appendVal(v);
            }
        } else if (args[i].isArray()) {
            for (const auto& v : args[i].array->elements) appendVal(v);
        } else {
            appendVal(args[i].scalar);
        }
    }
    return out;
}

CellValue fnFindOrSearch(const std::vector<formula::EvalArg>& args, bool caseSensitive) {
    std::string needle = toDisplayString(args[0].scalar);
    std::string hay = toDisplayString(args[1].scalar);
    std::size_t start0 = 0;
    if (args.size() >= 3) {
        auto s = toNumber(args[2].scalar);
        if (!s || *s < 1) return ErrorValue{ErrorType::Value};
        start0 = static_cast<std::size_t>(*s) - 1;
    }
    if (start0 > hay.size()) return ErrorValue{ErrorType::Value};
    if (!caseSensitive) {
        for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : hay) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    auto pos = hay.find(needle, start0);
    if (pos == std::string::npos) return ErrorValue{ErrorType::Value};
    return static_cast<double>(pos + 1);
}

CellValue fnSubstitute(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    std::string text = toDisplayString(args[0].scalar);
    std::string oldT = toDisplayString(args[1].scalar);
    std::string newT = toDisplayString(args[2].scalar);
    if (oldT.empty()) return text;
    int targetOcc = args.size() >= 4 ? static_cast<int>(toNumber(args[3].scalar).value_or(0.0)) : 0;
    std::string out;
    std::size_t pos = 0;
    int occ = 0;
    while (true) {
        auto f = text.find(oldT, pos);
        if (f == std::string::npos) { out.append(text, pos, text.size() - pos); break; }
        ++occ;
        out.append(text, pos, f - pos);
        if (targetOcc == 0 || occ == targetOcc) out.append(newT);
        else out.append(oldT);
        pos = f + oldT.size();
    }
    return out;
}

CellValue fnReplace(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    std::string text = toDisplayString(args[0].scalar);
    auto startOpt = toNumber(args[1].scalar), lenOpt = toNumber(args[2].scalar);
    if (!startOpt || !lenOpt || *startOpt < 1 || *lenOpt < 0) return ErrorValue{ErrorType::Value};
    std::string newT = toDisplayString(args[3].scalar);
    std::size_t s0 = std::min(static_cast<std::size_t>(*startOpt) - 1, text.size());
    std::size_t count = std::min(static_cast<std::size_t>(*lenOpt), text.size() - s0);
    text.replace(s0, count, newT);
    return text;
}

CellValue fnValue(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto n = toNumber(args[0].scalar);
    return n ? CellValue{*n} : CellValue{ErrorValue{ErrorType::Value}};
}

CellValue fnProper(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    std::string s = toDisplayString(args[0].scalar);
    bool newWord = true;
    for (char& ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isalpha(uc)) {
            ch = static_cast<char>(newWord ? std::toupper(uc) : std::tolower(uc));
            newWord = false;
        } else {
            newWord = true;
        }
    }
    return s;
}

CellValue fnRept(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    std::string s = toDisplayString(args[0].scalar);
    auto n = toNumber(args[1].scalar);
    if (!n || *n < 0) return ErrorValue{ErrorType::Value};
    std::size_t count = static_cast<std::size_t>(*n);
    std::string out;
    out.reserve(s.size() * count);
    for (std::size_t i = 0; i < count; ++i) out += s;
    return out;
}

// ---- Date & Time ----
CellValue fnEdate(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto ser = toNumber(args[0].scalar), months = toNumber(args[1].scalar);
    if (!ser || !months) return ErrorValue{ErrorType::Value};
    int y = 0, m = 0, d = 0;
    serialToCivil(*ser, y, m, d);
    int totalM = (y * 12 + (m - 1)) + static_cast<int>(std::trunc(*months));
    int ny = totalM / 12;
    int nm = (totalM % 12) + 1;
    if (nm <= 0) { nm += 12; --ny; }
    int nd = std::min(d, daysInMonth(ny, nm));
    return DateTimeValue{civilToSerial(ny, nm, nd)};
}

CellValue fnEoMonth(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto ser = toNumber(args[0].scalar), months = toNumber(args[1].scalar);
    if (!ser || !months) return ErrorValue{ErrorType::Value};
    int y = 0, m = 0, d = 0;
    serialToCivil(*ser, y, m, d);
    int totalM = (y * 12 + (m - 1)) + static_cast<int>(std::trunc(*months));
    int ny = totalM / 12;
    int nm = (totalM % 12) + 1;
    if (nm <= 0) { nm += 12; --ny; }
    return DateTimeValue{civilToSerial(ny, nm, daysInMonth(ny, nm))};
}

CellValue fnDateDif(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto s1 = toNumber(args[0].scalar), s2 = toNumber(args[1].scalar);
    if (!s1 || !s2 || *s1 > *s2) return ErrorValue{ErrorType::Num};
    std::string unit = toDisplayString(args[2].scalar);
    for (char& c : unit) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (unit == "D") return std::floor(*s2) - std::floor(*s1);
    int y1, m1, d1, y2, m2, d2;
    serialToCivil(*s1, y1, m1, d1);
    serialToCivil(*s2, y2, m2, d2);
    if (unit == "Y") {
        int dy = y2 - y1;
        if (m2 < m1 || (m2 == m1 && d2 < d1)) --dy;
        return static_cast<double>(dy);
    }
    if (unit == "M") {
        int dm = (y2 - y1) * 12 + (m2 - m1);
        if (d2 < d1) --dm;
        return static_cast<double>(dm);
    }
    return ErrorValue{ErrorType::Num};
}

CellValue fnNetworkDays(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto s1 = toNumber(args[0].scalar), s2 = toNumber(args[1].scalar);
    if (!s1 || !s2) return ErrorValue{ErrorType::Value};
    long long start = static_cast<long long>(std::floor(*s1));
    long long end = static_cast<long long>(std::floor(*s2));
    int sign = start <= end ? 1 : -1;
    if (start > end) std::swap(start, end);
    long long count = 0;
    for (long long d = start; d <= end; ++d) {
        int wd = weekdayFromSerial(static_cast<double>(d));
        if (wd != 1 && wd != 7) ++count;
    }
    return static_cast<double>(count * sign);
}

CellValue fnWorkday(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto s = toNumber(args[0].scalar), days = toNumber(args[1].scalar);
    if (!s || !days) return ErrorValue{ErrorType::Value};
    long long cur = static_cast<long long>(std::floor(*s));
    long long rem = static_cast<long long>(std::trunc(*days));
    int step = rem >= 0 ? 1 : -1;
    rem = std::llabs(rem);
    while (rem > 0) {
        cur += step;
        int wd = weekdayFromSerial(static_cast<double>(cur));
        if (wd != 1 && wd != 7) --rem;
    }
    return DateTimeValue{static_cast<double>(cur)};
}

CellValue fnWeekday(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto s = toNumber(args[0].scalar);
    if (!s) return ErrorValue{ErrorType::Value};
    return static_cast<double>(weekdayFromSerial(*s));
}

CellValue fnTimePart(const std::vector<formula::EvalArg>& args, int part /*0=H,1=M,2=S*/) {
    auto s = toNumber(args[0].scalar);
    if (!s) return ErrorValue{ErrorType::Value};
    double frac = *s - std::floor(*s);
    if (frac < 0.0) frac += 1.0;
    int totalSec = static_cast<int>(std::round(frac * 86400.0)) % 86400;
    if (part == 0) return static_cast<double>(totalSec / 3600);
    if (part == 1) return static_cast<double>((totalSec % 3600) / 60);
    return static_cast<double>(totalSec % 60);
}

} // namespace

void registerEverydayFunctions(formula::FunctionRegistry& registry) {
    // Logic & Lookup (10)
    registry.registerFunction({"IFS", 2, 255, fnIfs});
    registry.registerFunction({"IFNA", 2, 2, fnIfNa});
    registry.registerFunction({"SWITCH", 3, 255, fnSwitch});
    registry.registerFunction({"SUMPRODUCT", 1, 255, fnSumProduct});
    registry.registerFunction({"ROW", 0, 1, fnRow});
    registry.registerFunction({"COLUMN", 0, 1, fnColumn});
    registry.registerFunction({"ROWS", 1, 1, fnRows});
    registry.registerFunction({"COLUMNS", 1, 1, fnColumns});
    registry.registerFunction({"AVERAGEIF", 2, 3, fnAverageIf});
    registry.registerFunction({"AVERAGEIFS", 3, 255, fnAverageIfs});

    // Math & Stats (15)
    registry.registerFunction({"ROUNDUP", 2, 2, fnRoundUp});
    registry.registerFunction({"ROUNDDOWN", 2, 2, fnRoundDown});
    registry.registerFunction({"INT", 1, 1, fnInt});
    registry.registerFunction({"MOD", 2, 2, fnMod});
    registry.registerFunction({"CEILING", 2, 2, fnCeiling});
    registry.registerFunction({"FLOOR", 2, 2, fnFloor});
    registry.registerFunction({"SIGN", 1, 1, fnSign});
    registry.registerFunction({"RAND", 0, 0, fnRand});
    registry.registerFunction({"RANDBETWEEN", 2, 2, fnRandBetween});
    registry.registerFunction({"LARGE", 2, 2, fnLarge});
    registry.registerFunction({"SMALL", 2, 2, fnSmall});
    registry.registerFunction({"RANK", 2, 3, fnRank});
    registry.registerFunction({"PERCENTILE", 2, 2, fnPercentile});
    registry.registerFunction({"QUARTILE", 2, 2, fnQuartile});
    registry.registerFunction({"CORREL", 2, 2, fnCorrel});

    // Text (8)
    registry.registerFunction({"TEXTJOIN", 3, 255, fnTextJoin});
    registry.registerFunction({"FIND", 2, 3, [](const auto& a, const auto&) { return fnFindOrSearch(a, true); }});
    registry.registerFunction({"SEARCH", 2, 3, [](const auto& a, const auto&) { return fnFindOrSearch(a, false); }});
    registry.registerFunction({"SUBSTITUTE", 3, 4, fnSubstitute});
    registry.registerFunction({"REPLACE", 4, 4, fnReplace});
    registry.registerFunction({"VALUE", 1, 1, fnValue});
    registry.registerFunction({"PROPER", 1, 1, fnProper});
    registry.registerFunction({"REPT", 2, 2, fnRept});

    // Date & Time (9)
    registry.registerFunction({"EDATE", 2, 2, fnEdate});
    registry.registerFunction({"EOMONTH", 2, 2, fnEoMonth});
    registry.registerFunction({"DATEDIF", 3, 3, fnDateDif});
    registry.registerFunction({"NETWORKDAYS", 2, 3, fnNetworkDays});
    registry.registerFunction({"WORKDAY", 2, 3, fnWorkday});
    registry.registerFunction({"WEEKDAY", 1, 2, fnWeekday});
    registry.registerFunction({"HOUR", 1, 1, [](const auto& a, const auto&) { return fnTimePart(a, 0); }});
    registry.registerFunction({"MINUTE", 1, 1, [](const auto& a, const auto&) { return fnTimePart(a, 1); }});
    registry.registerFunction({"SECOND", 1, 1, [](const auto& a, const auto&) { return fnTimePart(a, 2); }});
}

} // namespace sheetcore::func_everyday
