//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file SemanticAnalyzer.cpp
/// @brief Core implementation of the BASIC semantic analyzer.
///
/// @details This file contains the main implementation of the BASIC semantic
/// analyzer, including constructor, accessors, and shared utility functions.
/// The semantic analyzer performs type checking, name resolution, and other
/// validation on the BASIC AST before code generation.
///
/// ## Semantic Analysis Overview
///
/// The BASIC semantic analyzer performs several critical validation phases:
///
/// ### Phase 1: Name Resolution
///
/// - Resolves variable and function names to their declarations
/// - Validates namespace and USING directive visibility
/// - Checks for undefined identifiers
///
/// ### Phase 2: Type Checking
///
/// - Validates expression types and operator compatibility
/// - Checks assignment compatibility (type coercion rules)
/// - Validates function call argument types
///
/// ### Phase 3: Control Flow Analysis
///
/// - Ensures all code paths return a value (for functions)
/// - Validates GOTO/GOSUB targets
/// - Checks loop and conditional structure
///
/// ### Phase 4: OOP Validation
///
/// - Validates class inheritance chains
/// - Checks interface implementation completeness
/// - Validates property get/set consistency
///
/// ## Symbol Table Management
///
/// The analyzer maintains several symbol tables:
/// - **symbols_**: Global set of all declared symbols
/// - **varTable_**: Local/global variable mappings
/// - **procReg_**: Procedure (SUB/FUNCTION) registry
///
/// ## Error Reporting
///
/// All semantic errors are reported through the DiagnosticEmitter interface,
/// providing source locations for precise error messages.
///
/// ## Usage
///
/// ```cpp
/// DiagnosticEmitter emitter;
/// SemanticAnalyzer analyzer(emitter);
/// bool success = analyzer.analyze(program);
/// // Check emitter for any errors
/// ```
///
/// @invariant Symbol tables are consistent with analyzed AST.
/// @invariant DiagnosticEmitter is never null during analysis.
/// @invariant AST nodes are not modified during analysis.
///
/// @see SemanticAnalyzer.hpp - Public interface
/// @see SemanticAnalyzer_Internal.hpp - Internal helpers
/// @see DiagnosticEmitter - Error reporting interface
///

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/SemanticAnalyzer_Internal.hpp"

#include "frontends/basic/BasicTypes.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

/// @brief Construct a semantic analyzer bound to a diagnostic emitter.
///
/// @param emitter Diagnostic sink used for reporting semantic errors and warnings.
SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &emitter) : de(emitter), procReg_(de) {}

/// @brief Access the set of symbols tracked during the last analysis run.
///
/// @return Reference to the internal symbol table.
const std::unordered_set<std::string> &SemanticAnalyzer::symbols() const
{
    return symbols_;
}

/// @brief Access the set of labels declared in the analyzed program.
///
/// @return Reference to the label set.
const std::unordered_set<int> &SemanticAnalyzer::labels() const
{
    return labels_;
}

/// @brief Access the set of labels referenced by control-flow statements.
///
/// @return Reference to the label reference set.
const std::unordered_set<int> &SemanticAnalyzer::labelRefs() const
{
    return labelRefs_;
}

/// @brief Expose the registered procedure table.
///
/// @return Reference to the registry of procedure signatures.
const ProcTable &SemanticAnalyzer::procs() const
{
    return procReg_.procs();
}

/// @brief Get canonical lowercase USING import namespaces from file scope.
///
/// @return Vector of imported namespace paths (e.g., "viper.terminal").
std::vector<std::string> SemanticAnalyzer::getUsingImports() const
{
    std::vector<std::string> result;
    if (!usingStack_.empty())
    {
        const UsingScope &scope = usingStack_.back();
        result.reserve(scope.imports.size());
        for (const auto &ns : scope.imports)
        {
            result.push_back(ns);
        }
    }
    return result;
}

/// @brief Lookup array metadata for a given array name.
///
/// @param name Array identifier to look up.
/// @return Pointer to ArrayMetadata if found, nullptr otherwise.
const ArrayMetadata *SemanticAnalyzer::lookupArrayMetadata(const std::string &name) const
{
    if (auto it = arrays_.find(name); it != arrays_.end())
        return &it->second;
    return nullptr;
}

