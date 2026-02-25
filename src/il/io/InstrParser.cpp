//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/InstrParser.cpp
// Purpose: Interpret textual IL instruction statements and populate IR nodes.
// Key invariants: ParserState must reference a current function and basic
//                 block for instruction insertion.
// Ownership/Lifetime: Instructions are appended to the ParserState's active block.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the IL instruction parser shared by the textual reader.
/// @details The parser recognises assignment syntax, consults opcode metadata to
///          determine operand layouts, and cooperates with operand parsing
///          helpers to populate SSA temporaries, branch targets, and call
///          details.  It produces diagnostic-rich errors whenever the textual
///          representation deviates from the IL specification.

#include "il/internal/io/InstrParser.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/internal/io/OperandParser.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "support/diag_expected.hpp"
#include "viper/il/io/OperandParse.hpp"

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
        oss << info.name << " expects ";
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
        return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
    }

    const bool hasResult = in.result.has_value();
    switch (info.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
            {
                std::ostringstream oss;
                oss << info.name << " does not produce a result";
                return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
            }
            break;
        case ResultArity::One:
            if (!hasResult)
            {
                std::ostringstream oss;
                oss << info.name << " requires a result";
                return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
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
            oss << info.name << " expects at least 1 label";
            return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
        }
    }
    else
    {
        if (in.labels.size() != info.numSuccessors)
        {
            std::ostringstream oss;
            oss << info.name << " expects " << static_cast<unsigned>(info.numSuccessors)
                << " label";
            if (info.numSuccessors != 1)
                oss << 's';
            return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
        }
    }

    if (variadicSucc)
    {
        if (!in.brArgs.empty() && in.brArgs.size() != in.labels.size())
        {
            std::ostringstream oss;
            oss << info.name << " expects branch arguments per label or none";
            return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
        }
    }
    else
    {
        if (in.brArgs.size() > info.numSuccessors)
        {
            std::ostringstream oss;
            oss << info.name << " expects at most " << static_cast<unsigned>(info.numSuccessors)
                << " branch argument list";
            if (info.numSuccessors != 1)
                oss << 's';
            return Expected<void>{makeError(in.loc, formatLineDiag(st.lineNo, oss.str()))};
        }

        if (!in.brArgs.empty() && in.brArgs.size() != info.numSuccessors)
        {
            std::ostringstream oss;
            oss << info.name << " expects " << static_cast<unsigned>(info.numSuccessors)
                << " branch argument list";
            if (info.numSuccessors != 1)
                oss << 's';
            oss << ", or none";
            return Expected<void>{makeError(in.loc, formatLineDiag(st.lineNo, oss.str()))};
        }
    }

    return {};
}

/// @brief Lazily build a lookup from textual mnemonics to opcode enums.
///
/// @details The lookup is shared by all instruction parses and therefore cached
///          in a static map initialised on first use.  This avoids repeated
///          linear scans over the opcode table when resolving mnemonic strings
///          during parsing.
///
/// @return Reference to the populated mnemonic lookup table.
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
///
/// @details Many opcodes have a fixed result type independent of operand
///          annotations.  The metadata encodes the category, which we translate
///          into a concrete @ref il::core::Type for the instruction prior to
///          applying operand-driven overrides.
///
/// @param info Opcode metadata describing the expected result type.
/// @param in Instruction receiving the default type.
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

