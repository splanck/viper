//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lowerer.Procedure.cpp
// Purpose: Implements procedure-level helpers for BASIC lowering including
//          signature caching, variable discovery, and staged emission helpers.
// Key invariants: Procedure helpers operate on the active Lowerer state and do
//                 not leak per-procedure state across invocations.
// Ownership/Lifetime: Operates on a borrowed Lowerer instance.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/ASTUtils.hpp"

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/EmitCommon.hpp"
#include "frontends/basic/LineUtils.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "viper/il/IRBuilder.hpp"

#include "viper/il/Module.hpp"

#include <cassert>
#include <memory>
#include <utility>

#ifdef DEBUG
#include <unordered_set>
#endif

using namespace il::core;

namespace il::frontends::basic
{

using pipeline_detail::coreTypeForAstType;

namespace
{

/// @brief AST walker that records symbol usage within a procedure body.
/// @details Traverses expressions and statements to discover variable references
///          prior to lowering.  Each visit marks the appropriate symbol as
///          referenced and, when necessary, records array-ness so the lowering
///          stage can allocate the correct slot types.  The walker never mutates
///          the AST; it solely updates the owning @ref Lowerer state.
class VarCollectWalker final : public BasicAstWalker<VarCollectWalker>
{
  public:
    /// @brief Create a walker bound to the current lowering instance.
    /// @param lowerer Owning lowering driver whose symbol tables are updated.
    explicit VarCollectWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Record usage of a scalar variable expression.
    /// @details Marks the referenced symbol so a stack slot can be allocated
    ///          later in lowering.  Empty names are ignored because they
    ///          represent parse errors handled elsewhere.
    ///
    /// @param expr Variable expression encountered during traversal.
    void after(const VarExpr &expr)
    {
        if (!expr.name.empty())
        {
            if (lowerer_.isFieldInScope(expr.name))
                return;
            // NOTE: Currently all referenced variables get local slots.
            // TODO: Skip allocation for module-level globals once IL supports
            //       mutable module-level globals (not just string constants).
            lowerer_.markSymbolReferenced(expr.name);
        }
    }

