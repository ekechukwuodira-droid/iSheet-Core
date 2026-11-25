#pragma once

#include "sheetcore/core/result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sheetcore::formula {

enum class TokenType {
    EndOfInput,
    Number,
    String,
    Boolean,
    ErrorLiteral,
    Identifier,
    QuotedIdentifier, // 'Sales 2026'
    CellRef,          // A1, $A$1
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    Ampersand,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Colon,
    Bang,
    Percent
};

struct Token {
    TokenType type = TokenType::EndOfInput;
    std::string text;
    std::size_t position = 0;
};

class Lexer {
public:
    static Result<std::vector<Token>> tokenize(std::string_view input);
};

} // namespace sheetcore::formula
