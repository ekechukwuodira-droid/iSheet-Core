#include "sheetcore/calc/calculation_engine.hpp"
#include "sheetcore/calc/workbook_adapter.hpp"
#include "sheetcore/formula/evaluator.hpp"
#include "sheetcore/formula/parser.hpp"

#include <future>

namespace sheetcore::calc {

CalculationEngine::CalculationEngine(Workbook& workbook, const formula::FunctionRegistry& registry)
    : workbook_(workbook), registry_(registry), threadPool_(std::make_unique<ThreadPool>()) {
    workbook_.addObserver(this);

    // Index any formulas already present in the workbook
    for (SheetId sid : workbook_.sheetIds()) {
        if (const Worksheet* ws = workbook_.getSheet(sid)) {
            ws->forEachCell([&](CellCoord coord, const CellRecord& rec) {
                if (rec.formulaText.has_value()) {
                    indexCellFormula(sid, coord, *rec.formulaText);
                }
            });
        }
    }
}

CalculationEngine::~CalculationEngine() {
    workbook_.removeObserver(this);
}

void CalculationEngine::onWorkbookEvent(const WorkbookEvent& event) {
    switch (event.type) {
        case WorkbookEventType::CellValueChanged: {
            if (!event.range) break;
            SheetCellCoord sc{event.sheetId, event.range->start};
            if (formulas_.contains(sc)) {
                formulas_.erase(sc);
                graph_.removeFormula(sc);
                dirtyCells_.erase(sc);
            }
            markDependentsDirty(sc);
            if (workbook_.calculationMode() == CalculationMode::Automatic) {
                recalculate();
            }
            break;
        }
        case WorkbookEventType::CellFormulaChanged: {
            if (!event.range) break;
            SheetCellCoord sc{event.sheetId, event.range->start};
            if (const Worksheet* ws = workbook_.getSheet(event.sheetId)) {
                if (const CellRecord* rec = ws->getCell(event.range->start)) {
                    if (rec->formulaText.has_value()) {
                        indexCellFormula(event.sheetId, event.range->start, *rec->formulaText);
                        markDependentsDirty(sc);
                        if (workbook_.calculationMode() == CalculationMode::Automatic) {
                            recalculate();
                        }
                    }
                }
            }
            break;
        }
        case WorkbookEventType::SheetRemoved: {
            std::vector<SheetCellCoord> toRemove;
            for (const auto& [sc, _] : formulas_) {
                if (sc.sheetId == event.sheetId) {
                    toRemove.push_back(sc);
                }
            }
            for (const auto& sc : toRemove) {
                formulas_.erase(sc);
                graph_.removeFormula(sc);
                dirtyCells_.erase(sc);
            }
            break;
        }
        default:
            break;
    }
}

void CalculationEngine::indexCellFormula(SheetId sheetId, CellCoord coord, const std::string& formulaText) {
    SheetCellCoord sc{sheetId, coord};
    auto parsed = formula::Parser::parse(formulaText);
    if (!parsed) {
        formulas_[sc] = CompiledFormula{nextFormulaId_++, nullptr};
        graph_.removeFormula(sc);
        dirtyCells_.insert(sc);
        return;
    }

    CompiledFormula compiled{nextFormulaId_++, std::move(parsed.value())};
    std::vector<SheetCellCoord> cellDeps;
    std::vector<SheetRangeAddress> rangeDeps;
    extractDependencies(sheetId, compiled.ast, cellDeps, rangeDeps);

    graph_.setDependencies(sc, std::move(cellDeps), std::move(rangeDeps));
    formulas_[sc] = std::move(compiled);
    dirtyCells_.insert(sc);
}

void CalculationEngine::extractDependencies(SheetId activeSheetId,
                                            const formula::ExprPtr& expr,
                                            std::vector<SheetCellCoord>& cellDeps,
                                            std::vector<SheetRangeAddress>& rangeDeps) const {
    if (!expr) return;
    switch (expr->type()) {
        case formula::ExpressionType::Literal:
            break;
        case formula::ExpressionType::Unary: {
            const auto& u = static_cast<const formula::UnaryExpr&>(*expr);
            extractDependencies(activeSheetId, u.operand, cellDeps, rangeDeps);
            break;
        }
        case formula::ExpressionType::Binary: {
            const auto& b = static_cast<const formula::BinaryExpr&>(*expr);
            extractDependencies(activeSheetId, b.left, cellDeps, rangeDeps);
            extractDependencies(activeSheetId, b.right, cellDeps, rangeDeps);
            break;
        }
        case formula::ExpressionType::FunctionCall: {
            const auto& fc = static_cast<const formula::FunctionCallExpr&>(*expr);
            for (const auto& arg : fc.args) {
                extractDependencies(activeSheetId, arg, cellDeps, rangeDeps);
            }
            break;
        }
        case formula::ExpressionType::CellReference: {
            const auto& cr = static_cast<const formula::CellRefExpr&>(*expr);
            if (!cr.target.isInvalidRef) {
                SheetId sid = activeSheetId;
                if (cr.target.sheetName.has_value()) {
                    if (const Worksheet* ws = workbook_.getSheet(*cr.target.sheetName)) {
                        sid = ws->id();
                    }
                }
                cellDeps.push_back(SheetCellCoord{sid, cr.target.coord});
            }
            break;
        }
        case formula::ExpressionType::RangeReference: {
            const auto& rr = static_cast<const formula::RangeRefExpr&>(*expr);
            if (!rr.start.isInvalidRef && !rr.end.isInvalidRef) {
                SheetId sid = activeSheetId;
                if (rr.sheetName.has_value()) {
                    if (const Worksheet* ws = workbook_.getSheet(*rr.sheetName)) {
                        sid = ws->id();
                    }
                }
                rangeDeps.push_back(SheetRangeAddress{sid, RangeAddress{rr.start.coord, rr.end.coord}.normalized()});
            }
            break;
        }
        case formula::ExpressionType::NamedReference: {
            const auto& nr = static_cast<const formula::NamedRefExpr&>(*expr);
            if (const auto* def = workbook_.namedRanges().resolve(nr.name, activeSheetId)) {
                if (auto parsed = formula::Parser::parse(def->expression)) {
                    extractDependencies(activeSheetId, parsed.value(), cellDeps, rangeDeps);
                }
            }
            break;
        }
        case formula::ExpressionType::StructuredReference: {
            const auto& sr = static_cast<const formula::StructuredRefExpr&>(*expr);
            if (tableResolver_) {
                if (auto rng = tableResolver_->resolveTableColumn(sr.tableName, sr.columnName)) {
                    rangeDeps.push_back(*rng);
                }
            }
            break;
        }
        case formula::ExpressionType::ArrayExpression: {
            const auto& arr = static_cast<const formula::ArrayExpr&>(*expr);
            for (const auto& row : arr.rows) {
                for (const auto& elem : row) {
                    extractDependencies(activeSheetId, elem, cellDeps, rangeDeps);
                }
            }
            break;
        }
    }
}

void CalculationEngine::markDependentsDirty(SheetCellCoord origin) {
    auto deps = graph_.collectDependents(origin);
    for (const auto& d : deps) {
        if (formulas_.contains(d)) {
            dirtyCells_.insert(d);
        }
    }
}

CalculationStats CalculationEngine::recalculate() {
    return evaluateDirtySet();
}

CalculationStats CalculationEngine::recalculateAll() {
    for (const auto& [sc, _] : formulas_) {
        dirtyCells_.insert(sc);
    }
    return evaluateDirtySet();
}

CalculationStats CalculationEngine::evaluateDirtySet() {
    CalculationStats stats{};
    if (dirtyCells_.empty()) {
        return stats;
    }

    auto plan = graph_.buildEvaluationPlan(dirtyCells_);
    WorkbookValueProvider provider(workbook_);
    formula::Evaluator evaluator(registry_);

    for (const auto& level : plan.levels) {
        if (parallel_ && level.size() >= 256 && threadPool_) {
            struct EvalOutput {
                SheetCellCoord cell;
                CellValue value;
                FormulaId formulaId;
            };
            const std::size_t numChunks = std::min(threadPool_->threadCount(), level.size());
            const std::size_t chunkSize = (level.size() + numChunks - 1) / numChunks;
            std::vector<std::future<std::vector<EvalOutput>>> futures;
            futures.reserve(numChunks);

            for (std::size_t start = 0; start < level.size(); start += chunkSize) {
                const std::size_t end = std::min(start + chunkSize, level.size());
                futures.push_back(threadPool_->submit([&, start, end]() -> std::vector<EvalOutput> {
                    std::vector<EvalOutput> batchOut;
                    batchOut.reserve(end - start);
                    for (std::size_t i = start; i < end; ++i) {
                        const SheetCellCoord& sc = level[i];
                        auto fit = formulas_.find(sc);
                        if (fit == formulas_.end()) continue;
                        const CompiledFormula& cf = fit->second;
                        if (!cf.ast) {
                            batchOut.push_back(EvalOutput{sc, ErrorValue{ErrorType::Value}, cf.id});
                        } else {
                            formula::EvalContext ctx{sc.sheetId, sc.coord, &provider, &provider, tableResolver_};
                            batchOut.push_back(EvalOutput{sc, evaluator.evaluate(cf.ast, ctx), cf.id});
                        }
                    }
                    return batchOut;
                }));
            }

            for (auto& fut : futures) {
                auto batch = fut.get();
                for (auto& out : batch) {
                    if (Worksheet* ws = workbook_.getSheet(out.cell.sheetId)) {
                        ws->setCachedValue(out.cell.coord, std::move(out.value), out.formulaId);
                    }
                    ++stats.formulasEvaluated;
                }
            }
        } else {
            for (const auto& sc : level) {
                auto fit = formulas_.find(sc);
                if (fit == formulas_.end()) continue;
                const CompiledFormula& cf = fit->second;
                CellValue val = ErrorValue{ErrorType::Value};
                if (cf.ast) {
                    formula::EvalContext ctx{sc.sheetId, sc.coord, &provider, &provider, tableResolver_};
                    val = evaluator.evaluate(cf.ast, ctx);
                }
                if (Worksheet* ws = workbook_.getSheet(sc.sheetId)) {
                    ws->setCachedValue(sc.coord, std::move(val), cf.id);
                }
                ++stats.formulasEvaluated;
            }
        }
    }

    for (const auto& cyc : plan.cyclicCells) {
        auto fit = formulas_.find(cyc);
        FormulaId fid = fit != formulas_.end() ? fit->second.id : kInvalidFormulaId;
        if (Worksheet* ws = workbook_.getSheet(cyc.sheetId)) {
            ws->setCachedValue(cyc.coord, ErrorValue{ErrorType::Circular}, fid);
        }
        ++stats.circularCellsDetected;
    }

    dirtyCells_.clear();
    workbook_.notify(WorkbookEvent{WorkbookEventType::CalculationCompleted, kInvalidSheetId, std::nullopt, {}});
    return stats;
}

} // namespace sheetcore::calc
