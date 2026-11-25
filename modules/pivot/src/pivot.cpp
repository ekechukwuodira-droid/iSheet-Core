#include "sheetcore/pivot/pivot.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>

namespace sheetcore::pivot {

namespace {

struct Accumulator {
    std::size_t count = 0;
    std::size_t numCount = 0;
    double sum = 0.0;
    double product = 1.0;
    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();
    std::vector<double> nums;
    std::unordered_set<std::string> distinct;

    void add(const CellValue& v) {
        if (isEmpty(v)) return;
        ++count;
        distinct.insert(toDisplayString(v));
        if (auto n = toNumber(v)) {
            ++numCount;
            sum += *n;
            product *= *n;
            minVal = std::min(minVal, *n);
            maxVal = std::max(maxVal, *n);
            nums.push_back(*n);
        }
    }

    double finalize(PivotAggFunc fn) const {
        switch (fn) {
            case PivotAggFunc::Sum: return sum;
            case PivotAggFunc::Count: return static_cast<double>(count);
            case PivotAggFunc::Average: return numCount > 0 ? (sum / static_cast<double>(numCount)) : 0.0;
            case PivotAggFunc::Min: return numCount > 0 ? minVal : 0.0;
            case PivotAggFunc::Max: return numCount > 0 ? maxVal : 0.0;
            case PivotAggFunc::Product: return numCount > 0 ? product : 0.0;
            case PivotAggFunc::CountDistinct: return static_cast<double>(distinct.size());
            case PivotAggFunc::Var:
            case PivotAggFunc::StDev: {
                if (nums.size() < 2) return 0.0;
                double mean = sum / static_cast<double>(nums.size());
                double sq = 0.0;
                for (double d : nums) {
                    double diff = d - mean;
                    sq += diff * diff;
                }
                double var = sq / static_cast<double>(nums.size() - 1);
                return fn == PivotAggFunc::Var ? var : std::sqrt(var);
            }
        }
        return 0.0;
    }
};

} // namespace

Result<PivotCache> PivotCache::fromRange(const Worksheet& ws, const RangeAddress& range) {
    auto n = range.normalized();
    if (n.rowCount() < 2) {
        return Error{ErrorCode::InvalidArgument, "Pivot source range needs a header row and at least one data row"};
    }
    PivotCache cache;
    ColIndex cols = n.colCount();
    cache.rowCount_ = n.rowCount() - 1;
    cache.headers_.reserve(cols);
    cache.columns_.resize(cols);

    for (ColIndex c = 0; c < cols; ++c) {
        cache.headers_.push_back(toDisplayString(ws.getValue(CellCoord{n.start.row, n.start.col + c})));
        cache.columns_[c].reserve(cache.rowCount_);
        for (RowIndex r = n.start.row + 1; r <= n.end.row; ++r) {
            cache.columns_[c].push_back(ws.getValue(CellCoord{r, n.start.col + c}));
        }
    }
    return cache;
}

int PivotCache::findFieldIndex(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < headers_.size(); ++i) {
        if (headers_[i] == name) return static_cast<int>(i);
    }
    return -1;
}