    /// @brief Record usage of an array element expression.
    /// @details Flags both the reference and that the symbol is an array so
    ///          future loads/stores use pointer slots and track bounds metadata
    ///          when enabled.
    /// @param expr Array expression encountered during traversal.
    void after(const ArrayExpr &expr)
    {
        if (!expr.name.empty())
        {
            if (lowerer_.isFieldInScope(expr.name))
                return;
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Record usage of an array lower-bound expression.
    /// @details Treats @p expr as both a variable reference and an indication
    ///          that the symbol participates in array semantics.
    /// @param expr Lower-bound expression inspected by the walker.
    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            if (lowerer_.isFieldInScope(expr.name))
                return;
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Record usage of an array upper-bound expression.
    /// @details Mirrors @ref after(const LBoundExpr &) to keep array metadata in
    ///          sync regardless of which bound is queried first.
    /// @param expr Upper-bound expression inspected by the walker.
    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            if (lowerer_.isFieldInScope(expr.name))
                return;
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    /// @brief Track variables introduced by DIM statements.
    /// @details DIM establishes the declared type and array-ness of a symbol.
    ///          The walker records both pieces of information so later passes do
    ///          not rely on name suffix heuristics.
    /// @param stmt DIM statement encountered in the AST.
    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    /// @brief Track constant declarations.
    /// @details CONST establishes the declared type and initializer of a constant symbol.
    ///          The walker records type information so storage can be allocated.
    ///          Constants are treated as variables with compile-time write protection.
    /// @param stmt CONST statement encountered in the AST.
    void before(const ConstStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
    }

    /// @brief Track STATIC variable declarations.
    /// @details STATIC declares a persistent procedure-local variable with module-level storage.
    ///          The walker records type information for lowering.
    /// @param stmt STATIC statement encountered in the AST.
    void before(const StaticStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markStatic(stmt.name); // Mark as static for special handling
    }

    /// @brief Track variables re-dimensioned at runtime.
    /// @details ReDim only conveys that a symbol is an array; type information is
    ///          left untouched to avoid clobbering DIM declarations.
    /// @param stmt REDIM statement encountered in the AST.
    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    /// @brief Record loop induction variables referenced by FOR statements.
    /// @param stmt FOR statement whose control variable is examined.
    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Record loop induction variables referenced by NEXT statements.
    /// @param stmt NEXT statement paired with a FOR loop.
    void before(const NextStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    /// @brief Record variables that participate in INPUT statements.
    /// @details Each listed name is marked as referenced so lowering allocates a
    ///          slot for user-provided values.
    /// @param stmt INPUT statement containing variable names.
    void before(const InputStmt &stmt)
    {
        for (const auto &name : stmt.vars)
        {
            if (!name.empty() && !lowerer_.isFieldInScope(name))
                lowerer_.markSymbolReferenced(name);
        }
    }

  private:
    Lowerer &lowerer_;
};

} // namespace

/// @brief Aggregate lowering state for a single procedure invocation.
/// @details The context bundles references to the shared @ref Lowerer state,
///          symbol table, and emitter while capturing procedure-specific
///          parameters such as the body statements, configuration hooks, and the
///          IR builder used to materialise code.  Copies of lightweight data
///          (such as the procedure name) are taken so subsequent passes can
///          reference them even if the caller's buffers are reclaimed.
ProcedureLowering::LoweringContext::LoweringContext(
    Lowerer &lowerer,
    std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols,
    il::build::IRBuilder &builder,
    lower::Emitter &emitter,
    std::string name,
    const std::vector<Param> &params,
    const std::vector<StmtPtr> &body,
    const Lowerer::ProcedureConfig &config)
    : lowerer(lowerer), symbols(symbols), builder(builder), emitter(emitter), name(std::move(name)),
      params(params), body(body), config(config)
{
}

/// @brief Ensure a symbol table entry exists for the given name.
/// @details Inserts a default @ref SymbolInfo when the symbol is first observed
///          so subsequent metadata updates can assume the entry exists.  Newly
///          created records start with inferred integer type information and no
///          recorded usage, mirroring BASIC's default variable semantics.
/// @param name BASIC identifier whose symbol information is requested.
/// @return Reference to the mutable symbol record.
Lowerer::SymbolInfo &Lowerer::ensureSymbol(std::string_view name)
{
    std::string key(name);
    auto [it, inserted] = symbols.emplace(std::move(key), SymbolInfo{});
    if (inserted)
    {
        it->second.type = AstType::I64;
        it->second.hasType = false;
        it->second.isArray = false;
        it->second.isBoolean = false;
        it->second.referenced = false;
        it->second.isObject = false;
        it->second.objectClass.clear();
    }
    return it->second;
}

/// @brief Look up a symbol record, creating no new entries.
/// @details Returns @c nullptr when the identifier has not been encountered,
///          allowing callers to treat unknown symbols as implicitly typed.
/// @param name Identifier to probe.
/// @return Mutable symbol metadata or @c nullptr when absent.
Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name)
{
    std::string key(name);
    if (auto it = symbols.find(key); it != symbols.end())
        return &it->second;
    for (auto scopeIt = fieldScopeStack_.rbegin(); scopeIt != fieldScopeStack_.rend(); ++scopeIt)
    {
        auto symIt = scopeIt->symbols.find(key);
        if (symIt != scopeIt->symbols.end())
            return &symIt->second;
    }
    return nullptr;
}

/// @brief Const-qualified symbol lookup helper.
/// @details Mirrors @ref findSymbol(std::string_view) while preserving const
///          correctness for call sites that only need to inspect metadata.
/// @param name Identifier to probe.
/// @return Const symbol metadata or @c nullptr when absent.
const Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) const
{
    std::string key(name);
    if (auto it = symbols.find(key); it != symbols.end())
        return &it->second;
    for (auto scopeIt = fieldScopeStack_.rbegin(); scopeIt != fieldScopeStack_.rend(); ++scopeIt)
    {
        auto symIt = scopeIt->symbols.find(key);
        if (symIt != scopeIt->symbols.end())
            return &symIt->second;
    }
    return nullptr;
}

/// @brief Record the declared type for a symbol and mark it as referenced.
/// @details Updates the symbol information with the explicit AST type and, when
///          applicable, notes that the identifier represents a boolean scalar.
///          Symbols that are later used as arrays have their boolean flag
///          cleared when @ref markArray executes.
/// @param name Identifier whose type is being fixed.
/// @param type AST-declared type for the symbol.
void Lowerer::setSymbolType(std::string_view name, AstType type)
{
    auto &info = ensureSymbol(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
}

/// @brief Record that a symbol denotes an object reference of a specific class.
/// @details Marks the symbol as typed, toggles the object flag so later slot
///          allocation emits pointer storage, and captures the class name for
///          runtime dispatch.
/// @param name Identifier being classified as an object.
/// @param className Fully qualified BASIC class identifier.
void Lowerer::setSymbolObjectType(std::string_view name, std::string className)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

/// @brief Infer variable type from semantic analyzer, then suffix, then fallback.
/// @details BUG-001 FIX: Queries semantic analyzer for value-based type inference
///          before falling back to suffix-based naming convention.
/// @param lowerer Lowerer instance providing access to semantic analyzer.
/// @param name Variable name to query.
/// @return Best-effort type derived from semantic analysis or naming convention.
static Type inferVariableTypeForLowering(const Lowerer &lowerer, std::string_view name)
{
    // Query semantic analyzer for value-based type inference
    if (const auto *sema = lowerer.semanticAnalyzer())
    {
        if (auto semaType = sema->lookupVarType(std::string{name}))
        {
            using SemaType = SemanticAnalyzer::Type;
            switch (*semaType)
            {
                case SemaType::Int:
                    return Type::I64;
                case SemaType::Float:
                    return Type::F64;
                case SemaType::String:
                    return Type::Str;
                case SemaType::Bool:
                    return Type::Bool;
                default:
                    break;
            }
        }
    }
    // Fall back to suffix-based inference
    return inferAstTypeFromName(name);
}

/// @brief Mark that a symbol has been referenced somewhere in the procedure.
/// @details Lazily infers the type from semantic analysis or name suffix when
///          absent, ensuring later slot allocation chooses the appropriate
///          storage width.  Empty names are ignored because they arise from
///          parse errors handled elsewhere.
/// @param name Identifier encountered during AST traversal.
void Lowerer::markSymbolReferenced(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    if (!info.hasType)
    {
        info.type = inferVariableTypeForLowering(*this, name);
        info.hasType = true;
        info.isBoolean = !info.isArray && info.type == AstType::Bool;
    }
    info.referenced = true;
}

/// @brief Flag that a symbol is used with array semantics.
/// @details Records the array bit on the symbol metadata and clears the boolean
///          flag because arrays are always pointer typed regardless of element
///          suffixes.
/// @param name Identifier representing an array value.
void Lowerer::markArray(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isArray = true;
    if (info.isBoolean)
        info.isBoolean = false;
}

void Lowerer::markStatic(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isStatic = true;
}

void Lowerer::pushFieldScope(const std::string &className)
{
    FieldScope scope;
    if (auto it = classLayouts_.find(className); it != classLayouts_.end())
    {
        scope.layout = &it->second;
        for (const auto &field : it->second.fields)
        {
            SymbolInfo info;
            info.type = field.type;
            info.hasType = true;
            info.isArray = false;
            info.isBoolean = (field.type == AstType::Bool);
            info.referenced = false;
            info.isObject = false;
            info.objectClass.clear();
            scope.symbols.emplace(field.name, std::move(info));
        }
    }
    fieldScopeStack_.push_back(std::move(scope));
}

void Lowerer::popFieldScope()
{
    if (!fieldScopeStack_.empty())
        fieldScopeStack_.pop_back();
}

const Lowerer::FieldScope *Lowerer::activeFieldScope() const
{
    if (fieldScopeStack_.empty())
        return nullptr;
    return &fieldScopeStack_.back();
}

bool Lowerer::isFieldInScope(std::string_view name) const
{
    if (name.empty())
        return false;
    std::string key(name);
    for (auto it = fieldScopeStack_.rbegin(); it != fieldScopeStack_.rend(); ++it)
    {
        if (it->symbols.find(key) != it->symbols.end())
            return true;
    }
    return false;
}

/// @brief Reset symbol metadata between procedure lowering runs.
/// @details Clears transient fields (slot identifiers, reference flags, type
///          overrides) for persistent string literals and removes all other
///          symbols entirely.  This prevents leakage of declaration information
///          from one procedure into the next without discarding the shared pool
///          of literal strings.
void Lowerer::resetSymbolState()
{
    for (auto it = symbols.begin(); it != symbols.end();)
    {
        SymbolInfo &info = it->second;
        if (!info.stringLabel.empty())
        {
            info.type = AstType::I64;
            info.hasType = false;
            info.isArray = false;
            info.isBoolean = false;
            info.referenced = false;
            info.isObject = false;
            info.objectClass.clear();
            info.slotId.reset();
            info.arrayLengthSlot.reset();
            ++it;
            continue;
        }
        it = symbols.erase(it);
    }
}

/// @brief Compute the lowering slot characteristics for a symbol.
/// @details Combines declared type information, inferred suffix defaults, and
///          object/array flags to produce the IL type stored in the procedure
///          frame together with helper booleans used for boolean packing and
///          array metadata allocation.
/// @param name Identifier describing the symbol.
/// @return Populated slot descriptor containing IL type and auxiliary flags.
Lowerer::SlotType Lowerer::getSlotType(std::string_view name) const
{
    SlotType info;
    AstType astTy = inferVariableTypeForLowering(*this, name);
    if (const auto *sym = findSymbol(name))
    {
        if (sym->isObject)
        {
            info.type = Type(Type::Kind::Ptr);
            info.isArray = false;
            info.isBoolean = false;
            info.isObject = true;
            info.objectClass = sym->objectClass;
            return info;
        }
        if (sym->hasType)
            astTy = sym->type;
        info.isArray = sym->isArray;
        if (sym->isBoolean && !info.isArray)
            info.isBoolean = true;
        else if (!sym->hasType && !info.isArray)
            info.isBoolean = (astTy == AstType::Bool);
        else
            info.isBoolean = false;
    }
    else
    {
        info.isArray = false;
        info.isBoolean = (astTy == AstType::Bool);
    }
    if (info.isArray)
        info.type = Type(Type::Kind::Ptr);
    else
        info.type = coreTypeForAstType(info.isBoolean ? AstType::Bool : astTy);
    return info;
}

std::optional<Lowerer::VariableStorage> Lowerer::resolveVariableStorage(std::string_view name,
                                                                        il::support::SourceLoc loc)
{
    if (name.empty())
        return std::nullopt;

    SlotType slotInfo = getSlotType(name);
    if (const auto *info = findSymbol(name))
    {
        if (info->slotId)
            return VariableStorage{slotInfo, Value::temp(*info->slotId), false};
    }

    if (auto field = resolveImplicitField(name, loc))
    {
        VariableStorage storage;
        storage.slotInfo = slotInfo;
        storage.slotInfo.type = field->ilType;
        storage.slotInfo.isArray = false;
        storage.slotInfo.isBoolean = (field->astType == AstType::Bool);
        storage.slotInfo.isObject = false;
        storage.slotInfo.objectClass.clear();
        storage.pointer = field->ptr;
        storage.isField = true;
        return storage;
    }

    return std::nullopt;
}

/// @brief Retrieve a cached procedure signature when available.
/// @details Looks up metadata gathered during @ref collectProcedureSignatures so
///          later lowering stages can inspect parameter and return types without
///          re-traversing the AST.
/// @param name Name of the procedure whose signature is requested.
/// @return Pointer to the cached signature or @c nullptr when unknown.
const Lowerer::ProcedureSignature *Lowerer::findProcSignature(const std::string &name) const
{
    auto it = procSignatures.find(name);
    if (it == procSignatures.end())
    {
        auto aliasIt = procNameAliases.find(name);
        if (aliasIt != procNameAliases.end())
        {
            auto it2 = procSignatures.find(aliasIt->second);
            if (it2 != procSignatures.end())
                return &it2->second;
        }
        // Try case-insensitive alias: canonicalize key
        std::string canon = CanonicalizeIdent(name);
        if (!canon.empty())
        {
            auto itAlias2 = procNameAliases.find(canon);
            if (itAlias2 != procNameAliases.end())
            {
                auto it3 = procSignatures.find(itAlias2->second);
                if (it3 != procSignatures.end())
                    return &it3->second;
            }
        }
        return nullptr;
    }
    return &it->second;
}

std::string Lowerer::resolveCalleeName(const std::string &name) const
{
    auto it = procNameAliases.find(name);
    if (it != procNameAliases.end())
        return it->second;
    return name;
}

/// @brief Construct procedure-lowering helpers bound to a parent Lowerer.
ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Scan a BASIC program and cache signatures for all declared procedures.
/// @details Visits each function and subroutine declaration, converting the AST
///          parameter and return types into IL types stored in the owning
///          @ref Lowerer.  Array parameters are normalised to pointer types so
///          later lowering logic can allocate the appropriate slots without
///          inspecting the AST again.
/// @param prog Program whose declarations should be indexed.
void ProcedureLowering::collectProcedureSignatures(const Program &prog)
{
    lowerer.procSignatures.clear();
    lowerer.procNameAliases.clear();
    for (const auto &decl : prog.procs)
    {
        if (auto *fn = as<const FunctionDecl>(*decl))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = lowerer.functionRetTypeFromHint(fn->name, fn->explicitRetType);
            sig.paramTypes.reserve(fn->params.size());
            for (const auto &p : fn->params)
            {
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                               : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            const std::string key = fn->qualifiedName.empty() ? fn->name : fn->qualifiedName;
            lowerer.procSignatures.emplace(key, std::move(sig));
            if (!fn->qualifiedName.empty())
                lowerer.procNameAliases.emplace(CanonicalizeIdent(fn->name), fn->qualifiedName);
            else
                // Map canonical unqualified name back to original for IL emission
                lowerer.procNameAliases.emplace(CanonicalizeIdent(fn->name), fn->name);
        }
        else if (auto *sub = as<const SubDecl>(*decl))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = il::core::Type(il::core::Type::Kind::Void);
            sig.paramTypes.reserve(sub->params.size());
            for (const auto &p : sub->params)
            {
                il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                               : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            const std::string key = sub->qualifiedName.empty() ? sub->name : sub->qualifiedName;
            lowerer.procSignatures.emplace(key, std::move(sig));
            if (!sub->qualifiedName.empty())
                lowerer.procNameAliases.emplace(CanonicalizeIdent(sub->name), sub->qualifiedName);
            else
                // Map canonical unqualified name back to original for IL emission
                lowerer.procNameAliases.emplace(CanonicalizeIdent(sub->name), sub->name);
        }
    }

    // Also scan namespace blocks in main for nested procedures.
    std::function<void(const std::vector<StmtPtr> &)> scan;
    scan = [&](const std::vector<StmtPtr> &stmts)
    {
        for (const auto &stmtPtr : stmts)
        {
            if (!stmtPtr)
                continue;
            switch (stmtPtr->stmtKind())
            {
                case Stmt::Kind::NamespaceDecl:
                    scan(static_cast<const NamespaceDecl &>(*stmtPtr).body);
                    break;
                case Stmt::Kind::FunctionDecl:
                {
                    const auto &fn = static_cast<const FunctionDecl &>(*stmtPtr);
                    Lowerer::ProcedureSignature sig;
                    sig.retType = lowerer.functionRetTypeFromHint(fn.name, fn.explicitRetType);
                    sig.paramTypes.reserve(fn.params.size());
                    for (const auto &p : fn.params)
                    {
                        il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                                       : coreTypeForAstType(p.type);
                        sig.paramTypes.push_back(ty);
                    }
                    if (!fn.qualifiedName.empty())
                    {
                        lowerer.procSignatures.emplace(fn.qualifiedName, std::move(sig));
                        lowerer.procNameAliases.emplace(CanonicalizeIdent(fn.name), fn.qualifiedName);
                    }
                    else
                    {
                        lowerer.procSignatures.emplace(fn.name, std::move(sig));
                        lowerer.procNameAliases.emplace(CanonicalizeIdent(fn.name), fn.name);
                    }
                    break;
                }
                case Stmt::Kind::SubDecl:
                {
                    const auto &sub = static_cast<const SubDecl &>(*stmtPtr);
                    Lowerer::ProcedureSignature sig;
                    sig.retType = il::core::Type(il::core::Type::Kind::Void);
                    sig.paramTypes.reserve(sub.params.size());
                    for (const auto &p : sub.params)
                    {
                        il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                                       : coreTypeForAstType(p.type);
                        sig.paramTypes.push_back(ty);
                    }
                    if (!sub.qualifiedName.empty())
                    {
                        lowerer.procSignatures.emplace(sub.qualifiedName, std::move(sig));
                        lowerer.procNameAliases.emplace(CanonicalizeIdent(sub.name), sub.qualifiedName);
                    }
                    else
                    {
                        lowerer.procSignatures.emplace(sub.name, std::move(sig));
                        lowerer.procNameAliases.emplace(CanonicalizeIdent(sub.name), sub.name);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    };
    scan(prog.main);
}

/// @brief Discover variable usage across a list of statements.
/// @details Drives @ref VarCollectWalker over each statement pointer, skipping
///          null entries to accommodate partially built AST lists.
/// @param stmts Statement pointers whose referenced symbols should be recorded.
void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

/// @brief Discover variable usage across an entire program.
/// @details Flattens procedure and main-body statements into a temporary array
///          before deferring to @ref collectVars(const std::vector<const Stmt *> &).
/// @param prog Program providing statements to analyse.
void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

/// @brief Build a lowering context for a specific procedure body.
/// @details Validates that the parent @ref Lowerer owns an active IR builder and
///          bundles together the core references required to emit IL for the
///          procedure.
/// @param name Procedure identifier.
/// @param params Parameter declarations in declaration order.
/// @param body Sequence of statements forming the body.
/// @param config Behavioural hooks controlling lowering.
/// @return Fully initialised lowering context ready for emission.
ProcedureLowering::LoweringContext ProcedureLowering::makeContext(
    const std::string &name,
    const std::vector<Param> &params,
    const std::vector<StmtPtr> &body,
    const Lowerer::ProcedureConfig &config)
{
    assert(lowerer.builder && "makeContext requires an active IRBuilder");
    return LoweringContext(
        lowerer, lowerer.symbols, *lowerer.builder, lowerer.emitter(), name, params, body, config);
}

/// @brief Reset shared lowering state prior to emitting a new procedure.
/// @details Currently defers entirely to @ref Lowerer::resetLoweringState; the
///          @p ctx parameter exists for symmetry with other hooks and future
///          expansion.
/// @param ctx Active lowering context (unused).
void ProcedureLowering::resetContext(LoweringContext &ctx)
{
    lowerer.resetLoweringState();
    (void)ctx;
}

/// @brief Compute metadata describing the procedure prior to emission.
/// @details Invokes @ref Lowerer::collectProcedureMetadata to gather parameter
///          names, IR parameter descriptions, and the flattened statement list.
///          The resulting shared metadata is cached on the context for use by
///          scheduling and emission stages.
/// @param ctx Lowering context receiving the computed metadata.
void ProcedureLowering::collectProcedureInfo(LoweringContext &ctx)
{
    auto metadata = std::make_shared<Lowerer::ProcedureMetadata>(
        lowerer.collectProcedureMetadata(ctx.params, ctx.body, ctx.config));
    ctx.metadata = metadata;
    ctx.paramCount = metadata->paramCount;
    ctx.bodyStmts = metadata->bodyStmts;
    ctx.paramNames = metadata->paramNames;
    ctx.irParams = metadata->irParams;
}

/// @brief Create the basic block skeleton for a procedure.
/// @details Validates required callbacks, allocates entry/exit blocks, assigns
///          synthetic labels for each unique source line, and materialises
///          parameter slots.  The resulting skeleton is ready for statement
///          emission performed by @ref emitProcedureIL.
/// @param ctx Lowering context describing the procedure.
void ProcedureLowering::scheduleBlocks(LoweringContext &ctx)
{
    const auto &config = ctx.config;
    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    auto metadata = ctx.metadata;
    auto &procCtx = lowerer.context();
    il::core::Function &f = ctx.builder.startFunction(ctx.name, config.retType, ctx.irParams);
    ctx.function = &f;
    procCtx.setFunction(&f);
    procCtx.setNextTemp(f.valueNames.size());

    lowerer.buildProcedureSkeleton(f, ctx.name, *metadata);

    if (!f.blocks.empty())
        procCtx.setCurrent(&f.blocks.front());

    lowerer.materializeParams(ctx.params);
    lowerer.allocateLocalSlots(ctx.paramNames, /*includeParams=*/false);
}

/// @brief Emit IL instructions for the procedure body.
/// @details Handles both the empty-body fast path (delegating entirely to the
///          configuration callback) and the general case where statements are
///          lowered sequentially.  After lowering completes, the helper performs
///          cleanup such as releasing retained runtime objects and invoking the
///          configured final return hook.
/// @param ctx Lowering context that owns the partially constructed function.
void ProcedureLowering::emitProcedureIL(LoweringContext &ctx)
{
    const auto &config = ctx.config;
    if (!config.emitEmptyBody || !config.emitFinalReturn || !ctx.function)
        return;

    auto &procCtx = lowerer.context();

    if (ctx.bodyStmts.empty())
    {
        lowerer.curLoc = {};
        config.emitEmptyBody();
        procCtx.blockNames().resetNamer();
        return;
    }

    lowerer.lowerStatementSequence(ctx.bodyStmts, /*stopOnTerminated=*/true);

    procCtx.setCurrent(&ctx.function->blocks[procCtx.exitIndex()]);
    lowerer.curLoc = {};
    lowerer.releaseObjectLocals(ctx.paramNames);
    lowerer.releaseObjectParams(ctx.paramNames);
    lowerer.releaseArrayLocals(ctx.paramNames);
    lowerer.releaseArrayParams(ctx.paramNames);
    lowerer.curLoc = {};
    config.emitFinalReturn();

    procCtx.blockNames().resetNamer();
}

/// @brief Gather metadata required to lower a single procedure body.
/// @details Records the number of parameters, flattens the body statements,
///          discovers symbol usage, and executes optional callbacks provided by
///          @p config.  Parameter IL types are computed here so downstream
///          stages can materialise stack slots without touching the AST again.
/// @param params Declaration-time parameters for the procedure.
/// @param body Statements composing the procedure body.
/// @param config Configuration hooks supplied by the caller.
/// @return Populated metadata structure consumed by later lowering stages.
Lowerer::ProcedureMetadata Lowerer::collectProcedureMetadata(const std::vector<Param> &params,
                                                             const std::vector<StmtPtr> &body,
                                                             const ProcedureConfig &config)
{
    ProcedureMetadata metadata;
    metadata.paramCount = params.size();
    metadata.bodyStmts.reserve(body.size());
    for (const auto &stmt : body)
        metadata.bodyStmts.push_back(stmt.get());

    collectVars(metadata.bodyStmts);

    if (config.postCollect)
        config.postCollect();

    metadata.irParams.reserve(params.size());
    for (const auto &p : params)
    {
        metadata.paramNames.insert(p.name);
        Type ty = p.is_array ? Type(Type::Kind::Ptr) : coreTypeForAstType(p.type);
        metadata.irParams.push_back({p.name, ty});
        if (p.is_array)
        {
            requireArrayI32Retain();
            requireArrayI32Release();
        }
    }

    return metadata;
}

/// @brief Compute or retrieve the synthetic line number for a statement.
/// @details BASIC statements may lack explicit line labels; this helper assigns
///          monotonically increasing synthetic numbers to keep block naming
///          deterministic.  When a user-provided line exists it is reused to
///          ensure diagnostics map back to the original source.
/// @param s Statement whose virtual line number is requested.
/// @return User-specified line or a generated synthetic value.
int Lowerer::virtualLine(const Stmt &s)
{
    auto it = stmtVirtualLines_.find(&s);
    if (it != stmtVirtualLines_.end())
        return it->second;

    const int userLine = s.line;
    if (hasUserLine(userLine))
    {
        stmtVirtualLines_[&s] = userLine;
        return userLine;
    }

    int synthLine = synthLineBase_ + (synthSeq_++);
    stmtVirtualLines_[&s] = synthLine;
    return synthLine;
}

/// @brief Construct the control-flow skeleton for a procedure function.
/// @details Establishes the entry block, assigns deterministic labels to each
///          source line, allocates per-line basic blocks, and records the exit
///          block index for later use.  Debug builds assert that synthetic line
///          numbers remain unique to prevent accidental block collisions.
/// @param f Function being populated.
/// @param name Procedure name used for block mangling.
/// @param metadata Metadata previously gathered for the procedure body.
void Lowerer::buildProcedureSkeleton(Function &f,
                                     const std::string &name,
                                     const ProcedureMetadata &metadata)
{
    ProcedureContext &ctx = context();
    ctx.blockNames().setNamer(std::make_unique<BlockNamer>(name));
    BlockNamer *blockNamer = ctx.blockNames().namer();

    auto &entry =
        builder->addBlock(f, blockNamer ? blockNamer->entry() : mangler.block("entry_" + name));
    entry.params = f.params;

#ifdef DEBUG
    std::vector<int> keys;
    keys.reserve(metadata.bodyStmts.size());
#endif

    auto &lineBlocks = ctx.blockNames().lineBlocks();
    for (const auto *stmt : metadata.bodyStmts)
    {
        int vLine = virtualLine(*stmt);
        if (lineBlocks.find(vLine) != lineBlocks.end())
            continue;
        size_t blockIdx = f.blocks.size();
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(vLine));
        else
            builder->addBlock(f, mangler.block("L" + std::to_string(vLine) + "_" + name));
        lineBlocks[vLine] = blockIdx;
#ifdef DEBUG
        keys.push_back(vLine);
#endif
    }

#ifdef DEBUG
    {
        std::unordered_set<int> seen;
        for (int k : keys)
        {
            assert(seen.insert(k).second &&
                   "Duplicate block key; unlabeled statements must have unique synthetic keys");
        }
    }
#endif

    ctx.setExitIndex(f.blocks.size());
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + name));
}

/// @brief Allocate stack slots for all referenced locals (and optionally params).
/// @details Iterates over the symbol table, allocating IL stack storage for each
///          referenced symbol lacking a slot.  Array values receive pointer
///          slots initialised to null, booleans are zeroed, and strings are
///          seeded with the runtime empty string.  When bounds checking is
///          enabled, auxiliary slots are reserved for array lengths.
/// @param paramNames Names of parameters for the current procedure.
/// @param includeParams When true, allocate slots for parameters as well as locals.
void Lowerer::allocateLocalSlots(const std::unordered_set<std::string> &paramNames,
                                 bool includeParams)
{
    // Emit in deterministic category order: arrays first, then booleans, then others.
    // This avoids platform-dependent unordered_map iteration differences.

    // Pass 1: booleans
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced)
            continue;
        if (info.isStatic)
            continue; // Static variables are module-level, not stack locals
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.slotId)
            continue;
        curLoc = {};
        SlotType slotInfo = getSlotType(name);
        if (slotInfo.isArray || !slotInfo.isBoolean)
            continue;
        Value slot = emitAlloca(1);
        info.slotId = slot.id;
        emitStore(ilBoolTy(), slot, emitBoolConst(false));
    }

