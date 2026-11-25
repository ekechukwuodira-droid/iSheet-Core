#include "sheetcore/core/types.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace sheetcore {

std::string colIndexToLetters(ColIndex col) {
    std::string result;
    std::uint64_t n = static_cast<std::uint64_t>(col) + 1;
    while (n > 0) {
        --n;
        result.push_back(static_cast<char>('A' + (n % 26)));
        n /= 26;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::optional<ColIndex> lettersToColIndex(std::string_view letters) {
    if (letters.empty() || letters.size() > 7) {
        return std::nullopt;
    }
    std::uint64_t acc = 0;
    for (char ch : letters) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (!std::isalpha(uc)) {
            return std::nullopt;
        }
        char upper = static_cast<char>(std::toupper(uc));
        acc = acc * 26 + static_cast<std::uint64_t>(upper - 'A' + 1);
        if (acc - 1 > UINT32_MAX) {
            return std::nullopt;
        }
    }
    return static_cast<ColIndex>(acc - 1);
}

std::string CellCoord::toA1(bool absCol, bool absRow) const {
    std::string out;
    if (absCol) {
        out.push_back('$');
    }
    out += colIndexToLetters(col);
    if (absRow) {
        out.push_back('$');
    }
    out += std::to_string(static_cast<std::uint64_t>(row) + 1);
    return out;
}

std::optional<CellCoord> CellCoord::fromA1(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::size_t pos = 0;
    if (text[pos] == '$') {
        ++pos;
    }
    std::size_t colStart = pos;
    while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos == colStart) {
        return std::nullopt;
    }
    auto colOpt = lettersToColIndex(text.substr(colStart, pos - colStart));
    if (!colOpt) {
        return std::nullopt;
    }
    if (pos < text.size() && text[pos] == '$') {
        ++pos;
    }
    if (pos >= text.size()) {
        return std::nullopt;
    }
    std::uint64_t row1Based = 0;
    const char* begin = text.data() + pos;
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, row1Based);
    if (ec != std::errc{} || ptr != end || row1Based == 0 || row1Based - 1 > UINT32_MAX) {
        return std::nullopt;
    }
    return CellCoord{static_cast<RowIndex>(row1Based - 1), *colOpt};
}

RangeAddress RangeAddress::normalized() const noexcept {
    return RangeAddress{
        CellCoord{std::min(start.row, end.row), std::min(start.col, end.col)},
        CellCoord{std::max(start.row, end.row), std::max(start.col, end.col)}
    };
}

bool RangeAddress::contains(CellCoord coord) const noexcept {
    auto n = normalized();
    return coord.row >= n.start.row && coord.row <= n.end.row &&
           coord.col >= n.start.col && coord.col <= n.end.col;
}

bool RangeAddress::intersects(const RangeAddress& other) const noexcept {
    auto a = normalized();
    auto b = other.normalized();
    return !(a.end.row < b.start.row || b.end.row < a.start.row ||
             a.end.col < b.start.col || b.end.col < a.start.col);
}

RowIndex RangeAddress::rowCount() const noexcept {
    auto n = normalized();
    return n.end.row - n.start.row + 1;
}

ColIndex RangeAddress::colCount() const noexcept {
    auto n = normalized();
    return n.end.col - n.start.col + 1;
}

std::uint64_t RangeAddress::cellCount() const noexcept {
    return static_cast<std::uint64_t>(rowCount()) * static_cast<std::uint64_t>(colCount());
}

std::string RangeAddress::toA1() const {
    if (start == end) {
        return start.toA1();
    }
    return start.toA1() + ":" + end.toA1();
}

std::optional<RangeAddress> RangeAddress::fromA1(std::string_view text) {
    auto colon = text.find(':');
    if (colon == std::string_view::npos) {
        auto single = CellCoord::fromA1(text);
        if (!single) {
            return std::nullopt;
        }
        return RangeAddress{*single, *single};
    }
    auto first = CellCoord::fromA1(text.substr(0, colon));
    auto second = CellCoord::fromA1(text.substr(colon + 1));
    if (!first || !second) {
        return std::nullopt;
    }
    return RangeAddress{*first, *second};
}

} // namespace sheetcore
