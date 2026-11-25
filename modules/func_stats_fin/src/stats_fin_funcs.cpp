#include "sheetcore/func_stats_fin/register.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sheetcore::func_stats_fin {

namespace {

std::optional<ErrorValue> collectNumbers(const std::vector<formula::EvalArg>& args,
                                         const formula::EvalContext& ctx,
                                         std::vector<double>& out) {
    for (const auto& arg : args) {
        if (arg.isRange()) {
            if (!ctx.values) return ErrorValue{ErrorType::Ref};
            for (const auto& v : ctx.values->getRangeValues(arg.rangeRef->sheetId, arg.rangeRef->range)) {
                if (isError(v)) return std::get<ErrorValue>(v);
                if (isNumber(v)) out.push_back(std::get<double>(v));
            }
        } else if (arg.isArray()) {
            for (const auto& v : arg.array->elements) {
                if (isError(v)) return std::get<ErrorValue>(v);
                if (isNumber(v)) out.push_back(std::get<double>(v));
            }
        } else {
            if (isError(arg.scalar)) return std::get<ErrorValue>(arg.scalar);
            if (auto n = toNumber(arg.scalar)) out.push_back(*n);
        }
    }
    return std::nullopt;
}

CellValue fnMedian(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::vector<double> nums;
    if (auto err = collectNumbers(args, ctx, nums)) return *err;
    if (nums.empty()) return ErrorValue{ErrorType::Num};
    std::sort(nums.begin(), nums.end());
    std::size_t mid = nums.size() / 2;
    if (nums.size() % 2 == 1) return nums[mid];
    return (nums[mid - 1] + nums[mid]) * 0.5;
}

CellValue fnMode(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::vector<double> nums;
    if (auto err = collectNumbers(args, ctx, nums)) return *err;
    if (nums.empty()) return ErrorValue{ErrorType::NA};
    std::unordered_map<double, std::size_t> counts;
    double bestVal = nums[0];
    std::size_t bestCount = 0;
    for (double v : nums) {
        std::size_t c = ++counts[v];
        if (c > bestCount) {
            bestCount = c;
            bestVal = v;
        }
    }
    if (bestCount <= 1) return ErrorValue{ErrorType::NA};
    return bestVal;
}

CellValue fnVar(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::vector<double> nums;
    if (auto err = collectNumbers(args, ctx, nums)) return *err;
    if (nums.size() < 2) return ErrorValue{ErrorType::DivZero};
    double mean = 0.0;
    for (double v : nums) mean += v;
    mean /= static_cast<double>(nums.size());
    double sumSq = 0.0;
    for (double v : nums) {
        double d = v - mean;
        sumSq += d * d;
    }
    return sumSq / static_cast<double>(nums.size() - 1);
}

CellValue fnStdev(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    CellValue v = fnVar(args, ctx);
    if (isError(v)) return v;
    return std::sqrt(std::get<double>(v));
}

// Financial: PMT(rate, nper, pv, [fv=0], [type=0])
CellValue fnPmt(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto rate = toNumber(args[0].scalar);
    auto nper = toNumber(args[1].scalar);
    auto pv = toNumber(args[2].scalar);
    double fv = args.size() >= 4 ? toNumber(args[3].scalar).value_or(0.0) : 0.0;
    double type = args.size() >= 5 ? toNumber(args[4].scalar).value_or(0.0) : 0.0;
    if (!rate || !nper || !pv || *nper == 0.0) return ErrorValue{ErrorType::Value};

    if (std::fabs(*rate) < 1e-12) {
        return -(*pv + fv) / (*nper);
    }
    double r = *rate;
    double factor = std::pow(1.0 + r, *nper);
    double pmt = -(r * (*pv * factor + fv)) / ((1.0 + r * type) * (factor - 1.0));
    return pmt;
}

// PV(rate, nper, pmt, [fv=0], [type=0])
CellValue fnPv(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto rate = toNumber(args[0].scalar);
    auto nper = toNumber(args[1].scalar);
    auto pmt = toNumber(args[2].scalar);
    double fv = args.size() >= 4 ? toNumber(args[3].scalar).value_or(0.0) : 0.0;
    double type = args.size() >= 5 ? toNumber(args[4].scalar).value_or(0.0) : 0.0;
    if (!rate || !nper || !pmt) return ErrorValue{ErrorType::Value};
    if (std::fabs(*rate) < 1e-12) {
        return -(*pmt * (*nper) + fv);
    }
    double r = *rate;
    double factor = std::pow(1.0 + r, *nper);
    return -((*pmt) * (1.0 + r * type) * ((factor - 1.0) / r) + fv) / factor;
}

// FV(rate, nper, pmt, [pv=0], [type=0])
CellValue fnFv(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    auto rate = toNumber(args[0].scalar);
    auto nper = toNumber(args[1].scalar);
    auto pmt = toNumber(args[2].scalar);
    double pv = args.size() >= 4 ? toNumber(args[3].scalar).value_or(0.0) : 0.0;
    double type = args.size() >= 5 ? toNumber(args[4].scalar).value_or(0.0) : 0.0;
    if (!rate || !nper || !pmt) return ErrorValue{ErrorType::Value};
    if (std::fabs(*rate) < 1e-12) {
        return -(pv + (*pmt) * (*nper));
    }
    double r = *rate;
    double factor = std::pow(1.0 + r, *nper);
    return -(pv * factor + (*pmt) * (1.0 + r * type) * ((factor - 1.0) / r));
}

// NPV(rate, value1, [value2], ...)
CellValue fnNpv(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    auto rate = toNumber(args[0].scalar);
    if (!rate) return ErrorValue{ErrorType::Value};
    std::vector<formula::EvalArg> cfArgs(args.begin() + 1, args.end());
    std::vector<double> cfs;
    if (auto err = collectNumbers(cfArgs, ctx, cfs)) return *err;

    double npv = 0.0;
    for (std::size_t i = 0; i < cfs.size(); ++i) {
        npv += cfs[i] / std::pow(1.0 + *rate, static_cast<double>(i + 1));
    }
    return npv;
}

// IRR(values, [guess=0.1])
CellValue fnIrr(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::vector<formula::EvalArg> cfArgs{args[0]};
    std::vector<double> cfs;
    if (auto err = collectNumbers(cfArgs, ctx, cfs)) return *err;
    if (cfs.size() < 2) return ErrorValue{ErrorType::Num};

    double r = args.size() >= 2 ? toNumber(args[1].scalar).value_or(0.1) : 0.1;
    for (int iter = 0; iter < 100; ++iter) {
        double f = 0.0;
        double df = 0.0;
        for (std::size_t t = 0; t < cfs.size(); ++t) {
            double denom = std::pow(1.0 + r, static_cast<double>(t));
            f += cfs[t] / denom;
            if (t > 0) {
                df -= static_cast<double>(t) * cfs[t] / (denom * (1.0 + r));
            }
        }
        if (std::fabs(f) < 1e-9) return r;
        if (std::fabs(df) < 1e-12) return ErrorValue{ErrorType::Num};
        r -= f / df;
        if (r <= -1.0) return ErrorValue{ErrorType::Num};
    }
    return ErrorValue{ErrorType::Num};
}

} // namespace

void registerStatsFinFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"MEDIAN", 1, 255, fnMedian});
    registry.registerFunction({"MODE", 1, 255, fnMode});
    registry.registerFunction({"VAR", 1, 255, fnVar});
    registry.registerFunction({"STDEV", 1, 255, fnStdev});
    registry.registerFunction({"PMT", 3, 5, fnPmt});
    registry.registerFunction({"PV", 3, 5, fnPv});
    registry.registerFunction({"FV", 3, 5, fnFv});
    registry.registerFunction({"NPV", 2, 255, fnNpv});
    registry.registerFunction({"IRR", 1, 2, fnIrr});
}

} // namespace sheetcore::func_stats_fin
