#include "sheetcore/data/filtering.hpp"

#include <cctype>

namespace sheetcore::data {

namespace {

std::string toLowerStr(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool evaluateCondition(const CellValue& val, const FilterCondition& cond) {
    switch (cond.op) {
        case FilterOp::IsEmpty:
            return isEmpty(val);
        case FilterOp::IsNotEmpty:
            return !isEmpty(val);
        case FilterOp::Contains: {
            std::string hay = toDisplayString(val);
            std::string needle = toDisplayString(cond.operand);
            if (!cond.caseSensitive) {
                hay = toLowerStr(hay);
                needle = toLowerStr(needle);
            }
            return hay.find(needle) != std::string::npos;
        }
        case FilterOp::Equals:
        case FilterOp::NotEquals: {
            bool eq = false;
            if (isText(val) && isText(cond.operand) && !cond.caseSensitive) {
                eq = toLowerStr(std::get<std::string>(val)) ==
                     toLowerStr(std::get<std::string>(cond.operand));
            } else {
                eq = (val == cond.operand);
            }
            return cond.op == FilterOp::Equals ? eq : !eq;
        }
        case FilterOp::Greater:
        case FilterOp::GreaterOrEqual:
        case FilterOp::Less:
        case FilterOp::LessOrEqual: {
            auto a = toNumber(val);
            auto b = toNumber(cond.operand);
            if (!a || !b || isEmpty(val)) return false;
            switch (cond.op) {
                case FilterOp::Greater: return *a > *b;
                case FilterOp::GreaterOrEqual: return *a >= *b;
                case FilterOp::Less: return *a < *b;
                case FilterOp::LessOrEqual: return *a <= *b;
                default: return false;
            }
        }
    }
    return false;
}

bool evaluateColumnFilter(const CellValue& val, const ColumnFilter& cf) {
    if (cf.conditions.empty()) return true;
    if (cf.matchAll) {
        for (const auto& c : cf.conditions) {
            if (!evaluateCondition(val, c)) return false;
        }
        return true;
    } else {
        for (const auto& c : cf.conditions) {
            if (evaluateCondition(val, c)) return true;
        }
        return false;
    }
}

} // namespace

Result<std::size_t> FilterEngine::apply(Worksheet& ws, const RangeAddress& range,
                                        const std::vector<ColumnFilter>& filters,
                                        bool hasHeader) {
    auto n = range.normalized();
    RowIndex startRow = hasHeader ? n.start.row + 1 : n.start.row;
    ColIndex numCols = n.colCount();
    for (const auto& f : filters) {
        if (f.columnOffset >= numCols) {
            return Error{ErrorCode::OutOfRange, "Filter columnOffset exceeds range width"};
        }
    }

    std::size_t visibleCount = 0;
    for (RowIndex r = startRow; r <= n.end.row; ++r) {
        bool visible = true;
        for (const auto& cf : filters) {
            CellValue val = ws.getValue(CellCoord{r, n.start.col + cf.columnOffset});
            if (!evaluateColumnFilter(val, cf)) {
                visible = false;
                break;
            }
        }
        ws.rowColumns().setRowHidden(r, !visible);
        if (visible) ++visibleCount;
    }
    return visibleCount;
}

void FilterEngine::clearFilter(Worksheet& ws, const RangeAddress& range) {
    auto n = range.normalized();
    for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
        ws.rowColumns().setRowHidden(r, false);
    }
}

} // namespace sheetcore::data
