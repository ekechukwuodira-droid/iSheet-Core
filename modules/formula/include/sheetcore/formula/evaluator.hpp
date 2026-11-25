#pragma once

#include "sheetcore/core/value.hpp"
#include "sheetcore/formula/ast.hpp"
#include "sheetcore/formula/context.hpp"
#include "sheetcore/formula/functions.hpp"

namespace sheetcore::formula {

class Evaluator final : public ILambdaInvoker {
public:
    explicit Evaluator(const FunctionRegistry& registry) : registry_(registry) {}

    // Evaluates to a single value (top-left of array results, implicit intersection for ranges).
    CellValue evaluate(const ExprPtr& node, const EvalContext& ctx) const;

    // Evaluates without collapsing: the result may be an array (to be spilled), a reference or a LAMBDA.
    EvalArg evaluateFull(const ExprPtr& node, const EvalContext& ctx) const;

    // ILambdaInvoker
    EvalArg invokeLambda(const LambdaValue& fn, const std::vector<EvalArg>& args,
                         const EvalContext& ctx) const override;

    // Maximum nesting depth of LAMBDA invocations / name expansion before #NUM! is returned.
    static constexpr int kMaxCallDepth = 160;

private:
    EvalArg evaluateArg(const ExprPtr& node, const EvalContext& ctx) const;
    CellValue evaluateScalar(const ExprPtr& node, const EvalContext& ctx) const;
    CellValue collapse(const EvalArg& arg, const EvalContext& ctx) const;

    EvalArg evalUnary(const ExprPtr& node, const EvalContext& ctx) const;
    EvalArg evalBinary(const ExprPtr& node, const EvalContext& ctx) const;
    EvalArg evalFunctionCall(const ExprPtr& node, const EvalContext& ctx) const;
    EvalArg evalNamed(const ExprPtr& node, const EvalContext& ctx) const;
    EvalArg evalIf(const FunctionCallExpr& fc, const EvalContext& ctx) const;
    EvalArg evalIfError(const FunctionCallExpr& fc, const EvalContext& ctx, bool onlyNA) const;
    EvalArg evalLet(const FunctionCallExpr& fc, const EvalContext& ctx) const;
    EvalArg evalLambda(const FunctionCallExpr& fc, const EvalContext& ctx) const;
    EvalArg callDefinition(const FunctionDefinition& def, const std::vector<EvalArg>& args,
                           const EvalContext& ctx) const;

    const FunctionRegistry& registry_;
};

} // namespace sheetcore::formula
