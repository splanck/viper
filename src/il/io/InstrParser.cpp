// File: src/il/io/InstrParser.cpp
// Purpose: Implements parsing of IL instruction statements (MIT License; see LICENSE).
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
#include "support/diag_expected.hpp"

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
using il::core::ResultArity;
using il::core::Type;
using il::core::Value;
using il::support::Expected;
using il::support::makeError;
using il::support::printDiag;

/// @brief Parses a textual operand token into an IL value.
///
/// Supports temporaries, globals, string constants, and immediates. An empty
/// token yields a zero integer, matching textual defaults for optional
/// operands.
///
/// @param tok Token extracted from the instruction body.
/// @param st Parser state providing temporary mappings and diagnostic context.
/// @return Parsed value or an error anchored to @p st.curLoc.
Expected<Value> parseValue_E(const std::string &tok, ParserState &st)
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
            {
                if (!std::isdigit(static_cast<unsigned char>(name[i])))
                {
                    digits = false;
                    break;
                }
            }
            if (digits)
            {
                try
                {
                    return Value::temp(static_cast<unsigned>(std::stoul(name.substr(1))));
                }
                catch (const std::exception &)
                {
                    std::ostringstream oss;
                    oss << "Line " << st.lineNo << ": invalid temp id '" << tok << "'";
                    return Expected<Value>{makeError(st.curLoc, oss.str())};
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
        std::ostringstream oss;
        oss << "Line " << st.lineNo << ": invalid floating literal '" << tok << "'";
        return Expected<Value>{makeError(st.curLoc, oss.str())};
    }
    long long intValue = 0;
    if (parseIntegerLiteral(tok, intValue))
        return Value::constInt(intValue);
    std::ostringstream oss;
    oss << "Line " << st.lineNo << ": invalid integer literal '" << tok << "'";
    return Expected<Value>{makeError(st.curLoc, oss.str())};
}

/// @brief Parses a textual type token and validates it against the IL type set.
///
/// @param tok Canonical type spelling, as emitted by the serializer.
/// @param st Parser state used for diagnostic line numbering.
/// @return Parsed type or an error describing the unknown spelling.
Expected<Type> parseType_E(const std::string &tok, ParserState &st)
{
    bool ok = false;
    Type ty = parseType(tok, &ok);
    if (!ok)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": unknown type '" << tok << "'";
        return Expected<Type>{makeError(st.curLoc, oss.str())};
    }
    return ty;
}

/// @brief Function object signature for per-opcode instruction parsers.
using InstrHandler = std::function<Expected<void>(const std::string &, Instr &, ParserState &)>;

/// @brief Ensures an instruction matches the arity described by its opcode.
///
/// Checks operand counts, result presence, successor labels, and branch argument
/// lists against the static opcode metadata. Diagnostics cite the parser state
/// line number and instruction source location.
///
/// @param in Fully populated instruction candidate.
/// @param st Parser state providing location and line information.
/// @return Empty on success; otherwise, a structured diagnostic.
Expected<void> validateShape_E(const Instr &in, ParserState &st)
{
    const auto &info = il::core::getOpcodeInfo(in.op);
    const size_t operandCount = in.operands.size();
    const bool variadic = il::core::isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadic && operandCount > info.numOperandsMax))
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": " << info.name << " expects ";
        if (info.numOperandsMin == info.numOperandsMax)
        {
            oss << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                oss << 's';
        }
        else if (variadic)
        {
            oss << "at least " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                oss << 's';
        }
        else
        {
            oss << "between " << static_cast<unsigned>(info.numOperandsMin) << " and "
                << static_cast<unsigned>(info.numOperandsMax) << " operands";
        }
        return Expected<void>{makeError(in.loc, oss.str())};
    }

    const bool hasResult = in.result.has_value();
    switch (info.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": " << info.name << " does not produce a result";
                return Expected<void>{makeError(in.loc, oss.str())};
            }
            break;
        case ResultArity::One:
            if (!hasResult)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": " << info.name << " requires a result";
                return Expected<void>{makeError(in.loc, oss.str())};
            }
            break;
        case ResultArity::Optional:
            break;
    }

    if (in.labels.size() != info.numSuccessors)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": " << info.name << " expects "
            << static_cast<unsigned>(info.numSuccessors) << " label";
        if (info.numSuccessors != 1)
            oss << 's';
        return Expected<void>{makeError(in.loc, oss.str())};
    }

    if (in.brArgs.size() > info.numSuccessors)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": " << info.name << " expects at most "
            << static_cast<unsigned>(info.numSuccessors) << " branch argument list";
        if (info.numSuccessors != 1)
            oss << 's';
        return Expected<void>{makeError(in.loc, oss.str())};
    }

    if (!in.brArgs.empty() && in.brArgs.size() != info.numSuccessors)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": " << info.name << " expects "
            << static_cast<unsigned>(info.numSuccessors) << " branch argument list";
        if (info.numSuccessors != 1)
            oss << 's';
        oss << ", or none";
        return Expected<void>{makeError(in.loc, oss.str())};
    }

    return {};
}

