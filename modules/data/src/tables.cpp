#include "sheetcore/data/tables.hpp"

#include <algorithm>

namespace sheetcore::data {

Result<TableId> TableStore::createTable(SheetId sheetId, std::string name, RangeAddress range,
                                        std::vector<std::string> columnNames,
                                        bool hasHeaderRow, bool hasTotalsRow) {
    if (name.empty()) {
        return Error{ErrorCode::InvalidArgument, "Table name cannot be empty"};
    }
    if (findByName(name) != nullptr) {
        return Error{ErrorCode::AlreadyExists, "Table already exists: " + name};
    }
    TableDef def;
    def.id = nextId_++;
    def.sheetId = sheetId;
    def.name = std::move(name);
    def.range = range.normalized();
    def.hasHeaderRow = hasHeaderRow;
    def.hasTotalsRow = hasTotalsRow;
    for (auto& cn : columnNames) {
        def.columns.push_back(TableColumn{std::move(cn), std::nullopt});
    }
    TableId id = def.id;
    tables_.push_back(std::move(def));
    return id;
}

bool TableStore::removeTable(TableId id) {
    auto it = std::find_if(tables_.begin(), tables_.end(), [id](const TableDef& t) { return t.id == id; });
    if (it == tables_.end()) return false;
    tables_.erase(it);
    return true;
}

const TableDef* TableStore::findById(TableId id) const noexcept {
    for (const auto& t : tables_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

const TableDef* TableStore::findByName(std::string_view name) const noexcept {
    for (const auto& t : tables_) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

std::optional<SheetRangeAddress> TableStore::resolveColumnRange(std::string_view tableName,
                                                                  std::string_view columnName) const noexcept {
    const TableDef* tbl = findByName(tableName);
    if (!tbl) return std::nullopt;
    for (std::size_t i = 0; i < tbl->columns.size(); ++i) {
        if (tbl->columns[i].name == columnName) {
            ColIndex col = tbl->range.start.col + static_cast<ColIndex>(i);
            RowIndex startRow = tbl->hasHeaderRow ? tbl->range.start.row + 1 : tbl->range.start.row;
            RowIndex endRow = tbl->hasTotalsRow && tbl->range.end.row > startRow
                ? tbl->range.end.row - 1
                : tbl->range.end.row;
            if (startRow > endRow) return std::nullopt;
            return SheetRangeAddress{tbl->sheetId, RangeAddress{CellCoord{startRow, col}, CellCoord{endRow, col}}};
        }
    }
    return std::nullopt;
}

} // namespace sheetcore::data
