#include "sheetcore/func_core/register.hpp"
#include "arg_helpers.hpp"

namespace sheetcore::func_core {

namespace {

CellValue fnIf(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    auto cond = toBool(args[0].scalar);
    if (!cond) return ErrorValue{ErrorType::Value};
    if (*cond) {
        return args[1].scalar;
    }
    return args.size() >= 3 ? args[2].scalar : CellValue{false};
}

CellValue fnAnd(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    bool allTrue = true;
    bool any = false;
    auto err = detail::forEachArgValue(args, ctx, [&](const CellValue& v, bool fromRange) {
        if (fromRange && isEmpty(v)) return;
        if (auto b = toBool(v)) {
            any = true;
            if (!*b) allTrue = false;
        }
    });
    if (err) return *err;
    return any ? CellValue{allTrue} : CellValue{ErrorValue{ErrorType::Value}};
}

CellValue fnOr(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    bool anyTrue = false;
    bool any = false;
    auto err = detail::forEachArgValue(args, ctx, [&](const CellValue& v, bool fromRange) {
        if (fromRange && isEmpty(v)) return;
        if (auto b = toBool(v)) {
            any = true;
            if (*b) anyTrue = true;
        }
    });
    if (err) return *err;
    return any ? CellValue{anyTrue} : CellValue{ErrorValue{ErrorType::Value}};
}

CellValue fnNot(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    auto b = toBool(args[0].scalar);
    if (!b) return ErrorValue{ErrorType::Value};
    return !(*b);
}

CellValue fnIfError(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) {
        return args[1].scalar;
    }
    return args[0].scalar;
}

} // namespace

void registerLogicalFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"IF", 2, 3, fnIf});
    registry.registerFunction({"AND", 1, 255, fnAnd});
    registry.registerFunction({"OR", 1, 255, fnOr});
    registry.registerFunction({"NOT", 1, 1, fnNot});
    registry.registerFunction({"IFERROR", 2, 2, fnIfError});
}

} // namespace sheetcore::func_core
