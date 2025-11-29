//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC statement parser routines that handle runtime-oriented
// statements such as RANDOMIZE, DIM, and terminal control commands.  The
// functions augment the core parser with helpers that translate surface syntax
// into the appropriate AST nodes while maintaining the parser's bookkeeping for
// arrays, procedures, and error handling state.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/Parser.hpp"

#include <cstdio>
#include <cstdlib>

/// @file
/// @brief Runtime statement parsing helpers for the BASIC front end.
/// @details These member functions extend @ref il::frontends::basic::Parser with
///          routines capable of recognising statements that interact with the
///          runtime library or terminal.  They construct the corresponding AST
///          nodes, update parser-managed symbol tables, and surface diagnostics
///          when syntax expectations are not met.

namespace il::frontends::basic
{
/// @brief Register parser callbacks for runtime-related statements.
/// @details Populates the @ref StatementParserRegistry with handlers for
///          statements such as DIM, RANDOMIZE, and terminal commands.  The
///          registry invokes the member functions listed here when the parser
///          encounters the associated leading token.
/// @param registry Registry that maps starting tokens to parser member functions.
void Parser::registerRuntimeParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordOn, &Parser::parseOnErrorGotoStatement);
    registry.registerHandler(TokenKind::KeywordResume, &Parser::parseResumeStatement);
    registry.registerHandler(TokenKind::KeywordEnd, &Parser::parseEndStatement);
    registry.registerHandler(TokenKind::KeywordDim, &Parser::parseDimStatement);
    registry.registerHandler(TokenKind::KeywordShared, &Parser::parseSharedStatement);
    registry.registerHandler(TokenKind::KeywordStatic, &Parser::parseStaticStatement);
    registry.registerHandler(TokenKind::KeywordConst, &Parser::parseConstStatement);
    registry.registerHandler(TokenKind::KeywordRedim, &Parser::parseReDimStatement);
    registry.registerHandler(TokenKind::KeywordRandomize, &Parser::parseRandomizeStatement);
    registry.registerHandler(TokenKind::KeywordSwap, &Parser::parseSwapStatement);
    registry.registerHandler(TokenKind::KeywordBeep, &Parser::parseBeepStatement);
    registry.registerHandler(TokenKind::KeywordCls, &Parser::parseClsStatement);
    registry.registerHandler(TokenKind::KeywordColor, &Parser::parseColorStatement);
    registry.registerHandler(TokenKind::KeywordLocate, &Parser::parseLocateStatement);
    registry.registerHandler(TokenKind::KeywordCursor, &Parser::parseCursorStatement);
    registry.registerHandler(TokenKind::KeywordAltscreen, &Parser::parseAltScreenStatement);
    registry.registerHandler(TokenKind::KeywordSleep, &Parser::parseSleepStatement);
}

/// @brief Parse an @c ON ERROR GOTO statement.
/// @details Consumes the @c ON, @c ERROR, and @c GOTO keywords, parses the
///          numeric label target, and builds an @ref OnErrorGoto AST node.  The
///          helper records whether the statement targets line zero so the
///          lowerer can emit a @c RESUME 0 semantic.
/// @return Newly constructed statement node.
StmtPtr Parser::parseOnErrorGotoStatement()
{
    auto loc = peek().loc;
    consume(); // ON
    expect(TokenKind::KeywordError);
    expect(TokenKind::KeywordGoto);
    Token targetTok = peek();
    int target = 0;
    bool toZero = false;
    if (targetTok.kind == TokenKind::Identifier)
    {
        target = ensureLabelNumber(targetTok.lexeme);
        noteNamedLabelReference(targetTok, target);
        consume();
    }
    else
    {
        target = std::atoi(targetTok.lexeme.c_str());
        expect(TokenKind::Number);
        noteNumericLabelUsage(target);
        toZero = targetTok.kind == TokenKind::Number && target == 0;
    }
    auto stmt = std::make_unique<OnErrorGoto>();
    stmt->loc = loc;
    stmt->target = target;
    stmt->toZero = toZero;
    return stmt;
}

