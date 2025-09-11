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
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::io
{

namespace
{

/// @brief Strip leading and trailing whitespace from a string.
/// @param s Input string that may contain surrounding spaces.
/// @return Substring with external whitespace removed.
/// @note Used throughout parsing to normalize tokens before inspection.
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

/// @brief Parse a textual type specifier.
/// @param t Lowercase token such as "i64" or "ptr".
/// @param ok Optional flag set true on success and false on unknown types.
/// @return Matching Type or a default-constructed Type on failure.
/// @details On error, @p ok is set to false and callers can treat the returned
/// value as an invalid type indicator.
Type parseType(const std::string &t, bool *ok = nullptr)
{
    if (t == "i64")
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
    return Type(); // error indicator
}

/// @brief Fetch the next token from a stream.
/// @param ss Stream containing whitespace and comma separated tokens.
/// @return Token with any trailing comma removed.
/// @note Used for parsing instruction operand lists where commas delimit
/// individual operands.
std::string readToken(std::istringstream &ss)
{
    std::string t;
    ss >> t;
    if (!t.empty() && t.back() == ',')
        t.pop_back();
    return t;
}

struct ParserState
{
    Module &m;
    Function *curFn = nullptr;
    BasicBlock *curBB = nullptr;
    std::unordered_map<std::string, unsigned> tempIds;
    unsigned nextTemp = 0;
    unsigned lineNo = 0;
    il::support::SourceLoc curLoc{};
    std::unordered_map<std::string, size_t> blockParamCount;
    bool hasError = false;

    struct PendingBr
    {
        std::string label;
        size_t args;
        unsigned line;
    };

    std::vector<PendingBr> pendingBrs;

    explicit ParserState(Module &mod) : m(mod) {}
};

/// @brief Convert a token into a typed IL value.
/// @param tok Token representing a value (temporary, literal, etc.).
/// @param st Current parser state for symbol lookup and diagnostics.
/// @param err Stream receiving diagnostic messages on failure.
/// @return Parsed Value or a placeholder when parsing fails.
/// @details Supports temps prefixed with '%', globals with '@', string,
/// integer and floating literals. Invalid tokens set ParserState::hasError
/// and emit an error with the current line number.
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
    {
        try
        {
            size_t idx = 0;
            double val = std::stod(tok, &idx);
            if (idx != tok.size())
                throw std::invalid_argument("trailing characters");
            return Value::constFloat(val);
        }
        catch (const std::exception &)
        {
            st.hasError = true;
            err << "Line " << st.lineNo << ": invalid floating literal '" << tok << "'\n";
            return Value::constFloat(0.0);
        }
    }
    try
    {
        size_t idx = 0;
        long long val = std::stoll(tok, &idx);
        if (idx != tok.size())
            throw std::invalid_argument("trailing characters");
        return Value::constInt(val);
    }
    catch (const std::exception &)
    {
        st.hasError = true;
        err << "Line " << st.lineNo << ": invalid integer literal '" << tok << "'\n";
        return Value::constInt(0);
    }
}

using InstrHandler =
    std::function<bool(const std::string &, Instr &, ParserState &, std::ostream &)>;

/// @brief Build a handler for binary arithmetic or logical instructions.
/// @param op Opcode to assign to the instruction.
/// @param ty Result type produced by the operation.
/// @return Callable that parses two operand tokens and populates an Instr.
/// @note Operand parsing delegates to parseValue(), which reports errors.
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

/// @brief Build a handler for unary operations.
/// @param op Opcode to assign.
/// @param ty Result type for the instruction.
/// @return Callable that parses a single operand using parseValue().
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

/// @brief Build a comparison instruction handler.
/// @param op Comparison opcode.
/// @return Binary handler preconfigured to produce an i1 result.
InstrHandler makeCmpHandler(Opcode op)
{
    return makeBinaryHandler(op, Type(Type::Kind::I1));
}

bool parseAllocaInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseGEPInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseLoadInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseStoreInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseAddrOfInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseConstStrInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseConstNullInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseCallInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err);
bool parseBrInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err);
bool parseCBrInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err);
bool parseRetInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &);
bool parseTrapInstr(const std::string &rest, Instr &in, ParserState &, std::ostream &);

