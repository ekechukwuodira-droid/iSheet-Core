#include "sheetcore/func_core/register.hpp"
#include "arg_helpers.hpp"

namespace sheetcore::func_core {

void registerMathFunctions(formula::FunctionRegistry& registry);
void registerLogicalFunctions(formula::FunctionRegistry& registry);
void registerTextFunctions(formula::FunctionRegistry& registry);

namespace {

CellValue fnIsNumber(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    return isNumber(args[0].scalar);
}

CellValue fnIsText(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    return isText(args[0].scalar);
}

CellValue fnIsBlank(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    return isEmpty(args[0].scalar);
}

CellValue fnIsError(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    return isError(args[0].scalar);
}

CellValue fnCount(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::size_t count = 0;
    for (const auto& arg : args) {
        if (arg.isRange()) {
            if (!ctx.values) return ErrorValue{ErrorType::Ref};
            for (const auto& v : ctx.values->getRangeValues(arg.rangeRef->sheetId, arg.rangeRef->range)) {
                if (isNumber(v)) ++count;
            }
        } else if (arg.isArray()) {
            for (const auto& v : arg.array->elements) {
                if (isNumber(v)) ++count;
            }
        } else {
            if (isNumber(arg.scalar)) ++count;
        }
    }
    return static_cast<double>(count);
}

CellValue fnCountA(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::size_t count = 0;
    for (const auto& arg : args) {
        if (arg.isRange()) {
            if (!ctx.values) return ErrorValue{ErrorType::Ref};
            for (const auto& v : ctx.values->getRangeValues(arg.rangeRef->sheetId, arg.rangeRef->range)) {
                if (!isEmpty(v)) ++count;
            }
        } else if (arg.isArray()) {
            for (const auto& v : arg.array->elements) {
                if (!isEmpty(v)) ++count;
            }
        } else {
            if (!isEmpty(arg.scalar)) ++count;
        }
    }
    return static_cast<double>(count);
}

CellValue fnCountIf(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    if (!args[0].isRange() || !ctx.values) {
        return ErrorValue{ErrorType::Value};
    }
    const auto vals = ctx.values->getRangeValues(args[0].rangeRef->sheetId, args[0].rangeRef->range);
    const CellValue& crit = args[1].scalar;
    std::size_t count = 0;

    if (isText(crit)) {
        const std::string& cs = std::get<std::string>(crit);
        if (cs.size() >= 2 && (cs[0] == '>' || cs[0] == '<')) {
            bool isGt = cs[0] == '>';
            bool isEq = cs[1] == '=';
            std::string_view numStr = std::string_view(cs).substr(isEq ? 2 : 1);
            auto target = toNumber(std::string(numStr));
            if (target) {
                for (const auto& v : vals) {
                    if (isNumber(v)) {
                        double d = std::get<double>(v);
                        bool match = isGt ? (isEq ? d >= *target : d > *target)
                                          : (isEq ? d <= *target : d < *target);
                        if (match) ++count;
                    }
                }
                return static_cast<double>(count);
            }
        }
    }

    for (const auto& v : vals) {
        if (v == crit) ++count;
    }
    return static_cast<double>(count);
}

} // namespace

void registerCoreFunctions(formula::FunctionRegistry& registry) {
    registerMathFunctions(registry);
    registerLogicalFunctions(registry);
    registerTextFunctions(registry);

    registry.registerFunction({"ISNUMBER", 1, 1, fnIsNumber});
    registry.registerFunction({"ISTEXT", 1, 1, fnIsText});
    registry.registerFunction({"ISBLANK", 1, 1, fnIsBlank});
    registry.registerFunction({"ISERROR", 1, 1, fnIsError});
    registry.registerFunction({"COUNT", 1, 255, fnCount});
    registry.registerFunction({"COUNTA", 1, 255, fnCountA});
    registry.registerFunction({"COUNTIF", 2, 2, fnCountIf});
}

} // namespace sheetcore::func_core
