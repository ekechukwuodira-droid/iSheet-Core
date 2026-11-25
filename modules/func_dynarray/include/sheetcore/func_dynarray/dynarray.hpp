#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"
#include "sheetcore/document/worksheet.hpp"
#include "sheetcore/formula/functions.hpp"

#include <unordered_map>
#include <vector>

namespace sheetcore::func_dynarray {

struct DynamicArrayResult {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<CellValue> values; // row-major

    const CellValue& at(std::size_t r, std::size_t c) const { return values[r * cols + c]; }
};

DynamicArrayResult makeSequence(std::size_t rows, std::size_t cols = 1, double start = 1.0, double step = 1.0);
DynamicArrayResult makeUnique(const std::vector<CellValue>& input);
DynamicArrayResult makeSort(const std::vector<CellValue>& input, bool ascending = true);
DynamicArrayResult makeFilter(const std::vector<CellValue>& input, const std::vector<bool>& mask);

class SpillManager {
public:
    // Applies a dynamic array starting at `anchor`.
    // Returns ErrorValue{ErrorType::Spill} (and writes #SPILL! to anchor) if any destination cell is occupied.
    CellValue applySpill(Worksheet& ws, CellCoord anchor, const DynamicArrayResult& result);
    void clearSpill(Worksheet& ws, SheetId sheetId, CellCoord anchor);

private:
    std::unordered_map<SheetCellCoord, RangeAddress, SheetCellCoordHash> activeSpills_;
};

void registerDynArrayFunctions(formula::FunctionRegistry& registry);

} // namespace sheetcore::func_dynarray