/// @brief Parse operands for an instruction using opcode metadata directives.
///
/// @details Each opcode describes how to interpret subsequent tokens via a
///          sequence of @ref OperandParseKind entries.  This function consumes
///          tokens, delegates to operand-specific helpers, and validates required
///          fields while preserving the remainder for branch/switch parsing.
///
/// @param opcode Opcode resolved from the mnemonic.
/// @param rest Remaining text on the line after the mnemonic.
/// @param in Instruction being populated.
/// @param st Parser state providing diagnostics, temporaries, and metadata.
/// @return Success or an error diagnostic describing malformed operands.
Expected<void> parseWithMetadata(Opcode opcode, const std::string &rest, Instr &in, ParserState &st)
{
    const auto &info = getOpcodeInfo(opcode);
    in.op = opcode;
    applyDefaultType(info, in);

    std::istringstream ss(rest);
    const std::string original = rest;
    OperandParser operandParser{st, in};

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
                    oss << "missing " << (spec.role ? spec.role : "type") << " for " << info.name;
                    return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
                }
                viper::parse::Cursor typeCursor{token, viper::parse::SourcePos{st.lineNo, 0}};
                viper::il::io::Context typeCtx{st, in};
                auto parsedType = viper::il::io::parseTypeOperand(typeCursor, typeCtx);
                if (!parsedType.ok())
                    return Expected<void>{parsedType.status.error()};
                break;
            }
            case OperandParseKind::Value:
            {
                std::string token = readToken(ss);
                if (token.empty())
                {
                    const bool readFailed = ss.fail();
                    if (spec.role)
                    {
                        std::ostringstream oss;
                        oss << "missing " << spec.role << " for " << info.name;
                        return Expected<void>{
                            il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
                    }
                    if (readFailed)
                    {
                        ss.clear();
                        break;
                    }
                    auto value = operandParser.parseValueToken(token);
                    if (!value)
                        return Expected<void>{value.error()};
                    in.operands.push_back(std::move(value.value()));
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
                auto value = operandParser.parseValueToken(token);
                if (!value)
                    return Expected<void>{value.error()};
                in.operands.push_back(std::move(value.value()));
                break;
            }
            case OperandParseKind::Call:
            {
                auto parsed = operandParser.parseCallOperands(original);
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
                auto parsed = operandParser.parseBranchTargets(remainder, branchCount);
                if (!parsed)
                    return parsed;
                return {};
            }
            case OperandParseKind::Switch:
            {
                std::string remainder;
                std::getline(ss, remainder);
                remainder = trim(remainder);
                auto parsed = operandParser.parseSwitchTargets(remainder);
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
            return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, "missing '='")};
        }
        std::string res = trim(work.substr(1, eq - 1));
        size_t colon = res.find(':');
        if (colon != std::string::npos)
        {
            std::string tyTok = trim(res.substr(colon + 1));
            res = trim(res.substr(0, colon));
            if (res.empty())
            {
                return Expected<void>{il::io::makeLineErrorDiag(
                    in.loc, st.lineNo, "missing temp name before type annotation")};
            }
            viper::parse::Cursor annotCursor{tyTok, viper::parse::SourcePos{st.lineNo, 0}};
            viper::il::io::Context annotCtx{st, in};
            auto parsedType = viper::il::io::parseTypeOperand(annotCursor, annotCtx);
            if (!parsedType.ok())
                return Expected<void>{parsedType.status.error()};
            if (in.type.kind == Type::Kind::Void)
            {
                return Expected<void>{
                    il::io::makeLineErrorDiag(in.loc, st.lineNo, "result type cannot be void")};
            }
            annotatedType = in.type;
        }
        auto [it, inserted] = st.tempIds.emplace(res, st.nextTemp);
        if (!inserted)
        {
            std::ostringstream oss;
            oss << "duplicate result name '%" << res << "'";
            return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
        }

        if (st.curFn->valueNames.size() <= st.nextTemp)
            st.curFn->valueNames.resize(st.nextTemp + 1);
        st.curFn->valueNames[st.nextTemp] = res;
        in.result = st.nextTemp;
        st.nextTemp++;
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
        oss << "unknown opcode " << op;
        return Expected<void>{il::io::makeLineErrorDiag(in.loc, st.lineNo, oss.str())};
    }
    auto parsed = parseWithMetadata(it->second, rest, in, st);
    if (!parsed)
        return parsed;
    if (annotatedType)
        in.type = *annotatedType;
    auto shape = validateShape_E(in, st);
    if (!shape)
        return shape;
    const bool isTerm = il::core::getOpcodeInfo(in.op).isTerminator;
    st.curBB->instructions.push_back(std::move(in));
    if (isTerm)
        st.curBB->terminated = true;
    return {};
}

} // namespace

/// @brief Parse an instruction and stream any resulting diagnostic messages.
///
/// @details Calls the exception-free helper @ref parseInstruction_E.  When an
///          error occurs the diagnostic is formatted via @ref il::support::printDiag
///          and written to @p err, matching the behaviour expected by higher-level
///          textual importers.
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
