#pragma once

#include "sheetcore/core/types.hpp"
#include "sheetcore/document/cell.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sheetcore {

inline constexpr std::uint32_t kTileShift = 6; // 64 x 64
inline constexpr std::uint32_t kTileSize = 1u << kTileShift;
inline constexpr std::uint32_t kTileMask = kTileSize - 1u;

struct TileCoord {
    std::uint32_t tileRow = 0;
    std::uint32_t tileCol = 0;

    constexpr bool operator==(const TileCoord&) const noexcept = default;
};

struct TileCoordHash {
    std::size_t operator()(const TileCoord& t) const noexcept {
        return mixHash64((static_cast<std::uint64_t>(t.tileRow) << 32) | static_cast<std::uint64_t>(t.tileCol));
    }
};

class CellTile {
public:
    CellRecord* find(std::uint16_t localIndex) noexcept;
    const CellRecord* find(std::uint16_t localIndex) const noexcept;
    CellRecord& getOrCreate(std::uint16_t localIndex);
    bool erase(std::uint16_t localIndex);
    bool empty() const noexcept { return cells_.empty(); }
    std::size_t count() const noexcept { return cells_.size(); }

    template <typename Fn>
    void forEach(TileCoord tc, Fn&& fn) const {
        for (const auto& [localIdx, rec] : cells_) {
            RowIndex r = (tc.tileRow << kTileShift) | (localIdx >> kTileShift);
            ColIndex c = (tc.tileCol << kTileShift) | (localIdx & kTileMask);
            fn(CellCoord{r, c}, rec);
        }
    }

private:
    std::unordered_map<std::uint16_t, CellRecord> cells_;
};

class TileDirectory {
public:
    static constexpr TileCoord toTileCoord(CellCoord coord) noexcept {
        return TileCoord{coord.row >> kTileShift, coord.col >> kTileShift};
    }

    static constexpr std::uint16_t toLocalIndex(CellCoord coord) noexcept {
        return static_cast<std::uint16_t>(((coord.row & kTileMask) << kTileShift) | (coord.col & kTileMask));
    }

    const CellRecord* get(CellCoord coord) const noexcept;
    CellRecord* getMutable(CellCoord coord) noexcept;
    CellRecord& getOrCreate(CellCoord coord);
    bool removeIfEmpty(CellCoord coord);
    bool clearCell(CellCoord coord);

    std::size_t allocatedTileCount() const noexcept { return tiles_.size(); }
    std::size_t cellCount() const noexcept;
    std::optional<RangeAddress> usedRange() const noexcept;

    void forEachCell(const std::function<void(CellCoord, const CellRecord&)>& fn) const;

private:
    std::unordered_map<TileCoord, std::unique_ptr<CellTile>, TileCoordHash> tiles_;
};

class RowColumnStore {
public:
    void setRowHeight(RowIndex row, double height);
    std::optional<double> rowHeight(RowIndex row) const noexcept;
    void setRowHidden(RowIndex row, bool hidden);
    bool isRowHidden(RowIndex row) const noexcept;

    void setColWidth(ColIndex col, double width);
    std::optional<double> colWidth(ColIndex col) const noexcept;
    void setColHidden(ColIndex col, bool hidden);
    bool isColHidden(ColIndex col) const noexcept;

private:
    std::unordered_map<RowIndex, double> rowHeights_;
    std::unordered_set<RowIndex> hiddenRows_;
    std::unordered_map<ColIndex, double> colWidths_;
    std::unordered_set<ColIndex> hiddenCols_;
};

class MergedRegionStore {
public:
    bool add(const RangeAddress& range);
    bool remove(const RangeAddress& range);
    bool intersects(const RangeAddress& range) const noexcept;
    const std::vector<RangeAddress>& regions() const noexcept { return regions_; }

private:
    std::vector<RangeAddress> regions_;
};

} // namespace sheetcore
