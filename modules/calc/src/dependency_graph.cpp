#include "sheetcore/calc/dependency_graph.hpp"

#include <algorithm>
#include <queue>

namespace sheetcore::calc {

void DependencyGraph::setDependencies(SheetCellCoord formulaCell,
                                      std::vector<SheetCellCoord> cellDeps,
                                      std::vector<SheetRangeAddress> rangeDeps) {
    removeFormula(formulaCell);

    for (const auto& dep : cellDeps) {
        reverseCells_[dep].insert(formulaCell);
    }
    for (const auto& rdep : rangeDeps) {
        reverseRangesBySheet_[rdep.sheetId].push_back(RangeDepEntry{formulaCell, rdep.range.normalized()});
    }

    forward_[formulaCell] = ForwardDeps{std::move(cellDeps), std::move(rangeDeps)};
}

void DependencyGraph::removeFormula(SheetCellCoord formulaCell) {
    auto it = forward_.find(formulaCell);
    if (it == forward_.end()) {
        return;
    }
    for (const auto& dep : it->second.cells) {
        auto rit = reverseCells_.find(dep);
        if (rit != reverseCells_.end()) {
            rit->second.erase(formulaCell);
            if (rit->second.empty()) {
                reverseCells_.erase(rit);
            }
        }
    }
    for (const auto& rdep : it->second.ranges) {
        auto sit = reverseRangesBySheet_.find(rdep.sheetId);
        if (sit != reverseRangesBySheet_.end()) {
            auto& vec = sit->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [&](const RangeDepEntry& e) { return e.formulaCell == formulaCell; }), vec.end());
            if (vec.empty()) {
                reverseRangesBySheet_.erase(sit);
            }
        }
    }
    forward_.erase(it);
}

void DependencyGraph::clear() {
    forward_.clear();
    reverseCells_.clear();
    reverseRangesBySheet_.clear();
}

std::vector<SheetCellCoord> DependencyGraph::collectDependents(SheetCellCoord changedCell) const {
    std::vector<SheetCellCoord> result;
    std::unordered_set<SheetCellCoord, SheetCellCoordHash> visited;
    std::queue<SheetCellCoord> q;

    auto enqueueDependents = [&](SheetCellCoord node) {
        auto cit = reverseCells_.find(node);
        if (cit != reverseCells_.end()) {
            for (const auto& dep : cit->second) {
                if (visited.insert(dep).second) {
                    q.push(dep);
                    result.push_back(dep);
                }
            }
        }
        auto rit = reverseRangesBySheet_.find(node.sheetId);
        if (rit != reverseRangesBySheet_.end()) {
            for (const auto& entry : rit->second) {
                if (entry.range.contains(node.coord)) {
                    if (visited.insert(entry.formulaCell).second) {
                        q.push(entry.formulaCell);
                        result.push_back(entry.formulaCell);
                    }
                }
            }
        }
    };

    enqueueDependents(changedCell);
    while (!q.empty()) {
        SheetCellCoord curr = q.front();
        q.pop();
        enqueueDependents(curr);
    }
    return result;
}

DependencyGraph::TopoPlan DependencyGraph::buildEvaluationPlan(
    const std::unordered_set<SheetCellCoord, SheetCellCoordHash>& dirtySet) const {
    TopoPlan plan;
    if (dirtySet.empty()) {
        return plan;
    }

    // Build in-degree and adjacency within the dirty subgraph.
    // Edge direction: dependency -> dependent (must evaluate dependency before dependent).
    std::unordered_map<SheetCellCoord, std::size_t, SheetCellCoordHash> inDegree;
    std::unordered_map<SheetCellCoord, std::vector<SheetCellCoord>, SheetCellCoordHash> adj;

    for (const auto& node : dirtySet) {
        inDegree[node] = 0;
    }

    for (const auto& node : dirtySet) {
        auto fit = forward_.find(node);
        if (fit == forward_.end()) continue;

        std::unordered_set<SheetCellCoord, SheetCellCoordHash> uniqueDeps;
        for (const auto& dep : fit->second.cells) {
            if (dirtySet.contains(dep)) {
                uniqueDeps.insert(dep);
            }
        }
        for (const auto& rdep : fit->second.ranges) {
            for (const auto& candidate : dirtySet) {
                if (candidate.sheetId == rdep.sheetId && rdep.range.contains(candidate.coord)) {
                    uniqueDeps.insert(candidate);
                }
            }
        }

        for (const auto& dep : uniqueDeps) {
            adj[dep].push_back(node);
            ++inDegree[node];
        }
    }

    std::vector<SheetCellCoord> currentLevel;
    for (const auto& [node, deg] : inDegree) {
        if (deg == 0) {
            currentLevel.push_back(node);
        }
    }

    std::size_t processed = 0;
    while (!currentLevel.empty()) {
        std::vector<SheetCellCoord> nextLevel;
        for (const auto& u : currentLevel) {
            ++processed;
            auto ait = adj.find(u);
            if (ait != adj.end()) {
                for (const auto& v : ait->second) {
                    if (--inDegree[v] == 0) {
                        nextLevel.push_back(v);
                    }
                }
            }
        }
        plan.levels.push_back(std::move(currentLevel));
        currentLevel = std::move(nextLevel);
    }

    if (processed < dirtySet.size()) {
        for (const auto& [node, deg] : inDegree) {
            if (deg > 0) {
                plan.cyclicCells.push_back(node);
            }
        }
    }

    return plan;
}

} // namespace sheetcore::calc
