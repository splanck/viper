// File: src/il/io/Parser.cpp
// Purpose: Implements parser for IL textual format.
// Key invariants: None.
// Ownership/Lifetime: Parser operates on externally managed strings.
// Links: docs/il-spec.md

#include "il/io/Parser.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "support/source_manager.hpp"
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::io
{

namespace
{

std::string trim(const std::string &s)
{
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

Type parseType(const std::string &t)
{
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

Value parseValue(const std::string &tok, const std::unordered_map<std::string, unsigned> &temps)
{
    if (tok.empty())
        return Value::constInt(0);
    if (tok[0] == '%')
    {
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
    if (tok.find('.') != std::string::npos || tok.find('e') != std::string::npos ||
        tok.find('E') != std::string::npos)
        return Value::constFloat(std::stod(tok));
    return Value::constInt(std::stoll(tok));
}

std::string readToken(std::istringstream &ss)
{
    std::string t;
    ss >> t;
    if (!t.empty() && t.back() == ',')
        t.pop_back();
    return t;
}

} // namespace

bool Parser::parse(std::istream &is, Module &m, std::ostream &err)
{
    std::string line;
    Function *curFn = nullptr;
    BasicBlock *curBB = nullptr;
    std::unordered_map<std::string, unsigned> tempIds;
    unsigned nextTemp = 0;
    unsigned lineNo = 0;
    il::support::SourceLoc curLoc{};
    while (std::getline(is, line))
    {
        ++lineNo;
        line = trim(line);
        if (line.empty())
            continue;
        if (!curFn)
        {
            if (line.rfind("il ", 0) == 0)
                continue;
            if (line.rfind("extern", 0) == 0)
            {
                size_t at = line.find('@');
                size_t lp = line.find('(', at);
                size_t rp = line.find(')', lp);
                size_t arr = line.find("->", rp);
                std::string name = line.substr(at + 1, lp - at - 1);
                std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
                std::vector<Type> params;
                std::stringstream pss(paramsStr);
                std::string p;
                while (std::getline(pss, p, ','))
                {
                    p = trim(p);
                    if (!p.empty())
                        params.push_back(parseType(p));
                }
                std::string retStr = trim(line.substr(arr + 2));
                m.externs.push_back({name, parseType(retStr), params});
                continue;
            }
            if (line.rfind("global", 0) == 0)
            {
                size_t at = line.find('@');
                size_t eq = line.find('=', at);
                size_t q1 = line.find('"', eq);
                size_t q2 = line.rfind('"');
                std::string name = trim(line.substr(at + 1, eq - at - 1));
                std::string init = line.substr(q1 + 1, q2 - q1 - 1);
                m.globals.push_back({name, Type(Type::Kind::Str), init});
                continue;
            }
            if (line.rfind("func", 0) == 0)
            {
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
                while (std::getline(pss, p, ','))
                {
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
            err << "line " << lineNo << ": unexpected line: " << line << "\n";
            return false;
        }
        else
        {
            if (line[0] == '}')
            {
                curFn = nullptr;
                curBB = nullptr;
                continue;
            }
            if (line.back() == ':' && line.find(' ') == std::string::npos)
            {
                std::string label = line.substr(0, line.size() - 1);
                curFn->blocks.push_back({label, {}, false});
                curBB = &curFn->blocks.back();
                continue;
            }
            if (!curBB)
            {
                err << "line " << lineNo << ": instruction outside block\n";
                return false;
            }
            if (line.rfind(".loc", 0) == 0)
            {
                std::istringstream ls(line.substr(4));
                uint32_t fid = 0, ln = 0, col = 0;
                ls >> fid >> ln >> col;
                curLoc = {fid, ln, col};
                continue;
            }
            Instr in;
            in.loc = curLoc;
            if (line[0] == '%')
            {
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
            static const std::unordered_map<std::string, Opcode> opMap = {
                {"add", Opcode::Add},
                {"sub", Opcode::Sub},
                {"mul", Opcode::Mul},
                {"sdiv", Opcode::SDiv},
                {"udiv", Opcode::UDiv},
                {"srem", Opcode::SRem},
                {"urem", Opcode::URem},
                {"and", Opcode::And},
                {"or", Opcode::Or},
                {"xor", Opcode::Xor},
                {"shl", Opcode::Shl},
                {"lshr", Opcode::LShr},
                {"ashr", Opcode::AShr},
                {"fadd", Opcode::FAdd},
                {"fsub", Opcode::FSub},
                {"fmul", Opcode::FMul},
                {"fdiv", Opcode::FDiv},
                {"icmp_eq", Opcode::ICmpEq},
                {"icmp_ne", Opcode::ICmpNe},
                {"scmp_lt", Opcode::SCmpLT},
                {"scmp_le", Opcode::SCmpLE},
                {"scmp_gt", Opcode::SCmpGT},
                {"scmp_ge", Opcode::SCmpGE},
                {"ucmp_lt", Opcode::UCmpLT},
                {"ucmp_le", Opcode::UCmpLE},
                {"ucmp_gt", Opcode::UCmpGT},
                {"ucmp_ge", Opcode::UCmpGE},
                {"fcmp_lt", Opcode::FCmpLT},
                {"fcmp_le", Opcode::FCmpLE},
                {"fcmp_gt", Opcode::FCmpGT},
                {"fcmp_ge", Opcode::FCmpGE},
                {"fcmp_eq", Opcode::FCmpEQ},
                {"fcmp_ne", Opcode::FCmpNE},
                {"sitofp", Opcode::Sitofp},
                {"fptosi", Opcode::Fptosi},
                {"zext1", Opcode::Zext1},
                {"trunc1", Opcode::Trunc1},
                {"alloca", Opcode::Alloca},
                {"gep", Opcode::GEP},
                {"load", Opcode::Load},
                {"store", Opcode::Store},
                {"addr_of", Opcode::AddrOf},
                {"const_str", Opcode::ConstStr},
                {"const_null", Opcode::ConstNull},
                {"call", Opcode::Call},
                {"br", Opcode::Br},
                {"cbr", Opcode::CBr},
                {"ret", Opcode::Ret},
                {"trap", Opcode::Trap}};
            auto itOp = opMap.find(op);
            if (itOp == opMap.end())
            {
                err << "line " << lineNo << ": unknown opcode " << op << "\n";
                return false;
            }
            in.op = itOp->second;
            switch (in.op)
            {
                case Opcode::Alloca:
                {
                    std::string sz = readToken(ss);
                    in.operands.push_back(parseValue(sz, tempIds));
                    in.type = Type(Type::Kind::Ptr);
                    break;
                }
                case Opcode::GEP:
                {
                    std::string base = readToken(ss);
                    std::string off = readToken(ss);
                    in.operands.push_back(parseValue(base, tempIds));
                    in.operands.push_back(parseValue(off, tempIds));
                    in.type = Type(Type::Kind::Ptr);
                    break;
                }
                case Opcode::Load:
                {
                    std::string ty = readToken(ss);
                    std::string ptr = readToken(ss);
                    in.type = parseType(ty);
                    in.operands.push_back(parseValue(ptr, tempIds));
                    break;
                }
                case Opcode::Store:
                {
                    std::string ty = readToken(ss);
                    std::string ptr = readToken(ss);
                    std::string val = readToken(ss);
                    in.type = parseType(ty);
                    in.operands.push_back(parseValue(ptr, tempIds));
                    in.operands.push_back(parseValue(val, tempIds));
                    break;
                }
                case Opcode::AddrOf:
                {
                    std::string g = readToken(ss);
                    in.operands.push_back(parseValue(g, tempIds));
                    in.type = Type(Type::Kind::Ptr);
                    break;
                }
                case Opcode::ConstStr:
                {
                    std::string g = readToken(ss);
                    if (!g.empty())
                        in.operands.push_back(parseValue(g, tempIds));
                    in.type = Type(Type::Kind::Str);
                    break;
                }
                case Opcode::ConstNull:
                {
                    in.type = Type(Type::Kind::Ptr);
                    break;
                }
                case Opcode::Call:
                {
                    size_t at = line.find('@');
                    size_t lp = line.find('(', at);
                    size_t rp = line.find(')', lp);
                    in.callee = line.substr(at + 1, lp - at - 1);
                    std::string args = line.substr(lp + 1, rp - lp - 1);
                    std::stringstream as(args);
                    std::string a;
                    while (std::getline(as, a, ','))
                    {
                        a = trim(a);
                        if (!a.empty())
                            in.operands.push_back(parseValue(a, tempIds));
                    }
                    in.type = Type(Type::Kind::Void);
                    break;
                }
                case Opcode::Br:
                {
                    std::string word = readToken(ss);
                    std::string l = readToken(ss);
                    in.labels.push_back(l);
                    in.type = Type(Type::Kind::Void);
                    break;
                }
                case Opcode::CBr:
                {
                    std::string c = readToken(ss);
                    std::string word = readToken(ss);
                    std::string l1 = readToken(ss);
                    word = readToken(ss);
                    std::string l2 = readToken(ss);
                    in.operands.push_back(parseValue(c, tempIds));
                    in.labels.push_back(l1);
                    in.labels.push_back(l2);
                    in.type = Type(Type::Kind::Void);
                    break;
                }
                case Opcode::Ret:
                {
                    std::string v;
                    if (ss >> v)
                        in.operands.push_back(parseValue(v, tempIds));
                    in.type = Type(Type::Kind::Void);
                    break;
                }
                case Opcode::Trap:
                {
                    in.type = Type(Type::Kind::Void);
                    break;
                }
                default:
                {
                    bool unary = in.op == Opcode::Sitofp || in.op == Opcode::Fptosi ||
                                 in.op == Opcode::Zext1 || in.op == Opcode::Trunc1;
                    std::string a = readToken(ss);
                    in.operands.push_back(parseValue(a, tempIds));
                    if (!unary)
                    {
                        std::string b = readToken(ss);
                        if (!b.empty())
                            in.operands.push_back(parseValue(b, tempIds));
                    }
                    if (op == "fadd" || op == "fsub" || op == "fmul" || op == "fdiv" ||
                        op == "sitofp")
                        in.type = Type(Type::Kind::F64);
                    else if (op.rfind("icmp_", 0) == 0 || op.rfind("scmp_", 0) == 0 ||
                             op.rfind("ucmp_", 0) == 0 || op.rfind("fcmp_", 0) == 0 ||
                             op == "trunc1")
                        in.type = Type(Type::Kind::I1);
                    else
                        in.type = Type(Type::Kind::I64);
                    break;
                }
            }
            curBB->instructions.push_back(std::move(in));
        }
    }
    return true;
}

} // namespace il::io
