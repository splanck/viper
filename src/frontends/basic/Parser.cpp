#include "frontends/basic/Parser.h"
#include <cstdlib>

namespace il::frontends::basic {

Parser::Parser(std::string_view src, uint32_t file_id) : lexer_(src, file_id) { advance(); }

void Parser::advance() { current_ = lexer_.next(); }

bool Parser::consume(TokenKind k) {
  if (check(k)) {
    advance();
    return true;
  }
  return false;
}

std::unique_ptr<Program> Parser::parseProgram() {
  auto prog = std::make_unique<Program>();
  while (!check(TokenKind::EndOfFile)) {
    while (check(TokenKind::EndOfLine))
      advance();
    if (check(TokenKind::EndOfFile))
      break;
    int line = 0;
    if (check(TokenKind::Number)) {
      line = std::atoi(current_.lexeme.c_str());
      advance();
    }
    auto stmt = parseStatement(line);
    stmt->line = line;
    prog->statements.push_back(std::move(stmt));
    if (check(TokenKind::EndOfLine))
      advance();
  }
  return prog;
}

StmtPtr Parser::parseStatement(int line) {
  if (check(TokenKind::KeywordPrint))
    return parsePrint();
  if (check(TokenKind::KeywordLet))
    return parseLet();
  if (check(TokenKind::KeywordIf))
    return parseIf(line);
  if (check(TokenKind::KeywordWhile))
    return parseWhile();
  if (check(TokenKind::KeywordGoto))
    return parseGoto();
  if (check(TokenKind::KeywordEnd))
    return parseEnd();
  return std::make_unique<EndStmt>();
}

StmtPtr Parser::parsePrint() {
  advance(); // PRINT
  auto e = parseExpression();
  auto stmt = std::make_unique<PrintStmt>();
  stmt->expr = std::move(e);
  return stmt;
}

StmtPtr Parser::parseLet() {
  advance(); // LET
  std::string name = current_.lexeme;
  consume(TokenKind::Identifier);
  consume(TokenKind::Equal);
  auto e = parseExpression();
  auto stmt = std::make_unique<LetStmt>();
  stmt->name = name;
  stmt->expr = std::move(e);
  return stmt;
}

StmtPtr Parser::parseIf(int line) {
  advance(); // IF
  auto cond = parseExpression();
  consume(TokenKind::KeywordThen);
  auto thenStmt = parseStatement(line);
  StmtPtr elseStmt;
  if (check(TokenKind::KeywordElse)) {
    advance();
    elseStmt = parseStatement(line);
  }
  auto stmt = std::make_unique<IfStmt>();
  stmt->cond = std::move(cond);
  stmt->then_branch = std::move(thenStmt);
  stmt->else_branch = std::move(elseStmt);
  if (stmt->then_branch)
    stmt->then_branch->line = line;
  if (stmt->else_branch)
    stmt->else_branch->line = line;
  return stmt;
}

StmtPtr Parser::parseWhile() {
  advance(); // WHILE
  auto cond = parseExpression();
  consume(TokenKind::EndOfLine);
  auto stmt = std::make_unique<WhileStmt>();
  stmt->cond = std::move(cond);
  while (true) {
    while (check(TokenKind::EndOfLine))
      advance();
    if (check(TokenKind::EndOfFile))
      break;
    int innerLine = 0;
    if (check(TokenKind::Number)) {
      innerLine = std::atoi(current_.lexeme.c_str());
      advance();
    }
    if (check(TokenKind::KeywordWend)) {
      advance();
      break;
    }
    auto bodyStmt = parseStatement(innerLine);
    bodyStmt->line = innerLine;
    stmt->body.push_back(std::move(bodyStmt));
    if (check(TokenKind::EndOfLine))
      advance();
  }
  return stmt;
}

StmtPtr Parser::parseGoto() {
  advance(); // GOTO
  int target = std::atoi(current_.lexeme.c_str());
  consume(TokenKind::Number);
  auto stmt = std::make_unique<GotoStmt>();
  stmt->target = target;
  return stmt;
}

StmtPtr Parser::parseEnd() {
  advance(); // END
  return std::make_unique<EndStmt>();
}

int Parser::precedence(TokenKind k) {
  switch (k) {
  case TokenKind::Star:
  case TokenKind::Slash:
    return 3;
  case TokenKind::Plus:
  case TokenKind::Minus:
    return 2;
  case TokenKind::Equal:
  case TokenKind::NotEqual:
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual:
    return 1;
  default:
    return 0;
  }
}

ExprPtr Parser::parseExpression(int min_prec) {
  auto left = parsePrimary();
  while (true) {
    int prec = precedence(current_.kind);
    if (prec < min_prec || prec == 0)
      break;
    TokenKind op = current_.kind;
    advance();
    auto right = parseExpression(prec + 1);
    auto bin = std::make_unique<BinaryExpr>();
    switch (op) {
    case TokenKind::Plus:
      bin->op = BinaryExpr::Op::Add;
      break;
    case TokenKind::Minus:
      bin->op = BinaryExpr::Op::Sub;
      break;
    case TokenKind::Star:
      bin->op = BinaryExpr::Op::Mul;
      break;
    case TokenKind::Slash:
      bin->op = BinaryExpr::Op::Div;
      break;
    case TokenKind::Equal:
      bin->op = BinaryExpr::Op::Eq;
      break;
    case TokenKind::NotEqual:
      bin->op = BinaryExpr::Op::Ne;
      break;
    case TokenKind::Less:
      bin->op = BinaryExpr::Op::Lt;
      break;
    case TokenKind::LessEqual:
      bin->op = BinaryExpr::Op::Le;
      break;
    case TokenKind::Greater:
      bin->op = BinaryExpr::Op::Gt;
      break;
    case TokenKind::GreaterEqual:
      bin->op = BinaryExpr::Op::Ge;
      break;
    default:
      bin->op = BinaryExpr::Op::Add;
    }
    bin->lhs = std::move(left);
    bin->rhs = std::move(right);
    left = std::move(bin);
  }
  return left;
}

ExprPtr Parser::parsePrimary() {
  if (check(TokenKind::Number)) {
    int v = std::atoi(current_.lexeme.c_str());
    auto e = std::make_unique<IntExpr>();
    e->value = v;
    advance();
    return e;
  }
  if (check(TokenKind::String)) {
    auto e = std::make_unique<StringExpr>();
    e->value = current_.lexeme;
    advance();
    return e;
  }
  if (check(TokenKind::Identifier)) {
    std::string name = current_.lexeme;
    advance();
    if (consume(TokenKind::LParen)) {
      std::vector<ExprPtr> args;
      if (!check(TokenKind::RParen)) {
        while (true) {
          args.push_back(parseExpression());
          if (consume(TokenKind::Comma))
            continue;
          break;
        }
      }
      consume(TokenKind::RParen);
      auto call = std::make_unique<CallExpr>();
      if (name == "LEN")
        call->builtin = CallExpr::Builtin::Len;
      else
        call->builtin = CallExpr::Builtin::Mid;
      call->args = std::move(args);
      return call;
    }
    auto v = std::make_unique<VarExpr>();
    v->name = name;
    return v;
  }
  if (consume(TokenKind::LParen)) {
    auto e = parseExpression();
    consume(TokenKind::RParen);
    return e;
  }
  auto e = std::make_unique<IntExpr>();
  e->value = 0;
  return e;
}

} // namespace il::frontends::basic
