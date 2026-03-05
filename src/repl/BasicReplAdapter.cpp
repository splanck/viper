//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/BasicReplAdapter.cpp
// Purpose: BASIC REPL adapter implementation. Compiles each input as a fresh
//          IL module using the BASIC compiler and executes via BytecodeVM.
// Key invariants:
//   - buildSource() constructs a complete, compilable BASIC program.
//   - Session state is only updated after successful compilation+execution.
//   - Variable persistence across inputs via DIM replay.
//   - Expression auto-print wraps input with PRINT.
// Ownership/Lifetime:
//   - Each eval() creates a fresh SourceManager, Module, BytecodeVM.
//   - RtContext is a global singleton that persists across calls.
// Links: src/repl/BasicReplAdapter.hpp, src/frontends/basic/BasicCompiler.hpp
//
//===----------------------------------------------------------------------===//

#include "BasicReplAdapter.hpp"

#include "ReplInputClassifier.hpp"

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "il/io/Serializer.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace viper::repl
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Skip leading whitespace and return the offset of the first non-space.
static size_t skipWS(const std::string &s, size_t pos = 0)
{
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
        ++pos;
    return pos;
}

/// @brief Case-insensitive check whether @p s starts with keyword @p kw at @p offset.
///        Keyword must be followed by space, '(', or end of string.
static bool startsWithKW(const std::string &s, size_t offset, const char *kw)
{
    size_t kwLen = std::strlen(kw);
    if (s.size() - offset < kwLen)
        return false;
    for (size_t i = 0; i < kwLen; ++i)
    {
        if (std::toupper(static_cast<unsigned char>(s[offset + i])) !=
            std::toupper(static_cast<unsigned char>(kw[i])))
            return false;
    }
    if (offset + kwLen < s.size())
    {
        char next = s[offset + kwLen];
        return std::isspace(static_cast<unsigned char>(next)) || next == '(';
    }
    return true;
}

// ---------------------------------------------------------------------------
// Construction / Reset
// ---------------------------------------------------------------------------

BasicReplAdapter::BasicReplAdapter() = default;

std::string_view BasicReplAdapter::languageName() const
{
    return "basic";
}

void BasicReplAdapter::reset()
{
    definedProcs_.clear();
    persistentVars_.clear();
}

InputKind BasicReplAdapter::classifyInput(const std::string &input)
{
    return ReplInputClassifier::classifyBasic(input);
}

// ---------------------------------------------------------------------------
// Persistent variable management
// ---------------------------------------------------------------------------

BasicPersistentVar *BasicReplAdapter::findPersistentVar(const std::string &name)
{
    for (auto &pv : persistentVars_)
    {
        // Case-insensitive compare for BASIC
        if (pv.name.size() == name.size())
        {
            bool match = true;
            for (size_t i = 0; i < name.size(); ++i)
            {
                if (std::toupper(static_cast<unsigned char>(pv.name[i])) !=
                    std::toupper(static_cast<unsigned char>(name[i])))
                {
                    match = false;
                    break;
                }
            }
            if (match)
                return &pv;
        }
    }
    return nullptr;
}

