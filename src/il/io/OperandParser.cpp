// File: src/il/io/OperandParser.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements helpers for parsing IL instruction operands.
// Key invariants: Operates on instructions tied to the current parser state.
// Ownership/Lifetime: Mutates instructions owned by the parser caller.
// Links: docs/il-guide.md#reference

#include "il/io/OperandParser.hpp"

#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"

#include "support/diag_expected.hpp"

#include <cctype>
#include <exception>
#include <cstring>
#include <sstream>
#include <string_view>
#include <utility>

namespace il::io::detail
{

using il::core::Instr;
using il::core::Value;
using il::support::Expected;
using il::support::makeError;

/// @brief Create an operand parser bound to the current parser state and instruction.
OperandParser::OperandParser(ParserState &state, Instr &instr) : state_(state), instr_(instr) {}

/// @brief Parse a single operand token into a Value representation.
///
/// Handles constants, temporaries, globals, null, and quoted string literals.
/// When parsing fails an error diagnostic is produced referencing the current
/// parser line.
///
/// @param tok Token extracted from the operand text.
/// @return Parsed value or an error diagnostic.
Expected<Value> OperandParser::parseValueToken(const std::string &tok) const
{
    if (tok.empty())
    {
        std::ostringstream oss;
        oss << "Line " << state_.lineNo << ": missing operand";
        return Expected<Value>{makeError(state_.curLoc, oss.str())};
    }

    const auto equalsIgnoreCase = [](const std::string &value, const char *literal) {
        const size_t len = std::strlen(literal);
        if (value.size() != len)
            return false;
        for (size_t i = 0; i < len; ++i)
        {
            const unsigned char lhs = static_cast<unsigned char>(value[i]);
            const unsigned char rhs = static_cast<unsigned char>(literal[i]);
            if (std::tolower(lhs) != std::tolower(rhs))
                return false;
        }
        return true;
    };

    if (equalsIgnoreCase(tok, "true"))
        return Value::constBool(true);
    if (equalsIgnoreCase(tok, "false"))
        return Value::constBool(false);
    if (tok[0] == '%')
    {
        std::string name = tok.substr(1);
        auto it = state_.tempIds.find(name);
        if (it != state_.tempIds.end())
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
                    oss << "Line " << state_.lineNo << ": invalid temp id '" << tok << "'";
                    return Expected<Value>{makeError(state_.curLoc, oss.str())};
                }
            }
        }
        std::ostringstream oss;
        oss << "Line " << state_.lineNo << ": unknown temp '" << tok << "'";
        return Expected<Value>{makeError(state_.curLoc, oss.str())};
    }
    if (tok[0] == '@')
        return Value::global(tok.substr(1));
    if (tok == "null")
        return Value::null();
    if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"')
    {
        std::string decoded;
        std::string errMsg;
        std::string literal = tok.substr(1, tok.size() - 2);
        if (!il::io::decodeEscapedString(literal, decoded, &errMsg))
        {
            std::ostringstream oss;
            oss << "Line " << state_.lineNo << ": " << errMsg;
            return Expected<Value>{makeError(state_.curLoc, oss.str())};
        }
        return Value::constStr(std::move(decoded));
    }
    if (tok.find('.') != std::string::npos || tok.find('e') != std::string::npos ||
        tok.find('E') != std::string::npos)
    {
        double value = 0.0;
        if (parseFloatLiteral(tok, value))
            return Value::constFloat(value);
        std::ostringstream oss;
        oss << "Line " << state_.lineNo << ": invalid floating literal '" << tok << "'";
        return Expected<Value>{makeError(state_.curLoc, oss.str())};
    }
    long long intValue = 0;
    if (parseIntegerLiteral(tok, intValue))
        return Value::constInt(intValue);
    std::ostringstream oss;
    oss << "Line " << state_.lineNo << ": invalid integer literal '" << tok << "'";
    return Expected<Value>{makeError(state_.curLoc, oss.str())};
}

