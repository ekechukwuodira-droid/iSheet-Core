#include "sheetcore/data/range_ops.hpp"

#include <algorithm>
#include <cctype>

namespace sheetcore::data {

Result<void> RangeOps::writeBlock(Worksheet& ws, CellCoord topLeft,
                                  const std::vector<std::vector<CellValue>>& rows) {
    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (std::size_t c = 0; c < rows[r].size(); ++c) {
            auto res = ws.setValue(CellCoord{
                topLeft.row + static_cast<RowIndex>(r),
                topLeft.col + static_cast<ColIndex>(c)
            }, rows[r][c]);
            if (!res) return res;
        }
    }
    return Result<void>::ok();
}

std::vector<std::vector<CellValue>> RangeOps::readBlock(const Worksheet& ws, const RangeAddress& range) {
    auto n = range.normalized();
    std::vector<std::vector<CellValue>> out(n.rowCount(), std::vector<CellValue>(n.colCount(), EmptyValue{}));
    for (RowIndex r = 0; r < n.rowCount(); ++r) {
        for (ColIndex c = 0; c < n.colCount(); ++c) {
            out[r][c] = ws.getValue(CellCoord{n.start.row + r, n.start.col + c});
        }
    }
    return out;
}

Result<void> RangeOps::copyRange(const Worksheet& src, const RangeAddress& srcRange,
                                 Worksheet& dst, CellCoord dstTopLeft) {
    auto n = srcRange.normalized();
    // Snapshot first so overlapping copy within the same sheet is safe
    struct SnapshotItem {
        RowIndex dr;
        ColIndex dc;
        CellRecord rec;
    };
    std::vector<SnapshotItem> items;
    for (RowIndex r = 0; r < n.rowCount(); ++r) {
        for (ColIndex c = 0; c < n.colCount(); ++c) {
            if (const CellRecord* rec = src.getCell(CellCoord{n.start.row + r, n.start.col + c})) {
                items.push_back(SnapshotItem{r, c, *rec});
            }
        }
    }
    clearContents(dst, RangeAddress{
        dstTopLeft,
        CellCoord{dstTopLeft.row + n.rowCount() - 1, dstTopLeft.col + n.colCount() - 1}
    });
    for (const auto& item : items) {
        CellCoord target{dstTopLeft.row + item.dr, dstTopLeft.col + item.dc};
        if (item.rec.formulaText.has_value()) {
            dst.setFormula(target, *item.rec.formulaText);
            dst.setCachedValue(target, item.rec.value);
        } else if (!isEmpty(item.rec.value)) {
            dst.setValue(target, item.rec.value);
        }
        if (item.rec.styleId != kDefaultStyleId) {
            dst.setStyle(target, item.rec.styleId);
        }
    }
    return Result<void>::ok();
}

Result<void> RangeOps::moveRange(Worksheet& src, const RangeAddress& srcRange,
                                 Worksheet& dst, CellCoord dstTopLeft) {
    auto res = copyRange(src, srcRange, dst, dstTopLeft);
    if (!res) return res;
    auto n = srcRange.normalized();
    RangeAddress dstRect{
        dstTopLeft,
        CellCoord{dstTopLeft.row + n.rowCount() - 1, dstTopLeft.col + n.colCount() - 1}
    };
    for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
        for (ColIndex c = n.start.col; c <= n.end.col; ++c) {
            CellCoord coord{r, c};
            if (&src != &dst || !dstRect.contains(coord)) {
                src.clearCell(coord);
            }
        }
    }
    return Result<void>::ok();
}

void RangeOps::clearContents(Worksheet& ws, const RangeAddress& range) {
    auto n = range.normalized();
    for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
        for (ColIndex c = n.start.col; c <= n.end.col; ++c) {
            ws.setValue(CellCoord{r, c}, EmptyValue{});
        }
    }
}

void RangeOps::clearStyles(Worksheet& ws, const RangeAddress& range) {
    auto n = range.normalized();
    for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
        for (ColIndex c = n.start.col; c <= n.end.col; ++c) {
            ws.setStyle(CellCoord{r, c}, kDefaultStyleId);
        }
    }
}

