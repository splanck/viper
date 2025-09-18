// File: src/il/io/InstrParser.cpp
// Purpose: Implements parsing of IL instruction statements.
// Key invariants: ParserState must reference a current function and basic block.
// Ownership/Lifetime: Instructions are appended to the ParserState's active block.
// Links: docs/il-spec.md

#include "il/io/InstrParser.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"

#include "il/io/ParserUtil.hpp"
#include "il/io/TypeParser.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"

#include <cctype>
#include <exception>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::io::detail
{
namespace
{

using il::core::Instr;
using il::core::Opcode;
using il::core::Type;
using il::core::Value;
using il::core::ResultArity;

Value parseValue(const std::string &tok, ParserState &st, std::ostream &err)
{
    if (tok.empty())
        return Value::constInt(0);
    if (tok[0] == '%')
    {
        std::string name = tok.substr(1);
        auto it = st.tempIds.find(name);
        if (it != st.tempIds.end())
            return Value::temp(it->second);
        if (name.size() > 1 && name[0] == 't')
        {
            bool digits = true;
            for (size_t i = 1; i < name.size(); ++i)
                if (!std::isdigit(static_cast<unsigned char>(name[i])))
                    digits = false;
            if (digits)
            {
                try
                {
                    return Value::temp(std::stoul(name.substr(1)));
                }
                catch (const std::exception &)
                {
                    st.hasError = true;
                    err << "Line " << st.lineNo << ": invalid temp id '" << tok << "'\n";
                    return Value::temp(0);
                }
            }
        }
        return Value::temp(0);
    }
    if (tok[0] == '@')
        return Value::global(tok.substr(1));
    if (tok == "null")
        return Value::null();
    if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"')
        return Value::constStr(tok.substr(1, tok.size() - 2));
    if (tok.find('.') != std::string::npos || tok.find('e') != std::string::npos ||
        tok.find('E') != std::string::npos)
    {
        double value = 0.0;
        if (parseFloatLiteral(tok, value))
            return Value::constFloat(value);
        st.hasError = true;
        err << "Line " << st.lineNo << ": invalid floating literal '" << tok << "'\n";
        return Value::constFloat(0.0);
    }
    long long intValue = 0;
    if (parseIntegerLiteral(tok, intValue))
        return Value::constInt(intValue);
    st.hasError = true;
    err << "Line " << st.lineNo << ": invalid integer literal '" << tok << "'\n";
    return Value::constInt(0);
}

using InstrHandler =
    std::function<bool(const std::string &, Instr &, ParserState &, std::ostream &)>;

bool validateShape(const Instr &in, ParserState &st, std::ostream &err)
{
    const auto &info = il::core::getOpcodeInfo(in.op);
    bool ok = true;

    const size_t operandCount = in.operands.size();
    const bool variadic = il::core::isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadic && operandCount > info.numOperandsMax))
    {
        err << "line " << st.lineNo << ": " << info.name << " expects ";
        if (info.numOperandsMin == info.numOperandsMax)
        {
            err << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                err << 's';
        }
        else if (variadic)
        {
            err << "at least " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                err << 's';
        }
        else
        {
            err << "between " << static_cast<unsigned>(info.numOperandsMin) << " and "
                << static_cast<unsigned>(info.numOperandsMax) << " operands";
        }
        err << "\n";
        st.hasError = true;
        ok = false;
    }

    const bool hasResult = in.result.has_value();
    switch (info.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
            {
                err << "line " << st.lineNo << ": " << info.name << " does not produce a result\n";
                st.hasError = true;
                ok = false;
            }
            break;
        case ResultArity::One:
            if (!hasResult)
            {
                err << "line " << st.lineNo << ": " << info.name << " requires a result\n";
                st.hasError = true;
                ok = false;
            }
            break;
        case ResultArity::Optional:
            break;
    }

    if (in.labels.size() != info.numSuccessors)
    {
        err << "line " << st.lineNo << ": " << info.name << " expects "
            << static_cast<unsigned>(info.numSuccessors) << " label";
        if (info.numSuccessors != 1)
            err << 's';
        err << "\n";
        st.hasError = true;
        ok = false;
    }

    if (in.brArgs.size() > info.numSuccessors)
    {
        err << "line " << st.lineNo << ": " << info.name << " expects at most "
            << static_cast<unsigned>(info.numSuccessors) << " branch argument list";
        if (info.numSuccessors != 1)
            err << 's';
        err << "\n";
        st.hasError = true;
        ok = false;
    }
    else if (!in.brArgs.empty() && in.brArgs.size() != info.numSuccessors)
    {
        err << "line " << st.lineNo << ": " << info.name << " expects "
            << static_cast<unsigned>(info.numSuccessors) << " branch argument list";
        if (info.numSuccessors != 1)
            err << 's';
        err << ", or none" << "\n";
        st.hasError = true;
        ok = false;
    }

    return ok;
}

