#include "sheetcore/func_advanced/register.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace sheetcore::func_advanced {

namespace {

bool matchesCriteria(const CellValue& val, const CellValue& crit) {
    if (isText(crit)) {
        const std::string& cs = std::get<std::string>(crit);
        if (cs.size() >= 2 && (cs[0] == '>' || cs[0] == '<')) {
            bool isGt = cs[0] == '>';
            bool isEq = cs[1] == '=';
            if (cs.size() >= 2 && cs[0] == '<' && cs[1] == '>') {
                std::string_view rhs = std::string_view(cs).substr(2);
                if (auto n = toNumber(std::string(rhs)); n && isNumber(val)) {
                    return std::fabs(std::get<double>(val) - *n) > 1e-12;
                }
                return toDisplayString(val) != rhs;
            }
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

CellValue fnTrim(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    std::string s = toDisplayString(args[0].scalar);
    std::string out;
    bool inSpace = true;
    for (char ch : s) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!inSpace) {
                out.push_back(' ');
                inSpace = true;
            }
        } else {
            out.push_back(ch);
            inSpace = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

CellValue fnText(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    auto num = toNumber(args[0].scalar);
    if (!num) return toDisplayString(args[0].scalar);
    std::string fmt = toDisplayString(args[1].scalar);
    bool isPct = fmt.find('%') != std::string::npos;
    double v = isPct ? (*num * 100.0) : *num;
    int decimals = 0;
    if (auto dot = fmt.find('.'); dot != std::string::npos) {
        for (std::size_t i = dot + 1; i < fmt.size(); ++i) {
            if (fmt[i] == '0' || fmt[i] == '#') ++decimals;
        }
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << v;
    if (isPct) oss << '%';
    return oss.str();
}

CellValue fnSumIf(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[0].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    auto critVals = ctx.values->getRangeValues(args[0].rangeRef->sheetId, args[0].rangeRef->range);
    auto sumVals = (args.size() >= 3 && args[2].isRange())
        ? ctx.values->getRangeValues(args[2].rangeRef->sheetId, args[2].rangeRef->range)
        : critVals;
    std::size_t n = std::min(critVals.size(), sumVals.size());
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (matchesCriteria(critVals[i], args[1].scalar) && isNumber(sumVals[i])) {
            total += std::get<double>(sumVals[i]);
        }
    }
    return total;
}

CellValue fnSumIfs(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (args.size() < 3 || (args.size() % 2) == 0 || !args[0].isRange() || !ctx.values) {
        return ErrorValue{ErrorType::Value};
    }
    auto sumVals = ctx.values->getRangeValues(args[0].rangeRef->sheetId, args[0].rangeRef->range);
    std::vector<std::vector<CellValue>> critRanges;
    std::vector<CellValue> crits;
    for (std::size_t i = 1; i < args.size(); i += 2) {
        if (!args[i].isRange()) return ErrorValue{ErrorType::Value};
        critRanges.push_back(ctx.values->getRangeValues(args[i].rangeRef->sheetId, args[i].rangeRef->range));
        crits.push_back(args[i + 1].scalar);
    }
    double total = 0.0;
    for (std::size_t r = 0; r < sumVals.size(); ++r) {
        bool ok = true;
        for (std::size_t k = 0; k < critRanges.size(); ++k) {
            if (r >= critRanges[k].size() || !matchesCriteria(critRanges[k][r], crits[k])) {
                ok = false;
                break;
            }
        }
        if (ok && isNumber(sumVals[r])) total += std::get<double>(sumVals[r]);
    }
    return total;
}

CellValue fnCountIfs(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (args.size() < 2 || (args.size() % 2) != 0 || !ctx.values) {
        return ErrorValue{ErrorType::Value};
    }
    std::vector<std::vector<CellValue>> critRanges;
    std::vector<CellValue> crits;
    for (std::size_t i = 0; i < args.size(); i += 2) {
        if (!args[i].isRange()) return ErrorValue{ErrorType::Value};
        critRanges.push_back(ctx.values->getRangeValues(args[i].rangeRef->sheetId, args[i].rangeRef->range));
        crits.push_back(args[i + 1].scalar);
    }
    std::size_t n = critRanges.empty() ? 0 : critRanges[0].size();
    std::size_t count = 0;
    for (std::size_t r = 0; r < n; ++r) {
        bool ok = true;
        for (std::size_t k = 0; k < critRanges.size(); ++k) {
            if (r >= critRanges[k].size() || !matchesCriteria(critRanges[k][r], crits[k])) {
                ok = false;
                break;
            }
        }
        if (ok) ++count;
    }
    return static_cast<double>(count);
}

// RATE(nper, pmt, pv, [fv=0], [type=0], [guess=0.1])
CellValue fnRate(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto nper = toNumber(args[0].scalar);
    auto pmt = toNumber(args[1].scalar);
    auto pv = toNumber(args[2].scalar);
    double fv = args.size() >= 4 ? toNumber(args[3].scalar).value_or(0.0) : 0.0;
    double type = args.size() >= 5 ? toNumber(args[4].scalar).value_or(0.0) : 0.0;
    double r = args.size() >= 6 ? toNumber(args[5].scalar).value_or(0.1) : 0.1;
    if (!nper || !pmt || !pv || *nper <= 0.0) return ErrorValue{ErrorType::Num};

    auto f = [&](double rate) {
        if (std::fabs(rate) < 1e-12) return *pv + (*pmt) * (*nper) + fv;
        double fac = std::pow(1.0 + rate, *nper);
        return (*pv) * fac + (*pmt) * (1.0 + rate * type) * ((fac - 1.0) / rate) + fv;
    };
    for (int iter = 0; iter < 100; ++iter) {
        double y = f(r);
        if (std::fabs(y) < 1e-9) return r;
        double eps = 1e-6;
        double dy = (f(r + eps) - f(r - eps)) / (2.0 * eps);
        if (std::fabs(dy) < 1e-12) return ErrorValue{ErrorType::Num};
        r -= y / dy;
        if (r <= -1.0) return ErrorValue{ErrorType::Num};
    }
    return ErrorValue{ErrorType::Num};
}

// Engineering: CONVERT(number, from_unit, to_unit)
CellValue fnConvert(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto num = toNumber(args[0].scalar);
    if (!num) return ErrorValue{ErrorType::Value};
    std::string from = toDisplayString(args[1].scalar);
    std::string to = toDisplayString(args[2].scalar);

    // Temperature
    if (from == "C" && to == "F") return (*num) * 9.0 / 5.0 + 32.0;
    if (from == "F" && to == "C") return ((*num) - 32.0) * 5.0 / 9.0;
    if (from == "C" && to == "K") return (*num) + 273.15;
    if (from == "K" && to == "C") return (*num) - 273.15;

    // Length (meters)
    auto lengthToMeters = [](const std::string& u) -> std::optional<double> {
        if (u == "m") return 1.0;
        if (u == "km") return 1000.0;
        if (u == "cm") return 0.01;
        if (u == "mm") return 0.001;
        if (u == "in") return 0.0254;
        if (u == "ft") return 0.3048;
        if (u == "mi") return 1609.344;
        return std::nullopt;
    };
    if (auto fm = lengthToMeters(from), tm = lengthToMeters(to); fm && tm) {
        return (*num) * (*fm) / (*tm);
    }

    // Mass (grams)
    auto massToGrams = [](const std::string& u) -> std::optional<double> {
        if (u == "g") return 1.0;
        if (u == "kg") return 1000.0;
        if (u == "lbm") return 453.59237;
        if (u == "ozm") return 28.349523125;
        return std::nullopt;
    };
    if (auto fm = massToGrams(from), tm = massToGrams(to); fm && tm) {
        return (*num) * (*fm) / (*tm);
    }
    return ErrorValue{ErrorType::NA};
}

// Engineering: COMPLEX(real_num, i_num, [suffix="i"])
CellValue fnComplex(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto re = toNumber(args[0].scalar);
    auto im = toNumber(args[1].scalar);
    if (!re || !im) return ErrorValue{ErrorType::Value};
    std::string suffix = args.size() >= 3 ? toDisplayString(args[2].scalar) : "i";
    if (suffix != "i" && suffix != "j") return ErrorValue{ErrorType::Value};
    std::ostringstream oss;
    if (*im == 0.0) {
        oss << *re;
    } else if (*re == 0.0) {
        oss << *im << suffix;
    } else {
        oss << *re << (*im >= 0.0 ? "+" : "") << *im << suffix;
    }
    return oss.str();
}

// Database helpers: DSUM / DCOUNT / DCOUNTA / DAVERAGE / DGET / DMAX / DMIN / DPRODUCT / DSTDEV / DSTDEVP / DVAR / DVARP
enum class DbMode {
    Sum, Count, CountA, Average, Get, Max, Min, Product, Stdev, StdevP, Var, VarP
};

CellValue evalDatabaseFn(const std::vector<formula::EvalArg>& args,
                         const formula::EvalContext& ctx,
                         DbMode mode) {
    if (!args[0].isRange() || !args[2].isRange() || !ctx.values) return ErrorValue{ErrorType::Value};
    SheetId dbSid = args[0].rangeRef->sheetId;
    RangeAddress dbRng = args[0].rangeRef->range.normalized();
    SheetId crSid = args[2].rangeRef->sheetId;
    RangeAddress crRng = args[2].rangeRef->range.normalized();
    if (dbRng.rowCount() < 2 || crRng.rowCount() < 2) return ErrorValue{ErrorType::Value};

    // Map DB header name -> col offset
    std::vector<std::string> dbHeaders;
    for (ColIndex c = dbRng.start.col; c <= dbRng.end.col; ++c) {
        dbHeaders.push_back(toDisplayString(ctx.values->getCellValue(dbSid, CellCoord{dbRng.start.row, c})));
    }

    int targetCol = -1;
    bool fieldOmitted = args[1].isOmitted() || isEmpty(args[1].scalar);
    if (!fieldOmitted) {
        if (isNumber(args[1].scalar)) {
            int idx1 = static_cast<int>(std::get<double>(args[1].scalar));
            if (idx1 >= 1 && idx1 <= static_cast<int>(dbHeaders.size())) targetCol = idx1 - 1;
        } else {
            std::string fieldName = toDisplayString(args[1].scalar);
            for (std::size_t i = 0; i < dbHeaders.size(); ++i) {
                if (dbHeaders[i] == fieldName) { targetCol = static_cast<int>(i); break; }
            }
        }
        if (targetCol < 0) return ErrorValue{ErrorType::Value};
    } else if (mode != DbMode::Count && mode != DbMode::CountA) {
        return ErrorValue{ErrorType::Value};
    }

    // Criteria columns -> DB col offsets
    std::vector<int> critColMap;
    for (ColIndex c = crRng.start.col; c <= crRng.end.col; ++c) {
        std::string h = toDisplayString(ctx.values->getCellValue(crSid, CellCoord{crRng.start.row, c}));
        int mapped = -1;
        for (std::size_t i = 0; i < dbHeaders.size(); ++i) {
            if (dbHeaders[i] == h) { mapped = static_cast<int>(i); break; }
        }
        critColMap.push_back(mapped);
    }

    std::vector<double> nums;
    std::vector<CellValue> allMatched;
    for (RowIndex r = dbRng.start.row + 1; r <= dbRng.end.row; ++r) {
        bool rowMatched = false;
        for (RowIndex cr = crRng.start.row + 1; cr <= crRng.end.row; ++cr) {
            bool allInCritRow = true;
            for (std::size_t cc = 0; cc < critColMap.size(); ++cc) {
                CellValue critVal = ctx.values->getCellValue(crSid, CellCoord{cr, crRng.start.col + static_cast<ColIndex>(cc)});
                if (isEmpty(critVal)) continue;
                if (critColMap[cc] < 0) { allInCritRow = false; break; }
                CellValue dbVal = ctx.values->getCellValue(dbSid, CellCoord{r, dbRng.start.col + static_cast<ColIndex>(critColMap[cc])});
                if (!matchesCriteria(dbVal, critVal)) { allInCritRow = false; break; }
            }
            if (allInCritRow) { rowMatched = true; break; }
        }
        if (rowMatched) {
            if (fieldOmitted) {
                allMatched.push_back(1.0);
                nums.push_back(1.0);
            } else {
                CellValue v = ctx.values->getCellValue(dbSid, CellCoord{r, dbRng.start.col + static_cast<ColIndex>(targetCol)});
                if (!isEmpty(v)) allMatched.push_back(v);
                if (isNumber(v)) nums.push_back(std::get<double>(v));
            }
        }
    }

    switch (mode) {
        case DbMode::Sum: {
            double s = 0.0;
            for (double d : nums) s += d;
            return s;
        }
        case DbMode::Count:
            return static_cast<double>(nums.size());
        case DbMode::CountA:
            return static_cast<double>(allMatched.size());
        case DbMode::Average: {
            if (nums.empty()) return ErrorValue{ErrorType::DivZero};
            double s = 0.0;
            for (double d : nums) s += d;
            return s / static_cast<double>(nums.size());
        }
        case DbMode::Get: {
            if (allMatched.empty()) return ErrorValue{ErrorType::Value};
            if (allMatched.size() > 1) return ErrorValue{ErrorType::Num};
            return allMatched.front();
        }
        case DbMode::Max: {
            if (nums.empty()) return 0.0;
            return *std::max_element(nums.begin(), nums.end());
        }
        case DbMode::Min: {
            if (nums.empty()) return 0.0;
            return *std::min_element(nums.begin(), nums.end());
        }
        case DbMode::Product: {
            if (nums.empty()) return 0.0;
            double p = 1.0;
            for (double d : nums) p *= d;
            return p;
        }
        case DbMode::Var:
        case DbMode::VarP:
        case DbMode::Stdev:
        case DbMode::StdevP: {
            bool sample = (mode == DbMode::Var || mode == DbMode::Stdev);
            if (nums.size() < (sample ? 2u : 1u)) return ErrorValue{ErrorType::DivZero};
            double mean = 0.0;
            for (double d : nums) mean += d;
            mean /= static_cast<double>(nums.size());
            double ss = 0.0;
            for (double d : nums) { double diff = d - mean; ss += diff * diff; }
            double var = ss / static_cast<double>(sample ? (nums.size() - 1) : nums.size());
            return (mode == DbMode::Stdev || mode == DbMode::StdevP) ? std::sqrt(var) : var;
        }
    }
    return ErrorValue{ErrorType::Value};
}

} // namespace

void registerAdvancedFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"TRIM", 1, 1, fnTrim});
    registry.registerFunction({"TEXT", 2, 2, fnText});
    registry.registerFunction({"SUMIF", 2, 3, fnSumIf});
    registry.registerFunction({"SUMIFS", 3, 255, fnSumIfs});
    registry.registerFunction({"COUNTIFS", 2, 255, fnCountIfs});
    registry.registerFunction({"RATE", 3, 6, fnRate});
    registry.registerFunction({"CONVERT", 3, 3, fnConvert});
    registry.registerFunction({"COMPLEX", 2, 3, fnComplex});
    registry.registerFunction({"DSUM", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Sum); }});
    registry.registerFunction({"DCOUNT", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Count); }});
    registry.registerFunction({"DCOUNTA", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::CountA); }});
    registry.registerFunction({"DAVERAGE", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Average); }});
    registry.registerFunction({"DGET", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Get); }});
    registry.registerFunction({"DMAX", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Max); }});
    registry.registerFunction({"DMIN", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Min); }});
    registry.registerFunction({"DPRODUCT", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Product); }});
    registry.registerFunction({"DSTDEV", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Stdev); }});
    registry.registerFunction({"DSTDEVP", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::StdevP); }});
    registry.registerFunction({"DVAR", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::Var); }});
    registry.registerFunction({"DVARP", 3, 3, [](const auto& a, const auto& c) { return evalDatabaseFn(a, c, DbMode::VarP); }});
}

} // namespace sheetcore::func_advanced