Result<PivotResult> PivotEngine::compute(const PivotCache& cache, const PivotConfig& config) {
    if (config.valueFields.empty()) {
        return Error{ErrorCode::InvalidArgument, "Pivot table requires at least one value field"};
    }

    auto resolveIndices = [&](const std::vector<std::string>& names) -> Result<std::vector<std::size_t>> {
        std::vector<std::size_t> idxs;
        for (const auto& nm : names) {
            int idx = cache.findFieldIndex(nm);
            if (idx < 0) return Error{ErrorCode::NotFound, "Pivot field not found: " + nm};
            idxs.push_back(static_cast<std::size_t>(idx));
        }
        return idxs;
    };

    auto rowIdxs = resolveIndices(config.rowFields);
    if (!rowIdxs) return rowIdxs.error();
    auto colIdxs = resolveIndices(config.colFields);
    if (!colIdxs) return colIdxs.error();

    std::vector<std::size_t> valIdxs;
    for (const auto& vf : config.valueFields) {
        int idx = cache.findFieldIndex(vf.fieldName);
        if (idx < 0) return Error{ErrorCode::NotFound, "Pivot value field not found: " + vf.fieldName};
        valIdxs.push_back(static_cast<std::size_t>(idx));
    }

    std::vector<std::pair<std::size_t, std::unordered_set<std::string>>> filters;
    for (const auto& f : config.reportFilters) {
        int idx = cache.findFieldIndex(f.fieldName);
        if (idx < 0) return Error{ErrorCode::NotFound, "Pivot filter field not found: " + f.fieldName};
        std::unordered_set<std::string> allowed(f.allowedValues.begin(), f.allowedValues.end());
        filters.emplace_back(static_cast<std::size_t>(idx), std::move(allowed));
    }

    std::map<std::string, std::size_t> rowKeyMap;
    std::map<std::string, std::size_t> colKeyMap;
    std::map<std::pair<std::string, std::string>, std::vector<Accumulator>> buckets;
    std::map<std::string, std::vector<Accumulator>> colTotals;

    auto buildKey = [&](const std::vector<std::size_t>& idxs, std::size_t r) {
        if (idxs.empty()) return std::string("(All)");
        std::string key;
        for (std::size_t i = 0; i < idxs.size(); ++i) {
            if (i > 0) key += " | ";
            key += toDisplayString(cache.column(idxs[i])[r]);
        }
        return key;
    };

    for (std::size_t r = 0; r < cache.rowCount(); ++r) {
        bool pass = true;
        for (const auto& [fIdx, allowed] : filters) {
            if (!allowed.contains(toDisplayString(cache.column(fIdx)[r]))) {
                pass = false;
                break;
            }
        }
        if (!pass) continue;

        std::string rk = buildKey(rowIdxs.value(), r);
        std::string ck = buildKey(colIdxs.value(), r);
        rowKeyMap.try_emplace(rk, 0);
        colKeyMap.try_emplace(ck, 0);

        auto& accVec = buckets[{rk, ck}];
        if (accVec.empty()) accVec.resize(valIdxs.size());
        auto& totVec = colTotals[ck];
        if (totVec.empty()) totVec.resize(valIdxs.size());

        for (std::size_t vi = 0; vi < valIdxs.size(); ++vi) {
            const CellValue& val = cache.column(valIdxs[vi])[r];
            accVec[vi].add(val);
            totVec[vi].add(val);
        }
    }

    PivotResult res;
    for (auto& [k, idx] : rowKeyMap) {
        idx = res.rowKeys.size();
        res.rowKeys.push_back(k);
    }
    for (auto& [k, idx] : colKeyMap) {
        idx = res.colKeys.size();
        res.colKeys.push_back(k);
    }

    const std::size_t vCount = config.valueFields.size();
    const std::size_t totalCols = res.colKeys.size() * vCount;
    res.matrix.assign(res.rowKeys.size(), std::vector<double>(totalCols, 0.0));
    res.grandTotals.assign(totalCols, 0.0);

    for (const auto& [rk, ri] : rowKeyMap) {
        for (const auto& [ck, ci] : colKeyMap) {
            auto it = buckets.find({rk, ck});
            if (it == buckets.end()) continue;
            for (std::size_t vi = 0; vi < vCount; ++vi) {
                res.matrix[ri][ci * vCount + vi] = it->second[vi].finalize(config.valueFields[vi].agg);
            }
        }
    }

    if (config.showGrandTotals) {
        for (const auto& [ck, ci] : colKeyMap) {
            auto it = colTotals.find(ck);
            if (it == colTotals.end()) continue;
            for (std::size_t vi = 0; vi < vCount; ++vi) {
                res.grandTotals[ci * vCount + vi] = it->second[vi].finalize(config.valueFields[vi].agg);
            }
        }
    }

    return res;
}

} // namespace sheetcore::pivot
