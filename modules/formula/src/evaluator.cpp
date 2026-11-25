#include "sheetcore/formula/evaluator.hpp"
#include "sheetcore/formula/array_utils.hpp"
#include "sheetcore/formula/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sheetcore::formula {

namespace {

thread_local int tlCallDepth = 0;

struct CallDepthGuard {
    bool overflow = false;
    CallDepthGuard() {
        if (++tlCallDepth > Evaluator::kMaxCallDepth) {
            overflow = true;
        }
    }
    ~CallDepthGuard() { --tlCallDepth; }
};

std::string toUpperAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return out;
}

int compareValues(const CellValue& a, const CellValue& b) {
    auto na = toNumber(a);
    auto nb = toNumber(b);
    if (isNumber(a) && isNumber(b)) {
        double da = *na, db = *nb;
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }
    if (isBool(a) && isBool(b)) {
        bool ba = std::get<bool>(a), bb = std::get<bool>(b);
        return (ba == bb) ? 0 : (ba ? 1 : -1);
    }
    std::string sa = toDisplayString(a);
    std::string sb = toDisplayString(b);
    auto lowerStr = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    sa = lowerStr(std::move(sa));
    sb = lowerStr(std::move(sb));
    if (sa < sb) return -1;
    if (sa > sb) return 1;
    return 0;
}

CellValue applyUnaryScalar(UnaryOp op, const CellValue& v) {
    if (isError(v)) return v;
    auto num = toNumber(v);
    if (!num) return ErrorValue{ErrorType::Value};
    switch (op) {
        case UnaryOp::Plus: return *num;
        case UnaryOp::Negate: return -(*num);
        case UnaryOp::Percent: return (*num) / 100.0;
    }
    return ErrorValue{ErrorType::Value};
}

CellValue applyBinaryScalar(BinaryOp op, const CellValue& lv, const CellValue& rv) {
    if (isError(lv)) return lv;
    if (isError(rv)) return rv;

    if (op == BinaryOp::Concat) {
        return toDisplayString(lv) + toDisplayString(rv);
    }
    if (op >= BinaryOp::Equal && op <= BinaryOp::GreaterEqual) {
        int cmp = compareValues(lv, rv);
        switch (op) {
            case BinaryOp::Equal: return cmp == 0;
            case BinaryOp::NotEqual: return cmp != 0;
            case BinaryOp::Less: return cmp < 0;
            case BinaryOp::LessEqual: return cmp <= 0;
            case BinaryOp::Greater: return cmp > 0;
            case BinaryOp::GreaterEqual: return cmp >= 0;
            default: break;
        }
    }
    auto ln = toNumber(lv);
    auto rn = toNumber(rv);
    if (!ln || !rn) return ErrorValue{ErrorType::Value};
    switch (op) {
        case BinaryOp::Add: return *ln + *rn;
        case BinaryOp::Subtract: return *ln - *rn;
        case BinaryOp::Multiply: return *ln * *rn;
        case BinaryOp::Divide:
            if (*rn == 0.0) return ErrorValue{ErrorType::DivZero};
            return *ln / *rn;
        case BinaryOp::Power: {
            double res = std::pow(*ln, *rn);
            if (std::isnan(res) || std::isinf(res)) return ErrorValue{ErrorType::Num};
            return res;
        }
        default:
            return ErrorValue{ErrorType::Value};
    }
}

bool isMultiCellArg(const EvalArg& a) {
    if (a.isArray()) return a.array->rows > 1 || a.array->cols > 1;
    if (a.isRange()) {
        auto r = a.rangeRef->range.normalized();
        return r.rowCount() > 1 || r.colCount() > 1;
    }
    return false;
}

} // namespace

CellValue Evaluator::evaluate(const ExprPtr& node, const EvalContext& ctx) const {
    return collapse(evaluateFull(node, ctx), ctx);
}

EvalArg Evaluator::evaluateFull(const ExprPtr& node, const EvalContext& ctx) const {
    EvalContext withInvoker = ctx;
    withInvoker.invoker = this;
    return evaluateArg(node, withInvoker);
}

CellValue Evaluator::collapse(const EvalArg& arg, const EvalContext& ctx) const {
    return collapseToScalar(arg, ctx);
}

CellValue Evaluator::evaluateScalar(const ExprPtr& node, const EvalContext& ctx) const {
    return collapse(evaluateArg(node, ctx), ctx);
}

