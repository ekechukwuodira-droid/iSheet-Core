#pragma once

#include "sheetcore/core/types.hpp"
#include "sheetcore/core/value.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sheetcore::formula {

enum class ExpressionType {
    Literal,
    Unary,
    Binary,
    FunctionCall,
    CellReference,
    RangeReference,
    NamedReference,
    StructuredReference,
    ArrayExpression
};

enum class UnaryOp {
    Plus,
    Negate,
    Percent
};

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Power,
    Concat,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

struct CellRefTarget {
    std::optional<std::string> sheetName;
    CellCoord coord{};
    bool absRow = false;
    bool absCol = false;
    bool isInvalidRef = false; // #REF!
};

class ExpressionNode {
public:
    virtual ~ExpressionNode() = default;
    virtual ExpressionType type() const noexcept = 0;
};

using ExprPtr = std::shared_ptr<const ExpressionNode>;

class LiteralExpr final : public ExpressionNode {
public:
    explicit LiteralExpr(CellValue val) : value(std::move(val)) {}
    ExpressionType type() const noexcept override { return ExpressionType::Literal; }
    CellValue value;
};

class UnaryExpr final : public ExpressionNode {
public:
    UnaryExpr(UnaryOp op, ExprPtr operand) : op(op), operand(std::move(operand)) {}
    ExpressionType type() const noexcept override { return ExpressionType::Unary; }
    UnaryOp op;
    ExprPtr operand;
};

class BinaryExpr final : public ExpressionNode {
public:
    BinaryExpr(BinaryOp op, ExprPtr left, ExprPtr right)
        : op(op), left(std::move(left)), right(std::move(right)) {}
    ExpressionType type() const noexcept override { return ExpressionType::Binary; }
    BinaryOp op;
    ExprPtr left;
    ExprPtr right;
};

class FunctionCallExpr final : public ExpressionNode {
public:
    FunctionCallExpr(std::string name, std::vector<ExprPtr> args)
        : name(std::move(name)), args(std::move(args)) {}
    ExpressionType type() const noexcept override { return ExpressionType::FunctionCall; }
    std::string name;
    std::vector<ExprPtr> args;
};

class CellRefExpr final : public ExpressionNode {
public:
    explicit CellRefExpr(CellRefTarget target) : target(std::move(target)) {}
    ExpressionType type() const noexcept override { return ExpressionType::CellReference; }
    CellRefTarget target;
};

class RangeRefExpr final : public ExpressionNode {
public:
    RangeRefExpr(std::optional<std::string> sheetName, CellRefTarget start, CellRefTarget end)
        : sheetName(std::move(sheetName)), start(std::move(start)), end(std::move(end)) {}
    ExpressionType type() const noexcept override { return ExpressionType::RangeReference; }
    std::optional<std::string> sheetName;
    CellRefTarget start;
    CellRefTarget end;
};

class NamedRefExpr final : public ExpressionNode {
public:
    explicit NamedRefExpr(std::string name) : name(std::move(name)) {}
    ExpressionType type() const noexcept override { return ExpressionType::NamedReference; }
    std::string name;
};

class StructuredRefExpr final : public ExpressionNode {
public:
    StructuredRefExpr(std::string tableName, std::string columnName)
        : tableName(std::move(tableName)), columnName(std::move(columnName)) {}
    ExpressionType type() const noexcept override { return ExpressionType::StructuredReference; }
    std::string tableName;
    std::string columnName;
};

class ArrayExpr final : public ExpressionNode {
public:
    explicit ArrayExpr(std::vector<std::vector<ExprPtr>> rows) : rows(std::move(rows)) {}
    ExpressionType type() const noexcept override { return ExpressionType::ArrayExpression; }
    std::vector<std::vector<ExprPtr>> rows;
};

} // namespace sheetcore::formula