/// @brief Parse an @c END statement.
/// @details Consumes the @c END keyword and emits an @ref EndStmt node anchored
///          at the current source location.  No operands or trailing tokens are
///          required for this statement form.
/// @return Newly constructed statement node.
StmtPtr Parser::parseEndStatement()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a @c RESUME statement.
/// @details Handles the optional @c NEXT keyword or numeric label.  When neither
///          is present the statement resumes at the point of the original error.
/// @return Newly constructed statement node.
StmtPtr Parser::parseResumeStatement()
{
    auto loc = peek().loc;
    consume(); // RESUME
    auto stmt = std::make_unique<Resume>();
    stmt->loc = loc;
    if (at(TokenKind::KeywordNext))
    {
        consume();
        stmt->mode = Resume::Mode::Next;
    }
    else if (!(at(TokenKind::EndOfLine) || at(TokenKind::EndOfFile) || at(TokenKind::Colon) ||
               isStatementStart(peek().kind)))
    {
        Token labelTok = peek();
        int target = 0;
        if (labelTok.kind == TokenKind::Identifier)
        {
            target = ensureLabelNumber(labelTok.lexeme);
            noteNamedLabelReference(labelTok, target);
            consume();
        }
        else
        {
            target = std::atoi(labelTok.lexeme.c_str());
            expect(TokenKind::Number);
            noteNumericLabelUsage(target);
        }
        stmt->mode = Resume::Mode::Label;
        stmt->target = target;
    }
    return stmt;
}

/// @brief Parse a @c DIM statement.
/// @details Captures the declared name, optional array bounds, and optional type
///          annotation.  Array declarations update the parser's array tracking so
///          later phases can generate runtime allocation requests. Supports optional
///          initializer syntax: DIM name = value
/// @return Newly constructed statement node.
StmtPtr Parser::parseDimStatement()
{
    auto loc = peek().loc;
    consume(); // DIM

    // Parse a single DIM item: <name> [ ( <size> ) ] [ AS <type> ] [ = <expr> ]
    auto parseOne = [&](Token firstNameTok = Token{}) -> StmtPtr
    {
        auto isSoftIdent = [&](TokenKind k)
        {
            if (k == TokenKind::Identifier)
                return true;
            switch (k)
            {
                case TokenKind::KeywordColor:
                case TokenKind::KeywordFloor:
                case TokenKind::KeywordRandom:
                case TokenKind::KeywordCos:
                case TokenKind::KeywordSin:
                case TokenKind::KeywordPow:
                    return true;
                default:
                    return false;
            }
        };

        Token nameTok;
        if (firstNameTok.kind == TokenKind::Identifier)
        {
            nameTok = firstNameTok;
        }
        else if (isSoftIdent(peek().kind))
        {
            nameTok = consume();
        }
        else
        {
            nameTok = expect(TokenKind::Identifier);
        }

        auto node = std::make_unique<DimStmt>();
        node->loc = loc;
        node->name = nameTok.lexeme;
        node->type = typeFromSuffix(nameTok.lexeme);

        if (at(TokenKind::LParen))
        {
            node->isArray = true;
            consume();

            // Parse comma-separated dimension sizes: DIM a(2,3,4)
            node->dimensions.push_back(parseExpression());
            while (at(TokenKind::Comma))
            {
                consume(); // ','
                node->dimensions.push_back(parseExpression());
            }

            // For backward compatibility with single-dimensional arrays,
            // move the first dimension to 'size' field if there's only one
            if (node->dimensions.size() == 1)
            {
                node->size = std::move(node->dimensions[0]);
            }

            expect(TokenKind::RParen);
            if (at(TokenKind::KeywordAs))
            {
                consume();
                // Peek to decide between builtin keyword vs. qualified class name
                if (at(TokenKind::Identifier))
                {
                    auto toUpper = [](std::string_view text)
                    {
                        std::string result;
                        result.reserve(text.size());
                        for (char ch : text)
                        {
                            unsigned char byte = static_cast<unsigned char>(ch);
                            result.push_back(static_cast<char>(std::toupper(byte)));
                        }
                        return result;
                    };
                    std::string first = peek().lexeme;
                    std::string upper = toUpper(first);
                    if (upper == "INTEGER" || upper == "INT" || upper == "LONG" ||
                        upper == "DOUBLE" || upper == "FLOAT" || upper == "SINGLE" ||
                        upper == "STRING" || upper == "BOOLEAN")
                    {
                        node->type = parseTypeKeyword();
                    }
                    else
                    {
                        // Parse qualified class name: Ident ('.' Ident)*
                        std::vector<std::string> segs;
                        segs.push_back(CanonicalizeIdent(first));
                        consume();
                        while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier)
                        {
                            consume(); // dot
                            segs.push_back(CanonicalizeIdent(peek().lexeme));
                            consume();
                        }
                        node->explicitClassQname = std::move(segs);
                    }
                }
                else
                {
                    node->type = parseTypeKeyword();
                }
            }
            arrays_.insert(node->name);
        }
        else
        {
            node->isArray = false;
            if (at(TokenKind::KeywordAs))
            {
                consume();
                if (at(TokenKind::Identifier))
                {
                    auto toUpper = [](std::string_view text)
                    {
                        std::string result;
                        result.reserve(text.size());
                        for (char ch : text)
                        {
                            unsigned char byte = static_cast<unsigned char>(ch);
                            result.push_back(static_cast<char>(std::toupper(byte)));
                        }
                        return result;
                    };
                    std::string first = peek().lexeme;
                    std::string upper = toUpper(first);
                    if (upper == "INTEGER" || upper == "INT" || upper == "LONG" ||
                        upper == "DOUBLE" || upper == "FLOAT" || upper == "SINGLE" ||
                        upper == "STRING" || upper == "BOOLEAN")
                    {
                        node->type = parseTypeKeyword();
                    }
                    else
                    {
                        std::vector<std::string> segs;
                        segs.push_back(CanonicalizeIdent(first));
                        consume();
                        while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier)
                        {
                            consume();
                            segs.push_back(CanonicalizeIdent(peek().lexeme));
                            consume();
                        }
                        node->explicitClassQname = std::move(segs);
                    }
                }
                else
                {
                    node->type = parseTypeKeyword();
                }
            }
        }

        // Check for optional initializer: DIM name = value
        if (at(TokenKind::Equal))
        {
            consume(); // '='
            auto initExpr = parseExpression();

            // Create a VarExpr lvalue for the assignment
            auto varExpr = std::make_unique<VarExpr>();
            varExpr->loc = nameTok.loc;
            varExpr->Expr::loc = nameTok.loc;
            varExpr->name = nameTok.lexeme;

            // Create assignment statement
            auto assignStmt = std::make_unique<LetStmt>();
            assignStmt->loc = nameTok.loc;
            assignStmt->target = std::move(varExpr);
            assignStmt->expr = std::move(initExpr);

            // Return both DIM and assignment wrapped in a StmtList
            auto list = std::make_unique<StmtList>();
            list->loc = loc;
            list->stmts.push_back(std::move(node));
            list->stmts.push_back(std::move(assignStmt));
            return list;
        }

        return node;
    };

    // First declaration (always present)
    auto first = parseOne();

    // If there are comma-separated continuations, gather them into a StmtList.
    if (at(TokenKind::Comma))
    {
        auto list = std::make_unique<StmtList>();
        list->loc = loc;
        list->stmts.push_back(std::move(first));
        while (at(TokenKind::Comma))
        {
            consume(); // ','
            list->stmts.push_back(parseOne());
        }
        return list;
    }

    return first;
}

