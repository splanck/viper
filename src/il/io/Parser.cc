#include "il/io/Parser.h"
#include "il/core/Opcode.h"
#include "il/core/Value.h"
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::io {

namespace {
std::string trim(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
    ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
    --e;
  return s.substr(b, e - b);
}

Type parseType(const std::string &t) {
  if (t == "i64" || t == "i32")
    return Type(Type::Kind::I64);
  if (t == "i1")
    return Type(Type::Kind::I1);
  if (t == "f64")
    return Type(Type::Kind::F64);
  if (t == "ptr")
    return Type(Type::Kind::Ptr);
  if (t == "str")
    return Type(Type::Kind::Str);
  return Type(Type::Kind::Void);
}

Value parseValue(const std::string &tok, const std::unordered_map<std::string, unsigned> &temps) {
  if (tok.empty())
    return Value::constInt(0);
  if (tok[0] == '%') {
    auto it = temps.find(tok.substr(1));
    if (it != temps.end())
      return Value::temp(it->second);
    return Value::temp(0); // undefined temp will be diagnosed later
  }
  if (tok[0] == '@')
    return Value::global(tok.substr(1));
  if (tok == "null")
    return Value::null();
  if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"')
    return Value::constStr(tok.substr(1, tok.size() - 2));
  return Value::constInt(std::stoll(tok));
}

std::string readToken(std::istringstream &ss) {
  std::string t;
  ss >> t;
  if (!t.empty() && t.back() == ',')
    t.pop_back();
  return t;
}

} // namespace

bool Parser::parse(std::istream &is, Module &m, std::ostream &err) {
  std::string line;
  Function *curFn = nullptr;
  BasicBlock *curBB = nullptr;
  std::unordered_map<std::string, unsigned> tempIds;
  unsigned nextTemp = 0;
  while (std::getline(is, line)) {
    line = trim(line);
    if (line.empty())
      continue;
    if (!curFn) {
      if (line.rfind("il ", 0) == 0)
        continue;
      if (line.rfind("extern", 0) == 0) {
        size_t at = line.find('@');
        size_t lp = line.find('(', at);
        size_t rp = line.find(')', lp);
        size_t arr = line.find("->", rp);
        std::string name = line.substr(at + 1, lp - at - 1);
        std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
        std::vector<Type> params;
        std::stringstream pss(paramsStr);
        std::string p;
        while (std::getline(pss, p, ',')) {
          p = trim(p);
          if (!p.empty())
            params.push_back(parseType(p));
        }
        std::string retStr = trim(line.substr(arr + 2));
        m.externs.push_back({name, parseType(retStr), params});
        continue;
      }
      if (line.rfind("global", 0) == 0) {
        size_t at = line.find('@');
        size_t eq = line.find('=', at);
        size_t q1 = line.find('"', eq);
        size_t q2 = line.rfind('"');
        std::string name = trim(line.substr(at + 1, eq - at - 1));
        std::string init = line.substr(q1 + 1, q2 - q1 - 1);
        m.globals.push_back({name, Type(Type::Kind::Str), init});
        continue;
      }
      if (line.rfind("func", 0) == 0) {
        size_t at = line.find('@');
        size_t lp = line.find('(', at);
        size_t rp = line.find(')', lp);
        size_t arr = line.find("->", rp);
        size_t lb = line.find('{', arr);
        std::string name = line.substr(at + 1, lp - at - 1);
        std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
        std::vector<Param> params;
        std::stringstream pss(paramsStr);
        std::string p;
        while (std::getline(pss, p, ',')) {
          p = trim(p);
          if (p.empty())
            continue;
          std::stringstream ps(p);
          std::string ty, nm;
          ps >> ty >> nm;
          if (!ty.empty() && !nm.empty() && nm[0] == '%')
            params.push_back({nm.substr(1), parseType(ty)});
        }
        std::string retStr = trim(line.substr(arr + 2, lb - arr - 2));
        m.functions.push_back({name, parseType(retStr), params, {}});
        curFn = &m.functions.back();
        curBB = nullptr;
        tempIds.clear();
        nextTemp = 0;
        continue;
      }
      err << "Unexpected line: " << line << "\n";
      return false;
    } else {
      if (line[0] == '}') {
        curFn = nullptr;
        curBB = nullptr;
        continue;
      }
      if (line.back() == ':' && line.find(' ') == std::string::npos) {
        std::string label = line.substr(0, line.size() - 1);
        curFn->blocks.push_back({label, {}, false});
        curBB = &curFn->blocks.back();
        continue;
      }
      if (!curBB) {
        err << "Instruction outside block\n";
        return false;
      }
      Instr in;
      if (line[0] == '%') {
        size_t eq = line.find('=');
        std::string res = trim(line.substr(1, eq - 1));
        auto [it, inserted] = tempIds.emplace(res, nextTemp);
        if (inserted)
          nextTemp++;
        in.result = it->second;
        line = trim(line.substr(eq + 1));
      }
      std::string op;
      std::istringstream ss(line);
      ss >> op;
      if (op == "add" || op == "mul" || op == "scmp_gt" || op == "scmp_le") {
        std::string a = readToken(ss);
        std::string b = readToken(ss);
        in.operands.push_back(parseValue(a, tempIds));
        in.operands.push_back(parseValue(b, tempIds));
        if (op == "add")
          in.op = Opcode::Add;
        else if (op == "mul")
          in.op = Opcode::Mul;
        else if (op == "scmp_gt")
          in.op = Opcode::SCmpGT;
        else
          in.op = Opcode::SCmpLE;
        in.type =
            (op == "scmp_gt" || op == "scmp_le") ? Type(Type::Kind::I1) : Type(Type::Kind::I64);
      } else if (op == "alloca") {
        std::string sz = readToken(ss);
        in.op = Opcode::Alloca;
        in.operands.push_back(parseValue(sz, tempIds));
        in.type = Type(Type::Kind::Ptr);
      } else if (op == "load") {
        std::string ty = readToken(ss);
        std::string ptr = readToken(ss);
        in.op = Opcode::Load;
        in.type = parseType(ty);
        in.operands.push_back(parseValue(ptr, tempIds));
      } else if (op == "store") {
        std::string ty = readToken(ss);
        std::string ptr = readToken(ss);
        std::string val = readToken(ss);
        in.op = Opcode::Store;
        in.type = parseType(ty);
        in.operands.push_back(parseValue(ptr, tempIds));
        in.operands.push_back(parseValue(val, tempIds));
      } else if (op == "const_str") {
        std::string g = readToken(ss);
        in.op = Opcode::ConstStr;
        if (!g.empty() && g[0] == '@')
          in.operands.push_back(Value::global(g.substr(1)));
        in.type = Type(Type::Kind::Str);
      } else if (op == "call") {
        size_t at = line.find('@');
        size_t lp = line.find('(', at);
        size_t rp = line.find(')', lp);
        in.op = Opcode::Call;
        in.callee = line.substr(at + 1, lp - at - 1);
        std::string args = line.substr(lp + 1, rp - lp - 1);
        std::stringstream as(args);
        std::string a;
        while (std::getline(as, a, ',')) {
          a = trim(a);
          if (!a.empty())
            in.operands.push_back(parseValue(a, tempIds));
        }
        in.type = Type(Type::Kind::Void);
      } else if (op == "br") {
        std::string w = readToken(ss);
        std::string l = readToken(ss);
        in.op = Opcode::Br;
        in.labels.push_back(l);
        in.type = Type(Type::Kind::Void);
      } else if (op == "cbr") {
        std::string c = readToken(ss);
        std::string word = readToken(ss);
        std::string l1 = readToken(ss);
        word = readToken(ss);
        std::string l2 = readToken(ss);
        in.op = Opcode::CBr;
        in.operands.push_back(parseValue(c, tempIds));
        in.labels.push_back(l1);
        in.labels.push_back(l2);
        in.type = Type(Type::Kind::Void);
      } else if (op == "ret") {
        std::string v;
        if (ss >> v)
          in.operands.push_back(parseValue(v, tempIds));
        in.op = Opcode::Ret;
        in.type = Type(Type::Kind::Void);
      } else {
        err << "Unknown opcode: " << op << "\n";
        return false;
      }
      curBB->instructions.push_back(std::move(in));
    }
  }
  return true;
}

} // namespace il::io
