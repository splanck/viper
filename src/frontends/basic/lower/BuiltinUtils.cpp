//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the registry-backed dispatcher for BASIC builtin lowering.  Small
// family-specific handlers are registered against builtin names so the entry
// point only needs to resolve the handler and invoke it.  The heavy lifting is
// delegated to the shared utilities in BuiltinCommon and the specialised
// handlers housed under frontends/basic/builtins.
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

} // namespace il::frontends::basic::lower
