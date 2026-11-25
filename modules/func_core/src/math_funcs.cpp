#include "sheetcore/func_core/register.hpp"
#include "arg_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sheetcore::func_core {

namespace {

CellValue fnSum(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    double sum = 0.0;
    auto err = detail::forEachArgValue(args, ctx, [&](const CellValue& v, bool fromRange) {
        if (isNumber(v)) {
            sum += std::get<double>(v);
        } else if (!fromRange) {
            if (auto n = toNumber(v)) sum += *n;
        }
    });
    if (err) return *err;
    return sum;
}

CellValue fnAverage(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    double sum = 0.0;
    std::size_t count = 0;
    auto err = detail::forEachArgValue(args, ctx, [&](const CellValue& v, bool fromRange) {
        if (isNumber(v)) {
            sum += std::get<double>(v);
            ++count;
        } else if (!fromRange && !isEmpty(v)) {
            if (auto n = toNumber(v)) {
                sum += *n;
                ++count;
            }
        }
    });
    if (err) return *err;
    if (count == 0) return ErrorValue{ErrorType::DivZero};
    return sum / static_cast<double>(count);
}

CellValue fnMin(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    double best = std::numeric_limits<double>::infinity();
    bool any = false;
    auto err = detail::forEachArgValue(args, ctx, [&](const CellValue& v, bool fromRange) {
        if (isNumber(v)) {
            best = std::min(best, std::get<double>(v));
            any = true;
        } else if (!fromRange && !isEmpty(v)) {
            if (auto n = toNumber(v)) {
                best = std::min(best, *n);
                any = true;
            }
        }
    });
    if (err) return *err;
    return any ? best : 0.0;
}

CellValue fnMax(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    double best = -std::numeric_limits<double>::infinity();
    bool any = false;
    auto err = detail::forEachArgValue(args, ctx, [&](const CellValue& v, bool fromRange) {
        if (isNumber(v)) {
            best = std::max(best, std::get<double>(v));
            any = true;
        } else if (!fromRange && !isEmpty(v)) {
            if (auto n = toNumber(v)) {
                best = std::max(best, *n);
                any = true;
            }
        }
    });
    if (err) return *err;
    return any ? best : 0.0;
}

CellValue fnAbs(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    auto n = toNumber(args[0].scalar);
    if (!n) return ErrorValue{ErrorType::Value};
    return std::fabs(*n);
}

CellValue fnSqrt(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    auto n = toNumber(args[0].scalar);
    if (!n) return ErrorValue{ErrorType::Value};
    if (*n < 0.0) return ErrorValue{ErrorType::Num};
    return std::sqrt(*n);
}

CellValue fnPower(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    if (isError(args[1].scalar)) return args[1].scalar;
    auto base = toNumber(args[0].scalar);
    auto exp = toNumber(args[1].scalar);
    if (!base || !exp) return ErrorValue{ErrorType::Value};
    double res = std::pow(*base, *exp);
    if (std::isnan(res) || std::isinf(res)) return ErrorValue{ErrorType::Num};
    return res;
}

CellValue fnRound(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    if (isError(args[1].scalar)) return args[1].scalar;
    auto num = toNumber(args[0].scalar);
    auto digits = toNumber(args[1].scalar);
    if (!num || !digits) return ErrorValue{ErrorType::Value};
    double factor = std::pow(10.0, std::trunc(*digits));
    if (std::isinf(factor) || factor == 0.0) return ErrorValue{ErrorType::Num};
    return std::round(*num * factor) / factor;
}

} // namespace

void registerMathFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"SUM", 1, 255, fnSum});
    registry.registerFunction({"AVERAGE", 1, 255, fnAverage});
    registry.registerFunction({"MIN", 1, 255, fnMin});
    registry.registerFunction({"MAX", 1, 255, fnMax});
    registry.registerFunction({"ABS", 1, 1, fnAbs});
    registry.registerFunction({"SQRT", 1, 1, fnSqrt});
    registry.registerFunction({"POWER", 2, 2, fnPower});
    registry.registerFunction({"ROUND", 2, 2, fnRound});
}

} // namespace sheetcore::func_core