InstrHandler makeBinaryHandler(Opcode op, Type ty)
{
    return [op, ty](const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
    {
        std::istringstream ss(rest);
        std::string a = readToken(ss);
        std::string b = readToken(ss);
        in.op = op;
        if (!a.empty())
            in.operands.push_back(parseValue(a, st, err));
        if (!b.empty())
            in.operands.push_back(parseValue(b, st, err));
        in.type = ty;
        return true;
    };
}

InstrHandler makeUnaryHandler(Opcode op, Type ty)
{
    return [op, ty](const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
    {
        std::istringstream ss(rest);
        std::string a = readToken(ss);
        in.op = op;
        if (!a.empty())
            in.operands.push_back(parseValue(a, st, err));
        in.type = ty;
        return true;
    };
}

InstrHandler makeCmpHandler(Opcode op)
{
    return makeBinaryHandler(op, Type(Type::Kind::I1));
}

bool parseAllocaInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string sz = readToken(ss);
    in.op = Opcode::Alloca;
    if (sz.empty())
    {
        err << "line " << st.lineNo << ": missing size for alloca\n";
        st.hasError = true;
    }
    else
    {
        in.operands.push_back(parseValue(sz, st, err));
    }
    in.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseGEPInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string base = readToken(ss);
    std::string off = readToken(ss);
    in.op = Opcode::GEP;
    in.operands.push_back(parseValue(base, st, err));
    in.operands.push_back(parseValue(off, st, err));
    in.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseLoadInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string ty = readToken(ss);
    std::string ptr = readToken(ss);
    in.op = Opcode::Load;
    in.type = parseType(ty);
    in.operands.push_back(parseValue(ptr, st, err));
    return true;
}

bool parseStoreInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string ty = readToken(ss);
    std::string ptr = readToken(ss);
    std::string val = readToken(ss);
    in.op = Opcode::Store;
    in.type = parseType(ty);
    in.operands.push_back(parseValue(ptr, st, err));
    in.operands.push_back(parseValue(val, st, err));
    return true;
}

bool parseAddrOfInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string g = readToken(ss);
    in.op = Opcode::AddrOf;
    if (!g.empty())
        in.operands.push_back(parseValue(g, st, err));
    in.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseConstStrInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string g = readToken(ss);
    in.op = Opcode::ConstStr;
    if (!g.empty())
        in.operands.push_back(parseValue(g, st, err));
    in.type = Type(Type::Kind::Str);
    return true;
}

bool parseConstNullInstr(const std::string &rest, Instr &in, ParserState &, std::ostream &)
{
    (void)rest;
    in.op = Opcode::ConstNull;
    in.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseCallInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    in.op = Opcode::Call;
    size_t at = rest.find('@');
    size_t lp = rest.find('(', at);
    size_t rp = rest.find(')', lp);
    if (at == std::string::npos || lp == std::string::npos || rp == std::string::npos)
    {
        err << "line " << st.lineNo << ": malformed call\n";
        return false;
    }
    in.callee = rest.substr(at + 1, lp - at - 1);
    std::string args = rest.substr(lp + 1, rp - lp - 1);
    std::stringstream as(args);
    std::string a;
    while (std::getline(as, a, ','))
    {
        a = trim(a);
        if (!a.empty())
            in.operands.push_back(parseValue(a, st, err));
    }
    in.type = Type(Type::Kind::Void);
    return true;
}

