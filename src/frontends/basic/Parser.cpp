#include "frontends/basic/Parser.h"
#include <stdexcept>

namespace il::basic {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token &Parser::peek() const { return tokens_[pos_]; }

bool Parser::match(TokenKind kind) {
  if (peek().kind != kind) return false;
  ++pos_;
  return true;
}

const Token &Parser::consume(TokenKind kind) {
  if (peek().kind != kind) throw std::runtime_error("unexpected token");
  return tokens_[pos_++];
}

Program Parser::parse() {
  Program prog;
  while (peek().kind != TokenKind::Eof) {
    auto stmt = parseLine();
    prog.statements.push_back(std::move(stmt));
    match(TokenKind::Newline);
  }
  return prog;
}

std::unique_ptr<Stmt> Parser::parseLine() {
  int line = 0;
  support::SourceLoc loc = peek().loc;
  if (peek().kind == TokenKind::Integer) {
    line = consume(TokenKind::Integer).int_value;
    loc = peek().loc;
  }
  auto stmt = parseStmt();
  stmt->line = line;
  stmt->loc = loc;
  return stmt;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
  switch (peek().kind) {
  case TokenKind::KeywordPrint: {
    consume(TokenKind::KeywordPrint);
    auto expr = parseExpr();
    return std::make_unique<PrintStmt>(std::move(expr), 0, peek().loc);
  }
  case TokenKind::KeywordLet: {
    consume(TokenKind::KeywordLet);
    std::string name = consume(TokenKind::Identifier).text;
    consume(TokenKind::Equals);
    auto expr = parseExpr();
    return std::make_unique<LetStmt>(name, std::move(expr), 0, peek().loc);
  }
  case TokenKind::KeywordIf: {
    consume(TokenKind::KeywordIf);
    auto cond = parseExpr();
    consume(TokenKind::KeywordThen);
    auto then_stmt = parseInlineStmt();
    std::unique_ptr<Stmt> else_stmt;
    if (match(TokenKind::KeywordElse)) {
      else_stmt = parseInlineStmt();
    }
    return std::make_unique<IfStmt>(std::move(cond), std::move(then_stmt),
                                    std::move(else_stmt), 0, peek().loc);
  }
  case TokenKind::KeywordWhile: {
    consume(TokenKind::KeywordWhile);
    auto cond = parseExpr();
    consume(TokenKind::Newline);
    std::vector<std::unique_ptr<Stmt>> body;
    while (true) {
      if (peek().kind == TokenKind::KeywordWend) break;
      if (peek().kind == TokenKind::Integer && tokens_[pos_ + 1].kind == TokenKind::KeywordWend) {
        consume(TokenKind::Integer);
        break;
      }
      auto st = parseLine();
      body.push_back(std::move(st));
      match(TokenKind::Newline);
    }
    consume(TokenKind::KeywordWend);
    return std::make_unique<WhileStmt>(std::move(cond), std::move(body), 0,
                                       peek().loc);
  }
  case TokenKind::KeywordGoto: {
    consume(TokenKind::KeywordGoto);
    int tgt = consume(TokenKind::Integer).int_value;
    return std::make_unique<GotoStmt>(tgt, 0, peek().loc);
  }
  case TokenKind::KeywordEnd: {
    consume(TokenKind::KeywordEnd);
    return std::make_unique<EndStmt>(0, peek().loc);
  }
  default:
    throw std::runtime_error("unknown statement");
  }
}

std::unique_ptr<Stmt> Parser::parseInlineStmt() {
  switch (peek().kind) {
  case TokenKind::KeywordPrint:
  case TokenKind::KeywordLet:
  case TokenKind::KeywordIf:
  case TokenKind::KeywordWhile:
  case TokenKind::KeywordGoto:
  case TokenKind::KeywordEnd:
    return parseStmt();
  default:
    throw std::runtime_error("invalid inline statement");
  }
}

int Parser::getPrecedence(TokenKind kind) const {
  switch (kind) {
  case TokenKind::Equals:
  case TokenKind::NotEqual:
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual:
    return 1;
  case TokenKind::Plus:
  case TokenKind::Minus:
    return 2;
  case TokenKind::Star:
  case TokenKind::Slash:
    return 3;
  default:
    return 0;
  }
}

std::unique_ptr<Expr> Parser::parseExpr(int prec) {
  auto lhs = parsePrimary();
  while (true) {
    int p = getPrecedence(peek().kind);
    if (p <= prec) break;
    Token op = consume(peek().kind);
    auto rhs = parseExpr(p);
    auto bin = std::make_unique<BinaryExpr>("", std::move(lhs), std::move(rhs), op.loc);
    switch (op.kind) {
    case TokenKind::Plus: bin->op = "+"; break;
    case TokenKind::Minus: bin->op = "-"; break;
    case TokenKind::Star: bin->op = "*"; break;
    case TokenKind::Slash: bin->op = "/"; break;
    case TokenKind::Equals: bin->op = "="; break;
    case TokenKind::NotEqual: bin->op = "<>"; break;
    case TokenKind::Less: bin->op = "<"; break;
    case TokenKind::LessEqual: bin->op = "<="; break;
    case TokenKind::Greater: bin->op = ">"; break;
    case TokenKind::GreaterEqual: bin->op = ">="; break;
    default: break;
    }
    lhs = std::move(bin);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
  const Token &tok = peek();
  switch (tok.kind) {
  case TokenKind::Integer:
    consume(TokenKind::Integer);
    return std::make_unique<IntExpr>(tok.int_value, tok.loc);
  case TokenKind::String:
    consume(TokenKind::String);
    return std::make_unique<StringExpr>(tok.text, tok.loc);
  case TokenKind::Identifier: {
    consume(TokenKind::Identifier);
    if (match(TokenKind::LParen)) {
      std::vector<std::unique_ptr<Expr>> args;
      if (!match(TokenKind::RParen)) {
        do {
          args.push_back(parseExpr());
        } while (match(TokenKind::Comma));
        consume(TokenKind::RParen);
      }
      return std::make_unique<CallExpr>(tok.text, std::move(args), tok.loc);
    }
    return std::make_unique<VarExpr>(tok.text, tok.loc);
  }
  case TokenKind::LParen: {
    consume(TokenKind::LParen);
    auto e = parseExpr();
    consume(TokenKind::RParen);
    return e;
  }
  default:
    throw std::runtime_error("bad primary");
  }
}

} // namespace il::basic
