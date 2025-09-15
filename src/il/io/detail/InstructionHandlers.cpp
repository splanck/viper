// File: src/il/io/detail/InstructionHandlers.cpp
// Purpose: Implements opcode-specific parsing helpers for the IL parser.
// Key invariants: Maintains parser state consistency when constructing instructions.
// Ownership/Lifetime: Functions mutate caller-provided ParserState and instruction objects.
// Links: docs/il-spec.md

#include "il/io/detail/InstructionHandlers.hpp"

#include "il/io/Lexer.hpp"
#include <cctype>
#include <exception>
#include <sstream>

using il::core::Instr;
using il::core::Opcode;
using il::core::Type;
using il::core::Value;

namespace il::io::detail
{

Type parseType(const std::string &token, bool *ok)
{
    if (token == "i64")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::I64);
    }
    if (token == "i1")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::I1);
    }
    if (token == "f64")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::F64);
    }
    if (token == "ptr")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::Ptr);
    }
    if (token == "str")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::Str);
    }
    if (token == "void")
    {
        if (ok)
            *ok = true;
        return Type(Type::Kind::Void);
    }
    if (ok)
        *ok = false;
    return Type();
}

Value parseValue(const std::string &token, ParserState &state, std::ostream &err)
{
    if (token.empty())
        return Value::constInt(0);
    if (token[0] == '%')
    {
        std::string name = token.substr(1);
        auto it = state.tempIds.find(name);
        if (it != state.tempIds.end())
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
                    state.hasError = true;
                    err << "Line " << state.lineNo << ": invalid temp id '" << token << "'\n";
                    return Value::temp(0);
                }
            }
        }
        return Value::temp(0);
    }
    if (token[0] == '@')
        return Value::global(token.substr(1));
    if (token == "null")
        return Value::null();
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        return Value::constStr(token.substr(1, token.size() - 2));
    if (token.find('.') != std::string::npos || token.find('e') != std::string::npos ||
        token.find('E') != std::string::npos)
    {
        try
        {
            size_t idx = 0;
            double val = std::stod(token, &idx);
            if (idx != token.size())
                throw std::invalid_argument("trailing characters");
            return Value::constFloat(val);
        }
        catch (const std::exception &)
        {
            state.hasError = true;
            err << "Line " << state.lineNo << ": invalid floating literal '" << token << "'\n";
            return Value::constFloat(0.0);
        }
    }
    try
    {
        size_t idx = 0;
        long long val = std::stoll(token, &idx);
        if (idx != token.size())
            throw std::invalid_argument("trailing characters");
        return Value::constInt(val);
    }
    catch (const std::exception &)
    {
        state.hasError = true;
        err << "Line " << state.lineNo << ": invalid integer literal '" << token << "'\n";
        return Value::constInt(0);
    }
}

namespace
{

InstrHandler makeBinaryHandler(Opcode op, Type type)
{
    return [op, type](const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
    {
        std::istringstream ss(rest);
        std::string a = Lexer::nextToken(ss);
        std::string b = Lexer::nextToken(ss);
        instr.op = op;
        if (!a.empty())
            instr.operands.push_back(parseValue(a, state, err));
        if (!b.empty())
            instr.operands.push_back(parseValue(b, state, err));
        instr.type = type;
        return true;
    };
}

InstrHandler makeUnaryHandler(Opcode op, Type type)
{
    return [op, type](const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
    {
        std::istringstream ss(rest);
        std::string a = Lexer::nextToken(ss);
        instr.op = op;
        if (!a.empty())
            instr.operands.push_back(parseValue(a, state, err));
        instr.type = type;
        return true;
    };
}

InstrHandler makeCmpHandler(Opcode op)
{
    return makeBinaryHandler(op, Type(Type::Kind::I1));
}

bool parseAllocaInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string sizeTok = Lexer::nextToken(ss);
    instr.op = Opcode::Alloca;
    if (sizeTok.empty())
    {
        err << "line " << state.lineNo << ": missing size for alloca\n";
        state.hasError = true;
    }
    else
    {
        instr.operands.push_back(parseValue(sizeTok, state, err));
    }
    instr.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseGEPInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string base = Lexer::nextToken(ss);
    std::string offset = Lexer::nextToken(ss);
    instr.op = Opcode::GEP;
    instr.operands.push_back(parseValue(base, state, err));
    instr.operands.push_back(parseValue(offset, state, err));
    instr.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseLoadInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string typeTok = Lexer::nextToken(ss);
    std::string ptr = Lexer::nextToken(ss);
    instr.op = Opcode::Load;
    instr.type = parseType(typeTok);
    instr.operands.push_back(parseValue(ptr, state, err));
    return true;
}

bool parseStoreInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string typeTok = Lexer::nextToken(ss);
    std::string ptr = Lexer::nextToken(ss);
    std::string val = Lexer::nextToken(ss);
    instr.op = Opcode::Store;
    instr.type = parseType(typeTok);
    instr.operands.push_back(parseValue(ptr, state, err));
    instr.operands.push_back(parseValue(val, state, err));
    return true;
}

bool parseAddrOfInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string global = Lexer::nextToken(ss);
    instr.op = Opcode::AddrOf;
    if (!global.empty())
        instr.operands.push_back(parseValue(global, state, err));
    instr.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseConstStrInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string literal = Lexer::nextToken(ss);
    instr.op = Opcode::ConstStr;
    if (!literal.empty())
        instr.operands.push_back(parseValue(literal, state, err));
    instr.type = Type(Type::Kind::Str);
    return true;
}

