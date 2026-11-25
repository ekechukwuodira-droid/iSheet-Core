#pragma once

#include "sheetcore/calc/dependency_graph.hpp"
#include "sheetcore/core/events.hpp"
#include "sheetcore/core/thread_pool.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/document/workbook.hpp"
#include "sheetcore/formula/ast.hpp"
#include "sheetcore/formula/context.hpp"
#include "sheetcore/formula/functions.hpp"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace sheetcore::calc {

struct CalculationStats {
    std::size_t formulasEvaluated = 0;
    std::size_t circularCellsDetected = 0;
};

class CalculationEngine final : public IWorkbookObserver {
public:
    CalculationEngine(Workbook& workbook, const formula::FunctionRegistry& registry);
    ~CalculationEngine() override;

    CalculationEngine(const CalculationEngine&) = delete;
    CalculationEngine& operator=(const CalculationEngine&) = delete;

    void setParallelEnabled(bool enabled) noexcept { parallel_ = enabled; }
    bool isParallelEnabled() const noexcept { return parallel_; }
    void setTableResolver(const formula::ITableResolver* resolver) noexcept { tableResolver_ = resolver; }

    CalculationStats recalculate();
    CalculationStats recalculateAll();

    std::size_t dirtyCount() const noexcept { return dirtyCells_.size(); }

    void onWorkbookEvent(const WorkbookEvent& event) override;

private:
    void indexCellFormula(SheetId sheetId, CellCoord coord, const std::string& formulaText);
    void extractDependencies(SheetId activeSheetId,
                             const formula::ExprPtr& expr,
                             std::vector<SheetCellCoord>& cellDeps,
                             std::vector<SheetRangeAddress>& rangeDeps) const;
    void markDependentsDirty(SheetCellCoord origin);
    CalculationStats evaluateDirtySet();

    Workbook& workbook_;
    const formula::FunctionRegistry& registry_;
    DependencyGraph graph_;

    struct CompiledFormula {
        FormulaId id = kInvalidFormulaId;
        formula::ExprPtr ast;
    };

    FormulaId nextFormulaId_ = 1;
    std::unordered_map<SheetCellCoord, CompiledFormula, SheetCellCoordHash> formulas_;
    std::unordered_set<SheetCellCoord, SheetCellCoordHash> dirtyCells_;

    bool parallel_ = true;
    const formula::ITableResolver* tableResolver_ = nullptr;
    std::unique_ptr<ThreadPool> threadPool_;
};

} // namespace sheetcore::calc
