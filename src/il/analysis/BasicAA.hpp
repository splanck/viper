// File: src/il/analysis/BasicAA.hpp
// Purpose: Provide a minimal alias analysis and call-side effect classifier for IL.
// Key invariants: Alias queries reason about SSA temporaries tied to allocas and
//                 noalias parameters; call ModRef results fold callee metadata
//                 from IL attributes and runtime registries.
// Ownership/Lifetime: BasicAA holds non-owning references to the analysed
//                     function/module and caches derived identifier sets.
// Links: docs/dev/analysis.md
#pragma once

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/signatures/Registry.hpp"

#include <string_view>
#include <unordered_set>

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
    [[nodiscard]] AliasResult alias(const il::core::Value &lhs, const il::core::Value &rhs) const;

    /// @brief Classify the ModRef behaviour for a call instruction.
    [[nodiscard]] ModRefResult modRef(const il::core::Instr &instr) const;

  private:
    const il::core::Function *function_;
    const il::core::Module *module_;
    std::unordered_set<unsigned> allocas_;
    std::unordered_set<unsigned> noaliasParams_;

    struct CallEffect
    {
        bool pure = false;
        bool readonly = false;
    };

    void collectFunctionInfo(const il::core::Function &function);

    [[nodiscard]] static bool equalValues(const il::core::Value &lhs, const il::core::Value &rhs);

    [[nodiscard]] bool isAlloca(unsigned id) const;

    [[nodiscard]] bool isNoAliasParam(unsigned id) const;

    [[nodiscard]] const il::core::Function *findFunction(std::string_view name) const;

    [[nodiscard]] CallEffect queryFunctionEffect(const il::core::Function &fn) const;

    [[nodiscard]] CallEffect queryRuntimeEffect(std::string_view name) const;

    [[nodiscard]] CallEffect computeCalleeEffect(std::string_view name) const;
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
        if (param.isNoAlias())
            noaliasParams_.insert(param.id);

    for (const auto &block : function.blocks)
        for (const auto &instr : block.instructions)
            if (instr.op == il::core::Opcode::Alloca && instr.result)
                allocas_.insert(*instr.result);
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

inline AliasResult BasicAA::alias(const il::core::Value &lhs, const il::core::Value &rhs) const
{
    if (equalValues(lhs, rhs))
        return AliasResult::MustAlias;

    if (lhs.kind == il::core::Value::Kind::Temp && rhs.kind == il::core::Value::Kind::Temp)
    {
        const unsigned lhsId = lhs.id;
        const unsigned rhsId = rhs.id;

        if (isAlloca(lhsId) && isAlloca(rhsId) && lhsId != rhsId)
            return AliasResult::NoAlias;

        if (isNoAliasParam(lhsId) && isNoAliasParam(rhsId) && lhsId != rhsId)
            return AliasResult::NoAlias;
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
