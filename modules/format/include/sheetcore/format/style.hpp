#pragma once

#include "sheetcore/core/types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sheetcore::format {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
    bool isAuto = true;

    constexpr bool operator==(const Color&) const noexcept = default;
    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept {
        return Color{r, g, b, a, false};
    }
};

struct FontStyle {
    std::string family = "Calibri";
    double sizePt = 11.0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    Color color{};

    bool operator==(const FontStyle&) const noexcept = default;
};

enum class FillPattern : std::uint8_t {
    None,
    Solid,
    Gray125,
    HorizontalStripe,
    VerticalStripe
};

struct FillStyle {
    FillPattern pattern = FillPattern::None;
    Color foreground{};
    Color background{};

    bool operator==(const FillStyle&) const noexcept = default;
};

enum class BorderLineStyle : std::uint8_t {
    None,
    Thin,
    Medium,
    Thick,
    Dashed,
    Dotted,
    Double
};

struct BorderEdge {
    BorderLineStyle style = BorderLineStyle::None;
    Color color{};

    bool operator==(const BorderEdge&) const noexcept = default;
};

struct BorderStyle {
    BorderEdge top{};
    BorderEdge bottom{};
    BorderEdge left{};
    BorderEdge right{};

    bool operator==(const BorderStyle&) const noexcept = default;
};

enum class HorizontalAlign : std::uint8_t {
    General,
    Left,
    Center,
    Right,
    Justify
};

enum class VerticalAlign : std::uint8_t {
    Top,
    Middle,
    Bottom
};

struct AlignmentStyle {
    HorizontalAlign horizontal = HorizontalAlign::General;
    VerticalAlign vertical = VerticalAlign::Bottom;
    std::uint8_t indent = 0;
    std::int16_t rotationDeg = 0;
    bool wrapText = false;
    bool shrinkToFit = false;

    bool operator==(const AlignmentStyle&) const noexcept = default;
};

enum class NumberFormatCategory : std::uint8_t {
    General,
    Number,
    Currency,
    Accounting,
    Percentage,
    Date,
    Time,
    Scientific,
    Fraction,
    Text,
    Custom
};

struct NumberFormat {
    NumberFormatCategory category = NumberFormatCategory::General;
    std::string formatCode = "General";
    std::uint8_t decimalPlaces = 2;
    bool useThousandsSeparator = false;
    std::string currencySymbol = "$";

    bool operator==(const NumberFormat&) const noexcept = default;
};

struct ProtectionStyle {
    bool locked = true;
    bool hiddenFormula = false;

    bool operator==(const ProtectionStyle&) const noexcept = default;
};

struct CellStyle {
    FontStyle font{};
    FillStyle fill{};
    BorderStyle border{};
    AlignmentStyle alignment{};
    NumberFormat numberFormat{};
    ProtectionStyle protection{};

    bool operator==(const CellStyle&) const noexcept = default;
};

class StyleTable {
public:
    StyleTable();

    StyleId intern(const CellStyle& style);
    const CellStyle& get(StyleId id) const noexcept;
    std::size_t count() const noexcept { return styles_.size(); }

private:
    static std::size_t hashStyle(const CellStyle& s) noexcept;

    std::vector<CellStyle> styles_;
    std::unordered_map<std::size_t, std::vector<StyleId>> buckets_;
};

} // namespace sheetcore::format
