// File: src/il/io/InstrParser.cpp
// Purpose: Implements parsing of IL instruction statements (MIT License; see LICENSE).
// Key invariants: ParserState must reference a current function and basic block.
// Ownership/Lifetime: Instructions are appended to the ParserState's active block.
// Links: docs/il-guide.md#reference

#include "il/io/InstrParser.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/TypeParser.hpp"
#include "support/diag_expected.hpp"

#include <cctype>
#include <exception>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::io::detail
{
namespace
{

using il::core::Instr;
using il::core::kNumOpcodes;
using il::core::Opcode;
using il::core::OpcodeInfo;
using il::core::OperandParseKind;
using il::core::ResultArity;
using il::core::Type;
using il::core::TypeCategory;
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
        std::ostringstream oss;
        oss << "Line " << st.lineNo << ": unknown temp '" << tok << "'";
        return Expected<Value>{makeError(st.curLoc, oss.str())};
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

    const bool variadicSucc = il::core::isVariadicSuccessorCount(info.numSuccessors);
    if (variadicSucc)
    {
        if (in.labels.empty())
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": " << info.name << " expects at least 1 label";
            return Expected<void>{makeError(in.loc, oss.str())};
        }
    }
    else
    {
        if (in.labels.size() != info.numSuccessors)
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": " << info.name << " expects "
                << static_cast<unsigned>(info.numSuccessors) << " label";
            if (info.numSuccessors != 1)
                oss << 's';
            return Expected<void>{makeError(in.loc, oss.str())};
        }
    }

    if (variadicSucc)
    {
        if (!in.brArgs.empty() && in.brArgs.size() != in.labels.size())
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": " << info.name
                << " expects branch arguments per label or none";
            return Expected<void>{makeError(in.loc, oss.str())};
        }
    }
    else
    {
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
Expected<void> checkBlockArgCount(const Instr &in,
                                  ParserState &st,
                                  const std::string &label,
                                  size_t argCount)
{
    if (in.op == Opcode::EhPush)
        return {};
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

/// @brief Lazily build a lookup from opcode mnemonics to Opcode enumerators.
const std::unordered_map<std::string, Opcode> &mnemonicTable()
{
    static const std::unordered_map<std::string, Opcode> table = []
    {
        std::unordered_map<std::string, Opcode> map;
        map.reserve(kNumOpcodes);
        for (size_t idx = 0; idx < kNumOpcodes; ++idx)
        {
            const auto op = static_cast<Opcode>(idx);
            map.emplace(getOpcodeInfo(op).name, op);
        }
        return map;
    }();
    return table;
}

/// @brief Stamp the instruction's result type based on opcode metadata.
void applyDefaultType(const OpcodeInfo &info, Instr &in)
{
    using Kind = Type::Kind;
    switch (info.resultType)
    {
        case TypeCategory::I1:
            in.type = Type(Kind::I1);
            break;
        case TypeCategory::I32:
            in.type = Type(Kind::I32);
            break;
        case TypeCategory::I64:
            in.type = Type(Kind::I64);
            break;
        case TypeCategory::F64:
            in.type = Type(Kind::F64);
            break;
        case TypeCategory::Ptr:
            in.type = Type(Kind::Ptr);
            break;
        case TypeCategory::Str:
            in.type = Type(Kind::Str);
            break;
        case TypeCategory::Error:
            in.type = Type(Kind::Error);
            break;
        case TypeCategory::ResumeTok:
            in.type = Type(Kind::ResumeTok);
            break;
        case TypeCategory::Void:
            in.type = Type(Kind::Void);
            break;
        default:
            in.type = Type(Kind::Void);
            break;
    }
}

/// @brief Parse the call operand syntax `<callee>(args...)`.
Expected<void> parseCallBody(const std::string &rest, Instr &in, ParserState &st)
{
    const size_t at = rest.find('@');
    const size_t lp = rest.find('(', at);
    const size_t rp = rest.find(')', lp);
    if (at == std::string::npos || lp == std::string::npos || rp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": malformed call";
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    in.callee = trim(rest.substr(at + 1, lp - at - 1));
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

/// @brief Parse a branch target `<label>(args...)` fragment.
Expected<void> parseBranchTarget(
    const std::string &part, Instr &in, ParserState &st, std::string &lbl, std::vector<Value> &args)
{
    std::string text = trim(part);
    if (text.rfind("label ", 0) == 0)
        text = trim(text.substr(6));
    if (!text.empty() && text[0] == '^')
        text = text.substr(1);
    const size_t lp = text.find('(');
    if (lp == std::string::npos)
    {
        lbl = trim(text);
        return {};
    }
    const size_t rp = text.find(')', lp);
    if (rp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": mismatched ')";
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    lbl = trim(text.substr(0, lp));
    std::string argsStr = text.substr(lp + 1, rp - lp - 1);
    std::stringstream as(argsStr);
    std::string token;
    while (std::getline(as, token, ','))
    {
        token = trim(token);
        if (token.empty())
            continue;
        auto val = parseValue_E(token, st);
        if (!val)
            return Expected<void>{val.error()};
        args.push_back(std::move(val.value()));
    }
    return {};
}

/// @brief Parse the list of branch targets for control-flow opcodes.
Expected<void> parseBranchTargetsFromString(const std::string &text,
                                            size_t expectedTargets,
                                            Instr &in,
                                            ParserState &st)
{
    std::string remaining = trim(text);
    const char *mnemonic = getOpcodeInfo(in.op).name;
    for (size_t idx = 0; idx < expectedTargets; ++idx)
    {
        if (remaining.empty())
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": malformed " << mnemonic;
            return Expected<void>{makeError(in.loc, oss.str())};
        }

        size_t split = remaining.size();
        size_t depth = 0;
        for (size_t pos = 0; pos < remaining.size(); ++pos)
        {
            char c = remaining[pos];
            if (c == '(')
                ++depth;
            else if (c == ')')
            {
                if (depth == 0)
                {
                    std::ostringstream oss;
                    oss << "line " << st.lineNo << ": mismatched ')";
                    return Expected<void>{makeError(in.loc, oss.str())};
                }
                --depth;
            }
            else if (c == ',' && depth == 0)
            {
                split = pos;
                break;
            }
        }

        std::string segment = trim(remaining.substr(0, split));
        if (segment.empty())
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": malformed " << mnemonic;
            return Expected<void>{makeError(in.loc, oss.str())};
        }

        std::vector<Value> args;
        std::string label;
        auto parsed = parseBranchTarget(segment, in, st, label, args);
        if (!parsed)
            return parsed;
        in.labels.push_back(label);
        in.brArgs.push_back(args);
        auto check = checkBlockArgCount(in, st, label, args.size());
        if (!check)
            return check;

        if (split < remaining.size())
            remaining = trim(remaining.substr(split + 1));
        else
            remaining.clear();
    }

    if (!trim(remaining).empty())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(in.loc, oss.str())};
    }

    in.type = Type(Type::Kind::Void);
    return {};
}

Expected<void> parseSwitchI32Operands(const std::string &text, Instr &in, ParserState &st)
{
    std::string remaining = trim(text);
    const char *mnemonic = getOpcodeInfo(in.op).name;
    if (remaining.empty())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(in.loc, oss.str())};
    }

    bool parsingDefault = true;
    while (!remaining.empty())
    {
        size_t split = remaining.size();
        size_t depth = 0;
        for (size_t pos = 0; pos < remaining.size(); ++pos)
        {
            char c = remaining[pos];
            if (c == '(')
                ++depth;
            else if (c == ')')
            {
                if (depth == 0)
                {
                    std::ostringstream oss;
                    oss << "line " << st.lineNo << ": mismatched ')";
                    return Expected<void>{makeError(in.loc, oss.str())};
                }
                --depth;
            }
            else if (c == ',' && depth == 0)
            {
                split = pos;
                break;
            }
        }

        std::string segment = trim(remaining.substr(0, split));
        if (segment.empty())
        {
            std::ostringstream oss;
            oss << "line " << st.lineNo << ": malformed " << mnemonic;
            return Expected<void>{makeError(in.loc, oss.str())};
        }

        if (parsingDefault)
        {
            std::vector<Value> args;
            std::string label;
            auto parsed = parseBranchTarget(segment, in, st, label, args);
            if (!parsed)
                return parsed;
            size_t argCount = args.size();
            in.labels.push_back(label);
            in.brArgs.push_back(std::move(args));
            auto check = checkBlockArgCount(in, st, label, argCount);
            if (!check)
                return check;
            parsingDefault = false;
        }
        else
        {
            const size_t arrow = segment.find("->");
            if (arrow == std::string::npos)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": malformed " << mnemonic;
                return Expected<void>{makeError(in.loc, oss.str())};
            }
            std::string valueText = trim(segment.substr(0, arrow));
            std::string targetText = trim(segment.substr(arrow + 2));
            if (valueText.empty() || targetText.empty())
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": malformed " << mnemonic;
                return Expected<void>{makeError(in.loc, oss.str())};
            }

            auto caseValue = parseValue_E(valueText, st);
            if (!caseValue)
                return Expected<void>{caseValue.error()};
            in.operands.push_back(std::move(caseValue.value()));

            std::vector<Value> args;
            std::string label;
            auto parsed = parseBranchTarget(targetText, in, st, label, args);
            if (!parsed)
                return parsed;
            size_t argCount = args.size();
            in.labels.push_back(label);
            in.brArgs.push_back(std::move(args));
            auto check = checkBlockArgCount(in, st, label, argCount);
            if (!check)
                return check;
        }

        if (split < remaining.size())
            remaining = trim(remaining.substr(split + 1));
        else
            break;
    }

    if (parsingDefault)
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(in.loc, oss.str())};
    }

    in.type = Type(Type::Kind::Void);
    return {};
}

