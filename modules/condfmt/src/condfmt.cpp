#include "sheetcore/condfmt/condfmt.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace sheetcore::condfmt {

namespace {

std::string toLowerStr(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

format::Color lerpColor(format::Color a, format::Color b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto mix = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::round(x + (y - x) * t));
    };
    return format::Color::rgb(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
}

} // namespace

void ConditionalFormatEngine::addRule(CondRule rule) {
    rule.range = rule.range.normalized();
    rules_.push_back(std::move(rule));
    std::stable_sort(rules_.begin(), rules_.end(),
        [](const CondRule& a, const CondRule& b) { return a.priority < b.priority; });
}

void ConditionalFormatEngine::clearRules(SheetId sheetId) {
    rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
        [sheetId](const CondRule& r) { return r.sheetId == sheetId; }), rules_.end());
}

StyleOverlay ConditionalFormatEngine::evaluateCell(const Worksheet& ws, CellCoord coord,
                                                   const CondFormulaEvaluator& formulaEval) const {
    StyleOverlay overlay{};
    CellValue val = ws.getValue(coord);

    for (const auto& rule : rules_) {
        if (rule.sheetId != ws.id() || !rule.range.contains(coord)) {
            continue;
        }

        bool triggered = false;
        switch (rule.type) {
            case CondRuleType::CellValueGreater:
                if (auto n = toNumber(val); n && !isEmpty(val)) triggered = *n > rule.threshold1;
                break;
            case CondRuleType::CellValueLess:
                if (auto n = toNumber(val); n && !isEmpty(val)) triggered = *n < rule.threshold1;
                break;
            case CondRuleType::CellValueBetween:
                if (auto n = toNumber(val); n && !isEmpty(val)) {
                    triggered = *n >= std::min(rule.threshold1, rule.threshold2) &&
                                *n <= std::max(rule.threshold1, rule.threshold2);
                }
                break;
            case CondRuleType::CellValueEqual:
                if (auto n = toNumber(val); n && !isEmpty(val)) triggered = std::fabs(*n - rule.threshold1) <= 1e-12;
                break;
            case CondRuleType::TextContains:
                triggered = toLowerStr(toDisplayString(val)).find(toLowerStr(rule.textMatch)) != std::string::npos;
                break;
            case CondRuleType::BlankCell:
                triggered = isEmpty(val);
                break;
            case CondRuleType::DuplicateValue:
            case CondRuleType::UniqueValue: {
                if (isEmpty(val)) break;
                std::string targetKey = toDisplayString(val);
                std::size_t freq = 0;
                for (RowIndex r = rule.range.start.row; r <= rule.range.end.row; ++r) {
                    for (ColIndex c = rule.range.start.col; c <= rule.range.end.col; ++c) {
                        CellValue other = ws.getValue(CellCoord{r, c});
                        if (!isEmpty(other) && toDisplayString(other) == targetKey) ++freq;
                    }
                }
                triggered = (rule.type == CondRuleType::DuplicateValue) ? (freq > 1) : (freq == 1);
                break;
            }
            case CondRuleType::TopN:
            case CondRuleType::BottomN: {
                auto n = toNumber(val);
                if (!n || isEmpty(val)) break;
                std::vector<double> nums;
                for (RowIndex r = rule.range.start.row; r <= rule.range.end.row; ++r) {
                    for (ColIndex c = rule.range.start.col; c <= rule.range.end.col; ++c) {
                        CellValue other = ws.getValue(CellCoord{r, c});
                        if (!isEmpty(other) && isNumber(other)) nums.push_back(std::get<double>(other));
                    }
                }
                if (nums.empty()) break;
                std::sort(nums.begin(), nums.end());
                std::size_t k = std::min(rule.rankCount, nums.size());
                if (rule.type == CondRuleType::TopN) {
                    triggered = (*n >= nums[nums.size() - k]);
                } else {
                    triggered = (*n <= nums[k - 1]);
                }
                break;
            }
            case CondRuleType::ColorScale:
            case CondRuleType::DataBar:
            case CondRuleType::IconSet: {
                auto n = toNumber(val);
                if (!n || isEmpty(val)) break;
                double minV = *n, maxV = *n;
                for (RowIndex r = rule.range.start.row; r <= rule.range.end.row; ++r) {
                    for (ColIndex c = rule.range.start.col; c <= rule.range.end.col; ++c) {
                        CellValue other = ws.getValue(CellCoord{r, c});
                        if (isNumber(other)) {
                            double d = std::get<double>(other);
                            minV = std::min(minV, d);
                            maxV = std::max(maxV, d);
                        }
                    }
                }
                double frac = (maxV > minV) ? ((*n - minV) / (maxV - minV)) : 1.0;
                triggered = true;
                if (rule.type == CondRuleType::ColorScale) {
                    overlay.colorScaleFill = lerpColor(rule.colorScale.minColor, rule.colorScale.maxColor, frac);
                } else if (rule.type == CondRuleType::DataBar) {
                    overlay.dataBarFraction = std::clamp(frac, 0.0, 1.0);
                } else if (rule.type == CondRuleType::IconSet) {
                    overlay.iconIndex = frac >= 0.67 ? 2 : (frac >= 0.33 ? 1 : 0);
                }
                break;
            }
            case CondRuleType::CustomFormula:
                if (formulaEval) triggered = formulaEval(ws.id(), coord, rule.formula);
                break;
        }

        if (triggered) {
            overlay.matched = true;
            if (rule.overlayStyle.has_value()) {
                overlay.style = rule.overlayStyle;
            }
            if (rule.stopIfTrue) {
                break;
            }
        }
    }

    return overlay;
}

} // namespace sheetcore::condfmt