/// @brief Parse a @c STATIC statement.
/// @details Declares a persistent procedure-local variable with module-level storage.
///          Supports: STATIC name [AS type]
/// @return Newly constructed statement node.
StmtPtr Parser::parseStaticStatement()
{
    auto loc = peek().loc;
    consume(); // STATIC

    Token nameTok = expect(TokenKind::Identifier);

    auto node = std::make_unique<StaticStmt>();
    node->loc = loc;
    node->name = nameTok.lexeme;
    node->type = typeFromSuffix(nameTok.lexeme);

    if (at(TokenKind::KeywordAs))
    {
        consume();
        node->type = parseTypeKeyword();
    }

    return node;
}

/// @brief Parse a @c SHARED statement.
/// @details Declares that one or more names refer to module-level variables.
///          Syntax: SHARED name (, name)*
/// @return Newly constructed statement node.
StmtPtr Parser::parseSharedStatement()
{
    auto loc = peek().loc;
    consume(); // SHARED

    auto node = std::make_unique<SharedStmt>();
    node->loc = loc;
    // At least one identifier
    Token nameTok = expect(TokenKind::Identifier);
    node->names.push_back(nameTok.lexeme);
    while (at(TokenKind::Comma))
    {
        consume();
        Token more = expect(TokenKind::Identifier);
        node->names.push_back(more.lexeme);
    }
    return node;
}

/// @brief Parse a @c REDIM statement.
/// @details Re-sizes an existing array and records the declaration in the parser's
///          array set so later passes know the symbol represents an array.
/// @return Newly constructed statement node.
StmtPtr Parser::parseReDimStatement()
{
    auto loc = peek().loc;
    consume(); // REDIM
    // Optionally consume PRESERVE keyword (REDIM already preserves by default)
    if (peek().kind == TokenKind::KeywordPreserve)
        consume();
    Token nameTok = expect(TokenKind::Identifier);
    expect(TokenKind::LParen);
    auto size = parseExpression();
    expect(TokenKind::RParen);
    auto stmt = std::make_unique<ReDimStmt>();
    stmt->loc = loc;
    stmt->name = nameTok.lexeme;
    stmt->size = std::move(size);
    arrays_.insert(stmt->name);
    return stmt;
}

