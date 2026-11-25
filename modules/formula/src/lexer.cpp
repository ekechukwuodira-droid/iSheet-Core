#include "sheetcore/formula/token.hpp"
#include "sheetcore/core/types.hpp"

#include <cctype>

namespace sheetcore::formula {

namespace {

bool isIdentStart(char ch) {
    unsigned char uc = static_cast<unsigned char>(ch);
    return std::isalpha(uc) || ch == '_' || ch == '$';
}

bool isIdentChar(char ch) {
    unsigned char uc = static_cast<unsigned char>(ch);
    return std::isalnum(uc) || ch == '_' || ch == '.' || ch == '$';
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

Result<std::vector<Token>> Lexer::tokenize(std::string_view input) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    const std::size_t n = input.size();

    if (i < n && input[i] == '=') {
        ++i;
    }

    while (i < n) {
        char ch = input[i];
        if (std::isspace(static_cast<unsigned char>(ch))) {
            ++i;
            continue;
        }

        std::size_t start = i;

        // String literal "..." with "" escape
        if (ch == '"') {
            ++i;
            std::string val;
            bool closed = false;
            while (i < n) {
                if (input[i] == '"') {
                    if (i + 1 < n && input[i + 1] == '"') {
                        val.push_back('"');
                        i += 2;
                    } else {
                        ++i;
                        closed = true;
                        break;
                    }
                } else {
                    val.push_back(input[i++]);
                }
            }
            if (!closed) {
                return Error{ErrorCode::ParseError, "Unterminated string literal"};
            }
            tokens.push_back(Token{TokenType::String, std::move(val), start});
            continue;
        }

        // Quoted sheet identifier '...' with '' escape
        if (ch == '\'') {
            ++i;
            std::string val;
            bool closed = false;
            while (i < n) {
                if (input[i] == '\'') {
                    if (i + 1 < n && input[i + 1] == '\'') {
                        val.push_back('\'');
                        i += 2;
                    } else {
                        ++i;
                        closed = true;
                        break;
                    }
                } else {
                    val.push_back(input[i++]);
                }
            }
            if (!closed) {
                return Error{ErrorCode::ParseError, "Unterminated quoted identifier"};
            }
            tokens.push_back(Token{TokenType::QuotedIdentifier, std::move(val), start});
            continue;
        }

        // Error literal (#DIV/0!, #N/A, etc.)
        if (ch == '#') {
            ++i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(input[i])) || input[i] == '_' ||
                             input[i] == '/' || input[i] == '!' || input[i] == '?')) {
                char c = input[i++];
                if (c == '!' || c == '?') break;
            }
            tokens.push_back(Token{TokenType::ErrorLiteral, std::string(input.substr(start, i - start)), start});
            continue;
        }

        // Number literal
        if (std::isdigit(static_cast<unsigned char>(ch)) ||
            (ch == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(input[i + 1])))) {
            while (i < n && std::isdigit(static_cast<unsigned char>(input[i]))) ++i;
            if (i < n && input[i] == '.') {
                ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(input[i]))) ++i;
            }
            if (i < n && (input[i] == 'e' || input[i] == 'E')) {
                std::size_t expPos = i + 1;
                if (expPos < n && (input[expPos] == '+' || input[expPos] == '-')) ++expPos;
                if (expPos < n && std::isdigit(static_cast<unsigned char>(input[expPos]))) {
                    i = expPos;
                    while (i < n && std::isdigit(static_cast<unsigned char>(input[i]))) ++i;
                }
            }
            tokens.push_back(Token{TokenType::Number, std::string(input.substr(start, i - start)), start});
            continue;
        }

        // Identifiers, Booleans, CellRefs
        if (isIdentStart(ch)) {
            ++i;
            while (i < n && isIdentChar(input[i])) {
                ++i;
            }
            std::string_view word = input.substr(start, i - start);
            if ((iequals(word, "TRUE") || iequals(word, "FALSE")) && (i >= n || input[i] != '(')) {
                tokens.push_back(Token{TokenType::Boolean, std::string(word), start});
            } else if ((i >= n || input[i] != '(') && CellCoord::fromA1(word).has_value()) {
                tokens.push_back(Token{TokenType::CellRef, std::string(word), start});
            } else {
                tokens.push_back(Token{TokenType::Identifier, std::string(word), start});
            }
            continue;
        }

        // Two-char comparison operators
        if (ch == '<') {
            if (i + 1 < n && input[i + 1] == '>') {
                tokens.push_back(Token{TokenType::NotEqual, "<>", start});
                i += 2;
            } else if (i + 1 < n && input[i + 1] == '=') {
                tokens.push_back(Token{TokenType::LessEqual, "<=", start});
                i += 2;
            } else {
                tokens.push_back(Token{TokenType::Less, "<", start});
                ++i;
            }
            continue;
        }
        if (ch == '>') {
            if (i + 1 < n && input[i + 1] == '=') {
                tokens.push_back(Token{TokenType::GreaterEqual, ">=", start});
                i += 2;
            } else {
                tokens.push_back(Token{TokenType::Greater, ">", start});
                ++i;
            }
            continue;
        }

        // Single-char operators & punctuation
        TokenType single = TokenType::EndOfInput;
        switch (ch) {
            case '+': single = TokenType::Plus; break;
            case '-': single = TokenType::Minus; break;
            case '*': single = TokenType::Star; break;
            case '/': single = TokenType::Slash; break;
            case '^': single = TokenType::Caret; break;
            case '&': single = TokenType::Ampersand; break;
            case '=': single = TokenType::Equal; break;
            case '(': single = TokenType::LParen; break;
            case ')': single = TokenType::RParen; break;
            case '{': single = TokenType::LBrace; break;
            case '}': single = TokenType::RBrace; break;
            case '[': single = TokenType::LBracket; break;
            case ']': single = TokenType::RBracket; break;
            case ',': single = TokenType::Comma; break;
            case ';': single = TokenType::Semicolon; break;
            case ':': single = TokenType::Colon; break;
            case '!': single = TokenType::Bang; break;
            case '%': single = TokenType::Percent; break;
            default:
                return Error{ErrorCode::ParseError,
                    std::string("Unexpected character in formula: '") + ch + "'"};
        }
        tokens.push_back(Token{single, std::string(1, ch), start});
        ++i;
    }

    tokens.push_back(Token{TokenType::EndOfInput, "", n});
    return tokens;
}

} // namespace sheetcore::formula
