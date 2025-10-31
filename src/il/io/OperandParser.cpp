//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/OperandParser.cpp
// Purpose: Decode textual IL operands into structured Value instances and
//          branch metadata.
// Key invariants: Operates on instructions tied to the current parser state.
// Ownership/Lifetime: Mutates instructions owned by the parser caller.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the operand parsing helpers shared by the IL instruction
///        parser.
/// @details This module tokenises operand substrings, classifies literal forms,
///          and translates them into @ref il::core::Value objects.  It also
///          manages branch argument bookkeeping to ensure control-flow edges
///          match block parameter signatures, emitting diagnostics when the
///          textual form deviates from the IL specification.

#include "il/io/OperandParser.hpp"

#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"
#include "il/io/TypeParser.hpp"

#include "support/diag_expected.hpp"
#include <charconv>
#include <limits>
#include <optional>

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace il::io::detail
{

using il::core::Instr;
using il::core::Type;
using il::core::Value;
using il::support::Expected;
using il::support::makeError;

namespace
{
using il::io::decodeEscapedString;
using il::io::formatLineDiag;
using il::io::parseFloatLiteral;
using il::io::parseIntegerLiteral;

using Operand = Value;

template <typename T> Expected<T> makeSyntaxError(ParserState &state, std::string message)
{
    return Expected<T>{makeError(state.curLoc, formatLineDiag(state.lineNo, std::move(message)))};
}

bool equalsIgnoreCase(std::string_view value, std::string_view literal)
{
    if (value.size() != literal.size())
        return false;
    for (size_t i = 0; i < literal.size(); ++i)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[i]);
        const unsigned char rhs = static_cast<unsigned char>(literal[i]);
        if (std::tolower(lhs) != std::tolower(rhs))
            return false;
    }
    return true;
}

void skipSpace(std::string_view &text)
{
    size_t consumed = 0;
    while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed])))
        ++consumed;
    text.remove_prefix(consumed);
}

bool isIdentStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

bool isIdentBody(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
}

std::optional<std::string_view> parseIdent(std::string_view &text)
{
    std::string_view original = text;
    if (original.empty() || !isIdentStart(original.front()))
        return std::nullopt;

    size_t length = 1;
    while (length < original.size() && isIdentBody(original[length]))
        ++length;

    text.remove_prefix(length);
    return original.substr(0, length);
}

bool parseInt(std::string_view &text, int64_t &value)
{
    std::string_view original = text;
    if (original.empty())
        return false;

    const char *begin = original.data();
    const char *end = begin + original.size();
    auto [ptr, ec] = std::from_chars(begin, end, value, 10);
    if (ec != std::errc{})
        return false;
    text.remove_prefix(static_cast<size_t>(ptr - begin));
    return true;
}

bool parseBracketed(std::string_view &text, std::string_view &out)
{
    std::string_view original = text;
    if (original.empty() || original.front() != '[')
        return false;

    size_t depth = 0;
    size_t start = 0;
    bool inString = false;
    bool escape = false;
    for (size_t index = 0; index < original.size(); ++index)
    {
        char c = original[index];
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

        if (c == '[')
        {
            if (depth == 0)
                start = index + 1;
            ++depth;
            continue;
        }

        if (c == ']')
        {
            if (depth == 0)
                return false;
            --depth;
            if (depth == 0)
            {
                out = original.substr(start, index - start);
                text.remove_prefix(index + 1);
                return true;
            }
            continue;
        }
    }
    return false;
}

class OperandReader
{
  public:
    explicit OperandReader(ParserState &state) : state_(state) {}

    Expected<size_t> parseOperand(std::string_view &text, Operand &out) const
    {
        bool matched = false;
        auto reg = tryParseRegister(text, out, matched);
        if (matched)
        {
            if (!reg)
                return reg;
            text.remove_prefix(reg.value());
            return reg;
        }

        auto mem = tryParseMemory(text, out, matched);
        if (matched)
        {
            if (!mem)
                return mem;
            text.remove_prefix(mem.value());
            return mem;
        }

        auto imm = parseImmediate(text, out);
        if (!imm)
            return imm;
        text.remove_prefix(imm.value());
        return imm;
    }

