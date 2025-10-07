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
        if (!il::io::decodeEscapedString(std::string_view(tok).substr(1, tok.size() - 2),
                                         decoded,
                                         &errMsg))
        {
            std::ostringstream oss;
            oss << "Line " << state_.lineNo << ": invalid string literal '" << tok << "'";
            if (!errMsg.empty())
                oss << ": " << errMsg;
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
    std::stringstream as(args);
    std::string a;
    while (std::getline(as, a, ','))
    {
        a = trim(a);
        if (a.empty())
            continue;
        auto argVal = parseValueToken(a);
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
    if (text.rfind("label ", 0) == 0)
        text = trim(text.substr(6));
    if (!text.empty() && text[0] == '^')
        text = text.substr(1);
    const size_t lp = text.find('(');
    if (lp == std::string::npos)
    {
        label = trim(text);
        return {};
    }
    const size_t rp = text.find(')', lp);
    if (rp == std::string::npos)
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": mismatched ')";
        return Expected<void>{makeError(instr_.loc, oss.str())};
    }
    label = trim(text.substr(0, lp));
    std::string argsStr = text.substr(lp + 1, rp - lp - 1);
    std::stringstream as(argsStr);
    std::string token;
    while (std::getline(as, token, ','))
    {
        token = trim(token);
        if (token.empty())
            continue;
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
    for (size_t idx = 0; idx < expectedTargets; ++idx)
    {
        if (remaining.empty())
        {
            std::ostringstream oss;
            oss << "line " << state_.lineNo << ": malformed " << mnemonic;
            return Expected<void>{makeError(instr_.loc, oss.str())};
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

        if (split < remaining.size())
            remaining = trim(remaining.substr(split + 1));
        else
            remaining.clear();
    }

    if (!trim(remaining).empty())
    {
        std::ostringstream oss;
        oss << "line " << state_.lineNo << ": malformed " << mnemonic;
        return Expected<void>{makeError(instr_.loc, oss.str())};
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