    // Pass 2: everything else in original map iteration order (arrays and other scalars)
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced)
            continue;
        if (info.isStatic)
            continue; // Static variables are module-level, not stack locals
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.slotId)
            continue;
        curLoc = {};
        SlotType slotInfo = getSlotType(name);
        if (slotInfo.isArray)
        {
            Value slot = emitAlloca(8);
            info.slotId = slot.id;
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
        }
        else if (!slotInfo.isBoolean)
        {
            Value slot = emitAlloca(8);
            info.slotId = slot.id;
            if (slotInfo.type.kind == Type::Kind::Str)
            {
                Value empty = emitCallRet(slotInfo.type, "rt_str_empty", {});
                emitStore(slotInfo.type, slot, empty);
            }
        }
    }

    if (!boundsChecks)
        return;

    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.isArray)
            continue;
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.arrayLengthSlot)
            continue;
        curLoc = {};
        Value slot = emitAlloca(8);
        info.arrayLengthSlot = slot.id;
    }
}

/// @brief Lazily materialise the stack used for GOSUB/RETURN bookkeeping.
/// @details Emits prologue allocations for the return-stack pointer and storage
///          array if they have not yet been created.  The helper temporarily
///          switches the builder's insertion point to the function entry block
///          and restores both location and block afterwards.
void Lowerer::ensureGosubStack()
{
    ProcedureContext &ctx = context();
    auto &state = ctx.gosub();
    if (state.hasPrologue())
        return;

    Function *func = ctx.function();
    if (!func)
        return;

    BasicBlock *savedBlock = ctx.current();
    BasicBlock *entry = &func->blocks.front();
    ctx.setCurrent(entry);

    auto savedLoc = curLoc;
    curLoc = {};

    Value spSlot = emitAlloca(8);
    Value stackSlot = emitAlloca(kGosubStackDepth * 4);
    emitStore(Type(Type::Kind::I64), spSlot, Value::constInt(0));
    state.setPrologue(spSlot, stackSlot);

    curLoc = savedLoc;
    ctx.setCurrent(savedBlock);
}