Result<void> RangeOps::fillDown(Worksheet& ws, const RangeAddress& range) {
    auto n = range.normalized();
    if (n.rowCount() < 2) return Result<void>::ok();
    for (ColIndex c = n.start.col; c <= n.end.col; ++c) {
        const CellRecord* top = ws.getCell(CellCoord{n.start.row, c});
        CellRecord copy = top ? *top : CellRecord{};
        for (RowIndex r = n.start.row + 1; r <= n.end.row; ++r) {
            CellCoord dst{r, c};
            if (copy.formulaText.has_value()) {
                ws.setFormula(dst, *copy.formulaText);
            } else {
                ws.setValue(dst, copy.value);
            }
            ws.setStyle(dst, copy.styleId);
        }
    }
    return Result<void>::ok();
}

Result<void> RangeOps::fillRight(Worksheet& ws, const RangeAddress& range) {
    auto n = range.normalized();
    if (n.colCount() < 2) return Result<void>::ok();
    for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
        const CellRecord* left = ws.getCell(CellCoord{r, n.start.col});
        CellRecord copy = left ? *left : CellRecord{};
        for (ColIndex c = n.start.col + 1; c <= n.end.col; ++c) {
            CellCoord dst{r, c};
            if (copy.formulaText.has_value()) {
                ws.setFormula(dst, *copy.formulaText);
            } else {
                ws.setValue(dst, copy.value);
            }
            ws.setStyle(dst, copy.styleId);
        }
    }
    return Result<void>::ok();
}

Result<void> RangeOps::transpose(const Worksheet& src, const RangeAddress& srcRange,
                                 Worksheet& dst, CellCoord dstTopLeft) {
    auto block = readBlock(src, srcRange);
    std::size_t rows = block.size();
    std::size_t cols = rows > 0 ? block[0].size() : 0;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            dst.setValue(CellCoord{
                dstTopLeft.row + static_cast<RowIndex>(c),
                dstTopLeft.col + static_cast<ColIndex>(r)
            }, block[r][c]);
        }
    }
    return Result<void>::ok();
}

std::size_t RangeOps::findAndReplace(Worksheet& ws, const RangeAddress& range,
                                     std::string_view findText, std::string_view replaceText,
                                     bool matchCase) {
    if (findText.empty()) return 0;
    auto n = range.normalized();
    std::size_t replaced = 0;

    auto toLower = [](std::string_view sv) {
        std::string out(sv);
        for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return out;
    };
    std::string needleLower = matchCase ? std::string(findText) : toLower(findText);

    for (RowIndex r = n.start.row; r <= n.end.row; ++r) {
        for (ColIndex c = n.start.col; c <= n.end.col; ++c) {
            CellCoord coord{r, c};
            const CellRecord* rec = ws.getCell(coord);
            if (!rec || !isText(rec->value)) continue;
            const std::string& orig = std::get<std::string>(rec->value);
            std::string hay = matchCase ? orig : toLower(orig);
            std::string updated;
            std::size_t pos = 0;
            bool changed = false;
            while (true) {
                std::size_t found = hay.find(needleLower, pos);
                if (found == std::string::npos) {
                    updated.append(orig, pos, orig.size() - pos);
                    break;
                }
                updated.append(orig, pos, found - pos);
                updated.append(replaceText);
                pos = found + findText.size();
                changed = true;
            }
            if (changed) {
                ws.setValue(coord, std::move(updated));
                ++replaced;
            }
        }
    }
    return replaced;
}

Result<void> RangeOps::insertRows(Worksheet& ws, RowIndex atRow, RowIndex count) {
    if (count == 0) return Result<void>::ok();
    struct Moved { CellCoord oldCoord; CellRecord rec; };
    std::vector<Moved> toMove;
    ws.forEachCell([&](CellCoord c, const CellRecord& rec) {
        if (c.row >= atRow) {
            toMove.push_back(Moved{c, rec});
        }
    });
    for (const auto& m : toMove) ws.clearCell(m.oldCoord);
    for (const auto& m : toMove) {
        CellCoord nc{m.oldCoord.row + count, m.oldCoord.col};
        if (m.rec.formulaText.has_value()) {
            ws.setFormula(nc, *m.rec.formulaText);
            ws.setCachedValue(nc, m.rec.value);
        } else if (!isEmpty(m.rec.value)) {
            ws.setValue(nc, m.rec.value);
        }
        if (m.rec.styleId != kDefaultStyleId) ws.setStyle(nc, m.rec.styleId);
    }
    return Result<void>::ok();
}

