//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Declares helpers for lowering BASIC builtin expressions.
/// @details `BuiltinExprLowering` provides a small façade over the builtin
///          registry and rule tables. It dispatches builtin calls to specialised
///          emitters (LOF/EOF/LOC/ERR) or the rule-driven engine, ensuring that
///          arguments are coerced and runtime feature tracking is updated. The
///          helper borrows a @ref Lowerer and does not own AST or IR state.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

/// @brief Lowering helper for BASIC builtin calls.
/// @details Encapsulates the dispatcher used to translate builtin calls into IL
///          and runtime helper invocations. The helper keeps the lowering logic
///          modular while still relying on the parent @ref Lowerer for emission,
///          diagnostics, and runtime feature requests.
struct BuiltinExprLowering
{
    /// @brief Bind the builtin lowering helper to a lowerer instance.
    /// @param lowerer Active lowering engine used to emit IL.
    explicit BuiltinExprLowering(Lowerer &lowerer) noexcept;

    /// @brief Lower a builtin call by dispatching to the correct emitter.
    /// @details Looks up the builtin in a static dispatch table and invokes the
    ///          matching emitter, falling back to the unsupported builtin handler
    ///          when no entry exists.
    /// @param expr Builtin call expression to lower.
    /// @return Lowered r-value representing the builtin result.
    [[nodiscard]] Lowerer::RVal lower(const BuiltinCallExpr &expr);

    /// @brief Signature for builtin emitter functions.
    /// @details Emitters take a lowerer and builtin call node and return the
    ///          lowered r-value.
    using EmitFn = Lowerer::RVal (*)(Lowerer &, const BuiltinCallExpr &);

    /// @brief Lower a builtin using the rule-driven registry engine.
    /// @details Delegates to the builtin registry, which applies argument
    ///          coercions, runtime helper requests, and specialised lowering
    ///          for registered builtin families.
    static Lowerer::RVal emitRuleDrivenBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);

    /// @brief Lower the LOF builtin (file length query).
    /// @details Normalizes the channel argument, emits the runtime call, and
    ///          inserts control flow to trap on runtime errors.
    static Lowerer::RVal emitLofBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);

    /// @brief Lower the EOF builtin (end-of-file predicate).
    /// @details Normalizes the channel argument, emits the runtime call, and
    ///          handles sentinel return values by trapping on errors and
    ///          widening the result to BASIC's logical representation.
    static Lowerer::RVal emitEofBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);

    /// @brief Lower the LOC builtin (current file position).
    /// @details Normalizes the channel argument, emits the runtime call, and
    ///          traps on runtime errors before returning the position value.
    static Lowerer::RVal emitLocBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);

    /// @brief Lower the ERR builtin (current runtime error code).
    /// @details Extracts the error code from the current handler context when
    ///          available; otherwise returns zero to indicate "no error."
    static Lowerer::RVal emitErrBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);

    /// @brief Fallback emitter used when no builtin lowering rule exists.
    /// @details Emits a diagnostic where possible and returns a placeholder
    ///          integer so compilation can continue.
    static Lowerer::RVal emitUnsupportedBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);

  private:
    /// @brief Borrowed lowering engine used for emission and diagnostics.
    Lowerer *lowerer_{nullptr};
};

/// @brief Lower a builtin call using an explicit lowerer.
/// @details Convenience wrapper that instantiates @ref BuiltinExprLowering and
///          forwards the call to its dispatcher.
/// @param lowerer Active lowering engine used for emission.
/// @param expr Builtin call expression to lower.
/// @return Lowered r-value representing the builtin result.
[[nodiscard]] Lowerer::RVal lowerBuiltinCall(Lowerer &lowerer, const BuiltinCallExpr &expr);

} // namespace il::frontends::basic
