//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Extends the BASIC statement parser with the object-oriented constructs used
// by the language's TYPE and CLASS features.  The routines in this translation
// unit mirror the recovery rules and optional line-number handling followed by
// the core statement parser while stitching together the nested loops required
// to parse class members, method bodies, and user-defined record fields.  Each
// helper confines the fiddly token juggling associated with optional keywords,
// suffix-based type inference, and legacy numbering rules so the main parser can
// remain readable.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief BASIC statement parser extensions for object-oriented constructs.
/// @details Declares the helper routines that recognise `CLASS`, `TYPE`, and
///          `DELETE` statements.  Keeping the implementations separate from the
///          core statement parser preserves readability while ensuring the
///          object-oriented grammar shares the same recovery behaviour and type
///          inference shims as procedural code.

#include "frontends/basic/Parser.hpp"
#include <cctype>
#include <string>
#include <utility>
namespace il::frontends::basic
{

/// @brief Parse a BASIC `CLASS` declaration from the current token stream.
/// @details The parser consumes the opening keyword, captures the class name,
///          and then iteratively processes field and member declarations until
///          the matching `END CLASS` terminator is encountered.  During the
///          field pass the helper tolerates optional line numbers, recognises
///          explicit `AS` type annotations, and defaults unspecified members to
///          integer types to preserve legacy semantics.  For the member pass the
///          routine recognises constructors (`SUB NEW`), methods, functions with
///          suffix-driven return types, and destructors.  Each body is delegated
///          to the general procedure parser so control-flow, locals, and
///          recovery all remain consistent with non-OOP procedures.
/// @return Newly allocated @ref ClassDecl describing the parsed declaration.
StmtPtr Parser::parseClassDecl()
{
    auto loc = peek().loc;
    consume(); // CLASS

    Token nameTok = expect(TokenKind::Identifier);

    auto decl = std::make_unique<ClassDecl>();
    decl->loc = loc;
    if (nameTok.kind == TokenKind::Identifier)
        decl->name = nameTok.lexeme;

    auto equalsIgnoreCase = [](const std::string &lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
            return false;
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            unsigned char lc = static_cast<unsigned char>(lhs[i]);
            unsigned char rc = static_cast<unsigned char>(rhs[i]);
            if (std::toupper(lc) != std::toupper(rc))
                return false;
        }
        return true;
    };

    if (at(TokenKind::EndOfLine))
        consume();

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::Identifier && peek(2).kind == TokenKind::KeywordAs)
            {
                consume();
                continue;
            }
        }

        const bool looksLikeFieldDecl =
            (at(TokenKind::Identifier) && peek(1).kind == TokenKind::KeywordAs) ||
            (at(TokenKind::KeywordDim) && peek(1).kind == TokenKind::Identifier &&
             peek(2).kind == TokenKind::KeywordAs);

        if (!looksLikeFieldDecl)
            break;

        if (at(TokenKind::KeywordDim))
            consume();

        Token fieldNameTok = expect(TokenKind::Identifier);
        if (fieldNameTok.kind != TokenKind::Identifier)
            break;

        Token asTok = expect(TokenKind::KeywordAs);
        if (asTok.kind != TokenKind::KeywordAs)
            continue;

        Type fieldType = Type::I64;
        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
        {
            fieldType = parseTypeKeyword();
        }
        else
        {
            expect(TokenKind::Identifier);
        }

        ClassDecl::Field field;
        field.name = fieldNameTok.lexeme;
        field.type = fieldType;
        decl->fields.push_back(std::move(field));

        if (at(TokenKind::EndOfLine))
            consume();
    }

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::KeywordSub || nextKind == TokenKind::KeywordFunction ||
                nextKind == TokenKind::KeywordDestructor ||
                (nextKind == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordClass))
            {
                consume();
                continue;
            }
        }

        if (at(TokenKind::KeywordSub))
        {
            auto subLoc = peek().loc;
            consume(); // SUB
            Token subNameTok;
            if (peek().kind == TokenKind::KeywordNew)
            {
                subNameTok = peek();
                consume();
                subNameTok.kind = TokenKind::Identifier;
            }
            else
            {
                subNameTok = expect(TokenKind::Identifier);
                if (subNameTok.kind != TokenKind::Identifier)
                    break;
            }

            if (equalsIgnoreCase(subNameTok.lexeme, "NEW"))
            {
                auto ctor = std::make_unique<ConstructorDecl>();
                ctor->loc = subLoc;
                ctor->params = parseParamList();
                parseProcedureBody(TokenKind::KeywordSub, ctor->body);
                decl->members.push_back(std::move(ctor));
                continue;
            }

            auto method = std::make_unique<MethodDecl>();
            method->loc = subLoc;
            method->name = subNameTok.lexeme;
            method->params = parseParamList();
            parseProcedureBody(TokenKind::KeywordSub, method->body);
            decl->members.push_back(std::move(method));
            continue;
        }

        if (at(TokenKind::KeywordFunction))
        {
            auto fnLoc = peek().loc;
            consume(); // FUNCTION
            Token fnNameTok = expect(TokenKind::Identifier);
            if (fnNameTok.kind != TokenKind::Identifier)
                break;

            auto method = std::make_unique<MethodDecl>();
            method->loc = fnLoc;
            method->name = fnNameTok.lexeme;
            method->ret = typeFromSuffix(fnNameTok.lexeme);
            method->params = parseParamList();
            if (at(TokenKind::KeywordAs))
            {
                consume();
                if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
                {
                    method->ret = parseTypeKeyword();
                }
                else
                {
                    expect(TokenKind::Identifier);
                }
            }
            parseProcedureBody(TokenKind::KeywordFunction, method->body);
            decl->members.push_back(std::move(method));
            continue;
        }

        if (at(TokenKind::KeywordDestructor))
        {
            auto dtorLoc = peek().loc;
            consume(); // DESTRUCTOR
            auto dtor = std::make_unique<DestructorDecl>();
            dtor->loc = dtorLoc;
            parseProcedureBody(TokenKind::KeywordDestructor, dtor->body);
            decl->members.push_back(std::move(dtor));
            continue;
        }

        break;
    }

    while (at(TokenKind::EndOfLine))
        consume();

    if (at(TokenKind::Number) && peek(1).kind == TokenKind::KeywordEnd &&
        peek(2).kind == TokenKind::KeywordClass)
    {
        consume();
    }

    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordClass);

    return decl;
}