EvalArg Evaluator::invokeLambda(const LambdaValue& fn, const std::vector<EvalArg>& args,
                                const EvalContext& ctx) const {
    CallDepthGuard guard;
    if (guard.overflow) {
        return makeScalar(ErrorValue{ErrorType::Num});
    }

    EvalContext withInvoker = ctx;
    withInvoker.invoker = this;

    // Eta-reduced built-in function (e.g. SUM passed directly to BYROW / GROUPBY / MAP).
    if (!fn.builtinName.empty()) {
        const FunctionDefinition* def = registry_.find(fn.builtinName);
        if (!def) return makeScalar(ErrorValue{ErrorType::Name});
        return callDefinition(*def, args, withInvoker);
    }

    if (!fn.body) {
        return makeScalar(ErrorValue{ErrorType::Calc});
    }
    if (args.size() > fn.params.size()) {
        return makeScalar(ErrorValue{ErrorType::Value});
    }

    LocalScope scope;
    if (fn.captured) {
        scope = *fn.captured;
    }
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i < args.size()) {
            scope[fn.params[i]] = args[i];
        } else {
            EvalArg omittedArg;
            omittedArg.omitted = true;
            scope[fn.params[i]] = std::move(omittedArg);
        }
    }

    EvalContext bodyCtx = withInvoker;
    bodyCtx.locals = &scope;
    return evaluateArg(fn.body, bodyCtx);
}

EvalArg Evaluator::callDefinition(const FunctionDefinition& def, const std::vector<EvalArg>& args,
                                  const EvalContext& ctx) const {
    if (args.size() < def.minArgs || args.size() > def.maxArgs) {
        return makeScalar(ErrorValue{ErrorType::Value});
    }
    if (def.arrayImpl) {
        return def.arrayImpl(args, ctx);
    }
    if (def.impl) {
        return makeScalar(def.impl(args, ctx));
    }
    return makeScalar(ErrorValue{ErrorType::Value});
}

EvalArg Evaluator::evalUnary(const ExprPtr& node, const EvalContext& ctx) const {
    const auto& u = static_cast<const UnaryExpr&>(*node);
    EvalArg opVal = evaluateArg(u.operand, ctx);
    if (isMultiCellArg(opVal)) {
        ArrayValue arr = toArray(opVal, ctx);
        for (auto& el : arr.elements) {
            el = applyUnaryScalar(u.op, el);
        }
        return makeArray(std::move(arr));
    }
    return makeScalar(applyUnaryScalar(u.op, collapse(opVal, ctx)));
}

EvalArg Evaluator::evalBinary(const ExprPtr& node, const EvalContext& ctx) const {
    const auto& b = static_cast<const BinaryExpr&>(*node);
    EvalArg lv = evaluateArg(b.left, ctx);
    EvalArg rv = evaluateArg(b.right, ctx);
    if (isMultiCellArg(lv) || isMultiCellArg(rv)) {
        ArrayValue la = toArray(lv, ctx);
        ArrayValue ra = toArray(rv, ctx);
        std::size_t rows = std::max(la.rows, ra.rows);
        std::size_t cols = std::max(la.cols, ra.cols);
        ArrayValue lb = (la.rows == rows && la.cols == cols) ? std::move(la) : broadcastTo(la, rows, cols);
        ArrayValue rb = (ra.rows == rows && ra.cols == cols) ? std::move(ra) : broadcastTo(ra, rows, cols);
        ArrayValue out(rows, cols);
        for (std::size_t i = 0; i < out.elements.size(); ++i) {
            out.elements[i] = applyBinaryScalar(b.op, lb.elements[i], rb.elements[i]);
        }
        return makeArray(std::move(out));
    }
    return makeScalar(applyBinaryScalar(b.op, collapse(lv, ctx), collapse(rv, ctx)));
}

