//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ZiaReplAdapter.cpp
// Purpose: Zia REPL adapter implementation. Compiles each input as a fresh
//          IL module using the Zia compiler and executes via BytecodeVM.
// Key invariants:
//   - buildSource() constructs a complete, compilable Zia program.
//   - Session state is only updated after successful compilation+execution.
//   - Variable persistence across inputs via statement replay in start().
//   - Expression auto-print tries Bool/Int/Num/String wrappers in order.
// Ownership/Lifetime:
//   - Each eval() creates a fresh SourceManager, Module, BytecodeVM.
//   - RtContext is a global singleton that persists across calls.
// Links: src/repl/ZiaReplAdapter.hpp, src/frontends/zia/Compiler.hpp
//
//===----------------------------------------------------------------------===//

#include "ZiaReplAdapter.hpp"

#include "ReplOutputCapture.hpp"

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "frontends/zia/Compiler.hpp"
#include "il/io/Serializer.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace viper::repl {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Skip leading whitespace and return the trimmed view start offset.
static size_t skipWhitespace(const std::string &s, size_t pos = 0) {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
        ++pos;
    return pos;
}

/// @brief Check if a string starts with a keyword followed by space or end.
static bool startsWithKeyword(const std::string &s, size_t offset, const char *kw) {
    size_t kwLen = std::strlen(kw);
    if (s.size() - offset < kwLen)
        return false;
    if (s.compare(offset, kwLen, kw) != 0)
        return false;
    // Must be followed by space, '(', '{', or end of string
    if (offset + kwLen < s.size()) {
        char next = s[offset + kwLen];
        return std::isspace(static_cast<unsigned char>(next)) || next == '(' || next == '{';
    }
    return true; // keyword at end of string
}