/// @brief Parse a BASIC `TYPE` declaration used for user-defined records.
/// @details After consuming the opening keyword the helper gathers the record
///          name and then iterates over the member list, tolerating optional
///          line numbers and blank lines between entries.  Each field must
///          supply an explicit `AS` clause; `parseTypeKeyword` bridges to the
///          shared type parsing routine so suffixes, aliases, and BOOLEAN
///          keywords are handled uniformly with the non-OOP parser.  Trailing
///          trivia is skipped before the closing `END TYPE` pair is enforced to
///          guarantee deterministic error recovery locations.
/// @return Newly allocated @ref TypeDecl describing the record type.
StmtPtr Parser::parseTypeDecl()
{
    auto loc = peek().loc;
    consume();

    Token nameTok = expect(TokenKind::Identifier);

    auto decl = std::make_unique<TypeDecl>();
    decl->loc = loc;
    if (nameTok.kind == TokenKind::Identifier)
        decl->name = nameTok.lexeme;

    if (at(TokenKind::EndOfLine))
        consume();

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordType)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::Identifier ||
                (nextKind == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordType))
            {
                consume();
                continue;
            }
        }

        Token fieldNameTok = expect(TokenKind::Identifier);
        if (fieldNameTok.kind != TokenKind::Identifier)
            break;

        Token asTok = expect(TokenKind::KeywordAs);
        if (asTok.kind != TokenKind::KeywordAs)
            continue;

        Type fieldType = Type::I64;
        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
        {
            fieldType = parseTypeKeyword();
        }
        else
        {
            expect(TokenKind::Identifier);
        }

        TypeDecl::Field field;
        field.name = fieldNameTok.lexeme;
        field.type = fieldType;
        decl->fields.push_back(std::move(field));

        if (at(TokenKind::EndOfLine))
            consume();
    }

    while (at(TokenKind::EndOfLine))
        consume();

    if (at(TokenKind::Number) && peek(1).kind == TokenKind::KeywordEnd &&
        peek(2).kind == TokenKind::KeywordType)
    {
        consume();
    }

    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordType);

    return decl;
}

/// @brief Parse the `DELETE` statement for object lifetimes.
/// @details The helper records the keyword location for diagnostics, parses the
///          following expression using the generic expression parser, and wraps
///          the result in a @ref DeleteStmt.  Validation of operand categories
///          (ensuring objects rather than primitives) is deferred to semantic
///          analysis so the parser can remain error-tolerant and avoid
///          duplicating type logic.
/// @return Newly allocated @ref DeleteStmt representing the statement.
StmtPtr Parser::parseDeleteStatement()
{
    auto loc = peek().loc;
    consume();

    auto target = parseExpression();
    auto stmt = std::make_unique<DeleteStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    return stmt;
}

} // namespace il::frontends::basic