/// @brief Split a comma-separated operand list while respecting nested constructs.
///
/// Tracks string literals, escape sequences, and parenthesis depth so nested
/// expressions do not break the split.  When malformed input is detected, an
/// error diagnostic is returned referencing the instruction being parsed.
///
/// @param text Raw operand text after the mnemonic.
/// @param context Human-readable description for error messages.
/// @return Vector of trimmed tokens or an error diagnostic.
Expected<std::vector<std::string>>
OperandParser::splitCommaSeparated(const std::string &text, const char *context) const
{
    std::vector<std::string> tokens;
    std::string current;
    bool inString = false;
    bool escape = false;
    size_t depth = 0;

    auto makeErrorWithMessage = [&](const std::string &message) {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": " << message;
        return Expected<std::vector<std::string>>{makeError(instr_.loc, oss.str())};
    };

    auto malformedError = [&]() {
        std::ostringstream msg;
        msg << "malformed " << context;
        return makeErrorWithMessage(msg.str());
    };

    const bool textIsWhitespaceOnly = trim(text).empty();

    for (char c : text)
    {
        if (inString)
        {
            current.push_back(c);
            if (escape)
            {
                escape = false;
                continue;
            }
            if (c == '\\')
            {
                escape = true;
                continue;
            }
            if (c == '"')
                inString = false;
            continue;
        }

        if (c == '"')
        {
            current.push_back(c);
            inString = true;
            continue;
        }

        if (c == '(')
        {
            current.push_back(c);
            ++depth;
            continue;
        }
        if (c == ')')
        {
            if (depth == 0)
                return makeErrorWithMessage("mismatched ')'");
            current.push_back(c);
            --depth;
            continue;
        }
        if (c == ',' && depth == 0)
        {
            std::string trimmed = trim(current);
            if (trimmed.empty() && !textIsWhitespaceOnly)
                return malformedError();
            if (!trimmed.empty())
                tokens.push_back(std::move(trimmed));
            current.clear();
            continue;
        }

        current.push_back(c);
    }

    if (escape || inString)
        return malformedError();
    if (depth != 0)
        return makeErrorWithMessage("mismatched ')'");

    std::string trimmed = trim(current);
    if (!trimmed.empty())
        tokens.push_back(std::move(trimmed));

    return Expected<std::vector<std::string>>{std::move(tokens)};
}

/// @brief Parse operands for call-style instructions.
///
/// Extracts the callee name, decodes each argument, and appends them to the
/// instruction.  The function verifies balanced parentheses, rejects trailing
/// junk after the argument list, and reports clear diagnostics on malformed
/// text.
///
/// @param text Operand substring following the mnemonic.
/// @return Success or an error diagnostic.
Expected<void> OperandParser::parseCallOperands(const std::string &text)
{
    const size_t at = text.find('@');
    const size_t lp = text.find('(', at);
    const size_t rp = text.find(')', lp);
    if (at == std::string::npos || lp == std::string::npos || rp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed call";
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    if (!trim(text.substr(rp + 1)).empty())
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed call";
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    instr_.callee = trim(text.substr(at + 1, lp - at - 1));
    std::string args = text.substr(lp + 1, rp - lp - 1);
    auto tokens = splitCommaSeparated(args, "call");
    if (!tokens)
        return Expected<void>{tokens.error()};
    for (const auto &token : tokens.value())
    {
        auto argVal = parseValueToken(token);
        if (!argVal)
            return Expected<void>{argVal.error()};
        instr_.operands.push_back(std::move(argVal.value()));
    }
    if (!instr_.result)
        instr_.type = il::core::Type(il::core::Type::Kind::Void);
    return {};
}

/// @brief Parse a single branch target segment into a label and argument list.
///
/// Supports optional "label" prefixes and nested argument lists.  String
/// literals and parentheses are tracked to avoid premature splitting.  Parsed
/// arguments are appended to @p args while the cleaned label is returned via
/// @p label.
///
/// @param segment Text describing one branch destination.
/// @param label Output parameter receiving the normalized label.
/// @param args Output parameter receiving decoded argument values.
/// @return Success or an error diagnostic describing the malformed segment.
Expected<void> OperandParser::parseBranchTarget(const std::string &segment,
                                                std::string &label,
                                                std::vector<Value> &args) const
{
    std::string text = trim(segment);
    const char *mnemonic = il::core::getOpcodeInfo(instr_.op).name;
    if (text.rfind("label ", 0) == 0)
        text = trim(text.substr(6));
    if (!text.empty() && text[0] == '^')
        text = text.substr(1);
    size_t lp = std::string::npos;
    bool inString = false;
    bool escape = false;
    for (size_t pos = 0; pos < text.size(); ++pos)
    {
        char c = text[pos];
        if (inString)
        {
            if (escape)
            {
                escape = false;
                continue;
            }
            if (c == '\\')
            {
                escape = true;
                continue;
            }
            if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == '(')
        {
            lp = pos;
            break;
        }
    }

    if (lp == std::string::npos)
    {
        label = trim(text);
        return {};
    }

    size_t rp = std::string::npos;
    size_t depth = 0;
    inString = false;
    escape = false;
    for (size_t pos = lp; pos < text.size(); ++pos)
    {
        char c = text[pos];
        if (pos == lp)
        {
            ++depth;
            continue;
        }
        if (inString)
        {
            if (escape)
            {
                escape = false;
                continue;
            }
            if (c == '\\')
            {
                escape = true;
                continue;
            }
            if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == '(')
        {
            ++depth;
            continue;
        }
        if (c == ')')
        {
            if (depth == 0)
                break;
            --depth;
            if (depth == 0)
            {
                rp = pos;
                break;
            }
            continue;
        }
    }

    if (rp == std::string::npos || depth != 0 || inString)
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": mismatched ')";
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    if (!trim(text.substr(rp + 1)).empty())
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    label = trim(text.substr(0, lp));
    std::string argsStr = text.substr(lp + 1, rp - lp - 1);
    auto tokens = splitCommaSeparated(argsStr, mnemonic);
    if (!tokens)
        return Expected<void>{tokens.error()};
    for (const auto &token : tokens.value())
    {
        auto val = parseValueToken(token);
        if (!val)
            return Expected<void>{val.error()};
        args.push_back(std::move(val.value()));
    }
    return {};
}

/// @brief Validate that a branch target supplies the expected number of arguments.
///
/// Consults known block parameter counts or records unresolved branches for
/// later verification when the block is defined.
///
/// @param label Target label being checked.
/// @param argCount Number of arguments supplied with the branch.
/// @return Success or an error diagnostic when a mismatch is detected.
Expected<void> OperandParser::checkBranchArgCount(const std::string &label,
                                                  size_t argCount) const
{
    if (instr_.op == il::core::Opcode::EhPush)
        return {};
    auto it = state_.blockParamCount.find(label);
    if (it != state_.blockParamCount.end())
    {
        if (it->second != argCount)
        {
            std::ostringstream oss;
            oss << "line " << state_.lineNo << ": bad arg count";
            return Expected<void>{makeError(instr_.loc, oss.str())};
        }
    }
    else
    {
        state_.pendingBrs.push_back({label, argCount, state_.lineNo});
    }
    return {};
}

/// @brief Parse all branch targets for a multi-target instruction.
///
/// Splits the target list, parses each segment, records labels/arguments on the
/// instruction, and verifies argument counts against known block signatures.
///
/// @param text Textual representation of the branch operand list.
/// @param expectedTargets Number of targets required by the opcode.
/// @return Success or an error diagnostic describing the malformed operand list.
Expected<void> OperandParser::parseBranchTargets(const std::string &text, size_t expectedTargets)
{
    std::string remaining = trim(text);
    const char *mnemonic = il::core::getOpcodeInfo(instr_.op).name;
    auto segments = splitCommaSeparated(remaining, mnemonic);
    if (!segments)
        return Expected<void>{segments.error()};

    const auto &segmentList = segments.value();
    if (segmentList.size() != expectedTargets)
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    for (const auto &segment : segmentList)
    {
        std::vector<Value> args;
        std::string label;
        auto parsed = parseBranchTarget(segment, label, args);
        if (!parsed)
            return parsed;
        auto validated = validateCaseArity(std::move(label), std::move(args));
        if (!validated)
            return validated;
    }

    instr_.type = il::core::Type(il::core::Type::Kind::Void);
    return {};
}

/// @brief Parse switch-style operands consisting of a default and case list.
///
/// Reads the default branch, then iteratively parses `value -> label(args)`
/// pairs, storing both the case value and target metadata on the instruction.
/// The function enforces balanced parentheses and emits diagnostics for
/// malformed specifications.
///
/// @param text Raw operand text following the switch mnemonic.
/// @return Success or an error diagnostic.
Expected<void> OperandParser::parseSwitchTargets(const std::string &text)
{
    std::string remaining = trim(text);
    const char *mnemonic = il::core::getOpcodeInfo(instr_.op).name;
    auto malformedSwitch = [&]() {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
    };

    if (remaining.empty())
        return malformedSwitch();

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
                    oss << "line " << state_.lineNo << ": mismatched ')";
                    return Expected<void>{makeError(instr_.loc, oss.str())};
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
            return malformedSwitch();
        }

        if (parsingDefault)
        {
            auto parsed = parseDefaultTarget(segment);
            if (!parsed)
                return parsed;
            parsingDefault = false;
        }
        else
        {
            auto parsed = parseCaseSegment(segment, mnemonic);
            if (!parsed)
                return parsed;
        }

        if (split < remaining.size())
            remaining = trim(remaining.substr(split + 1));
        else
            break;
    }

    if (parsingDefault)
    {
        return malformedSwitch();
    }

    instr_.type = il::core::Type(il::core::Type::Kind::Void);
    return {};
}