/// @brief Query the inferred type for a variable by canonical name.
///
/// @param name Variable name after scope resolution.
/// @return Inferred type when known; std::nullopt otherwise.
std::optional<SemanticAnalyzer::Type> SemanticAnalyzer::lookupVarType(const std::string &name) const
{
    if (auto it = varTypes_.find(name); it != varTypes_.end())
        return it->second;
    return std::nullopt;
}

/// @brief Query the declared runtime class name for an object variable.
///
/// @param name Variable name after scope resolution.
/// @return Qualified class name when known; std::nullopt otherwise.
std::optional<std::string> SemanticAnalyzer::lookupObjectClassQName(const std::string &name) const
{
    if (auto it = objectClassTypes_.find(name); it != objectClassTypes_.end())
        return it->second;
    return std::nullopt;
}

/// @brief Check if a symbol is registered at module level.
///
/// @details Returns true if the symbol exists in the module-level symbol table.
///          This helps the lowerer distinguish between module-level globals
///          (which should not get procedure-local slots) and procedure-local
///          variables (which need local allocation).
///
/// @param name Symbol identifier to check.
/// @return True when the symbol is registered at module scope.
bool SemanticAnalyzer::isModuleLevelSymbol(const std::string &name) const
{
    return symbols_.contains(name);
}

/// @brief Check if a symbol is a module-level CONST.
///
/// @details Returns true if the symbol was declared with CONST at module level.
///          This is used by the lowerer to distinguish between CONST references
///          (which should always read from rt_modvar) and local variables
///          (which may shadow CONSTs but use local storage for writes).
///
/// @param name Symbol identifier to check.
/// @return True when the symbol is a module-level CONST.
bool SemanticAnalyzer::isConstSymbol(const std::string &name) const
{
    return constants_.contains(name);
}

/// @brief Resolve a symbol through the scope stack and update default type tracking.
///
/// @details First searches local scopes via the scope tracker. If not found locally
///          and we're in a procedure scope, checks if the name exists at module level.
///          This enables procedures to access module-level globals without explicit
///          parameters. Local declarations shadow module globals.
///
/// @param name Symbol name, updated in-place when aliasing occurs.
/// @param kind Classification describing how the symbol is being used.
void SemanticAnalyzer::resolveAndTrackSymbol(std::string &name, SymbolKind kind)
{
    if (auto mapped = scopes_.resolve(name))
    {
        name = *mapped;
    }
    else if (scopes_.hasScope())
    {
        if (symbols_.contains(name))
        {
            // Name exists at module level and is not shadowed by a local.
            // Keep the original name - it will resolve to the module-level variable.
            // No action needed, name stays unchanged.
        }
    }

    if (kind == SymbolKind::Reference)
        return;

    auto insertResult = symbols_.insert(name);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteSymbolInserted(name);

    // BUG-093 fix: Do not override explicitly declared types (from DIM) when processing
    // INPUT statements. Only set default type if the variable has no type yet.
    auto itType = varTypes_.find(name);
    if (itType == varTypes_.end())
    {
        Type defaultType = Type::Int;
        if (!name.empty())
        {
            if (name.back() == '$')
                defaultType = Type::String;
            else if (name.back() == '#' || name.back() == '!')
                defaultType = Type::Float;
        }
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            activeProcScope_->noteVarTypeMutation(name, previous);
        }
        varTypes_[name] = defaultType;
    }
}

} // namespace il::frontends::basic

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Compute the Levenshtein edit distance between two strings.
///
/// @param a First string.
/// @param b Second string.
/// @return Minimum number of single-character edits required to transform @p a into @p b.
size_t levenshtein(const std::string &a, const std::string &b)
{
    const size_t m = a.size();
    const size_t n = b.size();
    std::vector<size_t> prev(n + 1), cur(n + 1);
    for (size_t j = 0; j <= n; ++j)
        prev[j] = j;
    for (size_t i = 1; i <= m; ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= n; ++j)
        {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[n];
}

/// @brief Translate an AST numeric type into the analyzer's semantic type enum.
///
/// @param ty BASIC AST type enumerator.
/// @return Equivalent semantic type classification.
SemanticAnalyzer::Type astToSemanticType(::il::frontends::basic::Type ty)
{
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return SemanticAnalyzer::Type::Int;
        case ::il::frontends::basic::Type::F64:
            return SemanticAnalyzer::Type::Float;
        case ::il::frontends::basic::Type::Str:
            return SemanticAnalyzer::Type::String;
        case ::il::frontends::basic::Type::Bool:
            return SemanticAnalyzer::Type::Bool;
    }
    return SemanticAnalyzer::Type::Int;
}

