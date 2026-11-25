#pragma once

#include "sheetcore/core/types.hpp"
#include "sheetcore/document/worksheet.hpp"
#include "sheetcore/format/style.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sheetcore::condfmt {

enum class CondRuleType {
    CellValueGreater,
    CellValueLess,
    CellValueBetween,
    CellValueEqual,
    TextContains,
    BlankCell,
    DuplicateValue,
    UniqueValue,
    TopN,
    BottomN,
    ColorScale,
    DataBar,
    IconSet,
    CustomFormula
};

struct ColorScaleSpec {
    format::Color minColor = format::Color::rgb(248, 105, 107);
    format::Color maxColor = format::Color::rgb(99, 190, 123);
};

struct CondRule {
    SheetId sheetId = kInvalidSheetId;
    RangeAddress range{};
    CondRuleType type = CondRuleType::CellValueGreater;
    int priority = 0;
    bool stopIfTrue = false;

    double threshold1 = 0.0;
    double threshold2 = 0.0;
    std::string textMatch;
    std::string formula;
    std::size_t rankCount = 10;

    std::optional<format::CellStyle> overlayStyle;
    ColorScaleSpec colorScale{};
    format::Color dataBarColor = format::Color::rgb(99, 142, 198);
};

struct StyleOverlay {
    bool matched = false;
    std::optional<format::CellStyle> style;
    std::optional<format::Color> colorScaleFill;
    std::optional<double> dataBarFraction; // 0.0 .. 1.0
    std::optional<int> iconIndex;          // 0, 1, 2
};

using CondFormulaEvaluator = std::function<bool(SheetId, CellCoord, const std::string&)>;

class ConditionalFormatEngine {
public:
    void addRule(CondRule rule);
    void clearRules(SheetId sheetId);

    StyleOverlay evaluateCell(const Worksheet& ws, CellCoord coord,
                              const CondFormulaEvaluator& formulaEval = nullptr) const;

private:
    std::vector<CondRule> rules_;
};

} // namespace sheetcore::condfmt