/// @brief Return true when @p c can appear in a Zia identifier.
/// @param c Character to classify.
/// @return True for ASCII letters, digits, and underscores.
static bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// @brief Find a top-level assignment operator in a Zia input line.
/// @details String literals and nested delimiters are ignored. Comparison operators
///          (`==`, `!=`, `<=`, `>=`) are deliberately skipped so expressions do not
///          get misclassified as statements.
/// @param input Source text to inspect.
/// @return Offset of the assignment operator, or `std::string::npos` when absent.
static size_t findTopLevelAssignmentOperator(const std::string &input) {
    bool inString = false;
    bool escape = false;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;

    for (size_t pos = 0; pos < input.size(); ++pos) {
        char c = input[pos];
        if (escape) {
            escape = false;
            continue;
        }
        if (inString && c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (inString)
            continue;

        if (c == '(') {
            ++parenDepth;
            continue;
        }
        if (c == ')' && parenDepth > 0) {
            --parenDepth;
            continue;
        }
        if (c == '[') {
            ++bracketDepth;
            continue;
        }
        if (c == ']' && bracketDepth > 0) {
            --bracketDepth;
            continue;
        }
        if (c == '{') {
            ++braceDepth;
            continue;
        }
        if (c == '}' && braceDepth > 0) {
            --braceDepth;
            continue;
        }

        if (c != '=' || parenDepth != 0 || bracketDepth != 0 || braceDepth != 0)
            continue;

        char prev = pos > 0 ? input[pos - 1] : '\0';
        char next = pos + 1 < input.size() ? input[pos + 1] : '\0';
        if (prev == '=' || prev == '!' || prev == '<' || prev == '>' || next == '=')
            continue;
        return pos;
    }

    return std::string::npos;
}

/// @brief Auto-append semicolon if input doesn't end with ; or }
static void appendAutoSemicolon(std::string &src, const std::string &input) {
    size_t lastNonSpace = input.find_last_not_of(" \t\r\n");
    if (lastNonSpace != std::string::npos && input[lastNonSpace] != ';' &&
        input[lastNonSpace] != '}') {
        src += ";";
    }
}

/// @brief Return a source literal that default-initializes @p type.
/// @param type Source-level Zia type name.
/// @return Default expression text, or empty when no safe literal is known.
static std::string defaultExprForZiaType(const std::string &type) {
    if (type == "Integer")
        return "0";
    if (type == "Number")
        return "0.0";
    if (type == "Boolean")
        return "false";
    if (type == "String")
        return "\"\"";
    return "";
}

// ---------------------------------------------------------------------------
// Construction / Reset
// ---------------------------------------------------------------------------

ZiaReplAdapter::ZiaReplAdapter() {
    bindStatements_.push_back("bind Viper.Terminal");
    bindStatements_.push_back("bind Viper.Text.Fmt as Fmt");
    bindStatements_.push_back("bind Viper.Core.Object as Obj");
}

std::string_view ZiaReplAdapter::languageName() const {
    return "zia";
}

void ZiaReplAdapter::reset() {
    bindStatements_.clear();
    definedFunctions_.clear();
    definedTypes_.clear();
    persistentVars_.clear();
    globalVarDecls_.clear();

    bindStatements_.push_back("bind Viper.Terminal");
    bindStatements_.push_back("bind Viper.Text.Fmt as Fmt");
    bindStatements_.push_back("bind Viper.Core.Object as Obj");
}

// ---------------------------------------------------------------------------
// Persistent variable management
// ---------------------------------------------------------------------------

PersistentVar *ZiaReplAdapter::findPersistentVar(const std::string &name) {
    for (auto &pv : persistentVars_)
        if (pv.name == name)
            return &pv;
    return nullptr;
}

const PersistentVar *ZiaReplAdapter::findPersistentVar(const std::string &name) const {
    for (const auto &pv : persistentVars_)
        if (pv.name == name)
            return &pv;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Input classification
// ---------------------------------------------------------------------------

bool ZiaReplAdapter::isBind(const std::string &input) const {
    size_t start = skipWhitespace(input);
    return startsWithKeyword(input, start, "bind");
}

bool ZiaReplAdapter::isFuncDef(const std::string &input) const {
    size_t start = skipWhitespace(input);
    return startsWithKeyword(input, start, "func");
}

bool ZiaReplAdapter::isTypeDef(const std::string &input) const {
    size_t start = skipWhitespace(input);
    return startsWithKeyword(input, start, "class") || startsWithKeyword(input, start, "struct") ||
           startsWithKeyword(input, start, "interface");
}

bool ZiaReplAdapter::isVarDecl(const std::string &input) const {
    size_t start = skipWhitespace(input);
    return startsWithKeyword(input, start, "var");
}

bool ZiaReplAdapter::isAssignment(const std::string &input) const {
    size_t pos = skipWhitespace(input);
    if (pos >= input.size() ||
        !(std::isalpha(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        return false;

    // Read identifier
    size_t idStart = pos;
    while (pos < input.size() && isIdentifierChar(input[pos]))
        ++pos;
    std::string ident = input.substr(idStart, pos - idStart);

    if (findTopLevelAssignmentOperator(input) == std::string::npos)
        return false;

    // Must assign to a known persistent root variable, including member/index writes.
    return findPersistentVar(ident) != nullptr;
}

std::string ZiaReplAdapter::extractAssignTarget(const std::string &input) const {
    size_t pos = skipWhitespace(input);
    size_t idStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    return input.substr(idStart, pos - idStart);
}

bool ZiaReplAdapter::isLikelyExpression(const std::string &input) const {
    size_t start = skipWhitespace(input);
    if (start >= input.size())
        return false;

    // Statement keywords — these are never expressions
    static const char *stmtKeywords[] = {"var",
                                         "func",
                                         "class",
                                         "struct",
                                         "interface",
                                         "if",
                                         "while",
                                         "for",
                                         "return",
                                         "bind",
                                         "module",
                                         "throw",
                                         "try",
                                         "break",
                                         "continue"};
    for (const char *kw : stmtKeywords) {
        if (startsWithKeyword(input, start, kw))
            return false;
    }

    // Already printing — no need to auto-print
    if (startsWithKeyword(input, start, "Say") || startsWithKeyword(input, start, "Print") ||
        startsWithKeyword(input, start, "SayInt") || startsWithKeyword(input, start, "PrintInt"))
        return false;

    // Assignment to a known variable — not an expression
    if (isAssignment(input))
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Name / type extraction
// ---------------------------------------------------------------------------

std::string ZiaReplAdapter::extractFuncName(const std::string &input) const {
    size_t pos = input.find("func ");
    if (pos == std::string::npos)
        return "";
    pos += 5;
    pos = skipWhitespace(input, pos);
    size_t nameStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    return input.substr(nameStart, pos - nameStart);
}

std::string ZiaReplAdapter::extractTypeName(const std::string &input) const {
    size_t pos = 0;
    if (input.find("class ") != std::string::npos)
        pos = input.find("class ") + 6;
    else if (input.find("struct ") != std::string::npos)
        pos = input.find("struct ") + 7;
    else if (input.find("interface ") != std::string::npos)
        pos = input.find("interface ") + 10;
    else
        return "";

    pos = skipWhitespace(input, pos);
    size_t nameStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    return input.substr(nameStart, pos - nameStart);
}

std::pair<std::string, std::string> ZiaReplAdapter::extractVarInfo(const std::string &input) const {
    size_t pos = input.find("var ");
    if (pos == std::string::npos)
        return {"", ""};

    pos += 4;
    pos = skipWhitespace(input, pos);

    size_t nameStart = pos;
    while (pos < input.size() &&
           (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        ++pos;
    std::string name = input.substr(nameStart, pos - nameStart);

    pos = skipWhitespace(input, pos);

    std::string type = "auto";
    if (pos < input.size() && input[pos] == ':') {
        ++pos;
        pos = skipWhitespace(input, pos);
        size_t typeStart = pos;
        while (pos < input.size() && !std::isspace(static_cast<unsigned char>(input[pos])) &&
               input[pos] != '=' && input[pos] != ';')
            ++pos;
        type = input.substr(typeStart, pos - typeStart);
    }

    return {name, type};
}

std::string ZiaReplAdapter::extractVarInitializer(const std::string &input) const {
    bool inString = false;
    bool escape = false;
    for (size_t pos = 0; pos < input.size(); ++pos) {
        char c = input[pos];
        if (escape) {
            escape = false;
            continue;
        }
        if (inString && c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (!inString && c == '=') {
            std::string init = input.substr(pos + 1);
            size_t start = skipWhitespace(init);
            init.erase(0, start);
            while (!init.empty() &&
                   (init.back() == ';' || std::isspace(static_cast<unsigned char>(init.back()))))
                init.pop_back();
            return init;
        }
    }
    return "";
}

std::string ZiaReplAdapter::inferPersistentVarType(const std::string &input,
                                                   const std::string &explicitType,
                                                   const std::string &initializer) {
    if (!explicitType.empty() && explicitType != "auto")
        return explicitType;

    size_t pos = skipWhitespace(initializer);
    if (startsWithKeyword(initializer, pos, "new")) {
        pos = skipWhitespace(initializer, pos + 3);
        size_t typeStart = pos;
        while (pos < initializer.size() &&
               (std::isalnum(static_cast<unsigned char>(initializer[pos])) ||
                initializer[pos] == '_' || initializer[pos] == '.'))
            ++pos;
        if (pos > typeStart)
            return initializer.substr(typeStart, pos - typeStart);
    }

    if (!initializer.empty()) {
        std::string probed = getExprType(initializer);
        if (probed == "Boolean" || probed == "Integer" || probed == "Number" ||
            probed == "String")
            return probed;
    }

    (void)input;
    return "auto";
}

std::string ZiaReplAdapter::makePersistentVarDecl(const std::string &name,
                                                  const std::string &type) const {
    if (type.empty() || type == "auto")
        return "";
    return "var " + name + ": " + type;
}

// ---------------------------------------------------------------------------
// Source building
// ---------------------------------------------------------------------------

std::string ZiaReplAdapter::buildSource(const std::string &input,
                                        const std::string &extraTopLevel) const {
    std::string src;
    src.reserve(2048);

    src += "module Repl;\n\n";

    // Bind statements
    for (const auto &b : bindStatements_) {
        src += b;
        src += ";\n";
    }
    src += "\n";

    // Type definitions
    for (const auto &[name, typeSrc] : definedTypes_) {
        src += typeSrc;
        src += "\n\n";
    }

    // Function definitions
    for (const auto &[name, funcSrc] : definedFunctions_) {
        src += funcSrc;
        src += "\n\n";
    }

    // Persistent REPL variables live as module globals so runtime storage
    // persists across fresh VM/module instances without replaying initializers.
    for (const auto &pv : persistentVars_) {
        if (!pv.declStatement.empty()) {
            src += pv.declStatement;
            appendAutoSemicolon(src, pv.declStatement);
            src += "\n";
        }
    }

    if (!extraTopLevel.empty()) {
        src += extraTopLevel;
        appendAutoSemicolon(src, extraTopLevel);
        src += "\n";
    }

    // Entry point
    src += "func start() {\n";

    // Current user input
    if (!input.empty()) {
        src += "    ";
        src += input;
        appendAutoSemicolon(src, input);
        src += "\n";
    }

    src += "}\n";

    return src;
}

// ---------------------------------------------------------------------------
// Compilation helpers
// ---------------------------------------------------------------------------

bool ZiaReplAdapter::tryCompileOnly(const std::string &source) const {
    using namespace il::frontends::zia;
    il::support::SourceManager sm;
    CompilerOptions opts;
    auto compileResult = compile({source, "<repl>"}, opts, sm);
    if (!compileResult.succeeded())
        return false;
    auto verification = il::verify::Verifier::verify(compileResult.module);
    return verification.hasValue();
}

std::string ZiaReplAdapter::compileOnlyDiagnostic(const std::string &source) const {
    using namespace il::frontends::zia;

    il::support::SourceManager sm;
    CompilerOptions opts;
    auto compileResult = compile({source, "<repl>"}, opts, sm);
    if (!compileResult.succeeded()) {
        std::ostringstream errStream;
        compileResult.diagnostics.printAll(errStream, &sm);
        return errStream.str();
    }

    auto verification = il::verify::Verifier::verify(compileResult.module);
    if (!verification)
        return "Type error: " + verification.error().message;
    return "";
}

EvalResult ZiaReplAdapter::compileAndRun(const std::string &source) {
    using namespace il::frontends::zia;

    EvalResult result;

    il::support::SourceManager sm;
    CompilerOptions opts;
    auto compileResult = compile({source, "<repl>"}, opts, sm);

    if (!compileResult.succeeded()) {
        std::ostringstream errStream;
        compileResult.diagnostics.printAll(errStream, &sm);
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
    viper::bytecode::BytecodeCompiler bcCompiler;
    viper::bytecode::BytecodeModule bcModule = bcCompiler.compile(compileResult.module);

    viper::bytecode::BytecodeVM bcVm;
    bcVm.setThreadedDispatch(true);
    bcVm.setRuntimeBridgeEnabled(true);
    bcVm.load(&bcModule);

    ScopedReplOutputCapture outputCapture;
    bcVm.exec("main", {});

    if (bcVm.state() == viper::bytecode::VMState::Trapped) {
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

EvalResult ZiaReplAdapter::eval(const std::string &input) {
    using namespace il::frontends::zia;

    EvalResult result;

    // --- Bind statements ---
    if (isBind(input)) {
        std::string testBind = input;
        while (!testBind.empty() && (testBind.back() == ';' || testBind.back() == ' '))
            testBind.pop_back();

        for (const auto &existing : bindStatements_) {
            if (existing == testBind) {
                result.success = true;
                result.output = "(already bound)";
                return result;
            }
        }

        bindStatements_.push_back(testBind);
        std::string testSrc = buildSource("");
        if (!tryCompileOnly(testSrc)) {
            bindStatements_.pop_back();
            result.success = false;
            result.errorMessage = compileOnlyDiagnostic(testSrc);
            return result;
        }

        result.success = true;
        return result;
    }

    // --- Function definitions ---
    if (isFuncDef(input)) {
        std::string funcName = extractFuncName(input);
        if (funcName.empty()) {
            result.success = false;
            result.errorMessage = "Could not parse function name.";
            return result;
        }

        auto oldFunc = definedFunctions_.find(funcName);
        std::string oldFuncSrc;
        if (oldFunc != definedFunctions_.end())
            oldFuncSrc = oldFunc->second;

        definedFunctions_[funcName] = input;
        std::string testSrc = buildSource("");
        if (!tryCompileOnly(testSrc)) {
            if (oldFuncSrc.empty())
                definedFunctions_.erase(funcName);
            else
                definedFunctions_[funcName] = oldFuncSrc;

            result.success = false;
            result.errorMessage = compileOnlyDiagnostic(testSrc);
            return result;
        }

        result.success = true;
        return result;
    }

    // --- Type definitions ---
    if (isTypeDef(input)) {
        std::string typeName = extractTypeName(input);
        if (typeName.empty()) {
            result.success = false;
            result.errorMessage = "Could not parse type name.";
            return result;
        }

        auto oldType = definedTypes_.find(typeName);
        std::string oldTypeSrc;
        if (oldType != definedTypes_.end())
            oldTypeSrc = oldType->second;

        definedTypes_[typeName] = input;
        std::string testSrc = buildSource("");
        if (!tryCompileOnly(testSrc)) {
            if (oldTypeSrc.empty())
                definedTypes_.erase(typeName);
            else
                definedTypes_[typeName] = oldTypeSrc;

            result.success = false;
            result.errorMessage = compileOnlyDiagnostic(testSrc);
            return result;
        }

        result.success = true;
        return result;
    }

    // --- Variable declarations ---
    if (isVarDecl(input)) {
        auto [varName, declaredType] = extractVarInfo(input);
        if (varName.empty()) {
            result.success = false;
            result.errorMessage = "Could not parse variable name.";
            return result;
        }

        std::string initializer = extractVarInitializer(input);
        std::string varType = inferPersistentVarType(input, declaredType, initializer);
        std::string globalDecl = makePersistentVarDecl(varName, varType);
        if (globalDecl.empty()) {
            // Fall back to the compiler's diagnostic for unsupported inference cases.
            result = compileAndRun(buildSource(input));
            return result;
        }

        std::string initStatement;
        if (!initializer.empty())
            initStatement = varName + " = " + initializer;
        else if (std::string defaultExpr = defaultExprForZiaType(varType); !defaultExpr.empty())
            initStatement = varName + " = " + defaultExpr;

        auto existing = findPersistentVar(varName);
        std::string oldDecl;
        std::string oldType;
        if (existing) {
            oldDecl = existing->declStatement;
            oldType = existing->type;
            existing->declStatement = globalDecl;
            existing->lastAssignment.clear();
            existing->type = varType;
        }

        std::string source = buildSource(initStatement, existing ? std::string() : globalDecl);
        result = compileAndRun(source);
        if (!result.success) {
            if (existing) {
                existing->declStatement = oldDecl;
                existing->type = oldType;
            }
            return result;
        }

        if (!existing)
            persistentVars_.push_back({varName, globalDecl, "", varType});
        globalVarDecls_[varName] = varType;
        result.resultType = ResultType::Statement;
        return result;
    }

    // --- Expression auto-print ---
    // Try wrapping the input as an expression with different formatters.
    // The wrapper order ensures the most specific type matches first.
    if (isLikelyExpression(input)) {
        struct Wrapper {
            const char *format; // printf-style: %s is the expression
            ResultType type;
        };

        static const Wrapper wrappers[] = {
            {"Say(Fmt.Bool(%s))", ResultType::Boolean},
            {"Say(Fmt.Int(%s))", ResultType::Integer},
            {"Say(Fmt.Num(%s))", ResultType::Number},
            {"Say(%s)", ResultType::String},
            {"Say(Obj.ToString(%s))", ResultType::Object},
        };

        // Strip trailing semicolons from expression for wrapping
        std::string expr = input;
        while (!expr.empty() &&
               (expr.back() == ';' || std::isspace(static_cast<unsigned char>(expr.back()))))
            expr.pop_back();

        for (const auto &w : wrappers) {
            // Build wrapped input: e.g., "Say(Fmt.Int(x + 1))"
            std::string wrapped;
            const char *fmt = w.format;
            const char *pct = std::strstr(fmt, "%s");
            if (pct) {
                wrapped.append(fmt, pct);
                wrapped += expr;
                wrapped += (pct + 2);
            }

            std::string testSource = buildSource(wrapped);
            if (tryCompileOnly(testSource)) {
                // Compile and execute with this wrapper
                result = compileAndRun(testSource);
                if (result.success)
                    result.resultType = w.type;
                return result;
            }
        }

        // No wrapper matched — fall through to bare statement execution
    }

    // --- Variable assignment persistence ---
    if (isAssignment(input)) {
        std::string target = extractAssignTarget(input);
        PersistentVar *pv = findPersistentVar(target);
        if (pv) {
            (void)pv;
            result = compileAndRun(buildSource(input));
            return result;
        }
    }

    // --- Regular statement / expression: compile and execute ---
    std::string source = buildSource(input);
    result = compileAndRun(source);

    // Track new variable declarations on success
    if (result.success && isVarDecl(input)) {
        auto [varName, varType] = extractVarInfo(input);
        if (!varName.empty()) {
            // Strip trailing semicolons from the declaration for storage
            std::string cleanDecl = input;
            while (!cleanDecl.empty() &&
                   (cleanDecl.back() == ';' ||
                    std::isspace(static_cast<unsigned char>(cleanDecl.back()))))
                cleanDecl.pop_back();

            // Add to persistent state first (so buildSource includes it)
            PersistentVar *existing = findPersistentVar(varName);
            if (existing) {
                existing->declStatement = cleanDecl;
                existing->lastAssignment.clear();
                existing->type = varType;
            } else {
                persistentVars_.push_back({varName, cleanDecl, "", varType});
            }

            // Now probe the actual type if it was inferred
            if (varType == "auto") {
                varType = getExprType(varName);
                // Update the stored type
                PersistentVar *pv = findPersistentVar(varName);
                if (pv)
                    pv->type = varType;
            }

            globalVarDecls_[varName] = varType;
        }
    }

    if (result.success)
        result.resultType = ResultType::Statement;

    return result;
}

// ---------------------------------------------------------------------------
// .type meta-command
// ---------------------------------------------------------------------------

std::string ZiaReplAdapter::getExprType(const std::string &expr) {
    // Try each type wrapper to determine the expression's type.
    // The first wrapper that compiles successfully reveals the type.
    std::string cleanExpr = expr;
    while (!cleanExpr.empty() &&
           (cleanExpr.back() == ';' || std::isspace(static_cast<unsigned char>(cleanExpr.back()))))
        cleanExpr.pop_back();

    struct TypeProbe {
        const char *format;
        const char *typeName;
    };

    static const TypeProbe probes[] = {
        {"Say(Fmt.Bool(%s))", "Boolean"},
        {"Say(Fmt.Int(%s))", "Integer"},
        {"Say(Fmt.Num(%s))", "Number"},
        {"Say(%s)", "String"},
        {"Say(Obj.ToString(%s))", "Object"},
    };

    for (const auto &p : probes) {
        std::string wrapped;
        const char *pct = std::strstr(p.format, "%s");
        if (pct) {
            wrapped.append(p.format, pct);
            wrapped += cleanExpr;
            wrapped += (pct + 2);
        }

        std::string testSource = buildSource(wrapped);
        if (tryCompileOnly(testSource))
            return p.typeName;
    }

    // Try compiling as a void statement (no return value)
    std::string testSource = buildSource(cleanExpr);
    if (tryCompileOnly(testSource))
        return "Void";

    return "(unknown — expression does not compile)";
}

// ---------------------------------------------------------------------------
// Tab completion
// ---------------------------------------------------------------------------

std::string ZiaReplAdapter::buildSourceForCompletion(const std::string &input,
                                                     size_t cursor,
                                                     int &line,
                                                     int &col) const {
    std::string src;
    src.reserve(2048);

    src += "module Repl;\n\n";

    for (const auto &b : bindStatements_) {
        src += b;
        src += ";\n";
    }
    src += "\n";

    for (const auto &[name, typeSrc] : definedTypes_) {
        src += typeSrc;
        src += "\n\n";
    }

    for (const auto &[name, funcSrc] : definedFunctions_) {
        src += funcSrc;
        src += "\n\n";
    }

    for (const auto &pv : persistentVars_) {
        if (!pv.declStatement.empty()) {
            src += pv.declStatement;
            appendAutoSemicolon(src, pv.declStatement);
            src += "\n";
        }
    }
    src += "\n";

    src += "func start() {\n";

    // Count newlines to find the line where the user input begins
    int inputLine = 1;
    for (char c : src) {
        if (c == '\n')
            ++inputLine;
    }

    // Add user input (only up to the source length, completion engine needs full source)
    src += "    ";
    src += input;
    appendAutoSemicolon(src, input);
    src += "\n";
    src += "}\n";

    // The cursor is at (inputLine, 4 + cursor) — 4 for the "    " indent
    line = inputLine;
    col = 4 + static_cast<int>(cursor);

    return src;
}

std::vector<std::string> ZiaReplAdapter::complete(const std::string &input, size_t cursor) {
    using namespace il::frontends::zia;

    std::vector<std::string> matches;
    cursor = std::min(cursor, input.size());

    if (input.empty())
        return matches;

    // Find the prefix being completed: scan back from cursor for identifier chars
    size_t tokenStart = cursor;
    while (tokenStart > 0 && (std::isalnum(static_cast<unsigned char>(input[tokenStart - 1])) ||
                              input[tokenStart - 1] == '_'))
        --tokenStart;

    std::string prefix = input.substr(tokenStart, cursor - tokenStart);
    std::string beforeToken = input.substr(0, tokenStart);
    std::string afterCursor = (cursor < input.size()) ? input.substr(cursor) : "";

    // Use CompletionEngine for rich completions (member access, types, runtime)
    int line = 0, col = 0;
    std::string source = buildSourceForCompletion(input, cursor, line, col);
    auto items = completionEngine_.complete(source, line, col, "<repl>", 30);

    // Track which labels we've already added (avoid duplicates)
    std::set<std::string> seen;

    for (const auto &item : items) {
        if (seen.insert(item.insertText).second)
            matches.push_back(beforeToken + item.insertText + afterCursor);
    }

    // Supplement with session variables (local to start(), invisible to CompletionEngine)
    if (!prefix.empty()) {
        for (const auto &pv : persistentVars_) {
            if (pv.name.size() >= prefix.size() && pv.name.compare(0, prefix.size(), prefix) == 0 &&
                seen.insert(pv.name).second) {
                matches.push_back(beforeToken + pv.name + afterCursor);
            }
        }

        // Supplement with session functions
        for (const auto &[name, src] : definedFunctions_) {
            if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
                seen.insert(name).second) {
                matches.push_back(beforeToken + name + afterCursor);
            }
        }
    }

    return matches;
}

// ---------------------------------------------------------------------------
// .il meta-command — show generated IL
// ---------------------------------------------------------------------------

std::string ZiaReplAdapter::getIL(const std::string &input) {
    using namespace il::frontends::zia;

    std::string cleanInput = input;
    while (!cleanInput.empty() && (cleanInput.back() == ';' ||
                                   std::isspace(static_cast<unsigned char>(cleanInput.back()))))
        cleanInput.pop_back();

    static const char *wrappers[] = {
        "Say(Fmt.Bool(%s))",
        "Say(Fmt.Int(%s))",
        "Say(Fmt.Num(%s))",
        "Say(%s)",
        "Say(Obj.ToString(%s))",
    };

    il::support::SourceManager sm;
    CompilerOptions opts;
    il::frontends::zia::CompilerResult compileResult;
    bool compiled = false;

    for (const char *fmt : wrappers) {
        std::string wrapped;
        const char *pct = std::strstr(fmt, "%s");
        if (!pct)
            continue;
        wrapped.append(fmt, pct);
        wrapped += cleanInput;
        wrapped += (pct + 2);

        std::string source = buildSource(wrapped);
        compileResult = compile({source, "<repl>"}, opts, sm);
        if (compileResult.succeeded()) {
            compiled = true;
            break;
        }
    }

    if (!compiled) {
        // Try as bare statement
        std::string source = buildSource(cleanInput);
        il::support::SourceManager sm2;
        compileResult = compile({source, "<repl>"}, opts, sm2);
        if (!compileResult.succeeded()) {
            std::ostringstream errStream;
            compileResult.diagnostics.printAll(errStream, &sm2);
            return "Compilation error: " + errStream.str();
        }
    }

    // Print the IL module
    return il::io::Serializer::toString(compileResult.module);
}

// ---------------------------------------------------------------------------
// Session state queries
// ---------------------------------------------------------------------------

std::vector<VarInfo> ZiaReplAdapter::listVariables() const {
    std::vector<VarInfo> vars;
    for (const auto &pv : persistentVars_) {
        vars.push_back({pv.name, pv.type});
    }
    return vars;
}

std::vector<FuncInfo> ZiaReplAdapter::listFunctions() const {
    std::vector<FuncInfo> funcs;
    for (const auto &[name, src] : definedFunctions_) {
        size_t parenStart = src.find('(');
        size_t braceStart = src.find('{');
        std::string sig;
        if (parenStart != std::string::npos) {
            size_t end = (braceStart != std::string::npos) ? braceStart : src.size();
            sig = src.substr(parenStart, end - parenStart);
            while (!sig.empty() && std::isspace(static_cast<unsigned char>(sig.back())))
                sig.pop_back();
        }
        funcs.push_back({name, sig});
    }
    return funcs;
}

std::vector<std::string> ZiaReplAdapter::listBinds() const {
    return bindStatements_;
}

} // namespace viper::repl