  private:
    Expected<size_t> tryParseRegister(std::string_view text, Operand &out, bool &matched) const
    {
        matched = false;
        if (text.empty() || text.front() != '%')
            return Expected<size_t>{size_t{0}};

        matched = true;
        text.remove_prefix(1);
        auto identText = text;
        auto ident = parseIdent(identText);
        if (!ident || ident->empty())
            return makeSyntaxError<size_t>(state_, "missing temp name");

        std::string name(ident->begin(), ident->end());
        auto it = state_.tempIds.find(name);
        if (it != state_.tempIds.end())
        {
            out = Operand::temp(it->second);
            return Expected<size_t>{1 + ident->size()};
        }

        if (name.size() > 1 && name.front() == 't')
        {
            std::string_view digits = name;
            digits.remove_prefix(1);
            std::string_view digitCursor = digits;
            int64_t parsed = 0;
            if (parseInt(digitCursor, parsed) && digitCursor.empty() && parsed >= 0 &&
                static_cast<uint64_t>(parsed) <= std::numeric_limits<unsigned>::max())
            {
                out = Operand::temp(static_cast<unsigned>(parsed));
                return Expected<size_t>{1 + ident->size()};
            }
        }

        std::ostringstream oss;
        oss << "unknown temp '%" << name << "'";
        return makeSyntaxError<size_t>(state_, oss.str());
    }

    Expected<size_t> tryParseMemory(std::string_view text, Operand &out, bool &matched) const
    {
        matched = false;
        if (text.empty() || text.front() != '[')
            return Expected<size_t>{size_t{0}};

        matched = true;
        std::string_view contents;
        auto cursor = text;
        if (!parseBracketed(cursor, contents))
            return makeSyntaxError<size_t>(state_, "unterminated memory operand");

        std::ostringstream oss;
        oss << "unsupported memory operand '[" << contents << "]'";
        return makeSyntaxError<size_t>(state_, oss.str());
    }

    Expected<size_t> parseImmediate(std::string_view text, Operand &out) const
    {
        if (text.empty())
            return makeSyntaxError<size_t>(state_, "missing operand");

        std::string token(text);
        if (equalsIgnoreCase(token, "true"))
        {
            out = Operand::constBool(true);
            return Expected<size_t>{token.size()};
        }
        if (equalsIgnoreCase(token, "false"))
        {
            out = Operand::constBool(false);
            return Expected<size_t>{token.size()};
        }
        if (token == "null")
        {
            out = Operand::null();
            return Expected<size_t>{token.size()};
        }
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        {
            std::string literal = token.substr(1, token.size() - 2);
            std::string decoded;
            std::string errMsg;
            if (!decodeEscapedString(literal, decoded, &errMsg))
                return makeSyntaxError<size_t>(state_, std::move(errMsg));
            out = Operand::constStr(std::move(decoded));
            return Expected<size_t>{token.size()};
        }

        const bool hasDecimalPoint = token.find('.') != std::string::npos;
        const bool isHexLiteral =
            token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X');
        const bool hasExponent = (!isHexLiteral) && (token.find('e') != std::string::npos ||
                                                     token.find('E') != std::string::npos);

        auto parseFloatingToken = [&](const std::string &literal) -> Expected<size_t>
        {
            double value = 0.0;
            if (parseFloatLiteral(literal, value))
            {
                out = Operand::constFloat(value);
                return Expected<size_t>{literal.size()};
            }
            std::ostringstream oss;
            oss << "invalid floating literal '" << literal << "'";
            return makeSyntaxError<size_t>(state_, oss.str());
        };

        if (hasDecimalPoint || hasExponent || equalsIgnoreCase(token, "nan") ||
            equalsIgnoreCase(token, "inf") || equalsIgnoreCase(token, "+inf") ||
            equalsIgnoreCase(token, "-inf"))
        {
            return parseFloatingToken(token);
        }

        long long value = 0;
        if (parseIntegerLiteral(token, value))
        {
            out = Operand::constInt(value);
            return Expected<size_t>{token.size()};
        }

        std::ostringstream oss;
        oss << "invalid integer literal '" << token << "'";
        return makeSyntaxError<size_t>(state_, oss.str());
    }

