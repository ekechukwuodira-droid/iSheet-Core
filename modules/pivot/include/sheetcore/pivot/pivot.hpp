#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"
#include "sheetcore/document/worksheet.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace sheetcore::pivot {

enum class PivotAggFunc {
    Sum,
    Count,
    Average,
    Min,
    Max,
    Product,
    StDev,
    Var,
    CountDistinct
};

struct PivotValueField {
    std::string fieldName;
    PivotAggFunc agg = PivotAggFunc::Sum;
    std::string displayName;
};

struct PivotFilterField {
    std::string fieldName;
    std::vector<std::string> allowedValues;
};

struct PivotConfig {
    std::vector<std::string> rowFields;
    std::vector<std::string> colFields;
    std::vector<PivotValueField> valueFields;
    std::vector<PivotFilterField> reportFilters;
    bool showGrandTotals = true;
};

// Columnar cache for fast aggregation without repeated sparse-tile lookups
class PivotCache {
public:
    static Result<PivotCache> fromRange(const Worksheet& ws, const RangeAddress& range);

    std::size_t rowCount() const noexcept { return rowCount_; }
    std::size_t colCount() const noexcept { return headers_.size(); }
    int findFieldIndex(std::string_view name) const noexcept;

    const std::vector<std::string>& headers() const noexcept { return headers_; }
    const std::vector<CellValue>& column(std::size_t idx) const { return columns_[idx]; }

private:
    std::vector<std::string> headers_;
    std::vector<std::vector<CellValue>> columns_;
    std::size_t rowCount_ = 0;
};

struct PivotResult {
    std::vector<std::string> rowKeys;
    std::vector<std::string> colKeys;
    // matrix[rowIdx][colIdx * valueFields.size() + valFieldIdx]
    std::vector<std::vector<double>> matrix;
    std::vector<double> grandTotals; // per (colIdx * valueFields.size() + valFieldIdx)
};

class PivotEngine {
public:
    static Result<PivotResult> compute(const PivotCache& cache, const PivotConfig& config);
};

} // namespace sheetcore::pivot