static const std::unordered_map<std::string, InstrHandler> kInstrHandlers = {
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

/// @brief Parse the "alloca" instruction.
/// @param rest Remaining text after the opcode, expecting an optional size operand.
/// @param in Instruction to populate.
/// @param st Parser state for diagnostics.
/// @param err Error stream updated on malformed size tokens.
/// @return True on successful parse.
bool parseAllocaInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    std::istringstream ss(rest);
    std::string sz = readToken(ss);
    in.op = Opcode::Alloca;
    if (!sz.empty())
        in.operands.push_back(parseValue(sz, st, err));
    in.type = Type(Type::Kind::Ptr);
    return true;
}

/// @brief Parse the "gep" instruction in the form "base, offset".
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

/// @brief Parse a "load" instruction of the form "<type> <ptr>".
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

/// @brief Parse a "store" instruction of the form "<type> <ptr> <val>".
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

/// @brief Parse the "addr_of" instruction expecting a global symbol.
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

/// @brief Parse the "const_str" instruction containing a quoted string literal.
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

/// @brief Parse the "const_null" instruction which yields a null pointer.
bool parseConstNullInstr(const std::string &rest, Instr &in, ParserState &, std::ostream &)
{
    (void)rest;
    in.op = Opcode::ConstNull;
    in.type = Type(Type::Kind::Ptr);
    return true;
}

/// @brief Parse a "call" instruction of the form "@name(arg1, arg2)".
/// @details Emits an error when the call syntax is malformed.
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

/// @brief Parse an unconditional branch instruction.
/// @details Accepts either "label" or "label(arg1, arg2)" forms and validates
/// argument counts against the target block's parameters.
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

/// @brief Parse a conditional branch of the form
/// "<cond>, label %a(...), label %b(...)".
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

/// @brief Parse a "ret" instruction with an optional return value.
bool parseRetInstr(const std::string &rest, Instr &in, ParserState &st, std::ostream &err)
{
    in.op = Opcode::Ret;
    std::string v = trim(rest);
    if (!v.empty())
        in.operands.push_back(parseValue(v, st, err));
    in.type = Type(Type::Kind::Void);
    return true;
}

/// @brief Parse the "trap" instruction which halts execution.
bool parseTrapInstr(const std::string &, Instr &in, ParserState &, std::ostream &)
{
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    return true;
}

