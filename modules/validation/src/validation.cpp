#include "sheetcore/validation/validation.hpp"

#include <algorithm>
#include <cmath>

namespace sheetcore::validation {

namespace {

bool checkOperator(ValidationOperator op, double v, double a, double b) {
    switch (op) {
        case ValidationOperator::Between: return v >= a && v <= b;
        case ValidationOperator::NotBetween: return v < a || v > b;
        case ValidationOperator::Equal: return std::fabs(v - a) <= 1e-12;
        case ValidationOperator::NotEqual: return std::fabs(v - a) > 1e-12;
        case ValidationOperator::GreaterThan: return v > a;
        case ValidationOperator::LessThan: return v < a;
        case ValidationOperator::GreaterOrEqual: return v >= a;
        case ValidationOperator::LessOrEqual: return v <= a;
    }
    return false;
}

} // namespace

void ValidationEngine::addRule(ValidationRule rule) {
    rule.range = rule.range.normalized();
    rules_.push_back(std::move(rule));
}

void ValidationEngine::clearRules(SheetId sheetId) {
    rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
        [sheetId](const ValidationRule& r) { return r.sheetId == sheetId; }), rules_.end());
}

const ValidationRule* ValidationEngine::findRule(SheetId sheetId, CellCoord coord) const noexcept {
    for (auto it = rules_.rbegin(); it != rules_.rend(); ++it) {
        if (it->sheetId == sheetId && it->range.contains(coord)) {
            return &(*it);
        }
    }
    return nullptr;
}

ValidationOutcome ValidationEngine::validateInput(SheetId sheetId, CellCoord coord,
                                                  const CellValue& proposed,
                                                  const CustomFormulaEvaluator& customEval) const {
    const ValidationRule* rule = findRule(sheetId, coord);
    if (!rule || rule->type == ValidationType::Any) {
        return ValidationOutcome{true, ValidationAlertStyle::Stop, {}};
    }

    if (isEmpty(proposed)) {
        if (rule->ignoreBlank) {
            return ValidationOutcome{true, rule->alertStyle, {}};
        }
        return ValidationOutcome{false, rule->alertStyle,
            rule->errorMessage.empty() ? "Blank values are not allowed." : rule->errorMessage};
    }

    bool ok = false;
    switch (rule->type) {
        case ValidationType::WholeNumber: {
            auto n = toNumber(proposed);
            if (n && std::fabs(*n - std::round(*n)) <= 1e-12) {
                ok = checkOperator(rule->op, *n, rule->minVal, rule->maxVal);
            }
            break;
        }
        case ValidationType::Decimal:
        case ValidationType::Date:
        case ValidationType::Time: {
            auto n = toNumber(proposed);
            if (n) {
                ok = checkOperator(rule->op, *n, rule->minVal, rule->maxVal);
            }
            break;
        }
        case ValidationType::List: {
            std::string s = toDisplayString(proposed);
            ok = std::find(rule->listItems.begin(), rule->listItems.end(), s) != rule->listItems.end();
            break;
        }
        case ValidationType::TextLength: {
            double len = static_cast<double>(toDisplayString(proposed).size());
            ok = checkOperator(rule->op, len, rule->minVal, rule->maxVal);
            break;
        }
        case ValidationType::CustomFormula: {
            if (customEval) {
                CellValue res = customEval(sheetId, coord, rule->customFormula, proposed);
                auto b = toBool(res);
                ok = b.value_or(false);
            } else {
                ok = true;
            }
            break;
        }
        case ValidationType::Any:
            ok = true;
            break;
    }

    if (ok) {
        return ValidationOutcome{true, rule->alertStyle, {}};
    }
    return ValidationOutcome{false, rule->alertStyle,
        rule->errorMessage.empty() ? "Value does not match validation rule." : rule->errorMessage};
}

} // namespace sheetcore::validation
