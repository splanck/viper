//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

#include "ReplOutputCapture.hpp"
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
#include <cstring>
#include <sstream>
#include <string>

namespace zanna::repl {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Skip leading whitespace and return the offset of the first non-space.
static size_t skipWS(const std::string &s, size_t pos = 0) {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
        ++pos;
    return pos;
}

/// @brief Case-insensitive check whether @p s starts with keyword @p kw at @p offset.
///        Keyword must be followed by space, '(', or end of string.
static bool startsWithKW(const std::string &s, size_t offset, const char *kw) {
    size_t kwLen = std::strlen(kw);
    if (s.size() - offset < kwLen)
        return false;
    for (size_t i = 0; i < kwLen; ++i) {
        if (std::toupper(static_cast<unsigned char>(s[offset + i])) !=
            std::toupper(static_cast<unsigned char>(kw[i])))
            return false;
    }
    if (offset + kwLen < s.size()) {
        char next = s[offset + kwLen];
        return std::isspace(static_cast<unsigned char>(next)) || next == '(';
    }
    return true;
}

/// @brief Trim trailing whitespace from @p text in place.
/// @param text String to mutate.
static void trimTrailingWS(std::string &text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
}

/// @brief Compare BASIC identifiers case-insensitively.
/// @param lhs First identifier.
/// @param rhs Second identifier.
/// @return True when both identifiers have the same spelling ignoring case.
static bool basicNameEquals(const std::string &lhs, const std::string &rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(lhs[i])) !=
            std::toupper(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

/// @brief Extract the first identifier from a BASIC statement.
/// @details Handles an optional leading LET before reading the target token.
/// @param input BASIC statement source.
/// @return First target identifier, or empty when none exists.
static std::string extractBasicLeadingIdentifier(const std::string &input) {
    size_t pos = skipWS(input);
    if (startsWithKW(input, pos, "LET"))
        pos = skipWS(input, pos + 3);
    if (pos >= input.size() ||
        !(std::isalpha(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        return "";
    size_t start = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    return input.substr(start, pos - start);
}

/// @brief Extract the variable name from a BASIC DIM replay statement.
/// @param input BASIC statement source.
/// @return DIM variable name, or empty when @p input is not a simple DIM.
static std::string extractBasicDimName(const std::string &input) {
    size_t pos = skipWS(input);
    if (!startsWithKW(input, pos, "DIM"))
        return "";
    pos = skipWS(input, pos + 3);
    if (pos >= input.size() ||
        !(std::isalpha(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        return "";
    size_t start = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    return input.substr(start, pos - start);
}

/// @brief Check whether a replay statement belongs to @p varName.
/// @details Used when a variable is redeclared so old DIM/assignment replay
///          entries do not conflict with the replacement declaration.
/// @param statement Replay statement source.
/// @param varName BASIC variable name being replaced.
/// @return True when the replay entry should be removed.
static bool replayStatementTouchesBasicVar(const std::string &statement,
                                           const std::string &varName) {
    std::string dimName = extractBasicDimName(statement);
    if (!dimName.empty())
        return basicNameEquals(dimName, varName);
    std::string target = extractBasicLeadingIdentifier(statement);
    return !target.empty() && basicNameEquals(target, varName);
}

/// @brief Determine whether a BASIC statement appears to mutate state.
/// @details This shallow scan recognizes LET/assignment forms, including field
///          and indexed targets, while rejecting equality comparisons and common
///          statement-leading keywords.
/// @param input BASIC statement source.
/// @return True when the statement should be replayed for session state.
static bool looksLikeBasicAssignmentStatement(const std::string &input) {
    size_t pos = skipWS(input);
    if (pos >= input.size())
        return false;

    static const char *nonAssignmentKeywords[] = {
        "IF",    "FOR",   "WHILE",  "DO",     "SUB",   "FUNCTION", "SELECT", "CLASS",
        "TYPE",  "PRINT", "INPUT",  "RETURN", "EXIT",  "END",      "NEXT",   "WEND",
        "LOOP",  "GOTO",  "GOSUB",  "ON",     "CALL",  "REM",      "TRY",    "THROW",
        "NAMESPACE"};
    for (const char *kw : nonAssignmentKeywords) {
        if (startsWithKW(input, pos, kw))
            return false;
    }

    if (startsWithKW(input, pos, "LET"))
        pos = skipWS(input, pos + 3);

    bool sawTargetChar = false;
    bool inString = false;
    for (; pos < input.size(); ++pos) {
        char c = input[pos];
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (inString)
            continue;
        if (c == '=') {
            if (pos + 1 < input.size() && input[pos + 1] == '=')
                return false;
            return sawTargetChar;
        }
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '(' ||
            c == ')' || c == '[' || c == ']' || std::isspace(static_cast<unsigned char>(c))) {
            if (!std::isspace(static_cast<unsigned char>(c)))
                sawTargetChar = true;
            continue;
        }
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Construction / Reset
// ---------------------------------------------------------------------------

BasicReplAdapter::BasicReplAdapter() = default;

std::string_view BasicReplAdapter::languageName() const {
    return "basic";
}

void BasicReplAdapter::reset() {
    definedProcs_.clear();
    persistentVars_.clear();
    replayStatements_.clear();
}

InputKind BasicReplAdapter::classifyInput(const std::string &input) {
    return ReplInputClassifier::classifyBasic(input);
}

// ---------------------------------------------------------------------------
// Persistent variable management
// ---------------------------------------------------------------------------

BasicPersistentVar *BasicReplAdapter::findPersistentVar(const std::string &name) {
    for (auto &pv : persistentVars_) {
        // Case-insensitive compare for BASIC
        if (pv.name.size() == name.size()) {
            bool match = true;
            for (size_t i = 0; i < name.size(); ++i) {
                if (std::toupper(static_cast<unsigned char>(pv.name[i])) !=
                    std::toupper(static_cast<unsigned char>(name[i]))) {
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

const BasicPersistentVar *BasicReplAdapter::findPersistentVar(const std::string &name) const {
    for (const auto &pv : persistentVars_) {
        if (pv.name.size() == name.size()) {
            bool match = true;
            for (size_t i = 0; i < name.size(); ++i) {
                if (std::toupper(static_cast<unsigned char>(pv.name[i])) !=
                    std::toupper(static_cast<unsigned char>(name[i]))) {
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

bool BasicReplAdapter::isSubOrFunc(const std::string &input) const {
    size_t start = skipWS(input);
    return startsWithKW(input, start, "SUB") || startsWithKW(input, start, "FUNCTION");
}

bool BasicReplAdapter::isDimDecl(const std::string &input) const {
    size_t start = skipWS(input);
    return startsWithKW(input, start, "DIM");
}

bool BasicReplAdapter::isAssignment(const std::string &input) const {
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

bool BasicReplAdapter::isLikelyExpression(const std::string &input) const {
    size_t start = skipWS(input);
    if (start >= input.size())
        return false;

    // Statement keywords that are never expressions
    static const char *stmtKeywords[] = {
        "DIM",  "IF",    "FOR",   "WHILE",  "DO",   "SUB", "FUNCTION", "SELECT", "CLASS",
        "TYPE", "PRINT", "INPUT", "RETURN", "EXIT", "END", "NEXT",     "WEND",   "LOOP",
        "GOTO", "GOSUB", "ON",    "CALL",   "LET",  "REM", "TRY",      "THROW",  "NAMESPACE"};
    for (const char *kw : stmtKeywords) {
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

std::string BasicReplAdapter::extractProcName(const std::string &input) const {
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

std::pair<std::string, std::string> BasicReplAdapter::extractDimInfo(
    const std::string &input) const {
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
    if (startsWithKW(input, pos, "AS")) {
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

std::string BasicReplAdapter::extractAssignTarget(const std::string &input) const {
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

std::string BasicReplAdapter::buildSource(const std::string &input) const {
    std::string src;
    src.reserve(2048);

    // SUB/FUNCTION definitions go first (they are top-level in BASIC)
    for (const auto &[name, procSrc] : definedProcs_) {
        src += procSrc;
        src += "\n\n";
    }

    // Replay persistent state in chronological order.
    for (const auto &stmt : replayStatements_) {
        src += stmt;
        src += "\n";
    }

    // Current user input as top-level code
    if (!input.empty()) {
        src += input;
        src += "\n";
    }

    return src;
}

// ---------------------------------------------------------------------------
// Compilation helpers
// ---------------------------------------------------------------------------

bool BasicReplAdapter::tryCompileOnly(const std::string &source) const {
    using namespace il::frontends::basic;
    il::support::SourceManager sm;
    BasicCompilerOptions opts;
    auto result = compileBasic({source, "<repl>"}, opts, sm);
    if (!result.succeeded())
        return false;
    auto verification = il::verify::Verifier::verify(result.module);
    return verification.hasValue();
}

std::string BasicReplAdapter::compileOnlyDiagnostic(const std::string &source) const {
    using namespace il::frontends::basic;

    il::support::SourceManager sm;
    BasicCompilerOptions opts;
    auto result = compileBasic({source, "<repl>"}, opts, sm);
    if (!result.succeeded()) {
        std::ostringstream errStream;
        if (result.emitter)
            result.emitter->printAll(errStream);
        return errStream.str();
    }

    auto verification = il::verify::Verifier::verify(result.module);
    if (!verification)
        return "Type error: " + verification.error().message;
    return "";
}

EvalResult BasicReplAdapter::compileAndRun(const std::string &source) {
    using namespace il::frontends::basic;

    EvalResult result;

    il::support::SourceManager sm;
    BasicCompilerOptions opts;
    auto compileResult = compileBasic({source, "<repl>"}, opts, sm);

    if (!compileResult.succeeded()) {
        std::ostringstream errStream;
        if (compileResult.emitter)
            compileResult.emitter->printAll(errStream);
        result.success = false;
        result.errorMessage = errStream.str();
        return result;
    }

    auto verification = il::verify::Verifier::verify(compileResult.module);
    if (!verification) {
        result.success = false;
        result.errorMessage = "Type error: " + verification.error().message;
        return result;
    }

    // Compile to bytecode and execute
    zanna::bytecode::BytecodeCompiler bcCompiler;
    zanna::bytecode::BytecodeModule bcModule = bcCompiler.compile(compileResult.module);

    zanna::bytecode::BytecodeVM bcVm;
    bcVm.setThreadedDispatch(true);
    bcVm.setRuntimeBridgeEnabled(true);
    bcVm.load(&bcModule);

    ScopedReplOutputCapture outputCapture;
    bcVm.exec("main", {});

    if (bcVm.state() == zanna::bytecode::VMState::Trapped) {
        result.success = false;
        result.trapped = true;
        result.errorMessage = "Runtime error: " + bcVm.trapMessage();
        return result;
    }

    result.success = true;
    result.output = outputCapture.output();
    return result;
}

// ---------------------------------------------------------------------------
// eval() — main REPL evaluation entry point
// ---------------------------------------------------------------------------

EvalResult BasicReplAdapter::eval(const std::string &input) {
    EvalResult result;

    // --- SUB / FUNCTION definitions ---
    if (isSubOrFunc(input)) {
        std::string procName = extractProcName(input);
        if (procName.empty()) {
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
        if (!tryCompileOnly(testSrc)) {
            if (oldProcSrc.empty())
                definedProcs_.erase(procName);
            else
                definedProcs_[procName] = oldProcSrc;

            result.success = false;
            result.errorMessage = compileOnlyDiagnostic(testSrc);
            return result;
        }

        result.success = true;
        return result;
    }

    // --- Expression auto-print ---
    // Wrap the input with PRINT to display the result.
    if (isLikelyExpression(input)) {
        // Strip trailing whitespace
        std::string expr = input;
        while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.back())))
            expr.pop_back();

        std::string wrapped = "PRINT " + expr;
        std::string testSource = buildSource(wrapped);
        if (tryCompileOnly(testSource)) {
            result = compileAndRun(testSource);
            if (result.success)
                result.resultType = ResultType::Statement;
            return result;
        }

        // Fall through to bare statement execution
    }

    // --- Variable assignment persistence ---
    if (isAssignment(input)) {
        std::string target = extractAssignTarget(input);
        BasicPersistentVar *pv = findPersistentVar(target);
        if (pv) {
            std::string cleanInput = input;
            trimTrailingWS(cleanInput);

            std::string source = buildSource(cleanInput);
            result = compileAndRun(source);

            if (result.success) {
                pv->lastAssign = cleanInput;
                replayStatements_.push_back(cleanInput);
            }
            return result;
        }
    }

    // --- Regular statement: compile and execute ---
    std::string source = buildSource(input);
    result = compileAndRun(source);

    // Track new variable declarations on success
    if (result.success && isDimDecl(input)) {
        auto [varName, varType] = extractDimInfo(input);
        if (!varName.empty()) {
            std::string cleanDecl = input;
            trimTrailingWS(cleanDecl);

            BasicPersistentVar *existing = findPersistentVar(varName);
            if (existing) {
                existing->dimStmt = cleanDecl;
                existing->lastAssign.clear();
                existing->type = varType;
                replayStatements_.erase(
                    std::remove_if(replayStatements_.begin(),
                                   replayStatements_.end(),
                                   [&](const std::string &stmt) {
                                       return replayStatementTouchesBasicVar(stmt, varName);
                                   }),
                    replayStatements_.end());
            } else {
                persistentVars_.push_back({varName, varType, cleanDecl, ""});
            }
            replayStatements_.push_back(cleanDecl);
        }
    }

    if (result.success)
        result.resultType = ResultType::Statement;

    if (result.success && !isDimDecl(input) && looksLikeBasicAssignmentStatement(input)) {
        std::string cleanInput = input;
        trimTrailingWS(cleanInput);
        replayStatements_.push_back(cleanInput);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Tab completion (BASIC keyword completion)
// ---------------------------------------------------------------------------

std::vector<std::string> BasicReplAdapter::complete(const std::string &input, size_t cursor) {
    std::vector<std::string> matches;
    cursor = std::min(cursor, input.size());

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

    for (const char *kw : keywords) {
        std::string kwStr(kw);
        if (kwStr.size() >= upperPrefix.size() &&
            kwStr.compare(0, upperPrefix.size(), upperPrefix) == 0) {
            matches.push_back(beforeToken + kwStr + afterCursor);
        }
    }

    // Session variables
    for (const auto &pv : persistentVars_) {
        std::string upperName = pv.name;
        for (char &c : upperName)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (upperName.size() >= upperPrefix.size() &&
            upperName.compare(0, upperPrefix.size(), upperPrefix) == 0) {
            matches.push_back(beforeToken + pv.name + afterCursor);
        }
    }

    // Session SUBs/FUNCTIONs
    for (const auto &[name, src] : definedProcs_) {
        std::string upperName = name;
        for (char &c : upperName)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (upperName.size() >= upperPrefix.size() &&
            upperName.compare(0, upperPrefix.size(), upperPrefix) == 0) {
            matches.push_back(beforeToken + name + afterCursor);
        }
    }

    return matches;
}

// ---------------------------------------------------------------------------
// Session state queries
// ---------------------------------------------------------------------------

std::vector<VarInfo> BasicReplAdapter::listVariables() const {
    std::vector<VarInfo> vars;
    for (const auto &pv : persistentVars_) {
        vars.push_back({pv.name, pv.type});
    }
    return vars;
}

std::vector<FuncInfo> BasicReplAdapter::listFunctions() const {
    std::vector<FuncInfo> funcs;
    for (const auto &[name, src] : definedProcs_) {
        // Extract the signature: everything from name to end of first line
        size_t newline = src.find('\n');
        std::string sig = (newline != std::string::npos) ? src.substr(0, newline) : src;
        funcs.push_back({name, sig});
    }
    return funcs;
}

std::vector<std::string> BasicReplAdapter::listBinds() const {
    // BASIC has no bind system
    return {};
}

} // namespace zanna::repl
