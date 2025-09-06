// File: src/frontends/basic/Parser.hpp
// Purpose: Declares BASIC parser producing Program with separate procedure and
//          main statement lists.
// Key invariants: Maintains token lookahead buffer.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lexer.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

class Parser
{
  public:
    Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter = nullptr);
    std::unique_ptr<Program> parseProgram();

  private:
    mutable Lexer lexer_;
    mutable std::vector<Token> tokens_;
    DiagnosticEmitter *emitter_ = nullptr;
    std::unordered_set<std::string> arrays_; ///< Known DIM'd arrays.

#include "frontends/basic/Parser_Token.hpp"

    StmtPtr parseStatement(int line);
    StmtPtr parsePrint();
    StmtPtr parseLet();
    StmtPtr parseIf(int line);
    StmtPtr parseWhile();
    StmtPtr parseFor();
    StmtPtr parseNext();
    StmtPtr parseGoto();
    StmtPtr parseEnd();
    StmtPtr parseInput();
    StmtPtr parseDim();
    StmtPtr parseRandomize();
    StmtPtr parseFunction();
    std::unique_ptr<FunctionDecl> parseFunctionHeader();
    void parseFunctionBody(FunctionDecl *fn);
    StmtPtr parseSub();
    StmtPtr parseReturn();
    std::vector<Param> parseParamList();
    Type typeFromSuffix(std::string_view name);

    ExprPtr parseExpression(int min_prec = 0);
    ExprPtr parseUnaryExpression();
    ExprPtr parseInfixRhs(ExprPtr left, int min_prec);
    ExprPtr parsePrimary();
    ExprPtr parseNumber();
    ExprPtr parseString();
    ExprPtr parseBuiltinCall(BuiltinCallExpr::Builtin builtin, il::support::SourceLoc loc);
    ExprPtr parseVariableRef(std::string name, il::support::SourceLoc loc);
    ExprPtr parseArrayRef(std::string name, il::support::SourceLoc loc);
    ExprPtr parseArrayOrVar();
    int precedence(TokenKind k);
};

} // namespace il::frontends::basic
