//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/constfold/Dispatch.hpp"
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

    // BUG-102 fix: Track current class for intra-class method call rewriting
    currentClass_ = decl.get();

    // Optional single inheritance: CLASS B : A or CLASS B:A or CLASS B : Namespace.Base
    if (at(TokenKind::Colon))
    {
        consume();
        if (peek().kind == TokenKind::Identifier)
        {
            std::string base = peek().lexeme;
            consume();
            while (at(TokenKind::Dot))
            {
                consume();
                Token seg = expect(TokenKind::Identifier);
                if (seg.kind != TokenKind::Identifier)
                    break;
                base.push_back('.');
                base.append(seg.lexeme);
            }
            decl->baseName = std::move(base);
        }
        else
        {
            expect(TokenKind::Identifier);
        }
    }

    // Optional IMPLEMENTS clause: CLASS C : B IMPLEMENTS I1, I2, ...
    if (at(TokenKind::KeywordImplements))
    {
        consume();
        // Parse comma-separated list of qualified interface names
        do
        {
            if (at(TokenKind::Comma))
                consume();

            std::vector<std::string> ifaceNameParts;
            if (at(TokenKind::Identifier))
            {
                ifaceNameParts.push_back(peek().lexeme);
                consume();
                while (at(TokenKind::Dot))
                {
                    consume();
                    Token seg = expect(TokenKind::Identifier);
                    if (seg.kind == TokenKind::Identifier)
                        ifaceNameParts.push_back(seg.lexeme);
                    else
                        break;
                }
                decl->implementsQualifiedNames.push_back(std::move(ifaceNameParts));
            }
            else
            {
                expect(TokenKind::Identifier);
                break;
            }
        } while (at(TokenKind::Comma));
    }

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

    if (at(TokenKind::Colon))
        consume();
    if (at(TokenKind::EndOfLine))
        consume();

    // Helper to optionally consume PUBLIC/PRIVATE and return it.
    auto parseAccessPrefix = [&]() -> std::optional<Access>
    {
        if (at(TokenKind::KeywordPublic))
        {
            consume();
            return Access::Public;
        }
        if (at(TokenKind::KeywordPrivate))
        {
            consume();
            return Access::Private;
        }
        return std::nullopt;
    };

    std::optional<Access> curAccess;
    bool pendingStaticField = false; // single-use STATIC modifier for next field

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine) || at(TokenKind::Colon))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        // Single-use PUBLIC/PRIVATE prefix for next member/field.
        if (auto acc = parseAccessPrefix())
        {
            curAccess = acc;
            // Continue so the following token sequence forms the actual field.
            continue;
        }

        // Single-use STATIC prefix for next field
        if (at(TokenKind::KeywordStatic))
        {
            consume();
            pendingStaticField = true;
            continue;
        }

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::Identifier && peek(2).kind == TokenKind::KeywordAs)
            {
                consume();
                continue;
            }
        }

        // BUG-OOP-042 fix: Use isSoftIdentToken() to accept soft keywords (BASE, FLOOR, etc.)
        // as field names in the lookahead checks.
        const bool looksLikeFieldDecl =
            // Shorthand: name AS TYPE
            (isSoftIdentToken(peek().kind) && peek(1).kind == TokenKind::KeywordAs) ||
            // DIM name [(...)] AS TYPE
            (at(TokenKind::KeywordDim) && isSoftIdentToken(peek(1).kind) &&
             (peek(2).kind == TokenKind::KeywordAs || peek(2).kind == TokenKind::LParen)) ||
            // Shorthand with array dims: name '(' ... ')' AS TYPE
            (isSoftIdentToken(peek().kind) && peek(1).kind == TokenKind::LParen);

        if (!looksLikeFieldDecl)
            break;

        if (at(TokenKind::KeywordDim))
            consume();

        // BUG-OOP-042 fix: Accept soft keywords (like BASE, FLOOR) as field names.
        Token fieldNameTok;
        if (isSoftIdentToken(peek().kind))
        {
            fieldNameTok = consume();
        }
        else
        {
            fieldNameTok = expect(TokenKind::Identifier);
            if (fieldNameTok.kind != TokenKind::Identifier)
                break;
        }

        // Parse array dimensions if present (BUG-056 fix)
        std::vector<long long> extents;
        bool isArray = false;
        if (at(TokenKind::LParen))
        {
            consume(); // (
            isArray = true;

            while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile))
            {
                // BUG-BASIC-001 fix: Parse dimension size as expression, then constant-fold.
                // This allows CONST values and expressions like MAX_SIZE or 10+5.
                auto dimExpr = parseExpression();
                long long size = 0;
                bool gotSize = false;

                // Try to get the integer value from the expression
                if (dimExpr)
                {
                    // Check if it's already an integer literal
                    if (auto *ie = as<IntExpr>(*dimExpr))
                    {
                        size = ie->value;
                        gotSize = true;
                    }
                    // Try constant folding for expressions
                    else if (auto folded = constfold::fold_expr(*dimExpr))
                    {
                        if (auto *ie2 = as<IntExpr>(**folded))
                        {
                            size = ie2->value;
                            gotSize = true;
                        }
                    }
                    // Check if it's a known CONST identifier
                    else if (auto *ve = as<VarExpr>(*dimExpr))
                    {
                        std::string canon = CanonicalizeIdent(ve->name);
                        auto it = knownConstInts_.find(canon);
                        if (it != knownConstInts_.end())
                        {
                            size = it->second;
                            gotSize = true;
                        }
                    }
                }

                if (gotSize)
                {
                    // BUG-094 fix: Store the declared extent as-is (e.g., 7 for DIM a(7)).
                    // The +1 conversion to length happens in the lowerer when computing
                    // allocation sizes and flat indices, not during parsing.
                    extents.push_back(size);
                }
                else
                {
                    emitError(
                        "B0001", fieldNameTok, "array dimension must be a constant expression");
                }

                if (at(TokenKind::Comma))
                    consume();
                else if (!at(TokenKind::RParen))
                    break;
            }
            expect(TokenKind::RParen);
        }

        Token asTok = expect(TokenKind::KeywordAs);
        if (asTok.kind != TokenKind::KeywordAs)
            continue;

        Type fieldType = Type::I64;
        std::string typeName; // BUG-082 fix: capture type name for object fields
        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
        {
            if (at(TokenKind::Identifier))
            {
                // BUG-OOP-039 fix: Parse qualified type names (e.g., Viper.Text.StringBuilder)
                typeName = peek().lexeme;
                // Check if it's a primitive type first
                std::string upper = typeName;
                for (auto &ch : upper)
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (upper == "INTEGER" || upper == "INT" || upper == "LONG" || upper == "DOUBLE" ||
                    upper == "FLOAT" || upper == "SINGLE" || upper == "STRING" ||
                    upper == "BOOLEAN")
                {
                    // It's a primitive - use parseTypeKeyword
                    fieldType = parseTypeKeyword();
                    typeName.clear(); // Not an object type
                }
                else
                {
                    // It's a class name - consume it and parse dotted path if present
                    consume();
                    while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier)
                    {
                        typeName += ".";
                        consume(); // dot
                        typeName += peek().lexeme;
                        consume();
                    }
                    fieldType = Type::I64; // Objects stored as pointers, default type
                }
            }
            else
            {
                fieldType = parseTypeKeyword();
            }
        }
        else
        {
            expect(TokenKind::Identifier);
        }

        ClassDecl::Field field;
        field.name = fieldNameTok.lexeme;
        field.type = fieldType;
        field.access = curAccess.value_or(Access::Public);
        field.isStatic = pendingStaticField;
        field.isArray = isArray;
        field.arrayExtents = std::move(extents);
        // BUG-082/BUG-OOP-039 fix: Set object class name if not a primitive
        if (!typeName.empty())
        {
            field.objectClassName = typeName;
        }
        curAccess.reset();
        pendingStaticField = false;
        decl->fields.push_back(std::move(field));

        if (at(TokenKind::EndOfLine))
            consume();
    }

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine) || at(TokenKind::Colon))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::KeywordSub || nextKind == TokenKind::KeywordFunction ||
                nextKind == TokenKind::KeywordDestructor ||
                nextKind == TokenKind::KeywordProperty ||
                (nextKind == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordClass))
            {
                consume();
                continue;
            }
        }

        // Parse optional access and method modifiers prefix.
        auto parseAccessPrefix = [&]() -> std::optional<Access>
        {
            if (at(TokenKind::KeywordPublic))
            {
                consume();
                return Access::Public;
            }
            if (at(TokenKind::KeywordPrivate))
            {
                consume();
                return Access::Private;
            }
            return std::nullopt;
        };

        if (auto acc = parseAccessPrefix())
            curAccess = acc;

        // Optional single-use STATIC modifier for the next member (method/property/ctor)
        bool pendingStaticMember = false;
        if (at(TokenKind::KeywordStatic))
        {
            consume();
            pendingStaticMember = true;
        }

        struct Modifiers
        {
            bool virt = false;
            bool over = false;
            bool abstr = false;
            bool fin = false;
        } mods;

        auto seenAnyModifier = [&]()
        {
            return at(TokenKind::KeywordVirtual) || at(TokenKind::KeywordOverride) ||
                   at(TokenKind::KeywordAbstract) || at(TokenKind::KeywordFinal);
        };

        while (seenAnyModifier())
        {
            auto tok = peek();
            switch (tok.kind)
            {
                case TokenKind::KeywordVirtual:
                    consume();
                    if (mods.virt)
                        emitWarning("B3005", tok, "duplicate VIRTUAL modifier");
                    mods.virt = true;
                    break;
                case TokenKind::KeywordOverride:
                    consume();
                    if (mods.over)
                        emitWarning("B3006", tok, "duplicate OVERRIDE modifier");
                    mods.over = true;
                    break;
                case TokenKind::KeywordAbstract:
                    consume();
                    if (mods.abstr)
                        emitWarning("B3007", tok, "duplicate ABSTRACT modifier");
                    mods.abstr = true;
                    break;
                case TokenKind::KeywordFinal:
                    consume();
                    if (mods.fin)
                        emitWarning("B3008", tok, "duplicate FINAL modifier");
                    mods.fin = true;
                    break;
                default:
                    break;
            }
        }

        // PROPERTY declaration
        if (at(TokenKind::KeywordProperty))
        {
            auto propLoc = peek().loc;
            consume();
            Token nameTok = expect(TokenKind::Identifier);
            if (nameTok.kind != TokenKind::Identifier)
                break;
            Token asTok = expect(TokenKind::KeywordAs);
            if (asTok.kind != TokenKind::KeywordAs)
                break;
            Type propTy = Type::I64;
            if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
                propTy = parseTypeKeyword();
            else
                expect(TokenKind::Identifier);

            auto prop = std::make_unique<PropertyDecl>();
            prop->loc = propLoc;
            prop->name = nameTok.lexeme;
            prop->type = propTy;
            prop->access = curAccess.value_or(Access::Public);
            prop->isStatic = pendingStaticMember;

            if (at(TokenKind::EndOfLine))
                consume();

            bool seenGet = false;
            bool seenSet = false;

            auto isIdentEq = [&](const Token &t, std::string_view s)
            {
                if (t.kind == TokenKind::Identifier)
                {
                    return equalsIgnoreCase(t.lexeme, s);
                }
                return false;
            };

            while (!at(TokenKind::EndOfFile))
            {
                while (at(TokenKind::EndOfLine) || at(TokenKind::Colon))
                    consume();
                if (at(TokenKind::Number))
                {
                    TokenKind nk = peek(1).kind;
                    if (nk == TokenKind::Identifier || nk == TokenKind::KeywordPublic ||
                        nk == TokenKind::KeywordPrivate || nk == TokenKind::KeywordEnd)
                        consume();
                }

                // END PROPERTY
                if (at(TokenKind::KeywordEnd) &&
                    (peek(1).kind == TokenKind::KeywordProperty || isIdentEq(peek(1), "PROPERTY")))
                {
                    consume();
                    consume();
                    break;
                }

                // Optional accessor access modifier
                std::optional<Access> acc = parseAccessPrefix();

                // GET
                if (at(TokenKind::KeywordGet) || isIdentEq(peek(), "GET"))
                {
                    consume();
                    prop->get.present = true;
                    prop->get.access = acc.value_or(prop->access);
                    auto ctx = statementSequencer();
                    ctx.collectStatements(
                        [&](int, il::support::SourceLoc)
                        {
                            return at(TokenKind::KeywordEnd) &&
                                   (peek(1).kind == TokenKind::KeywordGet ||
                                    isIdentEq(peek(1), "GET"));
                        },
                        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
                        {
                            consume();
                            consume();
                        },
                        prop->get.body);
                    seenGet = true;
                    continue;
                }

                // SET
                if (at(TokenKind::KeywordSet) || isIdentEq(peek(), "SET"))
                {
                    consume();
                    prop->set.present = true;
                    prop->set.access = acc.value_or(prop->access);
                    if (at(TokenKind::LParen))
                    {
                        consume();
                        std::string paramName;
                        Type paramTy = propTy;
                        if (at(TokenKind::Identifier) && peek(1).kind == TokenKind::KeywordAs)
                        {
                            paramName = peek().lexeme;
                            consume();
                            consume(); // AS
                            if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
                                paramTy = parseTypeKeyword();
                            else
                                expect(TokenKind::Identifier);
                        }
                        else
                        {
                            if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
                                paramTy = parseTypeKeyword();
                            else
                                expect(TokenKind::Identifier);
                        }
                        expect(TokenKind::RParen);
                        if (paramTy != propTy)
                            emitError(
                                "B3009", propLoc, "SET parameter type must match property type");
                        if (!paramName.empty())
                            prop->set.paramName = std::move(paramName);
                    }
                    auto ctx = statementSequencer();
                    ctx.collectStatements(
                        [&](int, il::support::SourceLoc)
                        {
                            return at(TokenKind::KeywordEnd) &&
                                   (peek(1).kind == TokenKind::KeywordSet ||
                                    isIdentEq(peek(1), "SET"));
                        },
                        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
                        {
                            consume();
                            consume();
                        },
                        prop->set.body);
                    seenSet = true;
                    continue;
                }

                emitError(
                    "B3010", peek(), "expected GET, SET, or END PROPERTY inside PROPERTY block");
                if (at(TokenKind::EndOfLine))
                    consume();
                else
                    consume();
            }

            if (!seenGet && !seenSet)
                emitError("B3011", propLoc, "PROPERTY must declare at least one of GET or SET");

            decl->members.push_back(std::move(prop));
            curAccess.reset();
            pendingStaticMember = false;
            continue;
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
                ctor->access = curAccess.value_or(Access::Public);
                ctor->isStatic = pendingStaticMember;
                // Modifiers not allowed on constructors.
                if (mods.virt || mods.over || mods.abstr || mods.fin)
                {
                    emitError("B3002", subLoc, "modifiers not allowed on constructors");
                }
                ctor->params = parseParamList();

                // BUG-086 fix: Register array parameters for constructor body parsing.
                std::vector<std::string> arrayParams;
                for (const auto &param : ctor->params)
                {
                    if (param.is_array)
                    {
                        arrays_.insert(param.name);
                        arrayParams.push_back(param.name);
                    }
                }

                parseProcedureBody(TokenKind::KeywordSub, ctor->body);

                // BUG-086 fix: Remove array parameters from global set after parsing.
                for (const auto &name : arrayParams)
                {
                    arrays_.erase(name);
                }

                decl->members.push_back(std::move(ctor));
                curAccess.reset();
                continue;
            }

            auto method = std::make_unique<MethodDecl>();
            method->loc = subLoc;
            method->name = subNameTok.lexeme;
            method->access = curAccess.value_or(Access::Public);
            method->isStatic = pendingStaticMember;
            method->params = parseParamList();
            method->isVirtual = mods.virt;
            method->isOverride = mods.over;
            method->isAbstract = mods.abstr;
            method->isFinal = mods.fin;

            // BUG-086 fix: Register array parameters for method body parsing.
            std::vector<std::string> arrayParams;
            for (const auto &param : method->params)
            {
                if (param.is_array)
                {
                    arrays_.insert(param.name);
                    arrayParams.push_back(param.name);
                }
            }

            if (method->isAbstract)
            {
                if (!at(TokenKind::EndOfLine))
                {
                    emitError("B3001", subLoc, "ABSTRACT method must not have a body");
                    parseProcedureBody(TokenKind::KeywordSub, method->body);
                }
            }
            else
            {
                parseProcedureBody(TokenKind::KeywordSub, method->body);
            }

            // BUG-086 fix: Remove array parameters from global set after parsing.
            for (const auto &name : arrayParams)
            {
                arrays_.erase(name);
            }

            decl->members.push_back(std::move(method));
            curAccess.reset();
            pendingStaticMember = false;
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
            method->access = curAccess.value_or(Access::Public);
            method->isStatic = pendingStaticMember;
            method->params = parseParamList();
            if (at(TokenKind::KeywordAs))
            {
                consume();
                // Try parsing as a primitive type keyword first
                if (at(TokenKind::KeywordBoolean))
                {
                    method->ret = parseTypeKeyword();
                }
                else if (at(TokenKind::Identifier))
                {
                    // Check if it's a primitive type name before consuming
                    std::string identName = peek().lexeme;
                    std::string upperName;
                    upperName.reserve(identName.size());
                    for (char ch : identName)
                    {
                        upperName.push_back(
                            static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                    }

                    bool isPrimitive =
                        (upperName == "INTEGER" || upperName == "INT" || upperName == "LONG" ||
                         upperName == "DOUBLE" || upperName == "FLOAT" || upperName == "SINGLE" ||
                         upperName == "STRING");

                    if (isPrimitive)
                    {
                        method->ret = parseTypeKeyword();
                    }
                    else
                    {
                        // It's a class name - parse as qualified name
                        // BUG-099 fix: Store original class name for correct method mangling
                        std::vector<std::string> segs;
                        segs.push_back(peek().lexeme);
                        consume();

                        // Parse dotted path if present: Class.SubClass
                        while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier)
                        {
                            consume(); // dot
                            segs.push_back(peek().lexeme);
                            consume();
                        }

                        if (!segs.empty())
                            method->explicitClassRetQname = std::move(segs);
                    }
                }
                else
                {
                    expect(TokenKind::Identifier);
                }
            }
            method->isVirtual = mods.virt;
            method->isOverride = mods.over;
            method->isAbstract = mods.abstr;
            method->isFinal = mods.fin;

            // BUG-086 fix: Register array parameters for method body parsing.
            std::vector<std::string> arrayParams;
            for (const auto &param : method->params)
            {
                if (param.is_array)
                {
                    arrays_.insert(param.name);
                    arrayParams.push_back(param.name);
                }
            }

            if (method->isAbstract)
            {
                if (!at(TokenKind::EndOfLine))
                {
                    emitError("B3001", fnLoc, "ABSTRACT method must not have a body");
                    parseProcedureBody(TokenKind::KeywordFunction, method->body);
                }
            }
            else
            {
                parseProcedureBody(TokenKind::KeywordFunction, method->body);
            }

            // BUG-086 fix: Remove array parameters from global set after parsing.
            for (const auto &name : arrayParams)
            {
                arrays_.erase(name);
            }

            decl->members.push_back(std::move(method));
            curAccess.reset();
            pendingStaticMember = false;
            continue;
        }

        if (at(TokenKind::KeywordDestructor))
        {
            auto dtorLoc = peek().loc;
            consume(); // DESTRUCTOR
            auto dtor = std::make_unique<DestructorDecl>();
            dtor->loc = dtorLoc;
            dtor->access = curAccess.value_or(Access::Public);
            parseProcedureBody(TokenKind::KeywordDestructor, dtor->body);
            decl->members.push_back(std::move(dtor));
            curAccess.reset();
            continue;
        }

        break;
    }

    while (at(TokenKind::EndOfLine) || at(TokenKind::Colon))
        consume();

    if (at(TokenKind::Number) && peek(1).kind == TokenKind::KeywordEnd &&
        peek(2).kind == TokenKind::KeywordClass)
    {
        consume();
    }

    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordClass);

    // BUG-102 fix: Reset current class tracking
    currentClass_ = nullptr;

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

    std::optional<Access> curAccess;

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordType)
            break;

        // Access prefixes are not applied to TYPE fields; ignore if present.
        // (Future ADR may define semantics for TYPE.)
        if (at(TokenKind::KeywordPublic) || at(TokenKind::KeywordPrivate))
        {
            consume();
        }

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