/// @brief Lower a BASIC FUNCTION declaration into IL.
/// @details Prepares a @ref ProcedureConfig that seeds the return value with the
///          correct default, ensures the function name's symbol adopts the
///          declared return type, and delegates to @ref lowerProcedure for the
///          heavy lifting.
/// @param decl AST node describing the function declaration.
void Lowerer::lowerFunctionDecl(const FunctionDecl &decl)
{
    auto defaultRet = [&]()
    {
        switch (decl.ret)
        {
            case ::il::frontends::basic::Type::I64:
                return Value::constInt(0);
            case ::il::frontends::basic::Type::F64:
                return Value::constFloat(0.0);
            case ::il::frontends::basic::Type::Str:
                return emitConstStr(getStringLabel(""));
            case ::il::frontends::basic::Type::Bool:
                return emitBoolConst(false);
        }
        return Value::constInt(0);
    };

    ProcedureConfig config;
    // Return type precedence for BASIC functions:
    // 1) explicitRetType from "AS <TYPE>" if present
    // 2) name suffix ($ => string, # => float)
    // 3) default to integer (I64)
    // SUBs are always void.
    config.retType = functionRetTypeFromHint(decl.name, decl.explicitRetType);
    config.postCollect = [&]() { setSymbolType(decl.name, decl.ret); };
    config.emitEmptyBody = [&]() { emitRet(defaultRet()); };
    config.emitFinalReturn = [&]()
    {
        // BUG-003: Check if function name was assigned (VB-style implicit return)
        if (auto storage = resolveVariableStorage(decl.name, {}))
        {
            // Function name was assigned, return its value
            Value val = emitLoad(storage->slotInfo.type, storage->pointer);
            emitRet(val);
        }
        else
        {
            // No assignment, return default value
            emitRet(defaultRet());
        }
    };

    const std::string ilName = decl.qualifiedName.empty() ? decl.name : decl.qualifiedName;
    lowerProcedure(ilName, decl.params, decl.body, config);
}