/// @brief Parse a @c SWAP statement.
/// @details Constructs a @ref SwapStmt capturing two lvalue expressions that
///          will be exchanged at runtime.  The parser consumes the @c SWAP
///          keyword, parses the first lvalue, expects a comma separator, and
///          then parses the second lvalue.
/// @return Newly constructed statement node.
StmtPtr Parser::parseSwapStatement()
{
    auto loc = peek().loc;
    consume(); // SWAP
    auto lhs = parseLetTarget();
    expect(TokenKind::Comma);
    auto rhs = parseLetTarget();
    auto stmt = std::make_unique<SwapStmt>();
    stmt->loc = loc;
    stmt->lhs = std::move(lhs);
    stmt->rhs = std::move(rhs);
    return stmt;
}

/// @brief Parse a @c RANDOMIZE statement.
/// @details Constructs a @ref RandomizeStmt capturing the optional seed
///          expression, enabling deterministic seeding when present.
/// @return Newly constructed statement node.
StmtPtr Parser::parseRandomizeStatement()
{
    auto loc = peek().loc;
    consume(); // RANDOMIZE
    auto stmt = std::make_unique<RandomizeStmt>();
    stmt->loc = loc;
    stmt->seed = parseExpression();
    return stmt;
}

/// @brief Parse a @c BEEP statement.
/// @details Consumes the @c BEEP keyword and emits a @ref BeepStmt node that
///          produces a beep/bell sound when executed.
/// @return Newly constructed statement node.
StmtPtr Parser::parseBeepStatement()
{
    auto loc = consume().loc; // BEEP
    auto stmt = std::make_unique<BeepStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a @c CLS statement.
/// @details Consumes the @c CLS keyword and emits a @ref ClsStmt node that clears
///          the terminal when executed.
/// @return Newly constructed statement node.
StmtPtr Parser::parseClsStatement()
{
    auto loc = consume().loc; // CLS
    auto stmt = std::make_unique<ClsStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a @c COLOR statement.
/// @details Recognises the required foreground expression and optional
///          background expression separated by a comma.
/// @return Newly constructed statement node.
StmtPtr Parser::parseColorStatement()
{
    auto loc = consume().loc; // COLOR
    auto stmt = std::make_unique<ColorStmt>();
    stmt->loc = loc;
    stmt->fg = parseExpression();
    if (at(TokenKind::Comma))
    {
        consume();
        stmt->bg = parseExpression();
    }
    return stmt;
}

/// @brief Parse a @c LOCATE statement.
/// @details Parses the required row expression and optional column expression,
///          allowing BASIC programs to reposition the terminal cursor.
/// @return Newly constructed statement node.
StmtPtr Parser::parseLocateStatement()
{
    auto loc = consume().loc; // LOCATE
    auto stmt = std::make_unique<LocateStmt>();
    stmt->loc = loc;
    stmt->row = parseExpression();
    if (at(TokenKind::Comma))
    {
        consume();
        stmt->col = parseExpression();
    }
    return stmt;
}

/// @brief Parse a @c CURSOR statement.
/// @details Recognises @c CURSOR ON or @c CURSOR OFF to control cursor visibility.
/// @return Newly constructed statement node.
StmtPtr Parser::parseCursorStatement()
{
    auto loc = consume().loc; // CURSOR
    auto stmt = std::make_unique<CursorStmt>();
    stmt->loc = loc;

    if (at(TokenKind::KeywordOn))
    {
        consume(); // ON
        stmt->visible = true;
    }
    else
    {
        expect(TokenKind::KeywordOff); // OFF
        stmt->visible = false;
    }

    return stmt;
}

/// @brief Parse an @c ALTSCREEN statement.
/// @details Recognises @c ALTSCREEN ON or @c ALTSCREEN OFF to control alternate screen buffer.
/// @return Newly constructed statement node.
StmtPtr Parser::parseAltScreenStatement()
{
    auto loc = consume().loc; // ALTSCREEN
    auto stmt = std::make_unique<AltScreenStmt>();
    stmt->loc = loc;

    if (at(TokenKind::KeywordOn))
    {
        consume(); // ON
        stmt->enable = true;
    }
    else
    {
        expect(TokenKind::KeywordOff); // OFF
        stmt->enable = false;
    }

    return stmt;
}

/// @brief Parse a @c SLEEP statement.
/// @details Parses the required millisecond duration expression and constructs
///          a @ref SleepStmt capturing the operand.
/// @return Newly constructed statement node.
StmtPtr Parser::parseSleepStatement()
{
    auto loc = consume().loc; // SLEEP
    auto stmt = std::make_unique<SleepStmt>();
    stmt->loc = loc;
    stmt->ms = parseExpression();
    return stmt;
}

} // namespace il::frontends::basic
