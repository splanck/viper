// File: src/il/io/Parser.cpp
// Purpose: Implements parser for IL textual format.
// Key invariants: None.
// Ownership/Lifetime: Parser operates on externally managed strings.
// Links: docs/il-spec.md

#include "il/io/Parser.hpp"

#include "il/io/Lexer.hpp"
#include "il/io/detail/InstructionHandlers.hpp"
#include "il/io/detail/ParserState.hpp"
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using il::core::BasicBlock;
using il::core::Function;
using il::core::Module;
using il::core::Param;
using il::core::Type;

namespace il::io
{

namespace
{

using detail::ParserState;

bool parseInstruction(const std::string &line, ParserState &state, std::ostream &err);
bool parseFunctionHeader(const std::string &header, ParserState &state, std::ostream &err);
bool parseBlockHeader(const std::string &header, ParserState &state, std::ostream &err);
bool parseFunction(std::istream &is, std::string &header, ParserState &state, std::ostream &err);
bool parseModuleHeader(std::istream &is, std::string &line, ParserState &state, std::ostream &err);

/// @brief Parse a single instruction line after optional result assignment.
/// @param line Full textual instruction.
/// @param state Current parser state providing context.
/// @param err Stream receiving diagnostics from opcode handlers.
/// @return True when parsing succeeds; false when errors occur.
bool parseInstruction(const std::string &line, ParserState &state, std::ostream &err)
{
    Function *fn = state.curFn;
    BasicBlock *bb = state.curBB;
    if (!fn || !bb)
    {
        err << "line " << state.lineNo << ": instruction outside block\n";
        return false;
    }

    core::Instr instr;
    instr.loc = state.curLoc;
    std::string work = line;
    if (!work.empty() && work[0] == '%')
    {
        size_t eq = work.find('=');
        if (eq == std::string::npos)
        {
            err << "line " << state.lineNo << ": missing '='\n";
            state.hasError = true;
            return false;
        }
        std::string resultName = Lexer::trim(work.substr(1, eq - 1));
        auto [it, inserted] = state.tempIds.emplace(resultName, state.nextTemp);
        if (inserted)
        {
            if (fn->valueNames.size() <= state.nextTemp)
                fn->valueNames.resize(state.nextTemp + 1);
            fn->valueNames[state.nextTemp] = resultName;
            state.nextTemp++;
        }
        instr.result = it->second;
        work = Lexer::trim(work.substr(eq + 1));
    }

    std::istringstream ss(work);
    std::string opcode;
    ss >> opcode;
    std::string rest;
    std::getline(ss, rest);
    rest = Lexer::trim(rest);

    const auto &handlers = detail::instructionHandlers();
    auto it = handlers.find(opcode);
    if (it == handlers.end())
    {
        err << "line " << state.lineNo << ": unknown opcode " << opcode << "\n";
        return false;
    }
    if (!it->second(rest, instr, state, err))
        return false;

    bb->instructions.push_back(std::move(instr));
    return true;
}

/// @brief Parse a function header and initialize function state.
/// @param header Complete header line beginning with "func".
/// @param state Parser state updated with function metadata.
/// @param err Diagnostic output stream.
/// @return True when the signature is well-formed.
bool parseFunctionHeader(const std::string &header, ParserState &state, std::ostream &err)
{
    size_t at = header.find('@');
    size_t lp = header.find('(', at);
    size_t rp = header.find(')', lp);
    size_t arrow = header.find("->", rp);
    size_t brace = header.find('{', arrow);
    if (arrow == std::string::npos || brace == std::string::npos)
    {
        err << "line " << state.lineNo << ": malformed function header\n";
        state.hasError = true;
        return false;
    }

    std::string name = header.substr(at + 1, lp - at - 1);
    std::string paramsStr = header.substr(lp + 1, rp - lp - 1);
    std::vector<Param> params;
    std::stringstream pss(paramsStr);
    std::string paramToken;
    while (std::getline(pss, paramToken, ','))
    {
        paramToken = Lexer::trim(paramToken);
        if (paramToken.empty())
            continue;
        std::stringstream ps(paramToken);
        std::string ty, nm;
        ps >> ty >> nm;
        if (!ty.empty() && !nm.empty() && nm[0] == '%')
            params.push_back({nm.substr(1), detail::parseType(ty)});
    }

    std::string retStr = Lexer::trim(header.substr(arrow + 2, brace - arrow - 2));
    state.tempIds.clear();
    unsigned idx = 0;
    for (auto &param : params)
    {
        param.id = idx;
        state.tempIds[param.name] = idx;
        ++idx;
    }

    state.m.functions.push_back({name, detail::parseType(retStr), params, {}, {}});
    state.curFn = &state.m.functions.back();
    state.curBB = nullptr;
    state.nextTemp = idx;
    state.curFn->valueNames.resize(state.nextTemp);
    for (auto &param : params)
        state.curFn->valueNames[param.id] = param.name;
    state.blockParamCount.clear();
    state.pendingBrs.clear();
    return true;
}

/// @brief Parse a block label with optional parameter list.
/// @param header Label text, potentially including parameters.
/// @param state Parser state tracking SSA numbering.
/// @param err Diagnostics stream for reporting malformations.
/// @return True if the header is valid.
bool parseBlockHeader(const std::string &header, ParserState &state, std::ostream &err)
{
    size_t lp = header.find('(');
    std::vector<Param> blockParams;
    std::string label = Lexer::trim(header);
    if (lp != std::string::npos)
    {
        size_t rp = header.find(')', lp);
        if (rp == std::string::npos)
        {
            err << "line " << state.lineNo << ": mismatched ')\n";
            return false;
        }
        label = Lexer::trim(header.substr(0, lp));
        std::string paramsStr = header.substr(lp + 1, rp - lp - 1);
        std::stringstream pss(paramsStr);
        std::string token;
        while (std::getline(pss, token, ','))
        {
            token = Lexer::trim(token);
            if (token.empty())
                continue;
            size_t colon = token.find(':');
            if (colon == std::string::npos)
            {
                err << "line " << state.lineNo << ": bad param\n";
                return false;
            }
            std::string nm = Lexer::trim(token.substr(0, colon));
            if (!nm.empty() && nm[0] == '%')
                nm = nm.substr(1);
            std::string tyStr = Lexer::trim(token.substr(colon + 1));
            bool ok = true;
            Type ty = detail::parseType(tyStr, &ok);
            if (!ok || ty.kind == Type::Kind::Void)
            {
                err << "line " << state.lineNo << ": unknown param type\n";
                return false;
            }
            blockParams.push_back({nm, ty, state.nextTemp});
            state.tempIds[nm] = state.nextTemp;
            if (state.curFn->valueNames.size() <= state.nextTemp)
                state.curFn->valueNames.resize(state.nextTemp + 1);
            state.curFn->valueNames[state.nextTemp] = nm;
            ++state.nextTemp;
        }
    }

    state.curFn->blocks.push_back({label, blockParams, {}, false});
    state.curBB = &state.curFn->blocks.back();
    state.blockParamCount[label] = blockParams.size();
    for (auto it = state.pendingBrs.begin(); it != state.pendingBrs.end();)
    {
        if (it->label == label)
        {
            if (it->args != blockParams.size())
            {
                err << "line " << it->line << ": bad arg count\n";
                return false;
            }
            it = state.pendingBrs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return true;
}

/// @brief Parse a function body after reading its header.
/// @param is Input stream delivering subsequent lines.
/// @param header Previously read function header line.
/// @param state Parser state owning the active function.
/// @param err Stream for diagnostic output.
/// @return True when the function parses without fatal errors.
bool parseFunction(std::istream &is, std::string &header, ParserState &state, std::ostream &err)
{
    if (!parseFunctionHeader(header, state, err))
        return false;

    std::string line;
    while (std::getline(is, line))
    {
        ++state.lineNo;
        line = Lexer::trim(line);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (line[0] == '}')
        {
            state.curFn = nullptr;
            state.curBB = nullptr;
            return true;
        }
        if (line.back() == ':')
        {
            if (!parseBlockHeader(line.substr(0, line.size() - 1), state, err))
                return false;
            continue;
        }
        if (line.rfind(".loc", 0) == 0)
        {
            std::istringstream ls(line.substr(4));
            uint32_t fid = 0, ln = 0, col = 0;
            ls >> fid >> ln >> col;
            state.curLoc = {fid, ln, col};
            continue;
        }
        if (!parseInstruction(line, state, err))
            return false;
    }
    return true;
}

/// @brief Parse top-level module declarations.
/// @param is Input stream for additional lines (used when encountering a function).
/// @param line Current source line to interpret.
/// @param state Parser state capturing module construction.
/// @param err Stream that collects diagnostics.
/// @return True when the declaration is handled successfully.
bool parseModuleHeader(std::istream &is, std::string &line, ParserState &state, std::ostream &err)
{
    if (line.rfind("il", 0) == 0)
    {
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        std::string ver;
        if (ls >> ver)
            state.m.version = ver;
        else
            state.m.version = "0.1.2";
        return true;
    }
    if (line.rfind("extern", 0) == 0)
    {
        size_t at = line.find('@');
        size_t lp = line.find('(', at);
        size_t rp = line.find(')', lp);
        size_t arrow = line.find("->", rp);
        if (arrow == std::string::npos)
        {
            err << "line " << state.lineNo << ": missing '->'\n";
            state.hasError = true;
            return false;
        }
        std::string name = line.substr(at + 1, lp - at - 1);
        std::string paramsStr = line.substr(lp + 1, rp - lp - 1);
        std::vector<Type> params;
        std::stringstream pss(paramsStr);
        std::string token;
        bool typesOk = true;
        while (std::getline(pss, token, ','))
        {
            token = Lexer::trim(token);
            if (token.empty())
                continue;
            bool ok = true;
            Type ty = detail::parseType(token, &ok);
            if (!ok)
            {
                err << "line " << state.lineNo << ": unknown type '" << token << "'\n";
                state.hasError = true;
                typesOk = false;
            }
            else
            {
                params.push_back(ty);
            }
        }
        std::string retStr = Lexer::trim(line.substr(arrow + 2));
        bool retOk = true;
        Type retTy = detail::parseType(retStr, &retOk);
        if (!retOk)
        {
            err << "line " << state.lineNo << ": unknown type '" << retStr << "'\n";
            state.hasError = true;
            typesOk = false;
        }
        if (!typesOk)
            return false;
        state.m.externs.push_back({name, retTy, params});
        return true;
    }
    if (line.rfind("global", 0) == 0)
    {
        size_t at = line.find('@');
        size_t eq = line.find('=', at);
        if (eq == std::string::npos)
        {
            err << "line " << state.lineNo << ": missing '='\n";
            state.hasError = true;
            return false;
        }
        size_t q1 = line.find('"', eq);
        size_t q2 = line.rfind('"');
        std::string name = Lexer::trim(line.substr(at + 1, eq - at - 1));
        std::string init = line.substr(q1 + 1, q2 - q1 - 1);
        state.m.globals.push_back({name, Type(Type::Kind::Str), init});
        return true;
    }
    if (line.rfind("func", 0) == 0)
        return parseFunction(is, line, state, err);

    err << "line " << state.lineNo << ": unexpected line: " << line << "\n";
    return false;
}

} // namespace

bool Parser::parse(std::istream &is, Module &m, std::ostream &err)
{
    ParserState state{m};
    std::string line;
    while (std::getline(is, line))
    {
        ++state.lineNo;
        line = Lexer::trim(line);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (!parseModuleHeader(is, line, state, err))
            return false;
    }
    return !state.hasError;
}

} // namespace il::io