Result<void> RangeOps::deleteRows(Worksheet& ws, RowIndex atRow, RowIndex count) {
    if (count == 0) return Result<void>::ok();
    struct Moved { CellCoord oldCoord; CellRecord rec; };
    std::vector<CellCoord> toDelete;
    std::vector<Moved> toMove;
    ws.forEachCell([&](CellCoord c, const CellRecord& rec) {
        if (c.row >= atRow && c.row < atRow + count) {
            toDelete.push_back(c);
        } else if (c.row >= atRow + count) {
            toMove.push_back(Moved{c, rec});
        }
    });
    for (const auto& c : toDelete) ws.clearCell(c);
    for (const auto& m : toMove) ws.clearCell(m.oldCoord);
    for (const auto& m : toMove) {
        CellCoord nc{m.oldCoord.row - count, m.oldCoord.col};
        if (m.rec.formulaText.has_value()) {
            ws.setFormula(nc, *m.rec.formulaText);
            ws.setCachedValue(nc, m.rec.value);
        } else if (!isEmpty(m.rec.value)) {
            ws.setValue(nc, m.rec.value);
        }
        if (m.rec.styleId != kDefaultStyleId) ws.setStyle(nc, m.rec.styleId);
    }
    return Result<void>::ok();
}

Result<void> RangeOps::insertCols(Worksheet& ws, ColIndex atCol, ColIndex count) {
    if (count == 0) return Result<void>::ok();
    struct Moved { CellCoord oldCoord; CellRecord rec; };
    std::vector<Moved> toMove;
    ws.forEachCell([&](CellCoord c, const CellRecord& rec) {
        if (c.col >= atCol) {
            toMove.push_back(Moved{c, rec});
        }
    });
    for (const auto& m : toMove) ws.clearCell(m.oldCoord);
    for (const auto& m : toMove) {
        CellCoord nc{m.oldCoord.row, m.oldCoord.col + count};
        if (m.rec.formulaText.has_value()) {
            ws.setFormula(nc, *m.rec.formulaText);
            ws.setCachedValue(nc, m.rec.value);
        } else if (!isEmpty(m.rec.value)) {
            ws.setValue(nc, m.rec.value);
        }
        if (m.rec.styleId != kDefaultStyleId) ws.setStyle(nc, m.rec.styleId);
    }
    return Result<void>::ok();
}

Result<void> RangeOps::deleteCols(Worksheet& ws, ColIndex atCol, ColIndex count) {
    if (count == 0) return Result<void>::ok();
    struct Moved { CellCoord oldCoord; CellRecord rec; };
    std::vector<CellCoord> toDelete;
    std::vector<Moved> toMove;
    ws.forEachCell([&](CellCoord c, const CellRecord& rec) {
        if (c.col >= atCol && c.col < atCol + count) {
            toDelete.push_back(c);
        } else if (c.col >= atCol + count) {
            toMove.push_back(Moved{c, rec});
        }
    });
    for (const auto& c : toDelete) ws.clearCell(c);
    for (const auto& m : toMove) ws.clearCell(m.oldCoord);
    for (const auto& m : toMove) {
        CellCoord nc{m.oldCoord.row, m.oldCoord.col - count};
        if (m.rec.formulaText.has_value()) {
            ws.setFormula(nc, *m.rec.formulaText);
            ws.setCachedValue(nc, m.rec.value);
        } else if (!isEmpty(m.rec.value)) {
            ws.setValue(nc, m.rec.value);
        }
        if (m.rec.styleId != kDefaultStyleId) ws.setStyle(nc, m.rec.styleId);
    }
    return Result<void>::ok();
}

} // namespace sheetcore::data