/// @brief Parse operands based on opcode metadata-driven descriptions.
Expected<void> parseWithMetadata(Opcode opcode, const std::string &rest, Instr &in, ParserState &st)
{
    const auto &info = getOpcodeInfo(opcode);
    in.op = opcode;
    applyDefaultType(info, in);

    std::istringstream ss(rest);
    const std::string original = rest;

    for (size_t idx = 0; idx < info.parse.size(); ++idx)
    {
        const auto &spec = info.parse[idx];
        switch (spec.kind)
        {
            case OperandParseKind::None:
                break;
            case OperandParseKind::TypeImmediate:
            {
                std::string token = readToken(ss);
                if (token.empty())
                {
                    std::ostringstream oss;
                    oss << "line " << st.lineNo << ": missing " << (spec.role ? spec.role : "type")
                        << " for " << info.name;
                    return Expected<void>{makeError(in.loc, oss.str())};
                }
                auto ty = parseType_E(token, st);
                if (!ty)
                    return Expected<void>{ty.error()};
                in.type = ty.value();
                break;
            }
            case OperandParseKind::Value:
            {
                std::string token = readToken(ss);
                if (token.empty())
                {
                    if (spec.role)
                    {
                        std::ostringstream oss;
                        oss << "line " << st.lineNo << ": missing " << spec.role << " for "
                            << info.name;
                        return Expected<void>{makeError(in.loc, oss.str())};
                    }
                    break;
                }
                if (opcode == Opcode::TrapKind)
                {
                    long long trapValue = 0;
                    if (parseTrapKindToken(token, trapValue))
                    {
                        in.operands.push_back(Value::constInt(trapValue));
                        break;
                    }
                }
                auto value = parseValue_E(token, st);
                if (!value)
                    return Expected<void>{value.error()};
                in.operands.push_back(std::move(value.value()));
                break;
            }
            case OperandParseKind::Call:
            {
                auto parsed = parseCallBody(original, in, st);
                if (!parsed)
                    return parsed;
                return {};
            }
            case OperandParseKind::BranchTarget:
            {
                size_t branchCount = 0;
                for (size_t j = idx; j < info.parse.size(); ++j)
                {
                    if (info.parse[j].kind == OperandParseKind::BranchTarget)
                        ++branchCount;
                }
                std::string remainder;
                std::getline(ss, remainder);
                remainder = trim(remainder);
                auto parsed = parseBranchTargetsFromString(remainder, branchCount, in, st);
                if (!parsed)
                    return parsed;
                return {};
            }
            case OperandParseKind::Switch:
            {
                std::string remainder;
                std::getline(ss, remainder);
                remainder = trim(remainder);
                auto parsed = parseSwitchI32Operands(remainder, in, st);
                if (!parsed)
                    return parsed;
                return {};
            }
        }
    }

    return {};
}

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
    std::optional<Type> annotatedType;
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
        size_t colon = res.find(':');
        if (colon != std::string::npos)
        {
            std::string tyTok = trim(res.substr(colon + 1));
            res = trim(res.substr(0, colon));
            if (res.empty())
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": missing temp name before type annotation";
                return Expected<void>{makeError(in.loc, oss.str())};
            }
            auto ty = parseType_E(tyTok, st);
            if (!ty)
                return Expected<void>{ty.error()};
            if (ty.value().kind == Type::Kind::Void)
            {
                std::ostringstream oss;
                oss << "line " << st.lineNo << ": result type cannot be void";
                return Expected<void>{makeError(in.loc, oss.str())};
            }
            annotatedType = ty.value();
        }
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
    const auto &table = mnemonicTable();
    auto it = table.find(op);
    if (it == table.end())
    {
        std::ostringstream oss;
        oss << "line " << st.lineNo << ": unknown opcode " << op;
        return Expected<void>{makeError(in.loc, oss.str())};
    }
    auto parsed = parseWithMetadata(it->second, rest, in, st);
    if (!parsed)
        return parsed;
    if (annotatedType)
        in.type = *annotatedType;
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