/// @brief Lower a BASIC SUB declaration into IL.
/// @details Configures a void-returning @ref ProcedureConfig and delegates to
///          @ref lowerProcedure.  SUBs never return values, so both empty and
///          final return handlers emit @ref emitRetVoid.
/// @param decl AST node describing the subroutine declaration.
void Lowerer::lowerSubDecl(const SubDecl &decl)
{
    ProcedureConfig config;
    config.retType = Type(Type::Kind::Void);
    config.emitEmptyBody = [&]() { emitRetVoid(); };
    config.emitFinalReturn = [&]() { emitRetVoid(); };

    const std::string ilName = decl.qualifiedName.empty() ? decl.name : decl.qualifiedName;
    lowerProcedure(ilName, decl.params, decl.body, config);
}

/// @brief Clear all procedure-specific lowering state.
/// @details Resets the symbol table, clears the procedure context, and drops the
///          cache of synthetic line numbers so the next procedure starts fresh.
void Lowerer::resetLoweringState()
{
    resetSymbolState();
    context().reset();
    stmtVirtualLines_.clear();
    synthSeq_ = 0;
}

/// @brief Allocate stack slots and store incoming arguments for parameters.
/// @details For each parameter the helper allocates a stack slot (with boolean
///          compaction when possible), stores default values for arrays, records
///          the slot identifier on the symbol, and writes the incoming argument
///          value into the slot.  Array parameters trigger retain/release logic
///          elsewhere and therefore start null.
/// @param params Procedure parameters in declaration order.
void Lowerer::materializeParams(const std::vector<Param> &params)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "materializeParams requires an active function");
    size_t ilParamOffset = 0;
    if (func->params.size() >= params.size())
        ilParamOffset = func->params.size() - params.size();

    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto &p = params[i];
        bool isBoolParam = !p.is_array && p.type == AstType::Bool;
        Value slot = emitAlloca(isBoolParam ? 1 : 8);
        if (p.is_array)
        {
            markArray(p.name);
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
        }
        setSymbolType(p.name, p.type);
        markSymbolReferenced(p.name);
        auto &info = ensureSymbol(p.name);
        info.slotId = slot.id;
        const size_t ilIndex = i + ilParamOffset;
        if (ilIndex >= func->params.size())
            continue;
        il::core::Type ty = func->params[ilIndex].type;
        Value incoming = Value::temp(func->params[ilIndex].id);
        if (p.is_array)
        {
            storeArray(slot, incoming);
        }
        else
        {
            emitStore(ty, slot, incoming);
        }
    }
}

