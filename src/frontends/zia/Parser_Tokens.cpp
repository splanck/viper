//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Tokens.cpp
/// @brief Token buffering and error handling for the Zia parser.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace il::frontends::zia {
namespace {

/// @brief True if @p text begins with @p prefix.
bool startsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

/// @brief True if @p needle occurs anywhere in @p text.
bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

std::string classifyParserDiagnostic(std::string_view message) {
    if (contains(message, "nesting too deep"))
        return "V-ZIA-PARSE-DEPTH";
    if (contains(message, "literal") || contains(message, "out of range"))
        return "V-ZIA-PARSE-LITERAL";
    if (contains(message, "type parameter") || contains(message, "expected type") ||
        contains(message, "fixed-size array"))
        return "V-ZIA-PARSE-TYPE";
    if (contains(message, "lambda parameters"))
        return "V-ZIA-PARSE-LAMBDA";
    if (contains(message, "pattern"))
        return "V-ZIA-PARSE-PATTERN";
    if (contains(message, "string interpolation") || contains(message, "interpolated string"))
        return "V-ZIA-PARSE-STRING";
    if (contains(message, "declaration") || contains(message, "module level") ||
        contains(message, "function body") || contains(message, "field") ||
        contains(message, "method") || contains(message, "property") ||
        contains(message, "namespace"))
        return "V-ZIA-PARSE-DECL";
    if (contains(message, "assignment target") || contains(message, "lvalue"))
        return "V-ZIA-PARSE-ASSIGNMENT";
    if (contains(message, "expected expression"))
        return "V-ZIA-PARSE-EXPR";
    if (startsWith(message, "expected "))
        return "V-ZIA-PARSE-EXPECTED";
    if (startsWith(message, "unexpected "))
        return "V-ZIA-PARSE-UNEXPECTED";
    return "V-ZIA-PARSE";
}

il::support::SourceRange tokenRange(const Token &token, SourceLoc loc) {
    if (!loc.isValid())
        return {};
    uint32_t length = 1;
    if (token.loc.file_id == loc.file_id && token.loc.line == loc.line &&
        token.loc.column == loc.column) {
        length = static_cast<uint32_t>(std::max<std::size_t>(1, token.text.size()));
    }
    return il::support::SourceRange{
        loc,
        il::support::SourceLoc{loc.file_id, loc.line, loc.column + length},
    };
}

} // namespace

Parser::Parser(Lexer &lexer, il::support::DiagnosticEngine &diag) : lexer_(lexer), diag_(diag) {
    tokens_.push_back(lexer_.next());
}

//===----------------------------------------------------------------------===//
// Token Handling
//===----------------------------------------------------------------------===//

Parser::Speculation::Speculation(Parser &parser)
    : parser_(parser), savedPos_(parser.tokenPos_), savedHasError_(parser.hasError_) {
    ++parser_.suppressionDepth_;
}

Parser::Speculation::~Speculation() {
    --parser_.suppressionDepth_;
    if (!committed_) {
        parser_.tokenPos_ = savedPos_;
        parser_.hasError_ = savedHasError_;
    }
}

const Token &Parser::peek(size_t offset) {
    while (tokens_.size() <= tokenPos_ + offset) {
        tokens_.push_back(lexer_.next());
    }
    return tokens_[tokenPos_ + offset];
}

Token Parser::advance() {
    Token cur = peek();
    ++tokenPos_;
    compactBufferedTokens();
    return cur;
}

bool Parser::check(TokenKind kind, size_t offset) {
    return peek(offset).kind == kind;
}

bool Parser::checkIdentifierLike() {
    // Allow identifiers and certain contextual keywords that can be used as names
    if (peek().kind == TokenKind::Identifier)
        return true;

    // These keywords can be used as identifiers in parameter/variable contexts
    switch (peek().kind) {
        case TokenKind::KwStruct: // Common parameter name (e.g., setValue(Integer value))
        case TokenKind::KwMatch:  // Common variable name (e.g., var match = false)
            return true;
        default:
            return false;
    }
}

bool Parser::isExpressionStart(TokenKind kind) const {
    switch (kind) {
        case TokenKind::Identifier:
        case TokenKind::IntegerLiteral:
        case TokenKind::NumberLiteral:
        case TokenKind::StringLiteral:
        case TokenKind::StringStart:
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
        case TokenKind::KwNull:
        case TokenKind::KwSelf:
        case TokenKind::KwSuper:
        case TokenKind::KwNew:
        case TokenKind::KwMatch:
        case TokenKind::KwStruct:
        case TokenKind::KwIf:
        case TokenKind::KwAwait:
        case TokenKind::LParen:
        case TokenKind::LBracket:
        case TokenKind::LBrace:
        case TokenKind::Minus:
        case TokenKind::Bang:
        case TokenKind::Tilde:
        case TokenKind::Ampersand:
        case TokenKind::KwNot:
            return true;
        default:
            return false;
    }
}

