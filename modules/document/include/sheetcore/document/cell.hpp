#pragma once

#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"

#include <optional>
#include <string>

namespace sheetcore {

struct CellMetadata {
    std::string comment;
    bool locked = true;
};

struct CellRecord {
    CellValue value = EmptyValue{};
    std::optional<std::string> formulaText;
    std::optional<FormulaId> formulaId;
    StyleId styleId = kDefaultStyleId;
    CellMetadata metadata{};

    bool isEffectivelyEmpty() const noexcept {
        return isEmpty(value) && !formulaText.has_value() && !formulaId.has_value() &&
               styleId == kDefaultStyleId && metadata.comment.empty();
    }
};

} // namespace sheetcore
