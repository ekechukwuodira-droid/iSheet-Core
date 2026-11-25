#include "sheetcore/document/tile_storage.hpp"

#include <algorithm>

namespace sheetcore {

CellRecord* CellTile::find(std::uint16_t localIndex) noexcept {
    auto it = cells_.find(localIndex);
    return it == cells_.end() ? nullptr : &it->second;
}

const CellRecord* CellTile::find(std::uint16_t localIndex) const noexcept {
    auto it = cells_.find(localIndex);
    return it == cells_.end() ? nullptr : &it->second;
}

CellRecord& CellTile::getOrCreate(std::uint16_t localIndex) {
    return cells_[localIndex];
}

bool CellTile::erase(std::uint16_t localIndex) {
    return cells_.erase(localIndex) > 0;
}

const CellRecord* TileDirectory::get(CellCoord coord) const noexcept {
    auto it = tiles_.find(toTileCoord(coord));
    if (it == tiles_.end()) {
        return nullptr;
    }
    return it->second->find(toLocalIndex(coord));
}

CellRecord* TileDirectory::getMutable(CellCoord coord) noexcept {
    auto it = tiles_.find(toTileCoord(coord));
    if (it == tiles_.end()) {
        return nullptr;
    }
    return it->second->find(toLocalIndex(coord));
}

CellRecord& TileDirectory::getOrCreate(CellCoord coord) {
    TileCoord tc = toTileCoord(coord);
    auto& tile = tiles_[tc];
    if (!tile) {
        tile = std::make_unique<CellTile>();
    }
    return tile->getOrCreate(toLocalIndex(coord));
}

bool TileDirectory::removeIfEmpty(CellCoord coord) {
    TileCoord tc = toTileCoord(coord);
    auto it = tiles_.find(tc);
    if (it == tiles_.end()) {
        return false;
    }
    std::uint16_t local = toLocalIndex(coord);
    const CellRecord* rec = it->second->find(local);
    if (!rec || !rec->isEffectivelyEmpty()) {
        return false;
    }
    it->second->erase(local);
    if (it->second->empty()) {
        tiles_.erase(it);
    }
    return true;
}

bool TileDirectory::clearCell(CellCoord coord) {
    TileCoord tc = toTileCoord(coord);
    auto it = tiles_.find(tc);
    if (it == tiles_.end()) {
        return false;
    }
    bool erased = it->second->erase(toLocalIndex(coord));
    if (erased && it->second->empty()) {
        tiles_.erase(it);
    }
    return erased;
}

std::size_t TileDirectory::cellCount() const noexcept {
    std::size_t total = 0;
    for (const auto& [tc, tile] : tiles_) {
        total += tile->count();
    }
    return total;
}

std::optional<RangeAddress> TileDirectory::usedRange() const noexcept {
    if (tiles_.empty()) {
        return std::nullopt;
    }
    RowIndex minRow = UINT32_MAX;
    ColIndex minCol = UINT32_MAX;
    RowIndex maxRow = 0;
    ColIndex maxCol = 0;
    bool any = false;
    for (const auto& [tc, tile] : tiles_) {
        tile->forEach(tc, [&](CellCoord c, const CellRecord&) {
            any = true;
            minRow = std::min(minRow, c.row);
            minCol = std::min(minCol, c.col);
            maxRow = std::max(maxRow, c.row);
            maxCol = std::max(maxCol, c.col);
        });
    }
    if (!any) {
        return std::nullopt;
    }
    return RangeAddress{CellCoord{minRow, minCol}, CellCoord{maxRow, maxCol}};
}

void TileDirectory::forEachCell(const std::function<void(CellCoord, const CellRecord&)>& fn) const {
    for (const auto& [tc, tile] : tiles_) {
        tile->forEach(tc, fn);
    }
}

void RowColumnStore::setRowHeight(RowIndex row, double height) { rowHeights_[row] = height; }

std::optional<double> RowColumnStore::rowHeight(RowIndex row) const noexcept {
    auto it = rowHeights_.find(row);
    return it == rowHeights_.end() ? std::nullopt : std::optional<double>(it->second);
}

void RowColumnStore::setRowHidden(RowIndex row, bool hidden) {
    if (hidden) hiddenRows_.insert(row);
    else hiddenRows_.erase(row);
}

bool RowColumnStore::isRowHidden(RowIndex row) const noexcept {
    return hiddenRows_.contains(row);
}

void RowColumnStore::setColWidth(ColIndex col, double width) { colWidths_[col] = width; }

std::optional<double> RowColumnStore::colWidth(ColIndex col) const noexcept {
    auto it = colWidths_.find(col);
    return it == colWidths_.end() ? std::nullopt : std::optional<double>(it->second);
}

void RowColumnStore::setColHidden(ColIndex col, bool hidden) {
    if (hidden) hiddenCols_.insert(col);
    else hiddenCols_.erase(col);
}

bool RowColumnStore::isColHidden(ColIndex col) const noexcept {
    return hiddenCols_.contains(col);
}

bool MergedRegionStore::add(const RangeAddress& range) {
    auto norm = range.normalized();
    if (intersects(norm)) {
        return false;
    }
    regions_.push_back(norm);
    return true;
}

bool MergedRegionStore::remove(const RangeAddress& range) {
    auto norm = range.normalized();
    auto it = std::find(regions_.begin(), regions_.end(), norm);
    if (it == regions_.end()) {
        return false;
    }
    regions_.erase(it);
    return true;
}

bool MergedRegionStore::intersects(const RangeAddress& range) const noexcept {
    auto norm = range.normalized();
    for (const auto& r : regions_) {
        if (r.intersects(norm)) {
            return true;
        }
    }
    return false;
}

} // namespace sheetcore