/// @brief Verifies branch argument counts match basic-block parameters.
///
/// Known blocks must agree with the provided arity; unknown blocks are queued in
/// ParserState::pendingBrs for later validation once seen.
///
/// @param in Instruction issuing the branch.
/// @param st Parser state tracking block signatures and pending branches.
/// @param label Target block label being referenced.
/// @param argCount Number of arguments supplied in the branch.
/// @return Empty on success; otherwise, a diagnostic referencing @p in.loc.
Expected<void> checkBlockArgCount(const Instr &in, ParserState &st, const std::string &label,
                                   size_t argCount)
{
    auto it = st.blockParamCount.find(label);
    if (it != st.blockParamCount.end())
    {
        if (it->second != argCount)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": bad arg count";
            return Expected<void>{makeError(in.loc, oss.str())};
        }
    }
    else
    {
        st.pendingBrs.push_back({label, argCount, st.lineNo});
    }
    return {};
}

/// @brief Builds a parser for two-operand instructions with a fixed result type.
///
/// @param op Opcode applied to the parsed operands.
/// @param ty Result type recorded on the instruction.
/// @return Handler that parses two value tokens and attaches them to the
/// instruction.
InstrHandler makeBinaryHandler(Opcode op, Type ty)
{
    return [op, ty](const std::string &rest, Instr &in, ParserState &st) -> Expected<void>
    {
        std::istringstream ss(rest);
        std::string a = readToken(ss);
        std::string b = readToken(ss);
        in.op = op;
        if (!a.empty())
        {
            auto lhs = parseValue_E(a, st);
            if (!lhs)
                return Expected<void>{lhs.error()};
            in.operands.push_back(std::move(lhs.value()));
        }
        if (!b.empty())
        {
            auto rhs = parseValue_E(b, st);
            if (!rhs)
                return Expected<void>{rhs.error()};
            in.operands.push_back(std::move(rhs.value()));
        }
        in.type = ty;
        return {};
    };
}

/// @brief Builds a parser for single-operand instructions with a fixed result type.
///
/// @param op Opcode applied to the parsed operand.
/// @param ty Result type recorded on the instruction.
/// @return Handler that parses one value token and attaches it to the instruction.
InstrHandler makeUnaryHandler(Opcode op, Type ty)
{
    return [op, ty](const std::string &rest, Instr &in, ParserState &st) -> Expected<void>
    {
        std::istringstream ss(rest);
        std::string a = readToken(ss);
        in.op = op;
        if (!a.empty())
        {
            auto operand = parseValue_E(a, st);
            if (!operand)
                return Expected<void>{operand.error()};
            in.operands.push_back(std::move(operand.value()));
        }
        in.type = ty;
        return {};
    };
}

/// @brief Convenience helper for building boolean comparison handlers.
///
/// @param op Comparison opcode requiring two operands and returning i1.
/// @return Binary handler specialised with an i1 result type.
InstrHandler makeCmpHandler(Opcode op)
{
    return makeBinaryHandler(op, Type(Type::Kind::I1));
}

