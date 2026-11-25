#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sheetcore::data {

struct TableColumn {
    std::string name;
    std::optional<std::string> calculatedFormula;
};

struct TableDef {
    TableId id = kInvalidTableId;
    SheetId sheetId = kInvalidSheetId;
    std::string name;
    RangeAddress range{};
    bool hasHeaderRow = true;
    bool hasTotalsRow = false;
    std::vector<TableColumn> columns;
};

class TableStore {
public:
    Result<TableId> createTable(SheetId sheetId, std::string name, RangeAddress range,
                                std::vector<std::string> columnNames,
                                bool hasHeaderRow = true, bool hasTotalsRow = false);
    bool removeTable(TableId id);

    const TableDef* findById(TableId id) const noexcept;
    const TableDef* findByName(std::string_view name) const noexcept;
    std::optional<SheetRangeAddress> resolveColumnRange(std::string_view tableName,
                                                          std::string_view columnName) const noexcept;

private:
    TableId nextId_ = 1;
    std::vector<TableDef> tables_;
};

} // namespace sheetcore::data
