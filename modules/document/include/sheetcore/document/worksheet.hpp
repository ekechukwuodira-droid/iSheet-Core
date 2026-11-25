#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"
#include "sheetcore/document/cell.hpp"
#include "sheetcore/document/tile_storage.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sheetcore {

class Workbook;

struct FrozenPanes {
    RowIndex rows = 0;
    ColIndex cols = 0;
};

struct SheetProtection {
    bool protectedSheet = false;
    bool allowSelectLocked = true;
    bool allowSelectUnlocked = true;
};

class Worksheet {
public:
    Worksheet(Workbook& owner, SheetId id, std::string name);

    SheetId id() const noexcept { return id_; }
    const std::string& name() const noexcept { return name_; }

    // Cell value & formula operations (by coordinate or A1 string)
    Result<void> setValue(CellCoord coord, CellValue value);
    Result<void> setValue(std::string_view a1, CellValue value);

    Result<void> setFormula(CellCoord coord, std::string formula);
    Result<void> setFormula(std::string_view a1, std::string formula);

    // Internal/calc-engine update of cached result without overwriting formulaText
    void setCachedValue(CellCoord coord, CellValue value, std::optional<FormulaId> formulaId = std::nullopt);

    Result<void> setStyle(CellCoord coord, StyleId styleId);
    Result<void> clearCell(CellCoord coord);
    Result<void> clearCell(std::string_view a1);

    CellValue getValue(CellCoord coord) const;
    CellValue getValue(std::string_view a1) const;
    const CellRecord* getCell(CellCoord coord) const noexcept;
    const CellRecord* getCell(std::string_view a1) const noexcept;

    // Sparse storage inspection
    std::size_t allocatedTileCount() const noexcept { return tiles_.allocatedTileCount(); }
    std::size_t cellCount() const noexcept { return tiles_.cellCount(); }
    std::optional<RangeAddress> usedRange() const noexcept { return tiles_.usedRange(); }
    void forEachCell(const std::function<void(CellCoord, const CellRecord&)>& fn) const;

    // Row/Column properties
    RowColumnStore& rowColumns() noexcept { return rowCols_; }
    const RowColumnStore& rowColumns() const noexcept { return rowCols_; }

    // Merged regions
    bool mergeCells(const RangeAddress& range);
    bool unmergeCells(const RangeAddress& range);
    const std::vector<RangeAddress>& mergedRegions() const noexcept { return merged_.regions(); }

    // View & protection metadata
    void setFrozenPanes(FrozenPanes panes) noexcept { frozen_ = panes; }
    FrozenPanes frozenPanes() const noexcept { return frozen_; }

    void setProtection(SheetProtection prot) noexcept { protection_ = prot; }
    const SheetProtection& protection() const noexcept { return protection_; }

private:
    friend class Workbook;
    void setNameInternal(std::string newName) { name_ = std::move(newName); }
    void rebindOwner(Workbook& newOwner) noexcept { owner_ = &newOwner; }

    Workbook* owner_;
    SheetId id_;
    std::string name_;
    TileDirectory tiles_;
    RowColumnStore rowCols_;
    MergedRegionStore merged_;
    FrozenPanes frozen_{};
    SheetProtection protection_{};
};

} // namespace sheetcore