/// @brief Forward variable discovery to the procedure lowering helper.
/// @param prog Program whose statements should be scanned for symbol usage.
void Lowerer::collectVars(const Program &prog)
{
    procedureLowering->collectVars(prog);
}

/// @brief Forward variable discovery for an arbitrary statement list.
/// @param stmts Statements whose referenced symbols should be recorded.
void Lowerer::collectVars(const std::vector<const Stmt *> &stmts)
{
    procedureLowering->collectVars(stmts);
}

/// @brief Forward signature collection to the procedure lowering helper.
/// @param prog Program containing declarations to index.
void Lowerer::collectProcedureSignatures(const Program &prog)
{
    procedureLowering->collectProcedureSignatures(prog);
}

/// @brief Access the mutable procedure context for the current lowering run.
/// @return Reference to the active procedure context.
Lowerer::ProcedureContext &Lowerer::context() noexcept
{
    return context_;
}

/// @brief Access the immutable procedure context for the current lowering run.
/// @return Const reference to the active procedure context.
const Lowerer::ProcedureContext &Lowerer::context() const noexcept
{
    return context_;
}

/// @brief Construct an @ref Emit helper bound to the current lowering state.
/// @return Lightweight emit helper ready to append instructions.
Emit Lowerer::emitCommon() noexcept
{
    return Emit(*this);
}