bool parseConstNullInstr(const std::string &rest, Instr &instr, ParserState &, std::ostream &)
{
    (void)rest;
    instr.op = Opcode::ConstNull;
    instr.type = Type(Type::Kind::Ptr);
    return true;
}

bool parseCallInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    instr.op = Opcode::Call;
    size_t at = rest.find('@');
    size_t lp = rest.find('(', at);
    size_t rp = rest.find(')', lp);
    if (at == std::string::npos || lp == std::string::npos || rp == std::string::npos)
    {
        err << "line " << state.lineNo << ": malformed call\n";
        return false;
    }
    instr.callee = rest.substr(at + 1, lp - at - 1);
    std::string args = rest.substr(lp + 1, rp - lp - 1);
    for (const auto &arg : Lexer::splitCommaSeparated(args))
    {
        if (!arg.empty())
            instr.operands.push_back(parseValue(arg, state, err));
    }
    instr.type = Type(Type::Kind::Void);
    return true;
}

bool parseBrInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    instr.op = Opcode::Br;
    std::string targetText = rest;
    if (targetText.rfind("label ", 0) == 0)
        targetText = Lexer::trim(targetText.substr(6));
    size_t lp = targetText.find('(');
    std::vector<Value> args;
    std::string label;
    if (lp == std::string::npos)
    {
        label = targetText;
    }
    else
    {
        size_t rp = targetText.find(')', lp);
        if (rp == std::string::npos)
        {
            err << "line " << state.lineNo << ": mismatched ')\n";
            return false;
        }
        label = Lexer::trim(targetText.substr(0, lp));
        std::string argsStr = targetText.substr(lp + 1, rp - lp - 1);
        for (const auto &arg : Lexer::splitCommaSeparated(argsStr))
        {
            if (!arg.empty())
                args.push_back(parseValue(arg, state, err));
        }
    }
    instr.labels.push_back(label);
    instr.brArgs.push_back(args);
    size_t argCount = args.size();
    auto it = state.blockParamCount.find(label);
    if (it != state.blockParamCount.end())
    {
        if (it->second != argCount)
        {
            err << "line " << state.lineNo << ": bad arg count\n";
            return false;
        }
    }
    else
        state.pendingBrs.push_back({label, argCount, state.lineNo});
    instr.type = Type(Type::Kind::Void);
    return true;
}

bool parseCBrInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    instr.op = Opcode::CBr;
    std::istringstream ss(rest);
    std::string condition = Lexer::nextToken(ss);
    std::string remainder;
    std::getline(ss, remainder);
    remainder = Lexer::trim(remainder);
    size_t comma = remainder.find(',');
    if (comma == std::string::npos)
    {
        err << "line " << state.lineNo << ": malformed cbr\n";
        return false;
    }
    std::string first = Lexer::trim(remainder.substr(0, comma));
    std::string second = Lexer::trim(remainder.substr(comma + 1));
    auto parseTarget = [&](const std::string &part, std::string &label, std::vector<Value> &args) -> bool
    {
        std::string text = part;
        if (text.rfind("label ", 0) == 0)
            text = Lexer::trim(text.substr(6));
        size_t lp = text.find('(');
        if (lp == std::string::npos)
        {
            label = Lexer::trim(text);
        }
        else
        {
            size_t rp = text.find(')', lp);
            if (rp == std::string::npos)
                return false;
            label = Lexer::trim(text.substr(0, lp));
            std::string argsStr = text.substr(lp + 1, rp - lp - 1);
            for (const auto &arg : Lexer::splitCommaSeparated(argsStr))
            {
                if (!arg.empty())
                    args.push_back(parseValue(arg, state, err));
            }
        }
        return true;
    };
    std::vector<Value> args1, args2;
    std::string label1, label2;
    if (!parseTarget(first, label1, args1) || !parseTarget(second, label2, args2))
    {
        err << "line " << state.lineNo << ": mismatched ')\n";
        return false;
    }
    instr.operands.push_back(parseValue(condition, state, err));
    instr.labels.push_back(label1);
    instr.labels.push_back(label2);
    instr.brArgs.push_back(args1);
    instr.brArgs.push_back(args2);
    size_t n1 = args1.size();
    auto it1 = state.blockParamCount.find(label1);
    if (it1 != state.blockParamCount.end())
    {
        if (it1->second != n1)
        {
            err << "line " << state.lineNo << ": bad arg count\n";
            return false;
        }
    }
    else
        state.pendingBrs.push_back({label1, n1, state.lineNo});
    size_t n2 = args2.size();
    auto it2 = state.blockParamCount.find(label2);
    if (it2 != state.blockParamCount.end())
    {
        if (it2->second != n2)
        {
            err << "line " << state.lineNo << ": bad arg count\n";
            return false;
        }
    }
    else
        state.pendingBrs.push_back({label2, n2, state.lineNo});
    instr.type = Type(Type::Kind::Void);
    return true;
}

bool parseRetInstr(const std::string &rest, Instr &instr, ParserState &state, std::ostream &err)
{
    instr.op = Opcode::Ret;
    std::string value = Lexer::trim(rest);
    if (!value.empty())
        instr.operands.push_back(parseValue(value, state, err));
    instr.type = Type(Type::Kind::Void);
    return true;
}

bool parseTrapInstr(const std::string &, Instr &instr, ParserState &, std::ostream &)
{
    instr.op = Opcode::Trap;
    instr.type = Type(Type::Kind::Void);
    return true;
}

} // namespace

const std::unordered_map<std::string, InstrHandler> &instructionHandlers()
{
    static const std::unordered_map<std::string, InstrHandler> handlers = {
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
        {"trap", parseTrapInstr},
    };
    return handlers;
}

} // namespace il::io::detail
