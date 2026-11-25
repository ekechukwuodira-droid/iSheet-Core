#pragma once

#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"
#include "sheetcore/formula/ast.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sheetcore::formula {

class ICellValueProvider {
public:
    virtual ~ICellValueProvider() = default;

    virtual std::optional<SheetId> resolveSheetId(std::string_view sheetName) const = 0;
    virtual CellValue getCellValue(SheetId sheetId, CellCoord coord) const = 0;
    // NOTE: for ranges larger than 4096 cells implementations may return only populated cells
    // (non-positional). Use formula::toArray() from array_utils.hpp for positional access.
    virtual std::vector<CellValue> getRangeValues(SheetId sheetId, const RangeAddress& range) const = 0;

    // ---- Optional workbook introspection (used by reference/information functions) ----
    // Formula text of a cell (including leading '='), if the cell holds a formula.
    virtual std::optional<std::string> getCellFormula(SheetId, CellCoord) const { return std::nullopt; }
    // Sheet names in workbook (tab) order.
    virtual std::vector<std::string> sheetNames() const { return {}; }
    // Name of the given sheet.
    virtual std::optional<std::string> sheetName(SheetId) const { return std::nullopt; }
    // Bounding box of populated cells on the sheet.
    virtual std::optional<RangeAddress> usedRange(SheetId) const { return std::nullopt; }
};

class INameResolver {
public:
    virtual ~INameResolver() = default;
    virtual std::optional<std::string> resolveNameExpression(std::string_view name, SheetId activeSheetId) const = 0;
};

class ITableResolver {
public:
    virtual ~ITableResolver() = default;
    virtual std::optional<SheetRangeAddress> resolveTableColumn(std::string_view tableName,
                                                                std::string_view columnName) const = 0;
};

struct ArrayValue {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<CellValue> elements; // row-major

    ArrayValue() = default;
    ArrayValue(std::size_t r, std::size_t c, CellValue fill = EmptyValue{})
        : rows(r), cols(c), elements(r * c, fill) {}
    ArrayValue(std::size_t r, std::size_t c, std::vector<CellValue> elems)
        : rows(r), cols(c), elements(std::move(elems)) {}

    bool empty() const noexcept { return rows == 0 || cols == 0; }
    std::size_t size() const noexcept { return elements.size(); }
    const CellValue& at(std::size_t r, std::size_t c) const { return elements[r * cols + c]; }
    CellValue& at(std::size_t r, std::size_t c) { return elements[r * cols + c]; }
};

struct LambdaValue;

struct EvalArg {
    CellValue scalar = EmptyValue{};
    std::optional<ArrayValue> array;
    std::optional<SheetRangeAddress> rangeRef;
    // Set when the argument evaluates to a LAMBDA (or an eta-reduced built-in such as SUM).
    std::shared_ptr<const LambdaValue> lambda;
    // True when the argument was omitted in the call (e.g. f(1,,3)) or a LAMBDA parameter was not supplied.
    bool omitted = false;

    bool isRange() const noexcept { return rangeRef.has_value(); }
    bool isArray() const noexcept { return array.has_value(); }
    bool isLambda() const noexcept { return lambda != nullptr; }
    bool isOmitted() const noexcept { return omitted; }
};

using LocalScope = std::unordered_map<std::string, EvalArg>;

struct LambdaValue {
    std::vector<std::string> params;          // upper-cased parameter names
    ExprPtr body;                             // null for built-in (eta-reduced) lambdas
    std::shared_ptr<const LocalScope> captured; // LET/LAMBDA variables visible at creation time
    std::string builtinName;                  // non-empty for eta-reduced built-ins, e.g. "SUM"
};

struct EvalContext;

// Implemented by the Evaluator; lets function implementations call LAMBDA values (MAP, REDUCE, ...).
class ILambdaInvoker {
public:
    virtual ~ILambdaInvoker() = default;
    virtual EvalArg invokeLambda(const LambdaValue& fn, const std::vector<EvalArg>& args,
                                 const EvalContext& ctx) const = 0;
};

struct EvalContext {
    SheetId activeSheetId = kInvalidSheetId;
    CellCoord currentCell{};
    const ICellValueProvider* values = nullptr;
    const INameResolver* names = nullptr;
    const ITableResolver* tables = nullptr;
    const LocalScope* locals = nullptr;
    const ILambdaInvoker* invoker = nullptr; // set by the Evaluator before calling a function
};

} // namespace sheetcore::formula