EvalArg Evaluator::evalNamed(const ExprPtr& node, const EvalContext& ctx) const {
    const auto& nr = static_cast<const NamedRefExpr&>(*node);
    std::string upper = toUpperAscii(nr.name);
    if (ctx.locals) {
        if (auto it = ctx.locals->find(upper); it != ctx.locals->end()) {
            return it->second;
        }
    }
    if (ctx.names) {
        if (auto exprStr = ctx.names->resolveNameExpression(nr.name, ctx.activeSheetId)) {
            CallDepthGuard guard;
            if (guard.overflow) return makeScalar(ErrorValue{ErrorType::Num});
            auto parsed = Parser::parse(*exprStr);
            if (!parsed) return makeScalar(ErrorValue{ErrorType::Name});
            return evaluateArg(parsed.value(), ctx);
        }
    }
    // Eta-reduced built-in function (e.g. BYROW(A1:C3, SUM), GROUPBY(..., SUM)).
    if (const FunctionDefinition* def = registry_.find(upper)) {
        auto lv = std::make_shared<LambdaValue>();
        lv->builtinName = def->name;
        EvalArg out;
        out.scalar = ErrorValue{ErrorType::Calc};
        out.lambda = std::move(lv);
        return out;
    }
    return makeScalar(ErrorValue{ErrorType::Name});
}

EvalArg Evaluator::evalIf(const FunctionCallExpr& fc, const EvalContext& ctx) const {
    if (fc.args.size() < 2 || fc.args.size() > 3) {
        return makeScalar(ErrorValue{ErrorType::Value});
    }
    EvalArg cond = evaluateArg(fc.args[0], ctx);
    if (isMultiCellArg(cond)) {
        ArrayValue ca = toArray(cond, ctx);
        EvalArg tv = evaluateArg(fc.args[1], ctx);
        EvalArg fv = (fc.args.size() >= 3 && fc.args[2]) ? evaluateArg(fc.args[2], ctx) : makeScalar(false);
        ArrayValue ta = toArray(tv, ctx);
        ArrayValue fa = toArray(fv, ctx);
        std::size_t rows = std::max({ca.rows, ta.rows, fa.rows});
        std::size_t cols = std::max({ca.cols, ta.cols, fa.cols});
        ArrayValue cb = broadcastTo(ca, rows, cols);
        ArrayValue tb = broadcastTo(ta, rows, cols);
        ArrayValue fb = broadcastTo(fa, rows, cols);
        ArrayValue out(rows, cols);
        for (std::size_t i = 0; i < out.elements.size(); ++i) {
            if (isError(cb.elements[i])) {
                out.elements[i] = cb.elements[i];
            } else if (auto b = toBool(cb.elements[i])) {
                out.elements[i] = *b ? tb.elements[i] : fb.elements[i];
            } else {
                out.elements[i] = ErrorValue{ErrorType::Value};
            }
        }
        return makeArray(std::move(out));
    }

    CellValue c = collapse(cond, ctx);
    if (isError(c)) return makeScalar(c);
    auto b = toBool(c);
    if (!b) return makeScalar(ErrorValue{ErrorType::Value});
    if (*b) {
        return fc.args[1] ? evaluateArg(fc.args[1], ctx) : makeScalar(0.0);
    }
    if (fc.args.size() >= 3 && fc.args[2]) {
        return evaluateArg(fc.args[2], ctx);
    }
    return makeScalar(false);
}

EvalArg Evaluator::evalIfError(const FunctionCallExpr& fc, const EvalContext& ctx, bool onlyNA) const {
    if (fc.args.size() != 2) {
        return makeScalar(ErrorValue{ErrorType::Value});
    }
    EvalArg primary = evaluateArg(fc.args[0], ctx);
    auto matchesErr = [onlyNA](const CellValue& v) {
        if (!isError(v)) return false;
        return !onlyNA || std::get<ErrorValue>(v).type == ErrorType::NA;
    };
    if (isMultiCellArg(primary)) {
        ArrayValue pa = toArray(primary, ctx);
        bool anyErr = false;
        for (const auto& el : pa.elements) {
            if (matchesErr(el)) { anyErr = true; break; }
        }
        if (!anyErr) return makeArray(std::move(pa));
        EvalArg fbArg = evaluateArg(fc.args[1], ctx);
        ArrayValue fa = toArray(fbArg, ctx);
        std::size_t rows = std::max(pa.rows, fa.rows);
        std::size_t cols = std::max(pa.cols, fa.cols);
        ArrayValue pb = broadcastTo(pa, rows, cols);
        ArrayValue fb = broadcastTo(fa, rows, cols);
        ArrayValue out(rows, cols);
        for (std::size_t i = 0; i < out.elements.size(); ++i) {
            out.elements[i] = matchesErr(pb.elements[i]) ? fb.elements[i] : pb.elements[i];
        }
        return makeArray(std::move(out));
    }
    CellValue v = collapse(primary, ctx);
    if (matchesErr(v)) {
        return evaluateArg(fc.args[1], ctx);
    }
    return primary;
}