    ParserState &state_;
};

Expected<Operand> parseSymbolRef(std::string_view &text, ParserState &state)
{
    skipSpace(text);
    if (text.empty() || text.front() != '@')
        return makeSyntaxError<Operand>(state, "missing global name");

    text.remove_prefix(1);
    auto nameCursor = text;
    auto ident = parseIdent(nameCursor);
    if (!ident || ident->empty())
        return makeSyntaxError<Operand>(state, "missing global name");

    skipSpace(nameCursor);
    if (!nameCursor.empty())
        return makeSyntaxError<Operand>(state, "malformed global name");

    text = nameCursor;
    return Operand::global(std::string(ident->begin(), ident->end()));
}

[[maybe_unused]] Expected<Type> parseType(std::string_view token, ParserState &state)
{
    std::string_view view = token;
    skipSpace(view);
    if (view.empty())
        return makeSyntaxError<Type>(state, "missing type");

    std::string literal(view);
    bool ok = false;
    Type ty = ::il::io::parseType(literal, &ok);
    if (!ok)
    {
        std::ostringstream oss;
        oss << "unknown type '" << literal << "'";
        return makeSyntaxError<Type>(state, oss.str());
    }

    return ty;
}

} // namespace

/// @brief Create an operand parser bound to the current parser state and instruction.
/// @note instr_ aliases the caller-owned instruction so operands are populated in-place.
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
    std::string_view remaining(tok);
    skipSpace(remaining);

    if (remaining.empty())
        return makeSyntaxError<Value>(state_, "missing operand");

    if (remaining.front() == '@')
    {
        auto symbol = parseSymbolRef(remaining, state_);
        if (!symbol)
            return Expected<Value>{symbol.error()};

        skipSpace(remaining);
        if (!remaining.empty())
            return makeSyntaxError<Value>(state_, "unexpected trailing characters");

        return Expected<Value>{std::move(symbol.value())};
    }

    OperandReader reader(state_);
    Operand operand;
    auto consumed = reader.parseOperand(remaining, operand);
    if (!consumed)
        return Expected<Value>{consumed.error()};

    skipSpace(remaining);
    if (!remaining.empty())
        return makeSyntaxError<Value>(state_, "unexpected trailing characters");

    return Expected<Value>{std::move(operand)};
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
Expected<std::vector<std::string>> OperandParser::splitCommaSeparated(const std::string &text,
                                                                      const char *context) const
{
    std::vector<std::string> tokens;
    std::string current;
    bool inString = false;
    bool escape = false;
    size_t depth = 0;

    auto makeErrorWithMessage = [&](const std::string &message)
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": " << message;
        return Expected<std::vector<std::string>>{makeError(instr_.loc, oss.str())};
    };

    auto malformedError = [&]()
    {
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
    if (label.empty())
    {
        return makeSyntaxError<void>(state_, "malformed branch target: missing label");
    }
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
Expected<void> OperandParser::checkBranchArgCount(const std::string &label, size_t argCount) const
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
    auto malformedSwitch = [&]()
    {
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

/// @brief Parse the default branch component of a switch operand list.
///
/// @param segment Text describing the default destination.
/// @return Success or an error diagnostic when the target is malformed.
Expected<void> OperandParser::parseDefaultTarget(const std::string &segment)
{
    std::vector<Value> args;
    std::string label;
    auto parsed = parseBranchTarget(segment, label, args);
    if (!parsed)
        return parsed;
    return validateCaseArity(std::move(label), std::move(args));
}

/// @brief Parse an individual `value -> label(args)` switch case.
///
/// @param segment Text describing the case value and destination.
/// @param mnemonic Name of the opcode for diagnostic messages.
/// @return Success or an error diagnostic when the case is malformed.
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

/// @brief Record a branch label and verify its argument arity for switch targets.
///
/// @param label Destination block label stripped of decorations.
/// @param args Branch arguments associated with the destination.
/// @return Success or an error diagnostic indicating an argument mismatch.
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
