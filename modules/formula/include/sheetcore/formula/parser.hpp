#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/formula/ast.hpp"

#include <string_view>

namespace sheetcore::formula {

class Parser {
public:
    static Result<ExprPtr> parse(std::string_view formulaText);
};

} // namespace sheetcore::formula