EvalArg Evaluator::evalLet(const FunctionCallExpr& fc, const EvalContext& ctx) const {
    if (fc.args.size() < 3 || (fc.args.size() % 2) == 0) {
        return makeScalar(ErrorValue{ErrorType::Value});
    }
    LocalScope scope;
    if (ctx.locals) scope = *ctx.locals;
    EvalContext childCtx = ctx;
    childCtx.locals = &scope;
    for (std::size_t i = 0; i + 1 < fc.args.size(); i += 2) {
        if (!fc.args[i] || fc.args[i]->type() != ExpressionType::NamedReference) {
            return makeScalar(ErrorValue{ErrorType::Value});
        }
        std::string varName = toUpperAscii(static_cast<const NamedRefExpr&>(*fc.args[i]).name);
        scope[varName] = evaluateArg(fc.args[i + 1], childCtx);
    }
    return evaluateArg(fc.args.back(), childCtx);
}

EvalArg Evaluator::evalLambda(const FunctionCallExpr& fc, const EvalContext& ctx) const {
    if (fc.args.empty()) {
        return makeScalar(ErrorValue{ErrorType::Value});
    }
    auto lv = std::make_shared<LambdaValue>();
    for (std::size_t i = 0; i + 1 < fc.args.size(); ++i) {
        if (!fc.args[i] || fc.args[i]->type() != ExpressionType::NamedReference) {
            return makeScalar(ErrorValue{ErrorType::Value});
        }
        lv->params.push_back(toUpperAscii(static_cast<const NamedRefExpr&>(*fc.args[i]).name));
    }
    lv->body = fc.args.back();
    if (ctx.locals && !ctx.locals->empty()) {
        lv->captured = std::make_shared<LocalScope>(*ctx.locals);
    }
    EvalArg out;
    out.scalar = ErrorValue{ErrorType::Calc};
    out.lambda = std::move(lv);
    return out;
}

EvalArg Evaluator::evalFunctionCall(const ExprPtr& node, const EvalContext& ctx) const {
    const auto& fc = static_cast<const FunctionCallExpr&>(*node);

    // Immediate invocation: expr(arg1, arg2, ...) encoded with empty name and callee at args[0].
    if (fc.name.empty()) {
        if (fc.args.empty()) return makeScalar(ErrorValue{ErrorType::Value});
        EvalArg callee = evaluateArg(fc.args[0], ctx);
        if (!callee.isLambda()) return makeScalar(ErrorValue{ErrorType::Value});
        std::vector<EvalArg> callArgs;
        callArgs.reserve(fc.args.size() - 1);
        for (std::size_t i = 1; i < fc.args.size(); ++i) {
            callArgs.push_back(evaluateArg(fc.args[i], ctx));
        }
        return invokeLambda(*callee.lambda, callArgs, ctx);
    }

    std::string upperName = FunctionRegistry::normalizeName(fc.name);

    // Special forms requiring lazy / lexical-scope evaluation:
    if (upperName == "LET") return evalLet(fc, ctx);
    if (upperName == "LAMBDA") return evalLambda(fc, ctx);
    if (upperName == "IF") return evalIf(fc, ctx);
    if (upperName == "IFERROR") return evalIfError(fc, ctx, false);
    if (upperName == "IFNA") return evalIfError(fc, ctx, true);

    // Calling a LAMBDA bound to a local variable (e.g. LET(f, LAMBDA(x, x*2), f(5))).
    if (ctx.locals) {
        if (auto it = ctx.locals->find(upperName); it != ctx.locals->end() && it->second.isLambda()) {
            std::vector<EvalArg> callArgs;
            callArgs.reserve(fc.args.size());
            for (const auto& argNode : fc.args) {
                callArgs.push_back(evaluateArg(argNode, ctx));
            }
            return invokeLambda(*it->second.lambda, callArgs, ctx);
        }
    }

    // Calling a LAMBDA stored in a workbook Defined Name.
    if (ctx.names && !registry_.contains(upperName)) {
        if (auto exprStr = ctx.names->resolveNameExpression(fc.name, ctx.activeSheetId)) {
            auto parsed = Parser::parse(*exprStr);
            if (parsed) {
                EvalArg callee = evaluateArg(parsed.value(), ctx);
                if (callee.isLambda()) {
                    std::vector<EvalArg> callArgs;
                    callArgs.reserve(fc.args.size());
                    for (const auto& argNode : fc.args) {
                        callArgs.push_back(evaluateArg(argNode, ctx));
                    }
                    return invokeLambda(*callee.lambda, callArgs, ctx);
                }
            }
        }
    }

    const FunctionDefinition* def = registry_.find(upperName);
    if (!def) return makeScalar(ErrorValue{ErrorType::Name});
    if (fc.args.size() < def->minArgs || fc.args.size() > def->maxArgs) {
        return makeScalar(ErrorValue{ErrorType::Value});
    }

    std::vector<EvalArg> args;
    args.reserve(fc.args.size());
    for (const auto& argNode : fc.args) {
        args.push_back(evaluateArg(argNode, ctx));
    }
    return callDefinition(*def, args, ctx);
}