/// @brief Construct an emit helper and pre-set its source location.
/// @param loc Source location associated with subsequently emitted instructions.
/// @return Configured emit helper.
Emit Lowerer::emitCommon(il::support::SourceLoc loc) noexcept
{
    Emit helper(*this);
    helper.at(loc);
    return helper;
}

/// @brief Retrieve the shared lowering emitter.
/// @details Asserts that the emitter has been initialised during construction.
/// @return Reference to the emitter instance.
lower::Emitter &Lowerer::emitter() noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}

/// @brief Retrieve the shared lowering emitter (const overload).
/// @details Asserts that the emitter has been initialised during construction.
/// @return Const reference to the emitter instance.
const lower::Emitter &Lowerer::emitter() const noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}

/// @brief Reserve a fresh temporary identifier for IL value creation.
/// @details Coordinates with either the active @ref IRBuilder or the cached
///          procedure context to ensure uniqueness, resizing the function's
///          value-name table when needed to keep debug printing stable.
/// @return Newly allocated temporary identifier.
unsigned Lowerer::nextTempId()
{
    ProcedureContext &ctx = context();
    unsigned id = 0;
    if (builder)
    {
        id = builder->reserveTempId();
    }
    else
    {
        id = ctx.nextTemp();
        ctx.setNextTemp(id + 1);
    }
    if (Function *func = ctx.function())
    {
        if (func->valueNames.size() <= id)
            func->valueNames.resize(id + 1);
        if (func->valueNames[id].empty())
            func->valueNames[id] = "%t" + std::to_string(id);
    }
    if (ctx.nextTemp() <= id)
        ctx.setNextTemp(id + 1);
    return id;
}

/// @brief Generate a unique fallback block label for ad-hoc control flow.
/// @details Uses the mangler helper to produce deterministic labels across runs
///          while incrementing a local counter to guarantee uniqueness.
/// @return Freshly generated block label string.
std::string Lowerer::nextFallbackBlockLabel()
{
    return mangler.block("bb_" + std::to_string(nextFallbackBlockId++));
}

} // namespace il::frontends::basic
