//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/BuiltinUtils.cpp
// Purpose: Materialise the registry-backed dispatcher used to lower BASIC
//          builtin calls.
// Key invariants: Each builtin name resolves to at most one handler and the
//                 registry initialises exactly once per process.
// Ownership/Lifetime: Relies on process-wide registration tables and does not
//                     allocate persistent state beyond handler bindings.
// Links: docs/basic-language.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/BuiltinUtils.hpp"

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/builtins/StringBuiltins.hpp"
#include "frontends/basic/lower/BuiltinCommon.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace il::frontends::basic::lower
{
namespace
{
constexpr std::string_view kDiagMissingBuiltinEmitter = "B4004";

using Builtin = BuiltinCallExpr::Builtin;

/// @brief Lower string-family builtins that have specialised emitters.
/// @details Resolves the builtin specification from the string builtin registry
///          and verifies the call arity falls within the supported range. If a
///          specialised emitter exists, delegates to it; otherwise falls back to
///          the generic lowering path so callers still receive a valid result.
/// @param ctx Context aggregating lowering helpers and call metadata.
/// @return Lowered value/type pair representing the builtin result.
Lowerer::RVal lowerStringBuiltin(BuiltinLowerContext &ctx)
{
    const auto *stringSpec = builtins::findBuiltin(ctx.info().name);
    if (!stringSpec)
        return lowerGenericBuiltin(ctx);

    const std::size_t argCount = ctx.call().args.size();
    if (argCount < static_cast<std::size_t>(stringSpec->minArity) ||
        argCount > static_cast<std::size_t>(stringSpec->maxArity))
        return lowerGenericBuiltin(ctx);

    builtins::LowerCtx strCtx(ctx.lowerer(), ctx.call());
    il::core::Value resultValue = stringSpec->fn(strCtx, strCtx.values());
    return {resultValue, strCtx.resultType()};
}

/// @brief Lower math-family builtins using the default lowering pipeline.
/// @details Math builtins currently share the generic lowering implementation,
///          so the helper simply forwards to @ref lowerGenericBuiltin for
///          clarity and potential future expansion.
/// @param ctx Builtin lowering context.
/// @return Lowered builtin result.
Lowerer::RVal lowerMathBuiltin(BuiltinLowerContext &ctx)
{
    return lowerGenericBuiltin(ctx);
}

/// @brief Lower conversion builtins with specialised overflow handling.
/// @details Delegates to @ref lowerConversionBuiltinImpl, which orchestrates
///          guard blocks and runtime helpers required to match BASIC semantics.
/// @param ctx Builtin lowering context.
/// @return Lowered conversion result.
Lowerer::RVal lowerConversionBuiltin(BuiltinLowerContext &ctx)
{
    return lowerConversionBuiltinImpl(ctx);
}

/// @brief Lower a builtin using the generic lowering pipeline.
/// @details Convenience wrapper that makes call sites easier to read when they
///          opt into the shared @ref lowerGenericBuiltin implementation.
/// @param ctx Builtin lowering context.
/// @return Lowered builtin result.
Lowerer::RVal lowerDefaultBuiltin(BuiltinLowerContext &ctx)
{
    return lowerGenericBuiltin(ctx);
}

/// @brief Register a family handler for a fixed list of builtin identifiers.
/// @details Iterates @p builtins and binds each builtin name in the registry to
///          @p handler. Existing registrations are overwritten, ensuring the
///          most specific handler wins during initialisation.
/// @param handler Function pointer invoked to lower the builtin family.
/// @param builtins Collection of builtin enumerators to associate with @p handler.
void registerFamilyHandlers(BuiltinHandler handler, std::initializer_list<Builtin> builtins)
{
    for (Builtin builtin : builtins)
        register_builtin(getBuiltinInfo(builtin).name, handler);
}

/// @brief Install default handlers for all builtins that lack specialisation.
/// @details Iterates every builtin enumerator and registers the generic
///          lowering handler except for those explicitly excluded. Subsequent
///          family registrations can override the defaults.
void registerDefaultHandlers()
{
    for (std::size_t ordinal = 0; ordinal <= static_cast<std::size_t>(Builtin::Loc); ++ordinal)
    {
        auto builtin = static_cast<Builtin>(ordinal);
        switch (builtin)
        {
            case Builtin::Eof:
            case Builtin::Lof:
            case Builtin::Loc:
                break;
            default:
                register_builtin(getBuiltinInfo(builtin).name, &lowerDefaultBuiltin);
                break;
        }
    }
}

/// @brief Lazily initialise the builtin handler registry.
/// @details Uses a function-local static to perform one-time registration of
///          default handlers and family-specific overrides. The guard ensures
///          registry mutation is thread-safe under C++ static initialisation
///          rules.
void ensureBuiltinHandlers()
{
    static const bool initialized = []
    {
        registerDefaultHandlers();

        registerFamilyHandlers(&lowerStringBuiltin,
                               {Builtin::Len,
                                Builtin::Mid,
                                Builtin::Left,
                                Builtin::Right,
                                Builtin::Str,
                                Builtin::Instr,
                                Builtin::Ltrim,
                                Builtin::Rtrim,
                                Builtin::Trim,
                                Builtin::Ucase,
                                Builtin::Lcase,
                                Builtin::Chr,
                                Builtin::Asc,
                                Builtin::InKey,
                                Builtin::GetKey});

        registerFamilyHandlers(&lowerConversionBuiltin,
                               {Builtin::Val, Builtin::Cint, Builtin::Clng, Builtin::Csng});

        registerFamilyHandlers(&lowerMathBuiltin,
                               {Builtin::Cdbl,
                                Builtin::Int,
                                Builtin::Fix,
                                Builtin::Round,
                                Builtin::Sqr,
                                Builtin::Abs,
                                Builtin::Floor,
                                Builtin::Ceil,
                                Builtin::Sin,
                                Builtin::Cos,
                                Builtin::Pow,
                                Builtin::Rnd});
        return true;
    }();
    (void)initialized;
}

} // namespace

/// @brief Entry point that lowers a BASIC builtin call.
/// @details Ensures handlers are registered, looks up the call target, and
///          dispatches to the resolved handler. When no handler exists, emits a
///          diagnostic (if available) and returns a zero-valued result to keep
///          lowering progressing.
/// @param lowerer Active lowering context used to emit IL.
/// @param call AST node representing the builtin invocation.
/// @return Lowered value/type pair for the builtin result.
Lowerer::RVal lowerBuiltinCall(Lowerer &lowerer, const BuiltinCallExpr &call)
{
    ensureBuiltinHandlers();

    BuiltinLowerContext ctx(lowerer, call);
    if (BuiltinHandler handler = find_builtin(ctx.info().name))
        return handler(ctx);

    if (auto *diag = lowerer.diagnosticEmitter())
    {
        ctx.setCurrentLoc(call.loc);
        diag->emit(il::support::Severity::Error,
                   std::string(kDiagMissingBuiltinEmitter),
                   call.loc,
                   0,
                   "no emitter registered for builtin call");
    }

    return ctx.makeZeroResult();
}

} // namespace il::frontends::basic::lower
