#pragma once

#include "sheetcore/core/value.hpp"
#include "sheetcore/formula/context.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sheetcore::formula {

// Scalar-returning implementation (the common case).
using FunctionImpl = std::function<CellValue(const std::vector<EvalArg>& args, const EvalContext& ctx)>;
// Implementation that may return an array (dynamic array / spill), a reference (OFFSET, INDIRECT, ...)
// or a LAMBDA. When an array or reference is returned, EvalArg::scalar should hold the top-left value.
using ArrayFunctionImpl = std::function<EvalArg(const std::vector<EvalArg>& args, const EvalContext& ctx)>;

struct FunctionDefinition {
    std::string name;
    std::size_t minArgs = 0;
    std::size_t maxArgs = 255;
    FunctionImpl impl;
    ArrayFunctionImpl arrayImpl;  // takes precedence over impl when set
    bool isVolatile = false;      // recalculated on every recalc (RAND, NOW, OFFSET, INDIRECT, ...)
};

class FunctionRegistry {
public:
    void registerFunction(FunctionDefinition def);
    void registerArrayFunction(std::string name, std::size_t minArgs, std::size_t maxArgs,
                               ArrayFunctionImpl impl, bool isVolatile = false);
    // Marks an already registered function as volatile.
    void setVolatile(std::string_view name, bool isVolatile = true);

    const FunctionDefinition* find(std::string_view name) const;
    bool contains(std::string_view name) const;
    std::size_t size() const noexcept { return functions_.size(); }
    // All registered (normalized, upper-case) function names, sorted.
    std::vector<std::string> names() const;

    // Upper-cases and strips the "_xlfn." / "_xlws." / "_xludf." prefixes used in OOXML files.
    static std::string normalizeName(std::string_view name);

private:
    std::unordered_map<std::string, FunctionDefinition> functions_;
};

} // namespace sheetcore::formula