Expected<void> OperandParser::parseDefaultTarget(const std::string &segment)
{
    std::vector<Value> args;
    std::string label;
    auto parsed = parseBranchTarget(segment, label, args);
    if (!parsed)
        return parsed;
    return validateCaseArity(std::move(label), std::move(args));
}

Expected<void> OperandParser::parseCaseSegment(const std::string &segment, const char *mnemonic)
{
    const size_t arrow = segment.find("->");
    if (arrow == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    std::string valueText = trim(segment.substr(0, arrow));
    std::string targetText = trim(segment.substr(arrow + 2));
    if (valueText.empty() || targetText.empty())
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    auto caseValue = parseValueToken(valueText);
    if (!caseValue)
        return Expected<void>{caseValue.error()};
    instr_.operands.push_back(std::move(caseValue.value()));

    std::vector<Value> args;
    std::string label;
    auto parsed = parseBranchTarget(targetText, label, args);
    if (!parsed)
        return parsed;

    return validateCaseArity(std::move(label), std::move(args));
}

Expected<void> OperandParser::validateCaseArity(std::string label, std::vector<Value> args)
{
    const size_t argCount = args.size();
    instr_.labels.push_back(std::move(label));
    instr_.brArgs.push_back(std::move(args));
    auto check = checkBranchArgCount(instr_.labels.back(), argCount);
    if (!check)
        return check;
    return {};
}

} // namespace il::io::detail
