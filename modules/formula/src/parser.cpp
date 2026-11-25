#include "sheetcore/formula/parser.hpp"
#include "sheetcore/formula/token.hpp"

#include <cctype>
#include <charconv>
#include <optional>

namespace sheetcore::formula {

namespace {

CellRefTarget parseCellTarget(std::string_view text, std::optional<std::string> sheetName) {
    CellRefTarget target;
    target.sheetName = std::move(sheetName);
    if (!text.empty() && text[0] == '$') {
        target.absCol = true;
    }
    auto dollarRow = text.find('$', 1);
    if (dollarRow != std::string_view::npos) {
        target.absRow = true;
    }
    auto coord = CellCoord::fromA1(text);
    if (coord) {
        target.coord = *coord;
    } else {
        target.isInvalidRef = true;
    }
    return target;
}

class ParserImpl {
public:
    explicit ParserImpl(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Result<ExprPtr> parseRoot() {
        if (peek().type == TokenType::EndOfInput) {
            return Error{ErrorCode::ParseError, "Empty formula"};
        }
        auto expr = parseComparison();
        if (!expr) return expr;
        if (peek().type != TokenType::EndOfInput) {
            return Error{ErrorCode::ParseError, "Unexpected token: " + peek().text};
        }
        return expr;
    }

private:
    const Token& peek(std::size_t offset = 0) const {
        std::size_t idx = pos_ + offset;
        if (idx >= tokens_.size()) return tokens_.back();
        return tokens_[idx];
    }

    const Token& advance() {
        const Token& t = peek();
        if (pos_ + 1 < tokens_.size()) ++pos_;
        return t;
    }

    bool match(TokenType tt) {
        if (peek().type == tt) {
            advance();
            return true;
        }
        return false;
    }

    Result<ExprPtr> parseComparison() {
        auto left = parseConcat();
        if (!left) return left;
        while (true) {
            BinaryOp op;
            switch (peek().type) {
                case TokenType::Equal: op = BinaryOp::Equal; break;
                case TokenType::NotEqual: op = BinaryOp::NotEqual; break;
                case TokenType::Less: op = BinaryOp::Less; break;
                case TokenType::LessEqual: op = BinaryOp::LessEqual; break;
                case TokenType::Greater: op = BinaryOp::Greater; break;
                case TokenType::GreaterEqual: op = BinaryOp::GreaterEqual; break;
                default: return left;
            }
            advance();
            auto right = parseConcat();
            if (!right) return right;
            left = std::make_shared<BinaryExpr>(op, std::move(*left), std::move(*right));
        }
    }

    Result<ExprPtr> parseConcat() {
        auto left = parseAddSub();
        if (!left) return left;
        while (match(TokenType::Ampersand)) {
            auto right = parseAddSub();
            if (!right) return right;
            left = std::make_shared<BinaryExpr>(BinaryOp::Concat, std::move(*left), std::move(*right));
        }
        return left;
    }

    Result<ExprPtr> parseAddSub() {
        auto left = parseMulDiv();
        if (!left) return left;
        while (peek().type == TokenType::Plus || peek().type == TokenType::Minus) {
            BinaryOp op = advance().type == TokenType::Plus ? BinaryOp::Add : BinaryOp::Subtract;
            auto right = parseMulDiv();
            if (!right) return right;
            left = std::make_shared<BinaryExpr>(op, std::move(*left), std::move(*right));
        }
        return left;
    }

    Result<ExprPtr> parseMulDiv() {
        auto left = parsePower();
        if (!left) return left;
        while (peek().type == TokenType::Star || peek().type == TokenType::Slash) {
            BinaryOp op = advance().type == TokenType::Star ? BinaryOp::Multiply : BinaryOp::Divide;
            auto right = parsePower();
            if (!right) return right;
            left = std::make_shared<BinaryExpr>(op, std::move(*left), std::move(*right));
        }
        return left;
    }

    Result<ExprPtr> parsePower() {
        auto left = parseUnary();
        if (!left) return left;
        if (match(TokenType::Caret)) {
            auto right = parsePower(); // right-associative
            if (!right) return right;
            return std::make_shared<BinaryExpr>(BinaryOp::Power, std::move(*left), std::move(*right));
        }
        return left;
    }

    Result<ExprPtr> parseUnary() {
        if (match(TokenType::Plus)) {
            auto operand = parseUnary();
            if (!operand) return operand;
            return std::make_shared<UnaryExpr>(UnaryOp::Plus, std::move(*operand));
        }
        if (match(TokenType::Minus)) {
            auto operand = parseUnary();
            if (!operand) return operand;
            return std::make_shared<UnaryExpr>(UnaryOp::Negate, std::move(*operand));
        }
        return parsePostfix();
    }

    Result<ExprPtr> parsePostfix() {
        auto expr = parsePrimary();
        if (!expr) return expr;
        while (match(TokenType::Percent)) {
            expr = std::make_shared<UnaryExpr>(UnaryOp::Percent, std::move(*expr));
        }
        return expr;
    }

    Result<ExprPtr> parsePrimary() {
        const Token& t = peek();

        if (t.type == TokenType::Number) {
            advance();
            double val = 0.0;
            const char* begin = t.text.data();
            const char* end = t.text.data() + t.text.size();
            auto [ptr, ec] = std::from_chars(begin, end, val);
            if (ec != std::errc{} || ptr != end) {
                return Error{ErrorCode::ParseError, "Invalid number: " + t.text};
            }
            return std::make_shared<LiteralExpr>(CellValue{val});
        }

        if (t.type == TokenType::String) {
            std::string s = t.text;
            advance();
            return std::make_shared<LiteralExpr>(CellValue{std::move(s)});
        }

        if (t.type == TokenType::Boolean) {
            bool b = std::toupper(static_cast<unsigned char>(t.text[0])) == 'T';
            advance();
            return std::make_shared<LiteralExpr>(CellValue{b});
        }

        if (t.type == TokenType::ErrorLiteral) {
            auto err = ErrorValue::fromString(t.text);
            advance();
            return std::make_shared<LiteralExpr>(CellValue{err.value_or(ErrorValue{ErrorType::Value})});
        }

        if (t.type == TokenType::LParen) {
            advance();
            auto inner = parseComparison();
            if (!inner) return inner;
            if (!match(TokenType::RParen)) {
                return Error{ErrorCode::ParseError, "Expected ')'"};
            }
            return parseInvocations(std::move(*inner));
        }

        if (t.type == TokenType::LBrace) {
            return parseArrayLiteral();
        }

        // Sheet-qualified reference: SheetName!A1 or 'Sales 2026'!A1:B10
        if ((t.type == TokenType::Identifier || t.type == TokenType::QuotedIdentifier) &&
            peek(1).type == TokenType::Bang) {
            std::string sheetName = t.text;
            advance(); // sheetName
            advance(); // !
            if (peek().type != TokenType::CellRef) {
                return Error{ErrorCode::ParseError, "Expected cell reference after '!'"};
            }
            return parseCellOrRange(std::move(sheetName));
        }

        // Unqualified cell or range reference
        if (t.type == TokenType::CellRef) {
            return parseCellOrRange(std::nullopt);
        }

        // Function call, structured table reference (Table[Col]), or named reference
        if (t.type == TokenType::Identifier) {
            std::string name = t.text;
            advance();
            if (match(TokenType::LBracket)) {
                std::string colName;
                while (peek().type != TokenType::RBracket && peek().type != TokenType::EndOfInput) {
                    if (!colName.empty()) colName.push_back(' ');
                    colName += advance().text;
                }
                if (!match(TokenType::RBracket) || colName.empty()) {
                    return Error{ErrorCode::ParseError, "Invalid structured table reference"};
                }
                return std::make_shared<StructuredRefExpr>(std::move(name), std::move(colName));
            }
            if (match(TokenType::LParen)) {
                std::vector<ExprPtr> args;
                if (auto err = parseArgList(args)) return *err;
                ExprPtr call = std::make_shared<FunctionCallExpr>(std::move(name), std::move(args));
                return parseInvocations(std::move(call));
            }
            return std::make_shared<NamedRefExpr>(std::move(name));
        }

        return Error{ErrorCode::ParseError, "Unexpected token: " + t.text};
    }

    // Parses "arg, arg, ...)" after the opening parenthesis. Omitted arguments (f(1,,3)) become null nodes.
    std::optional<Error> parseArgList(std::vector<ExprPtr>& args) {
        if (match(TokenType::RParen)) return std::nullopt;
        while (true) {
            if (peek().type == TokenType::Comma || peek().type == TokenType::RParen) {
                args.push_back(nullptr);
            } else {
                auto arg = parseComparison();
                if (!arg) return arg.error();
                args.push_back(std::move(*arg));
            }
            if (match(TokenType::Comma)) continue;
            if (match(TokenType::RParen)) return std::nullopt;
            return Error{ErrorCode::ParseError, "Expected ',' or ')' in function call"};
        }
    }

    // Immediate invocation of a function-valued expression, e.g. LAMBDA(x, x+1)(5).
    // Encoded as a FunctionCallExpr with an empty name whose first argument is the callee.
    Result<ExprPtr> parseInvocations(ExprPtr callee) {
        while (peek().type == TokenType::LParen) {
            advance();
            std::vector<ExprPtr> args;
            args.push_back(std::move(callee));
            if (auto err = parseArgList(args)) return *err;
            callee = std::make_shared<FunctionCallExpr>(std::string{}, std::move(args));
        }
        return callee;
    }

    Result<ExprPtr> parseCellOrRange(std::optional<std::string> sheetName) {
        std::string firstText = advance().text;
        CellRefTarget first = parseCellTarget(firstText, sheetName);
        if (match(TokenType::Colon)) {
            if (peek().type != TokenType::CellRef) {
                return Error{ErrorCode::ParseError, "Expected cell reference after ':'"};
            }
            std::string secondText = advance().text;
            CellRefTarget second = parseCellTarget(secondText, sheetName);
            return std::make_shared<RangeRefExpr>(std::move(sheetName), std::move(first), std::move(second));
        }
        return std::make_shared<CellRefExpr>(std::move(first));
    }

    Result<ExprPtr> parseArrayLiteral() {
        advance(); // '{'
        std::vector<std::vector<ExprPtr>> rows;
        std::vector<ExprPtr> currentRow;
        if (!match(TokenType::RBrace)) {
            while (true) {
                auto elem = parseComparison();
                if (!elem) return elem;
                currentRow.push_back(std::move(*elem));
                if (match(TokenType::Comma)) {
                    continue;
                }
                if (match(TokenType::Semicolon)) {
                    rows.push_back(std::move(currentRow));
                    currentRow.clear();
                    continue;
                }
                if (match(TokenType::RBrace)) {
                    rows.push_back(std::move(currentRow));
                    break;
                }
                return Error{ErrorCode::ParseError, "Expected ',', ';', or '}' in array literal"};
            }
        }
        return std::make_shared<ArrayExpr>(std::move(rows));
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

} // namespace

Result<ExprPtr> Parser::parse(std::string_view formulaText) {
    auto tokRes = Lexer::tokenize(formulaText);
    if (!tokRes) {
        return tokRes.error();
    }
    ParserImpl impl(std::move(tokRes.value()));
    return impl.parseRoot();
}

} // namespace sheetcore::formula
