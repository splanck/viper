//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/BuiltinCommon.hpp
// Purpose: Declares the shared lowering context and helper routines that
//          underpin BASIC builtin emission. Family-specific handlers operate
//          on the lightweight BuiltinLowerContext wrapper, which exposes
//          convenience APIs for argument materialisation, control-flow
//          synthesis, and runtime feature tracking.
// Key invariants: The context remains valid for the duration of one builtin
//                 call lowering. Synthetic arguments are owned by the context
//                 and are released when the context is destroyed.
// Ownership/Lifetime: Borrows Lowerer state; synthetic argument RVals are
//                     owned by the context.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/lower/common/CommonLowering.hpp"
#include "support/source_location.hpp"

#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic::lower
{
/// @brief Shared lowering context for BASIC builtin call emission.
/// @details Wraps a builtin call expression together with its lowering rule,
///          builtin info, and the enclosing Lowerer. Provides convenience APIs
///          for argument materialisation, type resolution, IL emission, and
///          control-flow synthesis that family-specific handlers use to lower
///          individual builtins.
class BuiltinLowerContext
{
  public:
    /// @brief Construct a context for lowering the given builtin call.
    /// @details Looks up the builtin's info and lowering rule from the registry,
    ///          pre-computes original argument types and source locations, and
    ///          prepares the lazy-lowering argument cache.
    /// @param lowerer The BASIC lowerer providing IL emission facilities.
    /// @param call The builtin call expression AST node to lower.
    BuiltinLowerContext(Lowerer &lowerer, const BuiltinCallExpr &call);

    /// @brief Access the owning Lowerer instance.
    /// @return Reference to the Lowerer driving the lowering process.
    [[nodiscard]] Lowerer &lowerer() const noexcept
    {
        return *lowerer_;
    }

    /// @brief Access the builtin call expression being lowered.
    /// @return Const reference to the BuiltinCallExpr AST node.
    [[nodiscard]] const BuiltinCallExpr &call() const noexcept
    {
        return *call_;
    }

    /// @brief Access the lowering rule describing how to translate this builtin.
    /// @return Const reference to the BuiltinLoweringRule for the builtin.
    [[nodiscard]] const BuiltinLoweringRule &rule() const noexcept
    {
        return *rule_;
    }

    /// @brief Access the static builtin info (name, arity, etc.).
    /// @return Const reference to the BuiltinInfo entry.
    [[nodiscard]] const BuiltinInfo &info() const noexcept
    {
        return *info_;
    }

    /// @brief Check whether the call has an argument at the given index.
    /// @param idx Zero-based argument index.
    /// @return True if an argument exists at position idx.
    [[nodiscard]] bool hasArg(std::size_t idx) const noexcept;

    /// @brief Retrieve the original BASIC expression type for argument idx.
    /// @param idx Zero-based argument index.
    /// @return The expression type if the argument exists, std::nullopt otherwise.
    [[nodiscard]] std::optional<Lowerer::ExprType> originalType(std::size_t idx) const noexcept;

    /// @brief Get the source location of argument idx for diagnostic reporting.
    /// @param idx Zero-based argument index.
    /// @return The source location of the argument expression.
    [[nodiscard]] il::support::SourceLoc argLoc(std::size_t idx) const noexcept;

    /// @brief Get the best source location for the call or a specific argument.
    /// @param idx Optional argument index; if nullopt, uses the call's own location.
    /// @return The resolved source location.
    [[nodiscard]] il::support::SourceLoc callLoc(
        const std::optional<std::size_t> &idx) const noexcept;

    /// @brief Lazily lower argument idx and return a reference to the cached RVal.
    /// @details If the argument has not yet been lowered, invokes the Lowerer's
    ///          expression lowering to materialise it. Subsequent calls return
    ///          the cached result.
    /// @param idx Zero-based argument index.
    /// @return Reference to the lowered RVal for the argument.
    [[nodiscard]] Lowerer::RVal &ensureLowered(std::size_t idx);

    /// @brief Ensure an argument matching the given rule spec is lowered and coerced.
    /// @details Resolves the argument source (by index or synthetic), applies
    ///          any type coercions required by the rule, and returns the result.
    /// @param spec The argument specification from the lowering rule.
    /// @return Reference to the prepared RVal.
    [[nodiscard]] Lowerer::RVal &ensureArgument(const BuiltinLoweringRule::Argument &spec);

    /// @brief Apply a sequence of transforms to an argument value.
    /// @details Transforms include type coercions, clamping, and other
    ///          pre-processing steps defined in the lowering rule.
    /// @param spec The argument specification identifying the source value.
    /// @param transforms The ordered list of transforms to apply.
    /// @return Reference to the transformed RVal.
    [[nodiscard]] Lowerer::RVal &applyTransforms(
        const BuiltinLoweringRule::Argument &spec,
        const std::vector<BuiltinLoweringRule::ArgTransform> &transforms);

    /// @brief Resolve the IL result type from the default rule specification.
    /// @return The IL type to use for the builtin call's return value.
    [[nodiscard]] il::core::Type resolveResultType();

    /// @brief Resolve the IL result type from an explicit result spec.
    /// @param spec The result specification describing the expected type.
    /// @return The IL type to use for the builtin call's return value.
    [[nodiscard]] il::core::Type resolveResultType(const BuiltinLoweringRule::ResultSpec &spec);

    /// @brief Create a zero-valued RVal of the builtin's result type.
    /// @details Used as a fallback when the builtin encounters an error path
    ///          that needs to produce a default value.
    /// @return An RVal containing the zero constant.
    [[nodiscard]] Lowerer::RVal makeZeroResult() const;

    /// @brief Set the current source location for IL instructions being emitted.
    /// @param loc The source location to attach to subsequent instructions.
    void setCurrentLoc(il::support::SourceLoc loc);

    /// @brief Return the IL type used for boolean values (i1).
    /// @return The IL i1 type.
    [[nodiscard]] il::core::Type boolType() const;

    /// @brief Emit a direct call to a runtime function.
    /// @param type The IL return type of the runtime function.
    /// @param runtime The runtime function name (e.g. "rt_math_abs").
    /// @param args The argument values to pass.
    /// @return The return value of the call.
    [[nodiscard]] il::core::Value emitCall(il::core::Type type,
                                           const char *runtime,
                                           const std::vector<il::core::Value> &args);

    /// @brief Emit a unary IL instruction.
    /// @param opcode The IL opcode for the unary operation.
    /// @param type The IL type of the operand and result.
    /// @param value The operand value.
    /// @return The result value.
    [[nodiscard]] il::core::Value emitUnary(il::core::Opcode opcode,
                                            il::core::Type type,
                                            il::core::Value value);

    /// @brief Emit a binary IL instruction.
    /// @param opcode The IL opcode for the binary operation.
    /// @param type The IL type of the operands and result.
    /// @param lhs The left-hand operand.
    /// @param rhs The right-hand operand.
    /// @return The result value.
    [[nodiscard]] il::core::Value emitBinary(il::core::Opcode opcode,
                                             il::core::Type type,
                                             il::core::Value lhs,
                                             il::core::Value rhs);

    /// @brief Emit a load instruction from a memory address.
    /// @param type The IL type of the value being loaded.
    /// @param addr The pointer value to load from.
    /// @return The loaded value.
    [[nodiscard]] il::core::Value emitLoad(il::core::Type type, il::core::Value addr);

    /// @brief Emit a stack allocation of the given size in bytes.
    /// @param bytes Number of bytes to allocate.
    /// @return A pointer value referencing the allocated stack slot.
    [[nodiscard]] il::core::Value emitAlloca(int bytes);

    /// @brief Emit a conditional branch based on a boolean condition.
    /// @param cond The i1 condition value.
    /// @param t The basic block to branch to if cond is true.
    /// @param f The basic block to branch to if cond is false.
    void emitCBr(il::core::Value cond, il::core::BasicBlock *t, il::core::BasicBlock *f);

    /// @brief Emit a trap instruction that terminates execution with an error.
    void emitTrap();

    /// @brief Switch the current insertion point to a different basic block.
    /// @param block The basic block to begin appending instructions to.
    void setCurrentBlock(il::core::BasicBlock *block);

    /// @brief Select the best matching rule variant for the current argument types.
    /// @details Iterates the rule's variants and returns the first one whose
    ///          type predicates match the actual argument types.
    /// @return Pointer to the matching variant, or nullptr if none match.
    [[nodiscard]] const BuiltinLoweringRule::Variant *selectVariant() const;

    /// @brief Register runtime features required by the selected variant.
    /// @details Calls requestHelper() for each RuntimeFeature listed in the
    ///          variant's feature requirements.
    /// @param variant The selected lowering rule variant.
    void applyFeatures(const BuiltinLoweringRule::Variant &variant);

    /// @brief A pair of basic blocks for guard (bounds-check) patterns.
    struct BranchPair
    {
        il::core::BasicBlock *cont{nullptr}; ///< Continuation block (check passed).
        il::core::BasicBlock *trap{nullptr}; ///< Trap block (check failed).
    };

    /// @brief Create a continuation/trap block pair for guard patterns.
    /// @param contHint Label hint for the continuation block.
    /// @param trapHint Label hint for the trap block.
    /// @return A BranchPair with the two newly created blocks.
    [[nodiscard]] BranchPair createGuardBlocks(const char *contHint, const char *trapHint);

    /// @brief A set of basic blocks for VAL-style numeric conversion patterns.
    struct ValBlocks
    {
        il::core::BasicBlock *cont{nullptr};     ///< Success continuation block.
        il::core::BasicBlock *trap{nullptr};     ///< General error trap block.
        il::core::BasicBlock *nan{nullptr};      ///< NaN-detected trap block.
        il::core::BasicBlock *overflow{nullptr}; ///< Overflow-detected trap block.
    };

    /// @brief Create the block set for a VAL-style numeric conversion builtin.
    /// @return A ValBlocks struct with all four blocks created.
    [[nodiscard]] ValBlocks createValBlocks();

    /// @brief Emit a trap for a failed numeric conversion (e.g. VAL("abc")).
    /// @param loc Source location for the trap instruction.
    void emitConversionTrap(il::support::SourceLoc loc);

    /// @brief Generate a unique block label with the given hint prefix.
    /// @param hint The prefix for the block label (e.g. "abs_neg").
    /// @return A unique block label string.
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
    lower::common::CommonLowering lowering_;
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
