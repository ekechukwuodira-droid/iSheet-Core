#pragma once

#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sheetcore::validation {

enum class ValidationType {
    Any,
    WholeNumber,
    Decimal,
    List,
    Date,
    Time,
    TextLength,
    CustomFormula
};

enum class ValidationOperator {
    Between,
    NotBetween,
    Equal,
    NotEqual,
    GreaterThan,
    LessThan,
    GreaterOrEqual,
    LessOrEqual
};

enum class ValidationAlertStyle {
    Stop,
    Warning,
    Information
};

struct ValidationRule {
    SheetId sheetId = kInvalidSheetId;
    RangeAddress range{};
    ValidationType type = ValidationType::Any;
    ValidationOperator op = ValidationOperator::Between;
    double minVal = 0.0;
    double maxVal = 0.0;
    std::vector<std::string> listItems;
    std::string customFormula;
    bool ignoreBlank = true;
    bool showDropdown = true;
    ValidationAlertStyle alertStyle = ValidationAlertStyle::Stop;
    std::string inputTitle;
    std::string inputMessage;
    std::string errorTitle;
    std::string errorMessage;
};

struct ValidationOutcome {
    bool valid = true;
    ValidationAlertStyle alertStyle = ValidationAlertStyle::Stop;
    std::string message;
};

// Optional hook so validation does not have to link sheetcore_formula/calc
using CustomFormulaEvaluator = std::function<CellValue(SheetId, CellCoord, const std::string&, const CellValue&)>;

class ValidationEngine {
public:
    void addRule(ValidationRule rule);
    void clearRules(SheetId sheetId);
    const ValidationRule* findRule(SheetId sheetId, CellCoord coord) const noexcept;

    ValidationOutcome validateInput(SheetId sheetId, CellCoord coord, const CellValue& proposed,
                                    const CustomFormulaEvaluator& customEval = nullptr) const;

private:
    std::vector<ValidationRule> rules_;
};

} // namespace sheetcore::validation
