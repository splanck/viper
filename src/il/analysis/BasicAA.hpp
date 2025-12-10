//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares BasicAA, a fundamental alias analysis pass that provides
// conservative memory disambiguation for IL optimizations. Alias analysis determines
// whether two memory references may point to the same location, enabling optimizations
// like load/store elimination, code motion, and memory optimization.
//
// BasicAA implements alias analysis using SSA-based reasoning about allocas,
// function parameters, and pointer arithmetic. The analysis tracks allocation
// sites (alloca instructions, parameters marked noalias) and uses flow-insensitive
// reasoning to determine when two references definitely alias, definitely don't alias,
// or may alias.
//
// Analysis Capabilities:
// - Alloca-based reasoning: Stack allocations from distinct alloca instructions
//   are known to not alias each other
// - Parameter annotations: Function parameters marked with noalias attribute are
//   treated as distinct from other allocations
// - Call side effects: Determines which memory locations a call instruction may
//   read or modify (ModRef analysis) using function attributes and runtime metadata
// - Conservative defaults: When precise analysis is unavailable, assumes may-alias
//
// The analysis integrates with the runtime signature system to obtain side effect
// information for runtime library calls, enabling optimization of code calling
// intrinsics and helper functions.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::analysis
{

/// @brief Describe the relationship between two pointer-like values.
enum class AliasResult
{
    NoAlias,
    MayAlias,
    MustAlias
};

/// @brief Summarise how a call interacts with memory.
enum class ModRefResult
{
    NoModRef,
    Ref,
    Mod,
    ModRef
};

/// @brief Lightweight alias analysis for IL functions.
class BasicAA
{
  public:
    /// @brief Build analysis state for @p function optionally referencing @p module.
    explicit BasicAA(const il::core::Function &function, const il::core::Module *module = nullptr);

    /// @brief Convenience constructor when both module and function are known.
    BasicAA(const il::core::Module &module, const il::core::Function &function);

    /// @brief Query aliasing behaviour for two pointer-like values.
    /// @param lhsSize Optional byte width of the lhs access when known.
    /// @param rhsSize Optional byte width of the rhs access when known.
    [[nodiscard]] AliasResult alias(const il::core::Value &lhs,
                                    const il::core::Value &rhs,
                                    std::optional<unsigned> lhsSize = std::nullopt,
                                    std::optional<unsigned> rhsSize = std::nullopt) const;

    /// @brief Classify the ModRef behaviour for a call instruction.
    [[nodiscard]] ModRefResult modRef(const il::core::Instr &instr) const;

    /// @brief Compute the byte width of a scalar IL type when known.
    [[nodiscard]] static std::optional<unsigned> typeSizeBytes(const il::core::Type &type);

  private:
    enum class BaseKind
    {
        Unknown,
        Alloca,
        Param,
        NoAliasParam,
        Global,
        ConstStr,
        Null
    };

    struct Location
    {
        BaseKind kind = BaseKind::Unknown;
        /// @brief Identity of the base (alloca id, param id).
        unsigned id = 0;
        /// @brief Global symbol backing the location.
        std::string global;
        /// @brief Byte offset from the base when known.
        long long offset = 0;
        bool hasOffset = true;
    };

    const il::core::Function *function_;
    const il::core::Module *module_;
    std::unordered_set<unsigned> allocas_;
    std::unordered_set<unsigned> noaliasParams_;
    std::unordered_set<unsigned> params_;
    struct DefInfo
    {
        il::core::Opcode op;
        std::vector<il::core::Value> operands;
    };
    std::unordered_map<unsigned, DefInfo> defs_;

    struct CallEffect
    {
        bool pure = false;
        bool readonly = false;
    };

    void collectFunctionInfo(const il::core::Function &function);

    [[nodiscard]] static bool equalValues(const il::core::Value &lhs, const il::core::Value &rhs);

    [[nodiscard]] bool isAlloca(unsigned id) const;

    [[nodiscard]] bool isNoAliasParam(unsigned id) const;

    [[nodiscard]] bool isParam(unsigned id) const;

    [[nodiscard]] const il::core::Function *findFunction(std::string_view name) const;

    [[nodiscard]] CallEffect queryFunctionEffect(const il::core::Function &fn) const;

    [[nodiscard]] CallEffect queryRuntimeEffect(std::string_view name) const;

    [[nodiscard]] CallEffect computeCalleeEffect(std::string_view name) const;

    [[nodiscard]] Location describe(const il::core::Value &value, unsigned depth = 0) const;

    [[nodiscard]] static bool constOffset(const il::core::Value &v, long long &out);
};

inline BasicAA::BasicAA(const il::core::Function &function, const il::core::Module *module)
    : function_(&function), module_(module)
{
    collectFunctionInfo(function);
}

inline BasicAA::BasicAA(const il::core::Module &module, const il::core::Function &function)
    : BasicAA(function, &module)
{
}

inline void BasicAA::collectFunctionInfo(const il::core::Function &function)
{
    for (const auto &param : function.params)
    {
        if (param.isNoAlias())
            noaliasParams_.insert(param.id);
        params_.insert(param.id);
    }

    for (const auto &block : function.blocks)
        for (const auto &instr : block.instructions)
        {
            if (instr.result)
                defs_.insert({*instr.result, DefInfo{instr.op, instr.operands}});
            if (instr.op == il::core::Opcode::Alloca && instr.result)
                allocas_.insert(*instr.result);
        }
}

inline bool BasicAA::equalValues(const il::core::Value &lhs, const il::core::Value &rhs)
{
    if (lhs.kind != rhs.kind)
        return false;

    using Kind = il::core::Value::Kind;
    switch (lhs.kind)
    {
        case Kind::Temp:
            return lhs.id == rhs.id;
        case Kind::ConstInt:
            return lhs.i64 == rhs.i64 && lhs.isBool == rhs.isBool;
        case Kind::ConstFloat:
            return lhs.f64 == rhs.f64;
        case Kind::ConstStr:
        case Kind::GlobalAddr:
            return lhs.str == rhs.str;
        case Kind::NullPtr:
            return true;
    }
    return false;
}

inline bool BasicAA::isAlloca(unsigned id) const
{
    return allocas_.count(id) != 0;
}

inline bool BasicAA::isNoAliasParam(unsigned id) const
{
    return noaliasParams_.count(id) != 0;
}

inline bool BasicAA::isParam(unsigned id) const
{
    return params_.count(id) != 0;
}

inline const il::core::Function *BasicAA::findFunction(std::string_view name) const
{
    if (name.empty())
        return nullptr;

    if (function_ && function_->name == name)
        return function_;

    if (!module_)
        return nullptr;

    for (const auto &fn : module_->functions)
        if (fn.name == name)
            return &fn;

    return nullptr;
}

inline BasicAA::CallEffect BasicAA::queryFunctionEffect(const il::core::Function &fn) const
{
    CallEffect effect;
    effect.pure = fn.attrs().pure;
    effect.readonly = fn.attrs().readonly;
    return effect;
}

inline BasicAA::CallEffect BasicAA::queryRuntimeEffect(std::string_view name) const
{
    CallEffect effect;
    for (const auto &signature : il::runtime::signatures::all_signatures())
    {
        if (signature.name == name)
        {
            effect.pure = signature.pure;
            effect.readonly = signature.readonly;
            break;
        }
    }
    return effect;
}

inline BasicAA::CallEffect BasicAA::computeCalleeEffect(std::string_view name) const
{
    CallEffect effect;

    if (const auto *fn = findFunction(name))
    {
        const CallEffect fnEffect = queryFunctionEffect(*fn);
        effect.pure = effect.pure || fnEffect.pure;
        effect.readonly = effect.readonly || fnEffect.readonly;
    }

    const CallEffect runtimeEffect = queryRuntimeEffect(name);
    effect.pure = effect.pure || runtimeEffect.pure;
    effect.readonly = effect.readonly || runtimeEffect.readonly;

    return effect;
}

inline bool BasicAA::constOffset(const il::core::Value &v, long long &out)
{
    if (v.kind == il::core::Value::Kind::ConstInt)
    {
        out = v.i64;
        return true;
    }
    return false;
}

inline BasicAA::Location BasicAA::describe(const il::core::Value &value, unsigned depth) const
{
    if (depth > 8)
        return {};

    using Kind = il::core::Value::Kind;
    Location loc;

    switch (value.kind)
    {
        case Kind::Temp:
        {
            if (isAlloca(value.id))
            {
                loc.kind = BaseKind::Alloca;
                loc.id = value.id;
                return loc;
            }
            if (isNoAliasParam(value.id))
            {
                loc.kind = BaseKind::NoAliasParam;
                loc.id = value.id;
                return loc;
            }
            if (isParam(value.id))
            {
                loc.kind = BaseKind::Param;
                loc.id = value.id;
                return loc;
            }

            auto defIt = defs_.find(value.id);
            if (defIt == defs_.end())
                return loc;
            const DefInfo &def = defIt->second;

            if (def.op == il::core::Opcode::GEP && def.operands.size() >= 2)
            {
                Location base = describe(def.operands[0], depth + 1);
                if (base.kind == BaseKind::Unknown)
                    return base;

                long long off = 0;
                if (constOffset(def.operands[1], off) && base.hasOffset)
                {
                    base.offset += off;
                }
                else
                {
                    base.hasOffset = false;
                }
                return base;
            }

            if ((def.op == il::core::Opcode::AddrOf || def.op == il::core::Opcode::GAddr) &&
                !def.operands.empty() &&
                def.operands[0].kind == Kind::GlobalAddr)
            {
                loc.kind = BaseKind::Global;
                loc.global = def.operands[0].str;
                return loc;
            }

            return loc;
        }
        case Kind::GlobalAddr:
            loc.kind = BaseKind::Global;
            loc.global = value.str;
            return loc;
        case Kind::ConstStr:
            loc.kind = BaseKind::ConstStr;
            loc.global = value.str;
            return loc;
        case Kind::NullPtr:
            loc.kind = BaseKind::Null;
            return loc;
        case Kind::ConstInt:
        case Kind::ConstFloat:
            return loc;
    }
    return loc;
}

inline std::optional<unsigned> BasicAA::typeSizeBytes(const il::core::Type &type)
{
    using K = il::core::Type::Kind;
    switch (type.kind)
    {
        case K::I1:
            return 1;
        case K::I16:
            return 2;
        case K::I32:
            return 4;
        case K::I64:
        case K::F64:
            return 8;
        case K::Ptr:
        case K::Str:
            return 8;
        default:
            return std::nullopt;
    }
}

inline AliasResult BasicAA::alias(const il::core::Value &lhs,
                                  const il::core::Value &rhs,
                                  std::optional<unsigned> lhsSize,
                                  std::optional<unsigned> rhsSize) const
{
    if (equalValues(lhs, rhs))
        return AliasResult::MustAlias;

    Location l = describe(lhs);
    Location r = describe(rhs);

    auto basesEqual = [](const Location &a, const Location &b)
    {
        if (a.kind == BaseKind::Global || a.kind == BaseKind::ConstStr)
            return a.kind == b.kind && a.global == b.global;
        return a.kind == b.kind && a.id == b.id;
    };

    if (l.kind == BaseKind::Unknown || r.kind == BaseKind::Unknown)
        return AliasResult::MayAlias;

    if (l.kind == BaseKind::Null || r.kind == BaseKind::Null)
        return basesEqual(l, r) ? AliasResult::MustAlias : AliasResult::NoAlias;

    if (l.kind == BaseKind::NoAliasParam && !basesEqual(l, r))
        return AliasResult::NoAlias;
    if (r.kind == BaseKind::NoAliasParam && !basesEqual(l, r))
        return AliasResult::NoAlias;

    auto isGlobalLike = [](BaseKind k)
    { return k == BaseKind::Global || k == BaseKind::ConstStr; };

    if ((l.kind == BaseKind::Alloca && isGlobalLike(r.kind)) ||
        (r.kind == BaseKind::Alloca && isGlobalLike(l.kind)))
        return AliasResult::NoAlias;

    if (l.kind == BaseKind::Alloca && r.kind == BaseKind::Alloca && l.id != r.id)
        return AliasResult::NoAlias;

    if (l.kind == BaseKind::Global && r.kind == BaseKind::Global && l.global != r.global)
        return AliasResult::NoAlias;

    if (isGlobalLike(l.kind) && isGlobalLike(r.kind) && l.global != r.global)
        return AliasResult::NoAlias;

    if (l.kind == BaseKind::Param && r.kind == BaseKind::Param && l.id != r.id)
        return AliasResult::MayAlias;

    if (basesEqual(l, r))
    {
        if (l.hasOffset && r.hasOffset)
        {
            if (l.offset == r.offset)
                return AliasResult::MustAlias;

            if (lhsSize && rhsSize)
            {
                const long long lEnd = l.offset + static_cast<long long>(*lhsSize);
                const long long rEnd = r.offset + static_cast<long long>(*rhsSize);
                if (lEnd <= r.offset || rEnd <= l.offset)
                    return AliasResult::NoAlias;
            }
        }
        return AliasResult::MayAlias;
    }

    return AliasResult::MayAlias;
}

inline ModRefResult BasicAA::modRef(const il::core::Instr &instr) const
{
    if (instr.op != il::core::Opcode::Call)
        return ModRefResult::ModRef;

    bool pure = instr.CallAttr.pure;
    bool readonly = instr.CallAttr.readonly;

    const CallEffect callee = computeCalleeEffect(instr.callee);
    pure = pure || callee.pure;
    readonly = readonly || callee.readonly;

    if (pure)
        return ModRefResult::NoModRef;
    if (readonly)
        return ModRefResult::Ref;
    return ModRefResult::ModRef;
}

} // namespace viper::analysis