/// @brief Parses an `alloca` instruction body.
///
/// @param rest Remaining text after the opcode keyword.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic when the size is missing or
/// invalid.
Expected<void> parseAllocaInstr(const std::string &rest, Instr &in, ParserState &st)
{
    std::istringstream ss(rest);
    std::string sz = readToken(ss);
    in.op = Opcode::Alloca;
    if (sz.empty())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": missing size for alloca";
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    auto sizeValue = parseValue_E(sz, st);
    if (!sizeValue)
        return Expected<void>{sizeValue.error()};
    in.operands.push_back(std::move(sizeValue.value()));
    in.type = Type(Type::Kind::Ptr);
    return {};
}

/// @brief Parses a `gep` instruction body.
///
/// @param rest Remaining text after the opcode keyword.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed operands.
Expected<void> parseGEPInstr(const std::string &rest, Instr &in, ParserState &st)
{
    std::istringstream ss(rest);
    std::string base = readToken(ss);
    std::string off = readToken(ss);
    in.op = Opcode::GEP;
    if (!base.empty())
    {
        auto baseVal = parseValue_E(base, st);
        if (!baseVal)
            return Expected<void>{baseVal.error()};
        in.operands.push_back(std::move(baseVal.value()));
    }
    if (!off.empty())
    {
        auto offVal = parseValue_E(off, st);
        if (!offVal)
            return Expected<void>{offVal.error()};
        in.operands.push_back(std::move(offVal.value()));
    }
    in.type = Type(Type::Kind::Ptr);
    return {};
}

/// @brief Parses a `load` instruction body.
///
/// @param rest Remaining text after the opcode keyword, starting with the
/// result type.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for unknown types or
/// malformed operands.
Expected<void> parseLoadInstr(const std::string &rest, Instr &in, ParserState &st)
{
    std::istringstream ss(rest);
    std::string ty = readToken(ss);
    std::string ptr = readToken(ss);
    in.op = Opcode::Load;
    auto parsedType = parseType_E(ty, st);
    if (!parsedType)
        return Expected<void>{parsedType.error()};
    in.type = parsedType.value();
    if (!ptr.empty())
    {
        auto ptrVal = parseValue_E(ptr, st);
        if (!ptrVal)
            return Expected<void>{ptrVal.error()};
        in.operands.push_back(std::move(ptrVal.value()));
    }
    return {};
}

/// @brief Parses a `store` instruction body.
///
/// @param rest Remaining text after the opcode keyword, starting with the
/// value type.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed operands.
Expected<void> parseStoreInstr(const std::string &rest, Instr &in, ParserState &st)
{
    std::istringstream ss(rest);
    std::string ty = readToken(ss);
    std::string ptr = readToken(ss);
    std::string val = readToken(ss);
    in.op = Opcode::Store;
    auto parsedType = parseType_E(ty, st);
    if (!parsedType)
        return Expected<void>{parsedType.error()};
    in.type = parsedType.value();
    if (!ptr.empty())
    {
        auto ptrVal = parseValue_E(ptr, st);
        if (!ptrVal)
            return Expected<void>{ptrVal.error()};
        in.operands.push_back(std::move(ptrVal.value()));
    }
    if (!val.empty())
    {
        auto valueVal = parseValue_E(val, st);
        if (!valueVal)
            return Expected<void>{valueVal.error()};
        in.operands.push_back(std::move(valueVal.value()));
    }
    return {};
}

/// @brief Parses an `addr_of` instruction body.
///
/// @param rest Remaining text after the opcode keyword.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed operands.
Expected<void> parseAddrOfInstr(const std::string &rest, Instr &in, ParserState &st)
{
    std::istringstream ss(rest);
    std::string g = readToken(ss);
    in.op = Opcode::AddrOf;
    if (!g.empty())
    {
        auto globalVal = parseValue_E(g, st);
        if (!globalVal)
            return Expected<void>{globalVal.error()};
        in.operands.push_back(std::move(globalVal.value()));
    }
    in.type = Type(Type::Kind::Ptr);
    return {};
}

/// @brief Parses a `const_str` instruction body.
///
/// @param rest Remaining text after the opcode keyword.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed operands.
Expected<void> parseConstStrInstr(const std::string &rest, Instr &in, ParserState &st)
{
    std::istringstream ss(rest);
    std::string g = readToken(ss);
    in.op = Opcode::ConstStr;
    if (!g.empty())
    {
        auto strVal = parseValue_E(g, st);
        if (!strVal)
            return Expected<void>{strVal.error()};
        in.operands.push_back(std::move(strVal.value()));
    }
    in.type = Type(Type::Kind::Str);
    return {};
}