bool Parser::isMatchExpressionAhead() {
    if (!check(TokenKind::KwMatch))
        return false;

    Speculation speculation(*this);
    advance(); // consume `match`

    ExprPtr scrutinee = parseExpression();
    if (!scrutinee)
        return false;

    return check(TokenKind::LBrace);
}

bool Parser::looksLikeBlockExpression() {
    if (!check(TokenKind::LBrace))
        return false;

    switch (peek(1).kind) {
        case TokenKind::KwVar:
        case TokenKind::KwFinal:
        case TokenKind::KwLet:
        case TokenKind::KwReturn:
        case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwDefer:
        case TokenKind::KwThrow:
        case TokenKind::KwGuard:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwTry:
            return true;
        case TokenKind::KwMatch: {
            Speculation speculation(*this);
            advance(); // consume the outer `{` so the `match` token is current
            return isMatchExpressionAhead();
        }
        default:
            break;
    }

    int nestedParen = 0;
    int nestedBracket = 0;
    int nestedBrace = 0;
    constexpr size_t kMaxBlockExpressionLookahead = 512;
    for (size_t offset = 1; offset <= kMaxBlockExpressionLookahead; ++offset) {
        TokenKind kind = peek(offset).kind;
        if (kind == TokenKind::Eof)
            return false;

        if (nestedParen == 0 && nestedBracket == 0 && nestedBrace == 0) {
            if (kind == TokenKind::Semicolon)
                return true;
            if (kind == TokenKind::RBrace || kind == TokenKind::Comma || kind == TokenKind::Colon ||
                kind == TokenKind::FatArrow) {
                return false;
            }
        }

        switch (kind) {
            case TokenKind::LParen:
                ++nestedParen;
                break;
            case TokenKind::RParen:
                if (nestedParen > 0)
                    --nestedParen;
                break;
            case TokenKind::LBracket:
                ++nestedBracket;
                break;
            case TokenKind::RBracket:
                if (nestedBracket > 0)
                    --nestedBracket;
                break;
            case TokenKind::LBrace:
                ++nestedBrace;
                break;
            case TokenKind::RBrace:
                if (nestedBrace > 0)
                    --nestedBrace;
                break;
            default:
                break;
        }
    }
    return false;
}

bool Parser::match(TokenKind kind, Token *out) {
    if (check(kind)) {
        Token tok = advance();
        if (out)
            *out = tok;
        return true;
    }
    return false;
}

bool Parser::expect(TokenKind kind, const char *what, Token *out) {
    if (check(kind)) {
        Token tok = advance();
        if (out)
            *out = tok;
        return true;
    }
    error(std::string("expected ") + what + ", got " + tokenKindToString(peek().kind));
    return false;
}

void Parser::compactBufferedTokens() {
    // Do not compact during speculative parsing; saved token positions are
    // relative to the current buffer.
    if (suppressionDepth_ > 0)
        return;

    constexpr size_t kCompactThreshold = 256;
    if (tokenPos_ < kCompactThreshold)
        return;

    tokens_.erase(tokens_.begin(), tokens_.begin() + static_cast<std::ptrdiff_t>(tokenPos_));
    tokenPos_ = 0;
}

void Parser::resyncAfterError() {
    // Bounded token consumption prevents compiler hang on pathological input
    // lacking statement boundaries.
    constexpr unsigned kMaxResyncTokens = 10000;
    unsigned consumed = 0;

    while (!check(TokenKind::Eof) && consumed < kMaxResyncTokens) {
        if (check(TokenKind::Semicolon)) {
            advance();
            return;
        }
        if (check(TokenKind::RBrace) || check(TokenKind::KwFunc) || check(TokenKind::KwStruct) ||
            check(TokenKind::KwClass) || check(TokenKind::KwInterface)) {
            return;
        }
        advance();
        ++consumed;
    }
    if (consumed == kMaxResyncTokens && !check(TokenKind::Eof))
        errorAt(peek().loc, "stopped error recovery after 10000 tokens");
}

//===----------------------------------------------------------------------===//
// Error Handling
//===----------------------------------------------------------------------===//

void Parser::error(const std::string &message) {
    errorAt(peek().loc, message);
}

void Parser::errorAt(SourceLoc loc, const std::string &message) {
    if (suppressionDepth_ > 0)
        return;
    hasError_ = true;
    const Token &token = peek();
    il::support::Diagnostic diag{
        il::support::Severity::Error,
        message,
        loc,
        classifyParserDiagnostic(message),
    };
    diag.range = tokenRange(token, loc);
    diag.stage = "parse";
    diag_.report(std::move(diag));
}

} // namespace il::frontends::zia