EvalArg Evaluator::evaluateArg(const ExprPtr& node, const EvalContext& ctx) const {
    if (!node) {
        EvalArg omittedArg;
        omittedArg.omitted = true;
        return omittedArg;
    }

    switch (node->type()) {
        case ExpressionType::Literal:
            return makeScalar(static_cast<const LiteralExpr&>(*node).value);

        case ExpressionType::Unary:
            return evalUnary(node, ctx);

        case ExpressionType::Binary:
            return evalBinary(node, ctx);

        case ExpressionType::CellReference: {
            const auto& ref = static_cast<const CellRefExpr&>(*node);
            if (ref.target.isInvalidRef || !ctx.values) {
                return makeScalar(ErrorValue{ErrorType::Ref});
            }
            SheetId sid = ctx.activeSheetId;
            if (ref.target.sheetName.has_value()) {
                auto resolved = ctx.values->resolveSheetId(*ref.target.sheetName);
                if (!resolved) return makeScalar(ErrorValue{ErrorType::Ref});
                sid = *resolved;
            }
            EvalArg out;
            out.scalar = ctx.values->getCellValue(sid, ref.target.coord);
            out.rangeRef = SheetRangeAddress{sid, RangeAddress{ref.target.coord, ref.target.coord}};
            return out;
        }

        case ExpressionType::RangeReference: {
            const auto& rng = static_cast<const RangeRefExpr&>(*node);
            if (rng.start.isInvalidRef || rng.end.isInvalidRef || !ctx.values) {
                return makeScalar(ErrorValue{ErrorType::Ref});
            }
            SheetId targetSheet = ctx.activeSheetId;
            if (rng.sheetName.has_value()) {
                auto sid = ctx.values->resolveSheetId(*rng.sheetName);
                if (!sid) {
                    return makeScalar(ErrorValue{ErrorType::Ref});
                }
                targetSheet = *sid;
            }
            RangeAddress addr = RangeAddress{rng.start.coord, rng.end.coord}.normalized();
            EvalArg out;
            out.rangeRef = SheetRangeAddress{targetSheet, addr};
            out.scalar = ctx.values->getCellValue(targetSheet, addr.start);
            return out;
        }

        case ExpressionType::StructuredReference: {
            const auto& sr = static_cast<const StructuredRefExpr&>(*node);
            if (!ctx.tables) {
                return makeScalar(ErrorValue{ErrorType::Ref});
            }
            auto resolved = ctx.tables->resolveTableColumn(sr.tableName, sr.columnName);
            if (!resolved) {
                return makeScalar(ErrorValue{ErrorType::Ref});
            }
            return makeRef(*resolved, ctx);
        }

        case ExpressionType::NamedReference:
            return evalNamed(node, ctx);

        case ExpressionType::FunctionCall:
            return evalFunctionCall(node, ctx);

        case ExpressionType::ArrayExpression: {
            const auto& arrExpr = static_cast<const ArrayExpr&>(*node);
            ArrayValue av;
            av.rows = arrExpr.rows.size();
            av.cols = av.rows > 0 ? arrExpr.rows[0].size() : 0;
            av.elements.reserve(av.rows * av.cols);
            for (const auto& row : arrExpr.rows) {
                for (const auto& cellExpr : row) {
                    av.elements.push_back(evaluateScalar(cellExpr, ctx));
                }
            }
            return makeArray(std::move(av));
        }
    }
    return makeScalar(ErrorValue{ErrorType::Value});
}

} // namespace sheetcore::formula