/// @brief Parses a `const_null` instruction body.
///
/// This instruction has no operands; the parser simply stamps the opcode and
/// result type.
///
/// @param rest Remaining text after the opcode keyword (unused).
/// @param in Instruction object to populate.
/// @param st Parser state (unused).
/// @return Always succeeds.
Expected<void> parseConstNullInstr([[maybe_unused]] const std::string &rest, Instr &in,
                                   [[maybe_unused]] ParserState &st)
{
    in.op = Opcode::ConstNull;
    in.type = Type(Type::Kind::Ptr);
    return {};
}

/// @brief Parses a `call` instruction body.
///
/// @param rest Remaining text after the opcode keyword, including callee name
/// and parenthesised operands.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed call syntax
/// or operands.
Expected<void> parseCallInstr(const std::string &rest, Instr &in, ParserState &st)
{
    in.op = Opcode::Call;
    size_t at = rest.find('@');
    size_t lp = rest.find('(', at);
    size_t rp = rest.find(')', lp);
    if (at == std::string::npos || lp == std::string::npos || rp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": malformed call";
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    in.callee = rest.substr(at + 1, lp - at - 1);
    std::string args = rest.substr(lp + 1, rp - lp - 1);
    std::stringstream as(args);
    std::string a;
    while (std::getline(as, a, ','))
    {
        a = trim(a);
        if (a.empty())
            continue;
        auto argVal = parseValue_E(a, st);
        if (!argVal)
            return Expected<void>{argVal.error()};
        in.operands.push_back(std::move(argVal.value()));
    }
    in.type = Type(Type::Kind::Void);
    return {};
}

/// @brief Parses an unconditional `br` instruction body.
///
/// @param rest Remaining text after the opcode keyword, optionally including a
/// branch argument tuple.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed labels or
/// operands.
Expected<void> parseBrInstr(const std::string &rest, Instr &in, ParserState &st)
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
        lbl = trim(t);
    }
    else
    {
        size_t rp = t.find(')', lp);
        if (rp == std::string::npos)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": mismatched ')";
            return Expected<void>{makeError(in.loc, oss.str())};
        }
        lbl = trim(t.substr(0, lp));
        std::string argsStr = t.substr(lp + 1, rp - lp - 1);
        std::stringstream as(argsStr);
        std::string a;
        while (std::getline(as, a, ','))
        {
            a = trim(a);
            if (a.empty())
                continue;
            auto argVal = parseValue_E(a, st);
            if (!argVal)
                return Expected<void>{argVal.error()};
            args.push_back(std::move(argVal.value()));
        }
    }
    in.labels.push_back(lbl);
    in.brArgs.push_back(args);
    auto countCheck = checkBlockArgCount(in, st, lbl, args.size());
    if (!countCheck)
        return countCheck;
    in.type = Type(Type::Kind::Void);
    return {};
}

