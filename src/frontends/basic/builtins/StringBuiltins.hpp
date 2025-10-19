// File: src/frontends/basic/builtins/StringBuiltins.hpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Declares registry and lowering helpers for BASIC string built-ins.
// Key invariants: Registry lookups return immutable metadata describing name,
//                 arity, and lowering handler for supported string built-ins.
// Ownership/Lifetime: Functions operate on the lowering context supplied by
//                     the BASIC front end; no dynamic allocation is performed.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/core/Value.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic::builtins
{
using il::core::Type;
using il::core::Value;

class LowerCtx;

/// @brief Alias used for passing contiguous argument views.
template <typename T>
using ArrayRef = std::span<T>;

/// @brief Alias for lightweight string views.
using StringRef = std::string_view;

/// @brief Function signature used by builtin lowering handlers.
using LoweringFn = Value (*)(LowerCtx &, ArrayRef<Value>);

/// @brief Specification record describing a registered builtin.
struct BuiltinSpec
{
    std::string name;   ///< Canonical BASIC spelling of the builtin.
    int minArity{0};    ///< Minimum number of accepted arguments.
    int maxArity{0};    ///< Maximum number of accepted arguments.
    LoweringFn fn{nullptr}; ///< Lowering entry point for the builtin.
};

/// @brief Lookup metadata for a string builtin by BASIC spelling.
/// @param name BASIC source spelling (case sensitive).
/// @return Pointer to the builtin spec when found; nullptr otherwise.
const BuiltinSpec *findBuiltin(StringRef name);

/// @brief Helper context exposing lowering utilities to builtin handlers.
class LowerCtx
{
  public:
    /// @brief Construct a lowering context.
    /// @param lowerer Owning lowering driver.
    /// @param call Builtin call being lowered.
    LowerCtx(Lowerer &lowerer, const BuiltinCallExpr &call);

    /// @brief Retrieve the lowering driver.
    Lowerer &lowerer() const noexcept;

    /// @brief Access the builtin call AST node.
    const BuiltinCallExpr &call() const noexcept;

    /// @brief Total number of argument slots recorded on the call.
    std::size_t argCount() const noexcept;

    /// @brief Check whether argument @p idx is present.
    bool hasArg(std::size_t idx) const noexcept;

    /// @brief Source location for argument @p idx or the call site when absent.
    il::support::SourceLoc argLoc(std::size_t idx) const noexcept;

    /// @brief Access the lowered argument at @p idx.
    Lowerer::RVal &arg(std::size_t idx);

    /// @brief Access the lowered argument value at @p idx.
    Value &argValue(std::size_t idx);

    /// @brief Immutable view over the materialized argument values.
    ArrayRef<Value> values() noexcept;

    /// @brief Update the result type that the handler will produce.
    void setResultType(Type ty) noexcept;

    /// @brief Final result type populated by the handler.
    Type resultType() const noexcept;

    /// @brief Ensure argument @p idx is materialized as a 64-bit integer.
    Lowerer::RVal &ensureI64(std::size_t idx, il::support::SourceLoc loc);

    /// @brief Ensure argument @p idx is materialized as a 64-bit float.
    Lowerer::RVal &ensureF64(std::size_t idx, il::support::SourceLoc loc);

    /// @brief Coerce argument @p idx to a 64-bit integer.
    Lowerer::RVal &coerceToI64(std::size_t idx, il::support::SourceLoc loc);

    /// @brief Coerce argument @p idx to a 64-bit float.
    Lowerer::RVal &coerceToF64(std::size_t idx, il::support::SourceLoc loc);

    /// @brief Add integer constant @p immediate to argument @p idx.
    Lowerer::RVal &addConst(std::size_t idx, std::int64_t immediate, il::support::SourceLoc loc);

    /// @brief Narrow integer argument @p idx to @p target with runtime checks.
    Lowerer::RVal &narrowInt(std::size_t idx, Type target, il::support::SourceLoc loc);

    /// @brief Classify the numeric type of the provided expression.
    TypeRules::NumericType classifyNumericType(const Expr &expr);

    /// @brief Request a runtime helper feature.
    void requestHelper(il::runtime::RuntimeFeature feature);

    /// @brief Track a runtime helper feature usage.
    void trackRuntime(il::runtime::RuntimeFeature feature);

    /// @brief Emit a runtime call returning a value.
    Value emitCallRet(Type ty,
                      const char *runtime,
                      const std::vector<Value> &args,
                      il::support::SourceLoc loc);

  private:
    Lowerer::RVal &ensureLowered(std::size_t idx);
    void syncValue(std::size_t idx) noexcept;

    Lowerer &lowerer_;
    const BuiltinCallExpr &call_;
    std::vector<std::optional<Lowerer::RVal>> loweredArgs_;
    std::vector<Value> argValues_;
    std::vector<il::support::SourceLoc> argLocs_;
    std::vector<bool> hasArg_;
    Type resultType_{Type(Type::Kind::I64)};
};

} // namespace il::frontends::basic::builtins
