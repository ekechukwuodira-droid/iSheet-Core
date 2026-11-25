#include "sheetcore/format/style.hpp"

namespace sheetcore::format {

namespace {

void hashCombine(std::size_t& seed, std::size_t val) noexcept {
    seed ^= val + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

std::size_t hashColor(const Color& c) noexcept {
    std::uint64_t packed = (static_cast<std::uint64_t>(c.r) << 24) |
                           (static_cast<std::uint64_t>(c.g) << 16) |
                           (static_cast<std::uint64_t>(c.b) << 8) |
                           static_cast<std::uint64_t>(c.a) |
                           (c.isAuto ? (1ULL << 32) : 0ULL);
    return std::hash<std::uint64_t>{}(packed);
}

} // namespace

StyleTable::StyleTable() {
    CellStyle def{};
    styles_.push_back(def);
    buckets_[hashStyle(def)].push_back(kDefaultStyleId);
}

std::size_t StyleTable::hashStyle(const CellStyle& s) noexcept {
    std::size_t h = std::hash<std::string>{}(s.font.family);
    hashCombine(h, std::hash<double>{}(s.font.sizePt));
    hashCombine(h, (s.font.bold ? 1u : 0u) | (s.font.italic ? 2u : 0u) |
                   (s.font.underline ? 4u : 0u) | (s.font.strikethrough ? 8u : 0u));
    hashCombine(h, hashColor(s.font.color));
    hashCombine(h, static_cast<std::size_t>(s.fill.pattern));
    hashCombine(h, hashColor(s.fill.foreground));
    hashCombine(h, hashColor(s.fill.background));
    hashCombine(h, static_cast<std::size_t>(s.border.top.style));
    hashCombine(h, static_cast<std::size_t>(s.border.bottom.style));
    hashCombine(h, static_cast<std::size_t>(s.border.left.style));
    hashCombine(h, static_cast<std::size_t>(s.border.right.style));
    hashCombine(h, static_cast<std::size_t>(s.alignment.horizontal));
    hashCombine(h, static_cast<std::size_t>(s.alignment.vertical));
    hashCombine(h, static_cast<std::size_t>(s.numberFormat.category));
    hashCombine(h, std::hash<std::string>{}(s.numberFormat.formatCode));
    hashCombine(h, s.numberFormat.decimalPlaces);
    hashCombine(h, s.numberFormat.useThousandsSeparator ? 1u : 0u);
    hashCombine(h, std::hash<std::string>{}(s.numberFormat.currencySymbol));
    hashCombine(h, (s.protection.locked ? 1u : 0u) | (s.protection.hiddenFormula ? 2u : 0u));
    return h;
}

StyleId StyleTable::intern(const CellStyle& style) {
    std::size_t h = hashStyle(style);
    auto& bucket = buckets_[h];
    for (StyleId id : bucket) {
        if (styles_[id] == style) {
            return id;
        }
    }
    StyleId newId = static_cast<StyleId>(styles_.size());
    styles_.push_back(style);
    bucket.push_back(newId);
    return newId;
}

const CellStyle& StyleTable::get(StyleId id) const noexcept {
    if (id >= styles_.size()) {
        return styles_[kDefaultStyleId];
    }
    return styles_[id];
}

} // namespace sheetcore::format
