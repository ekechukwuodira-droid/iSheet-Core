#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/document/worksheet.hpp"

#include <vector>

namespace sheetcore::data {

enum class SortOrder {
    Ascending,
    Descending
};

struct SortKey {
    ColIndex columnOffset = 0; // 0-based offset relative to range.start.col
    SortOrder order = SortOrder::Ascending;
    bool caseSensitive = false;
};

struct SortOptions {
    bool hasHeader = false;
    std::vector<SortKey> keys;

    void addKey(ColIndex colOffset, SortOrder order = SortOrder::Ascending, bool caseSensitive = false) {
        keys.push_back(SortKey{colOffset, order, caseSensitive});
    }
};

class Sorter {
public:
    static Result<void> sort(Worksheet& ws, const RangeAddress& range, const SortOptions& options);
};

} // namespace sheetcore::data
