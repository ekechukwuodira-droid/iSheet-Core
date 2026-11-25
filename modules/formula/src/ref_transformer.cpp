#include "sheetcore/formula/ref_transformer.hpp"

namespace sheetcore::formula {

namespace {

CellRefTarget shiftInsertRow(CellRefTarget t, RowIndex atRow, RowIndex count) {
    if (!t.isInvalidRef && !t.absRow && t.coord.row >= atRow) {
        t.coord.row += count;
    }
    return t;
}

CellRefTarget shiftDeleteRow(CellRefTarget t, RowIndex atRow, RowIndex count) {
    if (!t.isInvalidRef && !t.absRow) {
        if (t.coord.row >= atRow && t.coord.row < atRow + count) {
            t.isInvalidRef = true;
        } else if (t.coord.row >= atRow + count) {
            t.coord.row -= count;
        }
    }
    return t;
}

CellRefTarget shiftInsertCol(CellRefTarget t, ColIndex atCol, ColIndex count) {
    if (!t.isInvalidRef && !t.absCol && t.coord.col >= atCol) {
        t.coord.col += count;
    }
    return t;
}

CellRefTarget shiftDeleteCol(CellRefTarget t, ColIndex atCol, ColIndex count) {
    if (!t.isInvalidRef && !t.absCol) {
        if (t.coord.col >= atCol && t.coord.col < atCol + count) {
            t.isInvalidRef = true;
        } else if (t.coord.col >= atCol + count) {
            t.coord.col -= count;
        }
    }
    return t;
}

template <typename TargetFn>
ExprPtr transformExpr(const ExprPtr& expr, TargetFn&& fn) {
    if (!expr) return expr;
    switch (expr->type()) {
        case ExpressionType::Literal:
        case ExpressionType::NamedReference:
        case ExpressionType::StructuredReference:
            return expr;
        case ExpressionType::Unary: {
            const auto& u = static_cast<const UnaryExpr&>(*expr);
            return std::make_shared<UnaryExpr>(u.op, transformExpr(u.operand, fn));
        }
        case ExpressionType::Binary: {
            const auto& b = static_cast<const BinaryExpr&>(*expr);
            return std::make_shared<BinaryExpr>(b.op, transformExpr(b.left, fn), transformExpr(b.right, fn));
        }
        case ExpressionType::FunctionCall: {
            const auto& fc = static_cast<const FunctionCallExpr&>(*expr);
            std::vector<ExprPtr> newArgs;
            newArgs.reserve(fc.args.size());
            for (const auto& a : fc.args) {
                newArgs.push_back(transformExpr(a, fn));
            }
            return std::make_shared<FunctionCallExpr>(fc.name, std::move(newArgs));
        }
        case ExpressionType::CellReference: {
            const auto& cr = static_cast<const CellRefExpr&>(*expr);
            return std::make_shared<CellRefExpr>(fn(cr.target));
        }
        case ExpressionType::RangeReference: {
            const auto& rr = static_cast<const RangeRefExpr&>(*expr);
            return std::make_shared<RangeRefExpr>(rr.sheetName, fn(rr.start), fn(rr.end));
        }
        case ExpressionType::ArrayExpression: {
            const auto& arr = static_cast<const ArrayExpr&>(*expr);
            std::vector<std::vector<ExprPtr>> rows;
            rows.reserve(arr.rows.size());
            for (const auto& r : arr.rows) {
                std::vector<ExprPtr> newRow;
                newRow.reserve(r.size());
                for (const auto& e : r) {
                    newRow.push_back(transformExpr(e, fn));
                }
                rows.push_back(std::move(newRow));
            }
            return std::make_shared<ArrayExpr>(std::move(rows));
        }
    }
    return expr;
}

} // namespace

ExprPtr ReferenceTransformer::onInsertRows(const ExprPtr& expr, RowIndex atRow, RowIndex count) {
    return transformExpr(expr, [atRow, count](CellRefTarget t) { return shiftInsertRow(std::move(t), atRow, count); });
}

ExprPtr ReferenceTransformer::onDeleteRows(const ExprPtr& expr, RowIndex atRow, RowIndex count) {
    return transformExpr(expr, [atRow, count](CellRefTarget t) { return shiftDeleteRow(std::move(t), atRow, count); });
}

ExprPtr ReferenceTransformer::onInsertCols(const ExprPtr& expr, ColIndex atCol, ColIndex count) {
    return transformExpr(expr, [atCol, count](CellRefTarget t) { return shiftInsertCol(std::move(t), atCol, count); });
}

ExprPtr ReferenceTransformer::onDeleteCols(const ExprPtr& expr, ColIndex atCol, ColIndex count) {
    return transformExpr(expr, [atCol, count](CellRefTarget t) { return shiftDeleteCol(std::move(t), atCol, count); });
}

namespace {

std::string formatTarget(const CellRefTarget& t, bool includeSheet) {
    if (t.isInvalidRef) return "#REF!";
    std::string out;
    if (includeSheet && t.sheetName.has_value()) {
        bool needsQuote = t.sheetName->find(' ') != std::string::npos;
        if (needsQuote) out.push_back('\'');
        out += *t.sheetName;
        if (needsQuote) out.push_back('\'');
        out.push_back('!');
    }
    out += t.coord.toA1(t.absCol, t.absRow);
    return out;
}

std::string exprToString(const ExprPtr& expr) {
    if (!expr) return "";
    switch (expr->type()) {
        case ExpressionType::Literal: {
            const auto& v = static_cast<const LiteralExpr&>(*expr).value;
            if (isText(v)) return "\"" + std::get<std::string>(v) + "\"";
            return toDisplayString(v);
        }
        case ExpressionType::Unary: {
            const auto& u = static_cast<const UnaryExpr&>(*expr);
            if (u.op == UnaryOp::Percent) return exprToString(u.operand) + "%";
            return (u.op == UnaryOp::Negate ? "-" : "+") + exprToString(u.operand);
        }
        case ExpressionType::Binary: {
            const auto& b = static_cast<const BinaryExpr&>(*expr);
            const char* op = "+";
            switch (b.op) {
                case BinaryOp::Add: op = "+"; break;
                case BinaryOp::Subtract: op = "-"; break;
                case BinaryOp::Multiply: op = "*"; break;
                case BinaryOp::Divide: op = "/"; break;
                case BinaryOp::Power: op = "^"; break;
                case BinaryOp::Concat: op = "&"; break;
                case BinaryOp::Equal: op = "="; break;
                case BinaryOp::NotEqual: op = "<>"; break;
                case BinaryOp::Less: op = "<"; break;
                case BinaryOp::LessEqual: op = "<="; break;
                case BinaryOp::Greater: op = ">"; break;
                case BinaryOp::GreaterEqual: op = ">="; break;
            }
            return "(" + exprToString(b.left) + op + exprToString(b.right) + ")";
        }
        case ExpressionType::FunctionCall: {
            const auto& fc = static_cast<const FunctionCallExpr&>(*expr);
            std::size_t firstArg = 0;
            std::string out;
            if (fc.name.empty() && !fc.args.empty()) {
                // Immediate invocation: callee(args)
                out = exprToString(fc.args[0]) + "(";
                firstArg = 1;
            } else {
                out = fc.name + "(";
            }
            for (std::size_t i = firstArg; i < fc.args.size(); ++i) {
                if (i > firstArg) out += ",";
                out += exprToString(fc.args[i]);
            }
            out += ")";
            return out;
        }
        case ExpressionType::CellReference:
            return formatTarget(static_cast<const CellRefExpr&>(*expr).target, true);
        case ExpressionType::RangeReference: {
            const auto& rr = static_cast<const RangeRefExpr&>(*expr);
            if (rr.start.isInvalidRef || rr.end.isInvalidRef) return "#REF!";
            return formatTarget(rr.start, true) + ":" + formatTarget(rr.end, false);
        }
        case ExpressionType::NamedReference:
            return static_cast<const NamedRefExpr&>(*expr).name;
        case ExpressionType::StructuredReference: {
            const auto& sr = static_cast<const StructuredRefExpr&>(*expr);
            return sr.tableName + "[" + sr.columnName + "]";
        }
        case ExpressionType::ArrayExpression: {
            const auto& arr = static_cast<const ArrayExpr&>(*expr);
            std::string out = "{";
            for (std::size_t r = 0; r < arr.rows.size(); ++r) {
                if (r > 0) out += ";";
                for (std::size_t c = 0; c < arr.rows[r].size(); ++c) {
                    if (c > 0) out += ",";
                    out += exprToString(arr.rows[r][c]);
                }
            }
            out += "}";
            return out;
        }
    }
    return "";
}

} // namespace

std::string ReferenceTransformer::toFormulaString(const ExprPtr& expr) {
    return "=" + exprToString(expr);
}

} // namespace sheetcore::formula
