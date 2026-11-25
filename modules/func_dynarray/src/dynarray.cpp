#include "sheetcore/func_dynarray/dynarray.hpp"
#include "sheetcore/formula/array_utils.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace sheetcore::func_dynarray {

DynamicArrayResult makeSequence(std::size_t rows, std::size_t cols, double start, double step) {
    DynamicArrayResult res;
    res.rows = rows;
    res.cols = cols;
    res.values.reserve(rows * cols);
    double cur = start;
    for (std::size_t i = 0; i < rows * cols; ++i) {
        res.values.push_back(cur);
        cur += step;
    }
    return res;
}

DynamicArrayResult makeUnique(const std::vector<CellValue>& input) {
    DynamicArrayResult res;
    std::unordered_set<std::string> seen;
    for (const auto& v : input) {
        if (isEmpty(v)) continue;
        std::string key = toDisplayString(v);
        if (seen.insert(key).second) {
            res.values.push_back(v);
        }
    }
    res.rows = res.values.size();
    res.cols = res.rows > 0 ? 1 : 0;
    return res;
}

DynamicArrayResult makeSort(const std::vector<CellValue>& input, bool ascending) {
    DynamicArrayResult res;
    res.values = input;
    std::stable_sort(res.values.begin(), res.values.end(), [ascending](const CellValue& a, const CellValue& b) {
        auto na = toNumber(a), nb = toNumber(b);
        if (isNumber(a) && isNumber(b)) {
            return ascending ? (*na < *nb) : (*na > *nb);
        }
        std::string sa = toDisplayString(a), sb = toDisplayString(b);
        return ascending ? (sa < sb) : (sa > sb);
    });
    res.rows = res.values.size();
    res.cols = res.rows > 0 ? 1 : 0;
    return res;
}

DynamicArrayResult makeFilter(const std::vector<CellValue>& input, const std::vector<bool>& mask) {
    DynamicArrayResult res;
    std::size_t n = std::min(input.size(), mask.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (mask[i]) res.values.push_back(input[i]);
    }
    res.rows = res.values.size();
    res.cols = res.rows > 0 ? 1 : 0;
    return res;
}

void SpillManager::clearSpill(Worksheet& ws, SheetId sheetId, CellCoord anchor) {
    SheetCellCoord key{sheetId, anchor};
    auto it = activeSpills_.find(key);
    if (it == activeSpills_.end()) return;
    RangeAddress prev = it->second;
    for (RowIndex r = prev.start.row; r <= prev.end.row; ++r) {
        for (ColIndex c = prev.start.col; c <= prev.end.col; ++c) {
            CellCoord coord{r, c};
            if (coord != anchor) {
                ws.clearCell(coord);
            }
        }
    }
    activeSpills_.erase(it);
}

CellValue SpillManager::applySpill(Worksheet& ws, CellCoord anchor, const DynamicArrayResult& result) {
    clearSpill(ws, ws.id(), anchor);
    if (result.rows == 0 || result.cols == 0 || result.values.empty()) {
        ws.setCachedValue(anchor, ErrorValue{ErrorType::NA});
        return ErrorValue{ErrorType::NA};
    }

    RangeAddress target{
        anchor,
        CellCoord{anchor.row + static_cast<RowIndex>(result.rows - 1),
                  anchor.col + static_cast<ColIndex>(result.cols - 1)}
    };

    // Check for spill collision on any non-anchor cell
    for (RowIndex r = target.start.row; r <= target.end.row; ++r) {
        for (ColIndex c = target.start.col; c <= target.end.col; ++c) {
            CellCoord coord{r, c};
            if (coord == anchor) continue;
            const CellRecord* rec = ws.getCell(coord);
            if (rec && (!isEmpty(rec->value) || rec->formulaText.has_value())) {
                ws.setCachedValue(anchor, ErrorValue{ErrorType::Spill});
                return ErrorValue{ErrorType::Spill};
            }
        }
    }

    // Write spill cells
    for (std::size_t r = 0; r < result.rows; ++r) {
        for (std::size_t c = 0; c < result.cols; ++c) {
            CellCoord coord{anchor.row + static_cast<RowIndex>(r), anchor.col + static_cast<ColIndex>(c)};
            ws.setCachedValue(coord, result.at(r, c));
        }
    }
    activeSpills_[SheetCellCoord{ws.id(), anchor}] = target;
    return result.values.front();
}

void registerDynArrayExtFunctions(formula::FunctionRegistry& registry);

void registerDynArrayFunctions(formula::FunctionRegistry& registry) {
    registry.registerArrayFunction("SEQUENCE", 1, 4, [](const std::vector<formula::EvalArg>& args, const formula::EvalContext&) -> formula::EvalArg {
        auto rowsD = toNumber(args[0].scalar);
        if (!rowsD || *rowsD < 1.0) return formula::makeScalar(ErrorValue{ErrorType::Value});
        double colsD = (args.size() >= 2 && !args[1].isOmitted()) ? toNumber(args[1].scalar).value_or(0.0) : 1.0;
        if (colsD < 1.0) return formula::makeScalar(ErrorValue{ErrorType::Value});
        double start = (args.size() >= 3 && !args[2].isOmitted()) ? toNumber(args[2].scalar).value_or(1.0) : 1.0;
        double step = (args.size() >= 4 && !args[3].isOmitted()) ? toNumber(args[3].scalar).value_or(1.0) : 1.0;
        std::size_t rows = static_cast<std::size_t>(std::trunc(*rowsD));
        std::size_t cols = static_cast<std::size_t>(std::trunc(colsD));
        auto res = makeSequence(rows, cols, start, step);
        return formula::makeArray(formula::ArrayValue(res.rows, res.cols, std::move(res.values)));
    });
    registry.registerArrayFunction("UNIQUE", 1, 3, [](const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) -> formula::EvalArg {
        auto res = makeUnique(formula::flatten(args[0], ctx));
        if (res.values.empty()) return formula::makeScalar(ErrorValue{ErrorType::NA});
        return formula::makeArray(formula::ArrayValue(res.rows, res.cols, std::move(res.values)));
    });
    registry.registerArrayFunction("SORT", 1, 4, [](const std::vector<formula::EvalArg>& args, const formula::EvalContext& ctx) -> formula::EvalArg {
        bool asc = (args.size() >= 3 && !args[2].isOmitted()) ? (toNumber(args[2].scalar).value_or(1.0) >= 0.0) : true;
        auto res = makeSort(formula::flatten(args[0], ctx), asc);
        if (res.values.empty()) return formula::makeScalar(ErrorValue{ErrorType::NA});
        return formula::makeArray(formula::ArrayValue(res.rows, res.cols, std::move(res.values)));
    });
    registerDynArrayExtFunctions(registry);
}

} // namespace sheetcore::func_dynarray