/// @brief Retrieve the textual name associated with a builtin call expression.
///
/// @param b Builtin enumerator identifying the runtime routine.
/// @return Null-terminated string naming the builtin.
const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

/// @brief Convert a semantic type enumerator into a human-readable string.
///
/// @param type Semantic type to render.
/// @return Uppercase string describing the type.
const char *semanticTypeName(SemanticAnalyzer::Type type)
{
    using Type = SemanticAnalyzer::Type;
    switch (type)
    {
        case Type::Int:
            return "INT";
        case Type::Float:
            return "FLOAT";
        case Type::String:
            return "STRING";
        case Type::Bool:
            return "BOOLEAN";
        case Type::ArrayInt:
            return "ARRAY(INT)";
        case Type::ArrayString:
            return "ARRAY(STRING)";
        case Type::Object:
            return "OBJECT";
        case Type::Unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

/// @brief Map a logical operator to the keyword used in diagnostics.
///
/// @param op Logical operator enumerator.
/// @return Keyword name or a placeholder when the operator is not logical.
const char *logicalOpName(BinaryExpr::Op op)
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAndShort:
            return "ANDALSO";
        case BinaryExpr::Op::LogicalOrShort:
            return "ORELSE";
        case BinaryExpr::Op::LogicalAnd:
            return "AND";
        case BinaryExpr::Op::LogicalOr:
            return "OR";
        default:
            break;
    }
    return "<logical>";
}

/// @brief Render a condition expression into a concise textual form.
///
/// @param expr Expression to render.
/// @return String approximating the expression for diagnostics.
std::string conditionExprText(const Expr &expr)
{
    if (auto *var = as<const VarExpr>(expr))
        return var->name;
    if (auto *intExpr = as<const IntExpr>(expr))
        return std::to_string(intExpr->value);
    if (auto *floatExpr = as<const FloatExpr>(expr))
    {
        std::ostringstream oss;
        oss << floatExpr->value;
        return oss.str();
    }
    if (auto *boolExpr = as<const BoolExpr>(expr))
        return boolExpr->value ? "TRUE" : "FALSE";
    if (auto *strExpr = as<const StringExpr>(expr))
    {
        std::string text = "\"";
        text += strExpr->value;
        text += '"';
        return text;
    }
    return {};
}

/// @brief Derive the BASIC return type implied by the trailing name suffix.
///
/// @param name Procedure name potentially ending in a BASIC type sigil.
/// @return The matching @ref BasicType when the suffix is recognised.
std::optional<BasicType> suffixBasicType(std::string_view name)
{
    if (name.empty())
        return std::nullopt;
    switch (name.back())
    {
        case '$':
            return BasicType::String;
        case '#':
            return BasicType::Float;
        case '%':
            return BasicType::Int;
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Translate a BASIC return annotation into the analyzer's semantic type.
///
/// @param type BASIC-level return type (from an AS clause).
/// @return The corresponding semantic type when one exists; otherwise nullopt.
std::optional<SemanticAnalyzer::Type> semanticTypeFromBasic(BasicType type)
{
    using Type = SemanticAnalyzer::Type;
    switch (type)
    {
        case BasicType::Int:
            return Type::Int;
        case BasicType::Float:
            return Type::Float;
        case BasicType::String:
            return Type::String;
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Uppercase helper for printing BASIC type names in diagnostics.
///
/// @param type BASIC type whose name should be formatted.
/// @return Uppercase representation of @p type suitable for user output.
std::string uppercaseBasicTypeName(BasicType type)
{
    std::string text{toString(type)};
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

/// @brief Identify whether a semantic type should be treated as numeric.
///
/// @param type Semantic analyzer type to classify.
/// @return True when @p type denotes a numeric category.
bool isNumericSemanticType(SemanticAnalyzer::Type type) noexcept
{
    using Type = SemanticAnalyzer::Type;
    return type == Type::Int || type == Type::Float || type == Type::Bool;
}

} // namespace il::frontends::basic::semantic_analyzer_detail
