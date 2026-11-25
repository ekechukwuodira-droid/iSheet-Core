#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"
#include "sheetcore/document/worksheet.hpp"

#include <vector>

namespace sheetcore::data {

enum class FilterOp {
    Equals,
    NotEquals,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
    Contains,
    IsEmpty,
    IsNotEmpty
};

struct FilterCondition {
    FilterOp op = FilterOp::Equals;
    CellValue operand = EmptyValue{};
    bool caseSensitive = false;
};

struct ColumnFilter {
    ColIndex columnOffset = 0; // relative to range.start.col
    bool matchAll = true;      // true => AND, false => OR
    std::vector<FilterCondition> conditions;
};

class FilterEngine {
public:
    static Result<std::size_t> apply(Worksheet& ws, const RangeAddress& range,
                                     const std::vector<ColumnFilter>& filters,
                                     bool hasHeader = true);
    static void clearFilter(Worksheet& ws, const RangeAddress& range);
};

} // namespace sheetcore::data
