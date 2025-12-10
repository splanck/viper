//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser_Unit.cpp
// Purpose: Program/unit parsing for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Parser.hpp"

namespace il::frontends::pascal
{


std::pair<std::unique_ptr<Program>, std::unique_ptr<Unit>> Parser::parse()
{
    if (check(TokenKind::KwProgram))
    {
        return {parseProgram(), nullptr};
    }
    else if (check(TokenKind::KwUnit))
    {
        return {nullptr, parseUnit()};
    }
    else
    {
        error("expected 'program' or 'unit'");
        return {nullptr, nullptr};
    }
}

std::unique_ptr<Program> Parser::parseProgram()
{
    auto program = std::make_unique<Program>();
    program->loc = current_.loc;

    // Expect "program"
    if (!expect(TokenKind::KwProgram, "'program'"))
        return nullptr;

    // Expect program name
    if (!check(TokenKind::Identifier))
    {
        error("expected program name");
        return nullptr;
    }
    program->name = current_.text;
    advance();

    // Expect semicolon after program name
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    // Optional uses clause
    if (check(TokenKind::KwUses))
    {
        program->usedUnits = parseUses();
    }

    // Parse declarations
    program->decls = parseDeclarations();

    // Parse main block
    program->body = parseBlock();
    if (!program->body)
        return nullptr;

    // Expect final dot
    if (!expect(TokenKind::Dot, "'.'"))
        return nullptr;

    return program;
}

std::unique_ptr<Unit> Parser::parseUnit()
{
    auto unit = std::make_unique<Unit>();
    unit->loc = current_.loc;

    // Expect "unit"
    if (!expect(TokenKind::KwUnit, "'unit'"))
        return nullptr;

    // Expect unit name
    if (!check(TokenKind::Identifier))
    {
        error("expected unit name");
        return nullptr;
    }
    unit->name = current_.text;
    advance();

    // Expect semicolon
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    // Expect "interface"
    if (!expect(TokenKind::KwInterface, "'interface'"))
        return nullptr;

    // Optional uses clause in interface section
    if (check(TokenKind::KwUses))
    {
        unit->usedUnits = parseUses();
    }

    // Parse interface declarations (const, type, var, proc/func signatures)
    while (!check(TokenKind::KwImplementation) && !check(TokenKind::Eof))
    {
        if (check(TokenKind::KwConst))
        {
            auto decls = parseConstSection();
            for (auto &d : decls)
                unit->interfaceDecls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwType))
        {
            auto decls = parseTypeSection();
            for (auto &d : decls)
                unit->interfaceDecls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwVar))
        {
            auto decls = parseVarSection();
            for (auto &d : decls)
                unit->interfaceDecls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwProcedure))
        {
            // Parse just the signature (forward declaration)
            auto proc = parseProcedure();
            if (proc)
            {
                static_cast<ProcedureDecl *>(proc.get())->isForward = true;
                unit->interfaceDecls.push_back(std::move(proc));
            }
        }
        else if (check(TokenKind::KwFunction))
        {
            // Parse just the signature (forward declaration)
            auto func = parseFunction();
            if (func)
            {
                static_cast<FunctionDecl *>(func.get())->isForward = true;
                unit->interfaceDecls.push_back(std::move(func));
            }
        }
        else
        {
            break;
        }
    }

    // Expect "implementation"
    if (!expect(TokenKind::KwImplementation, "'implementation'"))
        return nullptr;

    // Optional uses clause in implementation section
    if (check(TokenKind::KwUses))
    {
        unit->implUsedUnits = parseUses();
    }

    // Parse implementation declarations
    unit->implDecls = parseDeclarations();

    // Optional initialization section
    if (check(TokenKind::KwInitialization))
    {
        advance();
        auto stmts = parseStatementList();
        unit->initSection = std::make_unique<BlockStmt>(std::move(stmts), current_.loc);
    }

    // Optional finalization section
    if (check(TokenKind::KwFinalization))
    {
        advance();
        auto stmts = parseStatementList();
        unit->finalSection = std::make_unique<BlockStmt>(std::move(stmts), current_.loc);
    }

    // Expect "end."
    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    if (!expect(TokenKind::Dot, "'.'"))
        return nullptr;

    return unit;
}

std::vector<std::string> Parser::parseUses()
{
    std::vector<std::string> units;

    if (!expect(TokenKind::KwUses, "'uses'"))
        return units;

    // Helper to parse a potentially dotted unit name (e.g., "Viper.Strings")
    auto parseUnitName = [&]() -> std::string
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected unit name");
            return "";
        }
        std::string name = current_.text;
        advance();

        // Check for dotted name (e.g., Viper.Strings)
        while (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected identifier after '.'");
                return name;
            }
            name += ".";
            name += current_.text;
            advance();
        }
        return name;
    };

    // First unit name
    std::string firstName = parseUnitName();
    if (!firstName.empty())
        units.push_back(firstName);

    // Additional unit names
    while (match(TokenKind::Comma))
    {
        std::string unitName = parseUnitName();
        if (!unitName.empty())
            units.push_back(unitName);
    }

    // Expect semicolon
    expect(TokenKind::Semicolon, "';'");

    return units;
}


} // namespace il::frontends::pascal