bool parseBrInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    in.op = Opcode::Br;
    std::string t = rest;
    if (t.rfind("label ", 0) == 0)
        t = trim(t.substr(6));
    size_t lp = t.find('(');
    std::vector<Value> args;
    std::string lbl;
    if (lp == std::string::npos)
    {
        lbl = t;
    }
    else
    {
        size_t rp = t.find(')', lp);
        if (rp == std::string::npos)
        {
            err << "line " << st.lineNo << ": mismatched ')\n";
            return false;
        }
        lbl = trim(t.substr(0, lp));
        std::string argsStr = t.substr(lp + 1, rp - lp - 1);
        std::stringstream as(argsStr);
        std::string a;
        while (std::getline(as, a, ','))
        {
            a = trim(a);
            if (!a.empty())
                args.push_back(parseValue(a, st, err));
        }
    }
    in.labels.push_back(lbl);
    in.brArgs.push_back(args);
    size_t argCount = args.size();
    auto it = st.blockParamCount.find(lbl);
    if (it != st.blockParamCount.end())
    {
        if (it->second != argCount)
        {
            err << "line " << st.lineNo << ": bad arg count\n";
            return false;
        }
    }
    else
        st.pendingBrs.push_back({lbl, argCount, st.lineNo});
    in.type = Type(Type::Kind::Void);
    return true;
}

bool parseCBrInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    in.op = Opcode::CBr;
    std::istringstream ss(rest);
    std::string c = readToken(ss);
    std::string rem;
    std::getline(ss, rem);
    rem = trim(rem);
    size_t comma = rem.find(',');
    if (comma == std::string::npos)
    {
        err << "line " << st.lineNo << ": malformed cbr\n";
        return false;
    }
    std::string first = trim(rem.substr(0, comma));
    std::string second = trim(rem.substr(comma + 1));
    auto parseTarget =
        [&](const std::string &part, std::string &lbl, std::vector<Value> &args) -> bool
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
                    args.push_back(parseValue(a, st, err));
            }
        }
        return true;
    };
    std::vector<Value> a1, a2;
    std::string l1, l2;
    if (!parseTarget(first, l1, a1) || !parseTarget(second, l2, a2))
    {
        err << "line " << st.lineNo << ": mismatched ')\n";
        return false;
    }
    in.operands.push_back(parseValue(c, st, err));
    in.labels.push_back(l1);
    in.labels.push_back(l2);
    in.brArgs.push_back(a1);
    in.brArgs.push_back(a2);
    size_t n1 = a1.size();
    auto it1 = st.blockParamCount.find(l1);
    if (it1 != st.blockParamCount.end())
    {
        if (it1->second != n1)
        {
            err << "line " << st.lineNo << ": bad arg count\n";
            return false;
        }
    }
    else
        st.pendingBrs.push_back({l1, n1, st.lineNo});
    size_t n2 = a2.size();
    auto it2 = st.blockParamCount.find(l2);
    if (it2 != st.blockParamCount.end())
    {
        if (it2->second != n2)
        {
            err << "line " << st.lineNo << ": bad arg count\n";
            return false;
        }
    }
    else
        st.pendingBrs.push_back({l2, n2, st.lineNo});
    in.type = Type(Type::Kind::Void);
    return true;
}

bool parseRetInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    in.op = Opcode::Ret;
    std::string v = trim(rest);
    if (!v.empty())
        in.operands.push_back(parseValue(v, st, err));
    in.type = Type(Type::Kind::Void);
    return true;
}

bool parseTrapInstr(const std::string &, Instr &in, ParserState &, std::ostream &)
{
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    return true;
}