/// @brief Parse an INTERFACE declaration.
/// @details Parses `INTERFACE Name ... END INTERFACE` including abstract method
///          signatures (SUB/FUNCTION declarations without bodies). Interface methods
///          are implicitly abstract and don't have implementation bodies.
/// @return Newly allocated @ref InterfaceDecl representing the parsed declaration.
StmtPtr Parser::parseInterfaceDecl()
{
    auto loc = peek().loc;
    consume(); // INTERFACE

    auto decl = std::make_unique<InterfaceDecl>();
    decl->loc = loc;

    // Parse qualified interface name: INTERFACE Namespace.SubNs.IName
    if (at(TokenKind::Identifier))
    {
        decl->qualifiedName.push_back(peek().lexeme);
        consume();
        while (at(TokenKind::Dot))
        {
            consume();
            Token seg = expect(TokenKind::Identifier);
            if (seg.kind == TokenKind::Identifier)
                decl->qualifiedName.push_back(seg.lexeme);
            else
                break;
        }
    }
    else
    {
        expect(TokenKind::Identifier);
    }

    // Consume optional statement separator
    if (at(TokenKind::Colon))
        consume();
    if (at(TokenKind::EndOfLine))
        consume();

    // Parse interface members (abstract method signatures only)
    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine) || at(TokenKind::Colon))
            consume();

        // Check for END INTERFACE
        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordInterface)
            break;

        // Parse SUB or FUNCTION signatures (no body allowed in interface)
        if (at(TokenKind::KeywordSub) || at(TokenKind::KeywordFunction))
        {
            bool isSub = at(TokenKind::KeywordSub);
            auto methodLoc = peek().loc;
            consume();

            // Parse method name
            Token nameTok = expect(TokenKind::Identifier);
            std::string methodName = (nameTok.kind == TokenKind::Identifier) ? nameTok.lexeme : "";

            // Parse parameter list
            std::vector<Param> params;
            if (at(TokenKind::LParen))
            {
                consume();
                while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile))
                {
                    if (at(TokenKind::Comma))
                        consume();

                    Param param;
                    param.isByRef = false;

                    // Optional BYVAL/BYREF
                    if (at(TokenKind::KeywordByVal))
                    {
                        consume();
                        param.isByRef = false;
                    }
                    else if (at(TokenKind::KeywordByRef))
                    {
                        consume();
                        param.isByRef = true;
                    }

                    // Parameter name
                    Token pnameTok = expect(TokenKind::Identifier);
                    if (pnameTok.kind == TokenKind::Identifier)
                        param.name = pnameTok.lexeme;

                    // AS Type
                    if (at(TokenKind::KeywordAs))
                    {
                        consume();
                        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
                            param.type = parseTypeKeyword();
                        else
                            param.type = Type::I64;
                    }
                    else
                    {
                        param.type = Type::I64;
                    }

                    params.push_back(std::move(param));

                    if (!at(TokenKind::Comma) && !at(TokenKind::RParen))
                        break;
                }
                if (at(TokenKind::RParen))
                    consume();
            }

            // Create abstract method declaration (no body)
            if (isSub)
            {
                auto methodDecl = std::make_unique<SubDecl>();
                methodDecl->loc = methodLoc;
                methodDecl->name = methodName;
                methodDecl->params = std::move(params);
                // Interface methods are implicitly abstract - no body
                decl->members.push_back(std::move(methodDecl));
            }
            else
            {
                auto methodDecl = std::make_unique<FunctionDecl>();
                methodDecl->loc = methodLoc;
                methodDecl->name = methodName;
                methodDecl->params = std::move(params);

                // Return type for FUNCTION
                if (at(TokenKind::KeywordAs))
                {
                    consume();
                    if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
                        methodDecl->ret = parseTypeKeyword();
                    else
                        methodDecl->ret = Type::I64;
                }
                // Interface methods are implicitly abstract - no body
                decl->members.push_back(std::move(methodDecl));
            }
        }
        else if (!at(TokenKind::EndOfFile))
        {
            // Skip unexpected tokens to recover
            consume();
        }
    }

    // Consume END INTERFACE
    while (at(TokenKind::EndOfLine))
        consume();
    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordInterface);

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