/// @brief Parse a single instruction line after removing result assignment.
/// @param line Full instruction text.
/// @param st Current parser state for context and diagnostics.
/// @param err Stream receiving error messages when opcode handlers fail.
/// @return True if parsing succeeded and the instruction was appended.
bool parseInstruction(const std::string &line, ParserState &st, std::ostream &err)
{
    Instr in;
    in.loc = st.curLoc;
    std::string work = line;
    if (work[0] == '%')
    {
        size_t eq = work.find('=');
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
    st.curBB->instructions.push_back(std::move(in));
    return true;
}

/// @brief Parse a function's header and initialize parser state.
bool parseFunctionHeader(const std::string &header, ParserState &st, std::ostream &err)
{
    size_t at = header.find('@');
    size_t lp = header.find('(', at);
    size_t rp = header.find(')', lp);
    size_t arr = header.find("->", rp);
    size_t lb = header.find('{', arr);
    std::string name = header.substr(at + 1, lp - at - 1);
    std::string paramsStr = header.substr(lp + 1, rp - lp - 1);
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
    std::string retStr = trim(header.substr(arr + 2, lb - arr - 2));
    st.tempIds.clear();
    unsigned idx = 0;
    for (auto &param : params)
    {
        param.id = idx;
        st.tempIds[param.name] = idx;
        ++idx;
    }
    st.m.functions.push_back({name, parseType(retStr), params, {}, {}});
    st.curFn = &st.m.functions.back();
    st.curBB = nullptr;
    st.nextTemp = idx;
    st.curFn->valueNames.resize(st.nextTemp);
    for (auto &param : params)
        st.curFn->valueNames[param.id] = param.name;
    st.blockParamCount.clear();
    st.pendingBrs.clear();
    (void)err; // err currently unused
    return true;
}

/// @brief Parse a block label and parameters, creating the basic block.
bool parseBlockHeader(const std::string &header, ParserState &st, std::ostream &err)
{
    size_t lp = header.find('(');
    std::vector<Param> bparams;
    std::string label = trim(header);
    if (lp != std::string::npos)
    {
        size_t rp = header.find(')', lp);
        if (rp == std::string::npos)
        {
            err << "line " << st.lineNo << ": mismatched ')\n";
            return false;
        }
        label = trim(header.substr(0, lp));
        std::string paramsStr = header.substr(lp + 1, rp - lp - 1);
        std::stringstream pss(paramsStr);
        std::string q;
        while (std::getline(pss, q, ','))
        {
            q = trim(q);
            if (q.empty())
                continue;
            size_t col = q.find(':');
            if (col == std::string::npos)
            {
                err << "line " << st.lineNo << ": bad param\n";
                return false;
            }
            std::string nm = trim(q.substr(0, col));
            if (!nm.empty() && nm[0] == '%')
                nm = nm.substr(1);
            std::string tyStr = trim(q.substr(col + 1));
            bool ok = true;
            Type ty = parseType(tyStr, &ok);
            if (!ok || ty.kind == Type::Kind::Void)
            {
                err << "line " << st.lineNo << ": unknown param type\n";
                return false;
            }
            bparams.push_back({nm, ty, st.nextTemp});
            st.tempIds[nm] = st.nextTemp;
            if (st.curFn->valueNames.size() <= st.nextTemp)
                st.curFn->valueNames.resize(st.nextTemp + 1);
            st.curFn->valueNames[st.nextTemp] = nm;
            ++st.nextTemp;
        }
    }
    st.curFn->blocks.push_back({label, bparams, {}, false});
    st.curBB = &st.curFn->blocks.back();
    st.blockParamCount[label] = bparams.size();
    for (auto it = st.pendingBrs.begin(); it != st.pendingBrs.end();)
    {
        if (it->label == label)
        {
            if (it->args != bparams.size())
            {
                err << "line " << it->line << ": bad arg count\n";
                return false;
            }
            it = st.pendingBrs.erase(it);
        }
        else
            ++it;
    }
    return true;
}

/// @brief Parse a function body after its header has been read.
/// @param is Input stream positioned after the function header.
/// @param header Previously read function header line.
/// @param st Parser state to populate.
/// @param err Diagnostic output stream.
/// @return True when the function and all contained blocks are parsed.
bool parseFunction(std::istream &is, std::string &header, ParserState &st, std::ostream &err)
{
    if (!parseFunctionHeader(header, st, err))
        return false;

    std::string line;
    while (std::getline(is, line))
    {
        ++st.lineNo;
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (line[0] == '}')
        {
            st.curFn = nullptr;
            st.curBB = nullptr;
            return true;
        }
        if (line.back() == ':')
        {
            if (!parseBlockHeader(line.substr(0, line.size() - 1), st, err))
                return false;
            continue;
        }
        if (!st.curBB)
        {
            err << "line " << st.lineNo << ": instruction outside block\n";
            return false;
        }
        if (line.rfind(".loc", 0) == 0)
        {
            std::istringstream ls(line.substr(4));
            uint32_t fid = 0, ln = 0, col = 0;
            ls >> fid >> ln >> col;
            st.curLoc = {fid, ln, col};
            continue;
        }
        if (!parseInstruction(line, st, err))
            return false;
    }
    return true;
}

/// @brief Parse top-level module declarations (il, extern, global, func).
/// @param is Input stream for additional lines when encountering a function.
/// @param line Current line to interpret.
/// @param st Parser state updated with module metadata.
/// @param err Output stream for error messages.
/// @return True if the line was successfully processed.
bool parseModuleHeader(std::istream &is, std::string &line, ParserState &st, std::ostream &err)
{
    if (line.rfind("il", 0) == 0)
    {
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        std::string ver;
        if (ls >> ver)
            st.m.version = ver;
        else
            st.m.version = "0.1.2";
        return true;
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
        st.m.externs.push_back({name, parseType(retStr), params});
        return true;
    }
    if (line.rfind("global", 0) == 0)
    {
        size_t at = line.find('@');
        size_t eq = line.find('=', at);
        size_t q1 = line.find('"', eq);
        size_t q2 = line.rfind('"');
        std::string name = trim(line.substr(at + 1, eq - at - 1));
        std::string init = line.substr(q1 + 1, q2 - q1 - 1);
        st.m.globals.push_back({name, Type(Type::Kind::Str), init});
        return true;
    }
    if (line.rfind("func", 0) == 0)
        return parseFunction(is, line, st, err);
    err << "line " << st.lineNo << ": unexpected line: " << line << "\n";
    return false;
}

} // namespace

/// @brief Entry point for parsing a textual IL module.
/// @param is Input stream containing IL text.
/// @param m Module to populate.
/// @param err Stream for error diagnostics.
/// @return True if no fatal parsing errors were encountered.
bool Parser::parse(std::istream &is, Module &m, std::ostream &err)
{
    ParserState st{m};
    std::string line;
    while (std::getline(is, line))
    {
        ++st.lineNo;
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (!parseModuleHeader(is, line, st, err))
            return false;
    }
    return !st.hasError;
}

} // namespace il::io
