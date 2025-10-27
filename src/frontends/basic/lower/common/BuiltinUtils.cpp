//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/common/BuiltinUtils.cpp
// Purpose: Materialise the registry-backed dispatcher used to lower BASIC
//          builtin calls.
// Key invariants: Each builtin name resolves to at most one handler and the
//                 registry initialises exactly once per process.
// Ownership/Lifetime: Relies on process-wide registration tables and does not
//                     allocate persistent state beyond handler bindings.
// Links: docs/basic-language.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/common/BuiltinUtils.hpp"

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/builtins/StringBuiltins.hpp"
#include "frontends/basic/lower/BuiltinCommon.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace il::frontends::basic::lower::common
{
namespace
{
constexpr std::string_view kDiagMissingBuiltinEmitter = "B4004";

using Builtin = BuiltinCallExpr::Builtin;

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

Lowerer::RVal lowerMathBuiltin(BuiltinLowerContext &ctx)
{
    return lowerGenericBuiltin(ctx);
}

Lowerer::RVal lowerConversionBuiltin(BuiltinLowerContext &ctx)
{
    return lowerConversionBuiltinImpl(ctx);
}

Lowerer::RVal lowerDefaultBuiltin(BuiltinLowerContext &ctx)
{
    return lowerGenericBuiltin(ctx);
}

void registerFamilyHandlers(BuiltinHandler handler, std::initializer_list<Builtin> builtins)
{
    for (Builtin builtin : builtins)
        register_builtin(getBuiltinInfo(builtin).name, handler);
}

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

void ensureBuiltinHandlersForTesting()
{
    ensureBuiltinHandlers();
}

} // namespace il::frontends::basic::lower::common
