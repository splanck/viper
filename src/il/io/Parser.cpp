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

Type parseType(const std::string &t, bool *ok = nullptr)
{
    if (t == "i64" || t == "i32")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::I64);
    }
    if (t == "i1")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::I1);
    }
    if (t == "f64")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::F64);
    }
    if (t == "ptr")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::Ptr);
    }
    if (t == "str")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::Str);
    }
    if (t == "void")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::Void);
    }
    if (ok)
        *ok = false;
    return Type(Type::Kind::Void);
}

Value parseValue(const std::string &tok, const std::unordered_map<std::string, unsigned> &temps)
{
    if (tok.empty())
        return Value::constInt(0);
    if (tok[0] == '%')
    {
        std::string name = tok.substr(1);
        auto it = temps.find(name);
        if (it != temps.end())
            return Value::temp(it->second);
        if (name.size() > 1 && name[0] == 't')
        {
            bool digits = true;
            for (size_t i = 1; i < name.size(); ++i)
                if (!std::isdigit(static_cast<unsigned char>(name[i])))
                    digits = false;
            if (digits)
                return Value::temp(std::stoul(name.substr(1)));
        }
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
    std::unordered_map<std::string, size_t> blockParamCount;

    struct PendingBr
    {
        std::string label;
        size_t args;
        unsigned line;
    };

    std::vector<PendingBr> pendingBrs;
    while (std::getline(is, line))
    {
        ++lineNo;
        line = trim(line);
        if (line.empty())
            continue;
        if (line.rfind("//", 0) == 0)
            continue;
        if (!curFn)
        {
            if (line.rfind("il", 0) == 0)
            {
                std::istringstream ls(line);
                std::string kw;
                ls >> kw;
                std::string ver;
                if (ls >> ver)
                    m.version = ver;
                else
                    m.version = "0.1.2";
                continue;
            }
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
                tempIds.clear();
                unsigned idx = 0;
                for (auto &p : params)
                {
                    p.id = idx;
                    tempIds[p.name] = idx;
                    ++idx;
                }
                m.functions.push_back({name, parseType(retStr), params, {}, {}});
                curFn = &m.functions.back();
                curBB = nullptr;
                nextTemp = idx;
                curFn->valueNames.resize(nextTemp);
                for (auto &p : params)
                    curFn->valueNames[p.id] = p.name;
                blockParamCount.clear();
                pendingBrs.clear();
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
            if (line.back() == ':')
            {
                std::string header = line.substr(0, line.size() - 1);
                size_t lp = header.find('(');
                std::vector<Param> params;
                std::string label = trim(header);
                if (lp != std::string::npos)
                {
                    size_t rp = header.find(')', lp);
                    if (rp == std::string::npos)
                    {
                        err << "line " << lineNo << ": mismatched ')\n";
                        return false;
                    }
                    label = trim(header.substr(0, lp));
                    std::string paramsStr = header.substr(lp + 1, rp - lp - 1);
                    std::stringstream pss(paramsStr);
                    std::string p;
                    while (std::getline(pss, p, ','))
                    {
                        p = trim(p);
                        if (p.empty())
                            continue;
                        size_t col = p.find(':');
                        if (col == std::string::npos)
                        {
                            err << "line " << lineNo << ": bad param\n";
                            return false;
                        }
                        std::string nm = trim(p.substr(0, col));
                        if (!nm.empty() && nm[0] == '%')
                            nm = nm.substr(1);
                        std::string tyStr = trim(p.substr(col + 1));
                        bool ok = true;
                        Type ty = parseType(tyStr, &ok);
                        if (!ok || ty.kind == Type::Kind::Void)
                        {
                            err << "line " << lineNo << ": unknown param type\n";
                            return false;
                        }
                        params.push_back({nm, ty, nextTemp});
                        tempIds[nm] = nextTemp;
                        if (curFn->valueNames.size() <= nextTemp)
                            curFn->valueNames.resize(nextTemp + 1);
                        curFn->valueNames[nextTemp] = nm;
                        ++nextTemp;
                    }
                }
                curFn->blocks.push_back({label, params, {}, false});
                curBB = &curFn->blocks.back();
                blockParamCount[label] = params.size();
                for (auto it = pendingBrs.begin(); it != pendingBrs.end();)
                {
                    if (it->label == label)
                    {
                        if (it->args != params.size())
                        {
                            err << "line " << it->line << ": bad arg count\n";
                            return false;
                        }
                        it = pendingBrs.erase(it);
                    }
                    else
                        ++it;
                }
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
                {
                    if (curFn->valueNames.size() <= nextTemp)
                        curFn->valueNames.resize(nextTemp + 1);
                    curFn->valueNames[nextTemp] = res;
                    nextTemp++;
                }
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
                    std::string rest;
                    std::getline(ss, rest);
                    rest = trim(rest);
                    if (rest.rfind("label ", 0) == 0)
                        rest = trim(rest.substr(6));
                    size_t lp = rest.find('(');
                    std::vector<Value> args;
                    std::string lbl;
                    if (lp == std::string::npos)
                    {
                        lbl = rest;
                    }
                    else
                    {
                        size_t rp = rest.find(')', lp);
                        if (rp == std::string::npos)
                        {
                            err << "line " << lineNo << ": mismatched ')\n";
                            return false;
                        }
                        lbl = trim(rest.substr(0, lp));
                        std::string argsStr = rest.substr(lp + 1, rp - lp - 1);
                        std::stringstream as(argsStr);
                        std::string a;
                        while (std::getline(as, a, ','))
                        {
                            a = trim(a);
                            if (!a.empty())
                                args.push_back(parseValue(a, tempIds));
                        }
                    }
                    in.labels.push_back(lbl);
                    in.brArgs.push_back(args);
                    size_t argCount = args.size();
                    auto it = blockParamCount.find(lbl);
                    if (it != blockParamCount.end())
                    {
                        if (it->second != argCount)
                        {
                            err << "line " << lineNo << ": bad arg count\n";
                            return false;
                        }
                    }
                    else
                        pendingBrs.push_back({lbl, argCount, lineNo});
                    in.type = Type(Type::Kind::Void);
                    break;
                }
                case Opcode::CBr:
                {
                    std::string c = readToken(ss);
                    std::string rest;
                    std::getline(ss, rest);
                    rest = trim(rest);
                    size_t comma = rest.find(',');
                    if (comma == std::string::npos)
                    {
                        err << "line " << lineNo << ": malformed cbr\n";
                        return false;
                    }
                    std::string first = trim(rest.substr(0, comma));
                    std::string second = trim(rest.substr(comma + 1));
                    auto parseTarget = [&](const std::string &part,
                                           std::string &lbl,
                                           std::vector<Value> &args) -> bool
                    {
                        std::string t = part;
                        if (t.rfind("label ", 0) == 0)
                            t = trim(t.substr(6));
                        size_t lp = t.find('(');
                        if (lp == std::string::npos)
                        {
                            lbl = trim(t);
                        }
                        else
                        {
                            size_t rp = t.find(')', lp);
                            if (rp == std::string::npos)
                                return false;
                            lbl = trim(t.substr(0, lp));
                            std::string argsStr = t.substr(lp + 1, rp - lp - 1);
                            std::stringstream as(argsStr);
                            std::string a;
                            while (std::getline(as, a, ','))
                            {
                                a = trim(a);
                                if (!a.empty())
                                    args.push_back(parseValue(a, tempIds));
                            }
                        }
                        return true;
                    };
                    std::vector<Value> a1, a2;
                    std::string l1, l2;
                    if (!parseTarget(first, l1, a1) || !parseTarget(second, l2, a2))
                    {
                        err << "line " << lineNo << ": mismatched ')\n";
                        return false;
                    }
                    in.operands.push_back(parseValue(c, tempIds));
                    in.labels.push_back(l1);
                    in.labels.push_back(l2);
                    in.brArgs.push_back(a1);
                    in.brArgs.push_back(a2);
                    size_t n1 = a1.size();
                    auto it1 = blockParamCount.find(l1);
                    if (it1 != blockParamCount.end())
                    {
                        if (it1->second != n1)
                        {
                            err << "line " << lineNo << ": bad arg count\n";
                            return false;
                        }
                    }
                    else
                        pendingBrs.push_back({l1, n1, lineNo});
                    size_t n2 = a2.size();
                    auto it2 = blockParamCount.find(l2);
                    if (it2 != blockParamCount.end())
                    {
                        if (it2->second != n2)
                        {
                            err << "line " << lineNo << ": bad arg count\n";
                            return false;
                        }
                    }
                    else
                        pendingBrs.push_back({l2, n2, lineNo});
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
