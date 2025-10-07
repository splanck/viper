// File: src/il/io/OperandParser.cpp
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

OperandParser::OperandParser(ParserState &state, Instr &instr) : state_(state), instr_(instr) {}

Expected<Value> OperandParser::parseValueToken(const std::string &tok) const
{
    if (tok.empty())
        return Value::constInt(0);

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
        instr_.labels.push_back(label);
        instr_.brArgs.push_back(args);
        auto check = checkBranchArgCount(label, args.size());
        if (!check)
            return check;
    }

    instr_.type = il::core::Type(il::core::Type::Kind::Void);
    return {};
}

Expected<void> OperandParser::parseSwitchTargets(const std::string &text)
{
    std::string remaining = trim(text);
    const char *mnemonic = il::core::getOpcodeInfo(instr_.op).name;
    if (remaining.empty())
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
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
            std::ostringstream oss;
            oss << "line " << state_.lineNo << ": malformed " << mnemonic;
            return Expected<void>{makeError(instr_.loc, oss.str())};
        }

        if (parsingDefault)
        {
            std::vector<Value> args;
            std::string label;
            auto parsed = parseBranchTarget(segment, label, args);
            if (!parsed)
                return parsed;
            size_t argCount = args.size();
            instr_.labels.push_back(label);
            instr_.brArgs.push_back(std::move(args));
            auto check = checkBranchArgCount(label, argCount);
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
            size_t argCount = args.size();
            instr_.labels.push_back(label);
            instr_.brArgs.push_back(std::move(args));
            auto check = checkBranchArgCount(label, argCount);
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
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }

    instr_.type = il::core::Type(il::core::Type::Kind::Void);
    return {};
}

} // namespace il::io::detail
