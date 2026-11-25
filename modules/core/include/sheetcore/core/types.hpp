#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sheetcore {

using SheetId = std::uint32_t;
using StyleId = std::uint32_t;
using FormulaId = std::uint32_t;
using TableId = std::uint32_t;
using RowIndex = std::uint32_t;
using ColIndex = std::uint32_t;

inline constexpr SheetId kInvalidSheetId = 0;
inline constexpr StyleId kDefaultStyleId = 0;
inline constexpr FormulaId kInvalidFormulaId = 0;
inline constexpr TableId kInvalidTableId = 0;

struct CellCoord {
    RowIndex row = 0; // 0-based
    ColIndex col = 0; // 0-based

    constexpr auto operator<=>(const CellCoord&) const = default;

    std::string toA1(bool absCol = false, bool absRow = false) const;
    static std::optional<CellCoord> fromA1(std::string_view text);
};

inline std::size_t mixHash64(std::uint64_t x) noexcept {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<std::size_t>(x);
}

struct CellCoordHash {
    std::size_t operator()(const CellCoord& c) const noexcept {
        return mixHash64((static_cast<std::uint64_t>(c.row) << 32) | static_cast<std::uint64_t>(c.col));
    }
};

struct RangeAddress {
    CellCoord start{};
    CellCoord end{};

    constexpr auto operator<=>(const RangeAddress&) const = default;

    RangeAddress normalized() const noexcept;
    bool contains(CellCoord coord) const noexcept;
    bool intersects(const RangeAddress& other) const noexcept;
    std::uint64_t cellCount() const noexcept;
    RowIndex rowCount() const noexcept;
    ColIndex colCount() const noexcept;

    std::string toA1() const;
    static std::optional<RangeAddress> fromA1(std::string_view text);
};

struct SheetCellCoord {
    SheetId sheetId = kInvalidSheetId;
    CellCoord coord{};

    constexpr auto operator<=>(const SheetCellCoord&) const = default;
};

struct SheetCellCoordHash {
    std::size_t operator()(const SheetCellCoord& sc) const noexcept {
        std::size_t h1 = std::hash<SheetId>{}(sc.sheetId);
        std::size_t h2 = CellCoordHash{}(sc.coord);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

struct SheetRangeAddress {
    SheetId sheetId = kInvalidSheetId;
    RangeAddress range{};

    constexpr auto operator<=>(const SheetRangeAddress&) const = default;
};

std::string colIndexToLetters(ColIndex col);
std::optional<ColIndex> lettersToColIndex(std::string_view letters);

} // namespace sheetcore