const std::unordered_map<std::string, InstrHandler> kInstrHandlers = {
    {"add", makeBinaryHandler(Opcode::Add, Type(Type::Kind::I64))},
    {"sub", makeBinaryHandler(Opcode::Sub, Type(Type::Kind::I64))},
    {"mul", makeBinaryHandler(Opcode::Mul, Type(Type::Kind::I64))},
    {"sdiv", makeBinaryHandler(Opcode::SDiv, Type(Type::Kind::I64))},
    {"udiv", makeBinaryHandler(Opcode::UDiv, Type(Type::Kind::I64))},
    {"srem", makeBinaryHandler(Opcode::SRem, Type(Type::Kind::I64))},
    {"urem", makeBinaryHandler(Opcode::URem, Type(Type::Kind::I64))},
    {"and", makeBinaryHandler(Opcode::And, Type(Type::Kind::I64))},
    {"or", makeBinaryHandler(Opcode::Or, Type(Type::Kind::I64))},
    {"xor", makeBinaryHandler(Opcode::Xor, Type(Type::Kind::I64))},
    {"shl", makeBinaryHandler(Opcode::Shl, Type(Type::Kind::I64))},
    {"lshr", makeBinaryHandler(Opcode::LShr, Type(Type::Kind::I64))},
    {"ashr", makeBinaryHandler(Opcode::AShr, Type(Type::Kind::I64))},
    {"fadd", makeBinaryHandler(Opcode::FAdd, Type(Type::Kind::F64))},
    {"fsub", makeBinaryHandler(Opcode::FSub, Type(Type::Kind::F64))},
    {"fmul", makeBinaryHandler(Opcode::FMul, Type(Type::Kind::F64))},
    {"fdiv", makeBinaryHandler(Opcode::FDiv, Type(Type::Kind::F64))},
    {"icmp_eq", makeCmpHandler(Opcode::ICmpEq)},
    {"icmp_ne", makeCmpHandler(Opcode::ICmpNe)},
    {"scmp_lt", makeCmpHandler(Opcode::SCmpLT)},
    {"scmp_le", makeCmpHandler(Opcode::SCmpLE)},
    {"scmp_gt", makeCmpHandler(Opcode::SCmpGT)},
    {"scmp_ge", makeCmpHandler(Opcode::SCmpGE)},
    {"ucmp_lt", makeCmpHandler(Opcode::UCmpLT)},
    {"ucmp_le", makeCmpHandler(Opcode::UCmpLE)},
    {"ucmp_gt", makeCmpHandler(Opcode::UCmpGT)},
    {"ucmp_ge", makeCmpHandler(Opcode::UCmpGE)},
    {"fcmp_lt", makeCmpHandler(Opcode::FCmpLT)},
    {"fcmp_le", makeCmpHandler(Opcode::FCmpLE)},
    {"fcmp_gt", makeCmpHandler(Opcode::FCmpGT)},
    {"fcmp_ge", makeCmpHandler(Opcode::FCmpGE)},
    {"fcmp_eq", makeCmpHandler(Opcode::FCmpEQ)},
    {"fcmp_ne", makeCmpHandler(Opcode::FCmpNE)},
    {"sitofp", makeUnaryHandler(Opcode::Sitofp, Type(Type::Kind::F64))},
    {"fptosi", makeUnaryHandler(Opcode::Fptosi, Type(Type::Kind::I64))},
    {"zext1", makeUnaryHandler(Opcode::Zext1, Type(Type::Kind::I64))},
    {"trunc1", makeUnaryHandler(Opcode::Trunc1, Type(Type::Kind::I1))},
    {"alloca", parseAllocaInstr},
    {"gep", parseGEPInstr},
    {"load", parseLoadInstr},
    {"store", parseStoreInstr},
    {"addr_of", parseAddrOfInstr},
    {"const_str", parseConstStrInstr},
    {"const_null", parseConstNullInstr},
    {"call", parseCallInstr},
    {"br", parseBrInstr},
    {"cbr", parseCBrInstr},
    {"ret", parseRetInstr},
    {"trap", parseTrapInstr}};

} // namespace

bool parseInstruction(const std::string &line, ParserState &st, std::ostream &err)
{
    Instr in;
    in.loc = st.curLoc;
    std::string work = line;
    if (work[0] == '%')
    {
        size_t eq = work.find('=');
        if (eq == std::string::npos)
        {
            err << "line " << st.lineNo << ": missing '='\n";
            st.hasError = true;
            return false;
        }
        std::string res = trim(work.substr(1, eq - 1));
        auto [it, inserted] = st.tempIds.emplace(res, st.nextTemp);
        if (inserted)
        {
            if (st.curFn->valueNames.size() <= st.nextTemp)
                st.curFn->valueNames.resize(st.nextTemp + 1);
            st.curFn->valueNames[st.nextTemp] = res;
            st.nextTemp++;
        }
        in.result = it->second;
        work = trim(work.substr(eq + 1));
    }
    std::istringstream ss(work);
    std::string op;
    ss >> op;
    std::string rest;
    std::getline(ss, rest);
    rest = trim(rest);
    auto it = kInstrHandlers.find(op);
    if (it == kInstrHandlers.end())
    {
        err << "line " << st.lineNo << ": unknown opcode " << op << "\n";
        return false;
    }
    if (!it->second(rest, in, st, err))
        return false;
    if (!validateShape(in, st, err))
        return false;
    st.curBB->instructions.push_back(std::move(in));
    return true;
}

} // namespace il::io::detail
