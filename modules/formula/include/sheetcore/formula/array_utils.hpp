#pragma once

// Shared helpers for array-aware / reference-aware function implementations.
// Header-only so that every func_* module can use them without new link dependencies.

#include "sheetcore/core/value.hpp"
#include "sheetcore/formula/context.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace sheetcore::formula {

// Ranges larger than this are clipped to the sheet's used range when materialized.
inline constexpr std::uint64_t kDirectRangeLimit = 4096;

// Positional, row-major materialization of a sheet range. Large ranges (e.g. A1:A1048576) are clipped
// to the used range of the sheet so that whole-column style references stay cheap.
inline ArrayValue rangeToArray(const SheetRangeAddress& ref, const EvalContext& ctx) {
    RangeAddress r = ref.range.normalized();
    if (!ctx.values) return ArrayValue(1, 1, CellValue{ErrorValue{ErrorType::Ref}});
    if (r.cellCount() > kDirectRangeLimit) {
        if (auto used = ctx.values->usedRange(ref.sheetId)) {
            RangeAddress u = used->normalized();
            r.end.row = std::max(r.start.row, std::min(r.end.row, u.end.row));
            r.end.col = std::max(r.start.col, std::min(r.end.col, u.end.col));
        }
    }
    const std::size_t rows = r.rowCount();
    const std::size_t cols = r.colCount();
    ArrayValue out(rows, cols);
    if (r.cellCount() <= kDirectRangeLimit) {
        auto vals = ctx.values->getRangeValues(ref.sheetId, r);
        if (vals.size() == out.elements.size()) {
            out.elements = std::move(vals);
            return out;
        }
    }
    for (std::size_t i = 0; i < rows; ++i) {
        for (std::size_t j = 0; j < cols; ++j) {
            out.at(i, j) = ctx.values->getCellValue(
                ref.sheetId, CellCoord{r.start.row + static_cast<RowIndex>(i), r.start.col + static_cast<ColIndex>(j)});
        }
    }
    return out;
}

// Any argument as a 2-D array: ranges are materialized, arrays copied, scalars become 1x1.
inline ArrayValue toArray(const EvalArg& arg, const EvalContext& ctx) {
    if (arg.isArray()) return *arg.array;
    if (arg.isRange()) return rangeToArray(*arg.rangeRef, ctx);
    if (arg.isLambda()) return ArrayValue(1, 1, CellValue{ErrorValue{ErrorType::Calc}});
    return ArrayValue(1, 1, arg.scalar);
}

// Row-major flattened values of an argument (positional; empties included).
inline std::vector<CellValue> flatten(const EvalArg& arg, const EvalContext& ctx) {
    return toArray(arg, ctx).elements;
}

// Number of rows / columns of an argument without materializing it.
inline std::size_t argRows(const EvalArg& arg) {
    if (arg.isArray()) return arg.array->rows;
    if (arg.isRange()) return arg.rangeRef->range.normalized().rowCount();
    return 1;
}
inline std::size_t argCols(const EvalArg& arg) {
    if (arg.isArray()) return arg.array->cols;
    if (arg.isRange()) return arg.rangeRef->range.normalized().colCount();
    return 1;
}

// Excel implicit intersection of a range with the formula cell's row / column.
inline CellValue implicitIntersection(const SheetRangeAddress& ref, const EvalContext& ctx) {
    if (!ctx.values) return ErrorValue{ErrorType::Ref};
    RangeAddress r = ref.range.normalized();
    if (r.start == r.end) return ctx.values->getCellValue(ref.sheetId, r.start);
    if (r.start.col == r.end.col && ctx.currentCell.row >= r.start.row && ctx.currentCell.row <= r.end.row) {
        return ctx.values->getCellValue(ref.sheetId, CellCoord{ctx.currentCell.row, r.start.col});
    }
    if (r.start.row == r.end.row && ctx.currentCell.col >= r.start.col && ctx.currentCell.col <= r.end.col) {
        return ctx.values->getCellValue(ref.sheetId, CellCoord{r.start.row, ctx.currentCell.col});
    }
    return ErrorValue{ErrorType::Value};
}

// Collapses any argument to a single value (top-left of arrays, implicit intersection for ranges).
inline CellValue collapseToScalar(const EvalArg& arg, const EvalContext& ctx) {
    if (arg.isLambda()) return ErrorValue{ErrorType::Calc};
    if (arg.isArray()) {
        if (arg.array->elements.empty()) return ErrorValue{ErrorType::Calc};
        return arg.array->elements.front();
    }
    if (arg.isRange()) return implicitIntersection(*arg.rangeRef, ctx);
    return arg.scalar;
}

// ---- Result builders ----
inline EvalArg makeScalar(CellValue v) {
    EvalArg out;
    out.scalar = std::move(v);
    return out;
}

// Array result. 1x1 arrays collapse to scalars; empty arrays become #CALC!.
inline EvalArg makeArray(ArrayValue arr) {
    EvalArg out;
    if (arr.empty() || arr.elements.empty()) {
        out.scalar = ErrorValue{ErrorType::Calc};
        return out;
    }
    if (arr.rows == 1 && arr.cols == 1) {
        out.scalar = arr.elements.front();
        return out;
    }
    out.scalar = arr.elements.front();
    out.array = std::move(arr);
    return out;
}

// Reference result (OFFSET, INDIRECT, INDEX-as-reference, ...). Scalar is filled with the top-left value.
inline EvalArg makeRef(const SheetRangeAddress& ref, const EvalContext& ctx) {
    EvalArg out;
    SheetRangeAddress n{ref.sheetId, ref.range.normalized()};
    out.rangeRef = n;
    out.scalar = ctx.values ? ctx.values->getCellValue(n.sheetId, n.range.start) : CellValue{ErrorValue{ErrorType::Ref}};
    return out;
}

// Calls a LAMBDA argument (MAP, REDUCE, BYROW, GROUPBY, ...). Returns #VALUE! if `fn` is not a lambda.
inline EvalArg callLambda(const EvalArg& fn, const std::vector<EvalArg>& args, const EvalContext& ctx) {
    if (!fn.isLambda() || !ctx.invoker) return makeScalar(ErrorValue{ErrorType::Value});
    return ctx.invoker->invokeLambda(*fn.lambda, args, ctx);
}

// Excel broadcasting of an array to rows x cols: single rows/columns are repeated, out-of-range -> #N/A.
inline ArrayValue broadcastTo(const ArrayValue& a, std::size_t rows, std::size_t cols) {
    ArrayValue out(rows, cols);
    for (std::size_t i = 0; i < rows; ++i) {
        for (std::size_t j = 0; j < cols; ++j) {
            std::size_t si = a.rows == 1 ? 0 : i;
            std::size_t sj = a.cols == 1 ? 0 : j;
            out.at(i, j) = (si < a.rows && sj < a.cols) ? a.at(si, sj) : CellValue{ErrorValue{ErrorType::NA}};
        }
    }
    return out;
}

} // namespace sheetcore::formula