const BasicPersistentVar *BasicReplAdapter::findPersistentVar(const std::string &name) const
{
    for (const auto &pv : persistentVars_)
    {
        if (pv.name.size() == name.size())
        {
            bool match = true;
            for (size_t i = 0; i < name.size(); ++i)
            {
                if (std::toupper(static_cast<unsigned char>(pv.name[i])) !=
                    std::toupper(static_cast<unsigned char>(name[i])))
                {
                    match = false;
                    break;
                }
            }
            if (match)
                return &pv;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Input classification
// ---------------------------------------------------------------------------

bool BasicReplAdapter::isSubOrFunc(const std::string &input) const
{
    size_t start = skipWS(input);
    return startsWithKW(input, start, "SUB") || startsWithKW(input, start, "FUNCTION");
}

bool BasicReplAdapter::isDimDecl(const std::string &input) const
{
    size_t start = skipWS(input);
    return startsWithKW(input, start, "DIM");
}

bool BasicReplAdapter::isAssignment(const std::string &input) const
{
    size_t pos = skipWS(input);
    if (pos >= input.size() ||
        !(std::isalpha(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        return false;

    // Read identifier
    size_t idStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    std::string ident = input.substr(idStart, pos - idStart);

    // Skip whitespace
    pos = skipWS(input, pos);

    // Check for = (not ==)
    if (pos >= input.size() || input[pos] != '=')
        return false;
    if (pos + 1 < input.size() && input[pos + 1] == '=')
        return false;

    // Must be a known variable
    return findPersistentVar(ident) != nullptr;
}

bool BasicReplAdapter::isLikelyExpression(const std::string &input) const
{
    size_t start = skipWS(input);
    if (start >= input.size())
        return false;

    // Statement keywords that are never expressions
    static const char *stmtKeywords[] = {
        "DIM",  "IF",    "FOR",   "WHILE",  "DO",   "SUB", "FUNCTION", "SELECT", "CLASS",
        "TYPE", "PRINT", "INPUT", "RETURN", "EXIT", "END", "NEXT",     "WEND",   "LOOP",
        "GOTO", "GOSUB", "ON",    "CALL",   "LET",  "REM", "TRY",      "THROW",  "NAMESPACE"};
    for (const char *kw : stmtKeywords)
    {
        if (startsWithKW(input, start, kw))
            return false;
    }

    // Single-line comment
    if (input[start] == '\'')
        return false;

    // Assignment to known variable
    if (isAssignment(input))
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Name / type extraction
// ---------------------------------------------------------------------------

std::string BasicReplAdapter::extractProcName(const std::string &input) const
{
    size_t pos = skipWS(input);
    // Skip SUB or FUNCTION keyword
    if (startsWithKW(input, pos, "SUB"))
        pos += 3;
    else if (startsWithKW(input, pos, "FUNCTION"))
        pos += 8;
    else
        return "";

    pos = skipWS(input, pos);
    size_t nameStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    return input.substr(nameStart, pos - nameStart);
}

std::pair<std::string, std::string> BasicReplAdapter::extractDimInfo(const std::string &input) const
{
    // DIM name AS Type [= value]
    size_t pos = skipWS(input);
    if (!startsWithKW(input, pos, "DIM"))
        return {"", ""};

    pos += 3;
    pos = skipWS(input, pos);

    size_t nameStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    std::string name = input.substr(nameStart, pos - nameStart);

    pos = skipWS(input, pos);

    std::string type = "Variant";
    if (startsWithKW(input, pos, "AS"))
    {
        pos += 2;
        pos = skipWS(input, pos);
        size_t typeStart = pos;
        while (pos < input.size() && !std::isspace(static_cast<unsigned char>(input[pos])) &&
               input[pos] != '=' && input[pos] != '\n')
            ++pos;
        type = input.substr(typeStart, pos - typeStart);
    }

    return {name, type};
}

std::string BasicReplAdapter::extractAssignTarget(const std::string &input) const
{
    size_t pos = skipWS(input);
    size_t idStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    return input.substr(idStart, pos - idStart);
}

// ---------------------------------------------------------------------------
// Source building
// ---------------------------------------------------------------------------

std::string BasicReplAdapter::buildSource(const std::string &input) const
{
    std::string src;
    src.reserve(2048);

    // SUB/FUNCTION definitions go first (they are top-level in BASIC)
    for (const auto &[name, procSrc] : definedProcs_)
    {
        src += procSrc;
        src += "\n\n";
    }

    // Replay persistent variable declarations and assignments
    for (const auto &pv : persistentVars_)
    {
        src += pv.dimStmt;
        src += "\n";

        if (!pv.lastAssign.empty())
        {
            src += pv.lastAssign;
            src += "\n";
        }
    }

    // Current user input as top-level code
    if (!input.empty())
    {
        src += input;
        src += "\n";
    }

    return src;
}

// ---------------------------------------------------------------------------
// Compilation helpers
// ---------------------------------------------------------------------------

bool BasicReplAdapter::tryCompileOnly(const std::string &source) const
{
    using namespace il::frontends::basic;
    il::support::SourceManager sm;
    BasicCompilerOptions opts;
    auto result = compileBasic({source, "<repl>"}, opts, sm);
    if (!result.succeeded())
        return false;
    auto verification = il::verify::Verifier::verify(result.module);
    return verification.hasValue();
}

EvalResult BasicReplAdapter::compileAndRun(const std::string &source)
{
    using namespace il::frontends::basic;

    EvalResult result;

    il::support::SourceManager sm;
    BasicCompilerOptions opts;
    auto compileResult = compileBasic({source, "<repl>"}, opts, sm);

    if (!compileResult.succeeded())
    {
        std::ostringstream errStream;
        if (compileResult.emitter)
            compileResult.emitter->printAll(errStream);
        result.success = false;
        result.errorMessage = errStream.str();
        return result;
    }

    auto verification = il::verify::Verifier::verify(compileResult.module);
    if (!verification)
    {
        result.success = false;
        result.errorMessage = "Type error: " + verification.error().message;
        return result;
    }

    // Compile to bytecode and execute
    viper::bytecode::BytecodeCompiler bcCompiler;
    viper::bytecode::BytecodeModule bcModule = bcCompiler.compile(compileResult.module);

    viper::bytecode::BytecodeVM bcVm;
    bcVm.setThreadedDispatch(true);
    bcVm.setRuntimeBridgeEnabled(true);
    bcVm.load(&bcModule);

    // Capture stdout during execution
    std::fflush(stdout);

    bool captured = false;
    int savedStdout = -1;
    int pipeFds[2] = {-1, -1};

#if defined(__unix__) || defined(__APPLE__)
    if (pipe(pipeFds) == 0)
    {
        savedStdout = dup(STDOUT_FILENO);
        dup2(pipeFds[1], STDOUT_FILENO);
        close(pipeFds[1]);
        pipeFds[1] = -1;
        captured = true;
    }
#elif defined(_WIN32)
    if (_pipe(pipeFds, 65536, _O_BINARY) == 0)
    {
        savedStdout = _dup(_fileno(stdout));
        _dup2(pipeFds[1], _fileno(stdout));
        _close(pipeFds[1]);
        pipeFds[1] = -1;
        captured = true;
    }
#endif

    bcVm.exec("main", {});

    std::string capturedOutput;
    if (captured)
    {
        std::fflush(stdout);

#if defined(__unix__) || defined(__APPLE__)
        dup2(savedStdout, STDOUT_FILENO);
        close(savedStdout);

        char readBuf[4096];
        ssize_t n;
        int flags = fcntl(pipeFds[0], F_GETFL);
        fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK);
        while ((n = read(pipeFds[0], readBuf, sizeof(readBuf))) > 0)
        {
            capturedOutput.append(readBuf, static_cast<size_t>(n));
        }
        close(pipeFds[0]);
#elif defined(_WIN32)
        _dup2(savedStdout, _fileno(stdout));
        _close(savedStdout);

        char readBuf[4096];
        int n;
        while ((n = _read(pipeFds[0], readBuf, sizeof(readBuf))) > 0)
        {
            capturedOutput.append(readBuf, static_cast<size_t>(n));
        }
        _close(pipeFds[0]);
#endif
    }

    if (bcVm.state() == viper::bytecode::VMState::Trapped)
    {
        result.success = false;
        result.trapped = true;
        result.errorMessage = "Runtime error: " + bcVm.trapMessage();
        return result;
    }

    result.success = true;
    result.output = capturedOutput;
    return result;
}

// ---------------------------------------------------------------------------
// eval() — main REPL evaluation entry point
// ---------------------------------------------------------------------------

EvalResult BasicReplAdapter::eval(const std::string &input)
{
    EvalResult result;

    // --- SUB / FUNCTION definitions ---
    if (isSubOrFunc(input))
    {
        std::string procName = extractProcName(input);
        if (procName.empty())
        {
            result.success = false;
            result.errorMessage = "Could not parse SUB/FUNCTION name.";
            return result;
        }

        auto oldProc = definedProcs_.find(procName);
        std::string oldProcSrc;
        if (oldProc != definedProcs_.end())
            oldProcSrc = oldProc->second;

        definedProcs_[procName] = input;
        std::string testSrc = buildSource("");
        if (!tryCompileOnly(testSrc))
        {
            if (oldProcSrc.empty())
                definedProcs_.erase(procName);
            else
                definedProcs_[procName] = oldProcSrc;

            il::support::SourceManager sm;
            il::frontends::basic::BasicCompilerOptions opts;
            auto cr = il::frontends::basic::compileBasic({testSrc, "<repl>"}, opts, sm);
            std::ostringstream errStream;
            if (cr.emitter)
                cr.emitter->printAll(errStream);
            result.success = false;
            result.errorMessage = errStream.str();
            return result;
        }

        result.success = true;
        return result;
    }

    // --- Expression auto-print ---
    // Wrap the input with PRINT to display the result.
    if (isLikelyExpression(input))
    {
        // Strip trailing whitespace
        std::string expr = input;
        while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.back())))
            expr.pop_back();

        std::string wrapped = "PRINT " + expr;
        std::string testSource = buildSource(wrapped);
        if (tryCompileOnly(testSource))
        {
            result = compileAndRun(testSource);
            if (result.success)
                result.resultType = ResultType::Statement;
            return result;
        }

        // Fall through to bare statement execution
    }

    // --- Variable assignment persistence ---
    if (isAssignment(input))
    {
        std::string target = extractAssignTarget(input);
        BasicPersistentVar *pv = findPersistentVar(target);
        if (pv)
        {
            std::string oldAssign = pv->lastAssign;
            std::string cleanInput = input;
            while (!cleanInput.empty() &&
                   std::isspace(static_cast<unsigned char>(cleanInput.back())))
                cleanInput.pop_back();
            pv->lastAssign = cleanInput;

            std::string source = buildSource("");
            result = compileAndRun(source);

            if (!result.success)
            {
                pv->lastAssign = oldAssign;
            }
            return result;
        }
    }

    // --- Regular statement: compile and execute ---
    std::string source = buildSource(input);
    result = compileAndRun(source);

    // Track new variable declarations on success
    if (result.success && isDimDecl(input))
    {
        auto [varName, varType] = extractDimInfo(input);
        if (!varName.empty())
        {
            std::string cleanDecl = input;
            while (!cleanDecl.empty() && std::isspace(static_cast<unsigned char>(cleanDecl.back())))
                cleanDecl.pop_back();

            BasicPersistentVar *existing = findPersistentVar(varName);
            if (existing)
            {
                existing->dimStmt = cleanDecl;
                existing->lastAssign.clear();
                existing->type = varType;
            }
            else
            {
                persistentVars_.push_back({varName, varType, cleanDecl, ""});
            }
        }
    }

    if (result.success)
        result.resultType = ResultType::Statement;

    return result;
}

// ---------------------------------------------------------------------------
// Tab completion (BASIC keyword completion)
// ---------------------------------------------------------------------------

std::vector<std::string> BasicReplAdapter::complete(const std::string &input, size_t cursor)
{
    std::vector<std::string> matches;

    if (input.empty())
        return matches;

    // Find the prefix being completed
    size_t tokenStart = cursor;
    while (tokenStart > 0 && (std::isalnum(static_cast<unsigned char>(input[tokenStart - 1])) ||
                              input[tokenStart - 1] == '_'))
        --tokenStart;

    std::string prefix = input.substr(tokenStart, cursor - tokenStart);
    if (prefix.empty())
        return matches;

    std::string beforeToken = input.substr(0, tokenStart);
    std::string afterCursor = (cursor < input.size()) ? input.substr(cursor) : "";

    // Uppercase prefix for case-insensitive matching
    std::string upperPrefix = prefix;
    for (char &c : upperPrefix)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // BASIC keywords
    static const char *keywords[] = {
        "DIM",      "AS",     "INTEGER",  "STRING", "DOUBLE",  "BOOLEAN",  "IF",     "THEN",
        "ELSE",     "ELSEIF", "END",      "FOR",    "TO",      "STEP",     "NEXT",   "WHILE",
        "WEND",     "DO",     "LOOP",     "UNTIL",  "SUB",     "FUNCTION", "RETURN", "CALL",
        "SELECT",   "CASE",   "PRINT",    "INPUT",  "AND",     "OR",       "NOT",    "TRUE",
        "FALSE",    "MOD",    "EXIT",     "GOTO",   "GOSUB",   "ON",       "CLASS",  "TYPE",
        "PROPERTY", "GET",    "SET",      "NEW",    "NOTHING", "NULL",     "TRY",    "CATCH",
        "FINALLY",  "THROW",  "NAMESPACE"};

    for (const char *kw : keywords)
    {
        std::string kwStr(kw);
        if (kwStr.size() >= upperPrefix.size() &&
            kwStr.compare(0, upperPrefix.size(), upperPrefix) == 0)
        {
            matches.push_back(beforeToken + kwStr + afterCursor);
        }
    }

    // Session variables
    for (const auto &pv : persistentVars_)
    {
        std::string upperName = pv.name;
        for (char &c : upperName)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (upperName.size() >= upperPrefix.size() &&
            upperName.compare(0, upperPrefix.size(), upperPrefix) == 0)
        {
            matches.push_back(beforeToken + pv.name + afterCursor);
        }
    }

    // Session SUBs/FUNCTIONs
    for (const auto &[name, src] : definedProcs_)
    {
        std::string upperName = name;
        for (char &c : upperName)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (upperName.size() >= upperPrefix.size() &&
            upperName.compare(0, upperPrefix.size(), upperPrefix) == 0)
        {
            matches.push_back(beforeToken + name + afterCursor);
        }
    }

    return matches;
}

// ---------------------------------------------------------------------------
// Session state queries
// ---------------------------------------------------------------------------

std::vector<VarInfo> BasicReplAdapter::listVariables() const
{
    std::vector<VarInfo> vars;
    for (const auto &pv : persistentVars_)
    {
        vars.push_back({pv.name, pv.type});
    }
    return vars;
}

std::vector<FuncInfo> BasicReplAdapter::listFunctions() const
{
    std::vector<FuncInfo> funcs;
    for (const auto &[name, src] : definedProcs_)
    {
        // Extract the signature: everything from name to end of first line
        size_t newline = src.find('\n');
        std::string sig = (newline != std::string::npos) ? src.substr(0, newline) : src;
        funcs.push_back({name, sig});
    }
    return funcs;
}

std::vector<std::string> BasicReplAdapter::listBinds() const
{
    // BASIC has no bind system
    return {};
}

} // namespace viper::repl
