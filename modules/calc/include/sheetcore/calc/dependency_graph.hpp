#pragma once

#include "sheetcore/core/types.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sheetcore::calc {

class DependencyGraph {
public:
    void setDependencies(SheetCellCoord formulaCell,
                         std::vector<SheetCellCoord> cellDeps,
                         std::vector<SheetRangeAddress> rangeDeps);

    void removeFormula(SheetCellCoord formulaCell);
    void clear();

    // Returns all transitive dependents of `changedCell` (including `changedCell` if it's a formula).
    std::vector<SheetCellCoord> collectDependents(SheetCellCoord changedCell) const;

    // Topological levels for a set of dirty formula cells.
    // Cycles are returned in `cyclicCells` rather than `levels`.
    struct TopoPlan {
        std::vector<std::vector<SheetCellCoord>> levels;
        std::vector<SheetCellCoord> cyclicCells;
    };

    TopoPlan buildEvaluationPlan(const std::unordered_set<SheetCellCoord, SheetCellCoordHash>& dirtySet) const;

private:
    struct ForwardDeps {
        std::vector<SheetCellCoord> cells;
        std::vector<SheetRangeAddress> ranges;
    };

    std::unordered_map<SheetCellCoord, ForwardDeps, SheetCellCoordHash> forward_;
    std::unordered_map<SheetCellCoord, std::unordered_set<SheetCellCoord, SheetCellCoordHash>, SheetCellCoordHash> reverseCells_;

    // Range dependencies indexed by SheetId to avoid scanning other sheets
    struct RangeDepEntry {
        SheetCellCoord formulaCell;
        RangeAddress range;
    };
    std::unordered_map<SheetId, std::vector<RangeDepEntry>> reverseRangesBySheet_;
};

} // namespace sheetcore::calc
