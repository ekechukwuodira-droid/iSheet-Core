#include "sheetcore/func_core/register.hpp"
#include "arg_helpers.hpp"

#include <algorithm>
#include <cctype>

namespace sheetcore::func_core {

namespace {

CellValue fnLen(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    return static_cast<double>(toDisplayString(args[0].scalar).size());
}

CellValue fnLeft(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    std::string s = toDisplayString(args[0].scalar);
    std::size_t count = 1;
    if (args.size() >= 2) {
        if (isError(args[1].scalar)) return args[1].scalar;
        auto n = toNumber(args[1].scalar);
        if (!n || *n < 0) return ErrorValue{ErrorType::Value};
        count = static_cast<std::size_t>(*n);
    }
    return s.substr(0, std::min(count, s.size()));
}

CellValue fnRight(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    std::string s = toDisplayString(args[0].scalar);
    std::size_t count = 1;
    if (args.size() >= 2) {
        if (isError(args[1].scalar)) return args[1].scalar;
        auto n = toNumber(args[1].scalar);
        if (!n || *n < 0) return ErrorValue{ErrorType::Value};
        count = static_cast<std::size_t>(*n);
    }
    if (count >= s.size()) return s;
    return s.substr(s.size() - count);
}

CellValue fnMid(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    if (isError(args[1].scalar)) return args[1].scalar;
    if (isError(args[2].scalar)) return args[2].scalar;
    std::string s = toDisplayString(args[0].scalar);
    auto startNum = toNumber(args[1].scalar);
    auto lenNum = toNumber(args[2].scalar);
    if (!startNum || !lenNum || *startNum < 1 || *lenNum < 0) {
        return ErrorValue{ErrorType::Value};
    }
    std::size_t start0 = static_cast<std::size_t>(*startNum) - 1;
    if (start0 >= s.size()) return std::string{};
    return s.substr(start0, static_cast<std::size_t>(*lenNum));
}

CellValue fnConcat(const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) {
    std::string out;
    auto err = detail::forEachArgValue(args, ctx, [&](const CellValue& v, bool) {
        out += toDisplayString(v);
    });
    if (err) return *err;
    return out;
}

CellValue fnUpper(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    std::string s = toDisplayString(args[0].scalar);
    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

CellValue fnLower(const std::vector<formula::EvalArg>& args, const formula::EvalContext&) {
    if (isError(args[0].scalar)) return args[0].scalar;
    std::string s = toDisplayString(args[0].scalar);
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

} // namespace

void registerTextFunctions(formula::FunctionRegistry& registry) {
    registry.registerFunction({"LEN", 1, 1, fnLen});
    registry.registerFunction({"LEFT", 1, 2, fnLeft});
    registry.registerFunction({"RIGHT", 1, 2, fnRight});
    registry.registerFunction({"MID", 3, 3, fnMid});
    registry.registerFunction({"CONCAT", 1, 255, fnConcat});
    registry.registerFunction({"UPPER", 1, 1, fnUpper});
    registry.registerFunction({"LOWER", 1, 1, fnLower});
}

} // namespace sheetcore::func_core
