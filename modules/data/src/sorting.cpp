#include "sheetcore/data/sorting.hpp"
#include "sheetcore/document/cell.hpp"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace sheetcore::data {

namespace {

int typeRank(const CellValue& v) {
    if (isEmpty(v)) return 4;
    if (isError(v)) return 3;
    if (isBool(v)) return 2;
    if (isText(v)) return 1;
    return 0; // double / DateTimeValue
}

int compareCells(const CellValue& a, const CellValue& b, bool caseSensitive) {
    int ra = typeRank(a);
    int rb = typeRank(b);
    if (ra != rb) return ra < rb ? -1 : 1;

    if (ra == 0) {
        double da = isDateTime(a) ? std::get<DateTimeValue>(a).serial : std::get<double>(a);
        double db = isDateTime(b) ? std::get<DateTimeValue>(b).serial : std::get<double>(b);
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }
    if (ra == 1) {
        std::string sa = std::get<std::string>(a);
        std::string sb = std::get<std::string>(b);
        if (!caseSensitive) {
            for (char& c : sa) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : sb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (sa < sb) return -1;
        if (sa > sb) return 1;
        return 0;
    }
    if (ra == 2) {
        bool ba = std::get<bool>(a), bb = std::get<bool>(b);
        return ba == bb ? 0 : (ba ? 1 : -1);
    }
    return 0;
}

} // namespace

Result<void> Sorter::sort(Worksheet& ws, const RangeAddress& range, const SortOptions& options) {
    auto n = range.normalized();
    RowIndex startRow = options.hasHeader ? n.start.row + 1 : n.start.row;
    if (startRow > n.end.row || options.keys.empty()) {
        return Result<void>::ok();
    }

    RowIndex numRows = n.end.row - startRow + 1;
    ColIndex numCols = n.colCount();

    for (const auto& k : options.keys) {
        if (k.columnOffset >= numCols) {
            return Error{ErrorCode::OutOfRange, "Sort key columnOffset exceeds range width"};
        }
    }

    // Snapshot rows
    std::vector<std::vector<CellRecord>> rows(numRows, std::vector<CellRecord>(numCols));
    for (RowIndex r = 0; r < numRows; ++r) {
        for (ColIndex c = 0; c < numCols; ++c) {
            if (const CellRecord* rec = ws.getCell(CellCoord{startRow + r, n.start.col + c})) {
                rows[r][c] = *rec;
            }
        }
    }

    std::vector<std::size_t> order(numRows);
    std::iota(order.begin(), order.end(), 0);

    std::stable_sort(order.begin(), order.end(), [&](std::size_t ia, std::size_t ib) {
        for (const auto& key : options.keys) {
            const CellValue& va = rows[ia][key.columnOffset].value;
            const CellValue& vb = rows[ib][key.columnOffset].value;
            int cmp = compareCells(va, vb, key.caseSensitive);
            if (cmp != 0) {
                return key.order == SortOrder::Ascending ? (cmp < 0) : (cmp > 0);
            }
        }
        return false;
    });

    for (RowIndex r = 0; r < numRows; ++r) {
        const auto& srcRow = rows[order[r]];
        for (ColIndex c = 0; c < numCols; ++c) {
            CellCoord dst{startRow + r, n.start.col + c};
            const CellRecord& rec = srcRow[c];
            if (rec.isEffectivelyEmpty()) {
                ws.clearCell(dst);
            } else {
                if (rec.formulaText.has_value()) {
                    ws.setFormula(dst, *rec.formulaText);
                    ws.setCachedValue(dst, rec.value);
                } else {
                    ws.setValue(dst, rec.value);
                }
                ws.setStyle(dst, rec.styleId);
            }
        }
    }

    return Result<void>::ok();
}

} // namespace sheetcore::data