/// @brief Parses one branch target (label plus optional arguments).
///
/// @param part Textual fragment describing the target, e.g. `label %bb(args)`.
/// @param in Instruction issuing the branch, used for diagnostics.
/// @param st Parser state for operand parsing and diagnostics.
/// @param lbl Output parameter receiving the parsed label.
/// @param args Output vector receiving parsed argument values.
/// @return Empty on success; otherwise, a diagnostic for malformed syntax or
/// operands.
Expected<void> parseBranchTarget(const std::string &part, Instr &in, ParserState &st, std::string &lbl,
                                 std::vector<Value> &args)
{
    std::string t = part;
    if (t.rfind("label ", 0) == 0)
        t = trim(t.substr(6));
    size_t lp = t.find('(');
    if (lp == std::string::npos)
    {
        lbl = trim(t);
        return {};
    }
    size_t rp = t.find(')', lp);
    if (rp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": mismatched ')";
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    lbl = trim(t.substr(0, lp));
    std::string argsStr = t.substr(lp + 1, rp - lp - 1);
    std::stringstream as(argsStr);
    std::string a;
    while (std::getline(as, a, ','))
    {
        a = trim(a);
        if (a.empty())
            continue;
        auto val = parseValue_E(a, st);
        if (!val)
            return Expected<void>{val.error()};
        args.push_back(std::move(val.value()));
    }
    return {};
}

/// @brief Parses a conditional `cbr` instruction body.
///
/// @param rest Remaining text after the opcode keyword, including condition and
/// two branch targets.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed syntax or
/// inconsistent branch arguments.
Expected<void> parseCBrInstr(const std::string &rest, Instr &in, ParserState &st)
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
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": malformed cbr";
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    std::string first = trim(rem.substr(0, comma));
    std::string second = trim(rem.substr(comma + 1));
    std::vector<Value> a1;
    std::vector<Value> a2;
    std::string l1;
    std::string l2;
    auto t1 = parseBranchTarget(first, in, st, l1, a1);
    if (!t1)
        return t1;
    auto t2 = parseBranchTarget(second, in, st, l2, a2);
    if (!t2)
        return t2;
    auto cond = parseValue_E(c, st);
    if (!cond)
        return Expected<void>{cond.error()};
    in.operands.push_back(std::move(cond.value()));
    in.labels.push_back(l1);
    in.labels.push_back(l2);
    in.brArgs.push_back(a1);
    in.brArgs.push_back(a2);
    auto check1 = checkBlockArgCount(in, st, l1, a1.size());
    if (!check1)
        return check1;
    auto check2 = checkBlockArgCount(in, st, l2, a2.size());
    if (!check2)
        return check2;
    in.type = Type(Type::Kind::Void);
    return {};
}

/// @brief Parses a `ret` instruction body.
///
/// @param rest Remaining text after the opcode keyword, optionally containing a
/// return value.
/// @param in Instruction object to populate.
/// @param st Parser state for operand parsing and diagnostics.
/// @return Empty on success; otherwise, a diagnostic for malformed operands.
Expected<void> parseRetInstr(const std::string &rest, Instr &in, ParserState &st)
{
    in.op = Opcode::Ret;
    std::string v = trim(rest);
    if (!v.empty())
    {
        auto val = parseValue_E(v, st);
        if (!val)
            return Expected<void>{val.error()};
        in.operands.push_back(std::move(val.value()));
    }
    in.type = Type(Type::Kind::Void);
    return {};
}

/// @brief Parses a `trap` instruction body.
///
/// The instruction carries no operands and always returns void.
///
/// @param rest Remaining text after the opcode keyword (unused).
/// @param in Instruction object to populate.
/// @param st Parser state (unused).
/// @return Always succeeds.
Expected<void> parseTrapInstr([[maybe_unused]] const std::string &rest, Instr &in,
                              [[maybe_unused]] ParserState &st)
{
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    return {};
}

/// @brief Lookup table mapping opcode mnemonics to parsing callbacks.
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

/// @brief Parses a complete instruction line, including optional result binding.
///
/// @param line Textual instruction as emitted by the serializer.
/// @param st Parser state mutated to record temporaries and append instructions.
/// @return Empty on success; otherwise, a diagnostic for malformed syntax,
/// operands, or shape violations.
Expected<void> parseInstruction_E(const std::string &line, ParserState &st)
{
    Instr in;
    in.loc = st.curLoc;
    std::string work = line;
    if (!work.empty() && work[0] == '%')
    {
        size_t eq = work.find('=');
        if (eq == std::string::npos)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": missing '='";
            return Expected<void>{makeError(in.loc, oss.str())};
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
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": unknown opcode " << op;
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    auto parsed = it->second(rest, in, st);
    if (!parsed)
        return parsed;
    auto shape = validateShape_E(in, st);
    if (!shape)
        return shape;
    st.curBB->instructions.push_back(std::move(in));
    return {};
}

} // namespace

/// @brief Public wrapper for parsing instructions that prints diagnostics.
///
/// @param line Textual instruction to parse.
/// @param st Parser state mutated with the decoded instruction.
/// @param err Stream receiving formatted diagnostics when parsing fails.
/// @return True on success; false if a diagnostic was emitted.
bool parseInstruction(const std::string &line, ParserState &st, std::ostream &err)
{
    auto r = parseInstruction_E(line, st);
    if (!r)
    {
        printDiag(r.error(), err);
        return false;
    }
    return true;
}

} // namespace il::io::detail
