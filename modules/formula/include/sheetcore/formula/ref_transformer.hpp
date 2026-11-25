#pragma once

#include "sheetcore/core/types.hpp"
#include "sheetcore/formula/ast.hpp"

#include <string>

namespace sheetcore::formula {

class ReferenceTransformer {
public:
    static ExprPtr onInsertRows(const ExprPtr& expr, RowIndex atRow, RowIndex count);
    static ExprPtr onDeleteRows(const ExprPtr& expr, RowIndex atRow, RowIndex count);
    static ExprPtr onInsertCols(const ExprPtr& expr, ColIndex atCol, ColIndex count);
    static ExprPtr onDeleteCols(const ExprPtr& expr, ColIndex atCol, ColIndex count);

    static std::string toFormulaString(const ExprPtr& expr);
};

} // namespace sheetcore::formula
