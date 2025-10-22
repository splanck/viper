//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Declares the shared lowering context and helper routines that underpin BASIC
// builtin emission.  Family-specific handlers operate on the lightweight
// BuiltinLowerContext wrapper, which exposes convenience APIs for argument
// materialisation, control-flow synthesis, and runtime feature tracking.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Shared utilities for lowering BASIC builtins.
/// @details The @ref BuiltinLowerContext provides a uniform view over builtin
///          calls so individual families can focus on bespoke logic.  Helper
///          routines implement the rule-driven lowering behaviour used by most
///          builtins and expose reusable building blocks for specialised
///          handlers such as numeric conversions.

#pragma once

#include "frontends/basic/BuiltinRegistry.hpp"
#include "support/source_location.hpp"

#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic::lower
{
class BuiltinLowerContext
{
  public:
    BuiltinLowerContext(Lowerer &lowerer, const BuiltinCallExpr &call);

    [[nodiscard]] Lowerer &lowerer() const noexcept
    {
        return *lowerer_;
    }

    [[nodiscard]] const BuiltinCallExpr &call() const noexcept
    {
        return *call_;
    }

    [[nodiscard]] const BuiltinLoweringRule &rule() const noexcept
    {
        return *rule_;
    }

    [[nodiscard]] const BuiltinInfo &info() const noexcept
    {
        return *info_;
    }

    [[nodiscard]] bool hasArg(std::size_t idx) const noexcept;
    [[nodiscard]] std::optional<Lowerer::ExprType> originalType(std::size_t idx) const noexcept;
    [[nodiscard]] il::support::SourceLoc argLoc(std::size_t idx) const noexcept;
    [[nodiscard]] il::support::SourceLoc callLoc(
        const std::optional<std::size_t> &idx) const noexcept;

    [[nodiscard]] Lowerer::RVal &ensureLowered(std::size_t idx);
    [[nodiscard]] Lowerer::RVal &ensureArgument(const BuiltinLoweringRule::Argument &spec);
    [[nodiscard]] Lowerer::RVal &applyTransforms(
        const BuiltinLoweringRule::Argument &spec,
        const std::vector<BuiltinLoweringRule::ArgTransform> &transforms);

    [[nodiscard]] il::core::Type resolveResultType();
    [[nodiscard]] il::core::Type resolveResultType(const BuiltinLoweringRule::ResultSpec &spec);
    [[nodiscard]] Lowerer::RVal makeZeroResult() const;

    void setCurrentLoc(il::support::SourceLoc loc);
    [[nodiscard]] il::core::Type boolType() const;
    [[nodiscard]] il::core::Value emitCall(il::core::Type type,
                                           const char *runtime,
                                           const std::vector<il::core::Value> &args);
    [[nodiscard]] il::core::Value emitUnary(il::core::Opcode opcode,
                                            il::core::Type type,
                                            il::core::Value value);
    [[nodiscard]] il::core::Value emitBinary(il::core::Opcode opcode,
                                             il::core::Type type,
                                             il::core::Value lhs,
                                             il::core::Value rhs);
    [[nodiscard]] il::core::Value emitLoad(il::core::Type type, il::core::Value addr);
    [[nodiscard]] il::core::Value emitAlloca(int bytes);
    void emitCBr(il::core::Value cond, il::core::BasicBlock *t, il::core::BasicBlock *f);
    void emitTrap();
    void setCurrentBlock(il::core::BasicBlock *block);

    [[nodiscard]] const BuiltinLoweringRule::Variant *selectVariant() const;
    void applyFeatures(const BuiltinLoweringRule::Variant &variant);

    struct BranchPair
    {
        il::core::BasicBlock *cont{nullptr};
        il::core::BasicBlock *trap{nullptr};
    };

    [[nodiscard]] BranchPair createGuardBlocks(const char *contHint, const char *trapHint);

    struct ValBlocks
    {
        il::core::BasicBlock *cont{nullptr};
        il::core::BasicBlock *trap{nullptr};
        il::core::BasicBlock *nan{nullptr};
        il::core::BasicBlock *overflow{nullptr};
    };

    [[nodiscard]] ValBlocks createValBlocks();
    void emitConversionTrap(il::support::SourceLoc loc);
    std::string makeBlockLabel(const char *hint);

  private:
    [[nodiscard]] Lowerer::RVal &appendSynthetic(Lowerer::RVal value);
    [[nodiscard]] il::support::SourceLoc selectArgLoc(
        const BuiltinLoweringRule::Argument &spec) const;
    [[nodiscard]] static il::core::Type typeFromExpr(Lowerer &lowerer, Lowerer::ExprType type);

    Lowerer *lowerer_;
    const BuiltinCallExpr *call_;
    const BuiltinLoweringRule *rule_;
    const BuiltinInfo *info_;
    std::vector<std::optional<Lowerer::ExprType>> originalTypes_;
    std::vector<std::optional<il::support::SourceLoc>> argLocs_;
    std::vector<std::optional<Lowerer::RVal>> loweredArgs_;
    std::vector<Lowerer::RVal> syntheticArgs_;
};

[[nodiscard]] Lowerer::RVal lowerGenericBuiltin(BuiltinLowerContext &ctx);
[[nodiscard]] Lowerer::RVal lowerConversionBuiltinImpl(BuiltinLowerContext &ctx);
[[nodiscard]] Lowerer::RVal emitBuiltinVariant(BuiltinLowerContext &ctx,
                                               const BuiltinLoweringRule::Variant &variant);
[[nodiscard]] Lowerer::RVal lowerNumericConversion(BuiltinLowerContext &ctx,
                                                   const BuiltinLoweringRule::Variant &variant,
                                                   il::core::Type resultType,
                                                   const char *contHint,
                                                   const char *trapHint);
[[nodiscard]] Lowerer::RVal lowerValBuiltin(BuiltinLowerContext &ctx,
                                            const BuiltinLoweringRule::Variant &variant);

} // namespace il::frontends::basic::lower
