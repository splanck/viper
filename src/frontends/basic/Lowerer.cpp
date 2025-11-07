//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lowerer.cpp
// Purpose: Provide the orchestration entry points that drive BASIC to IL
//          lowering. The implementation wires together the specialised helper
//          translation units while keeping this TU as a lightweight dispatcher.
// Key invariants: Sub-components remain singletons owned by the Lowerer
//                 instance and are re-used across procedure invocations.
// Ownership/Lifetime: Owns the lowering helpers and IR builder façade used to
//                     emit IL, but borrows AST nodes from callers.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "frontends/basic/lower/Emitter.hpp"

using il::core::Type;
using il::frontends::basic::BasicType;

namespace
{

Type ilTypeForBasicRet(const std::string &fnName, BasicType hint)
{
    using K = Type::Kind;
    if (hint == BasicType::String)
        return Type(K::Str);
    if (hint == BasicType::Float)
        return Type(K::F64);
    if (hint == BasicType::Int)
        return Type(K::I64);
    if (hint == BasicType::Void)
        return Type(K::Void);
    if (!fnName.empty())
    {
        char c = fnName.back();
        if (c == '$')
            return Type(K::Str);
        if (c == '#')
            return Type(K::F64);
    }
    return Type(K::I64);
}

} // namespace

namespace il::frontends::basic
{

Lowerer::Type Lowerer::functionRetTypeFromHint(const std::string &fnName, BasicType hint) const
{
    return ilTypeForBasicRet(fnName, hint);
}

std::optional<::il::frontends::basic::Type> Lowerer::findMethodReturnType(
    std::string_view className, std::string_view methodName) const
{
    if (className.empty())
        return std::nullopt;

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
    if (!info)
        return std::nullopt;

    auto it = info->methods.find(canonicalizeIdentifier(methodName));
    if (it == info->methods.end())
        return std::nullopt;
    if (it->second.returnType)
        return it->second.returnType;

    if (auto suffixType = inferAstTypeFromSuffix(methodName))
        return suffixType;

    return std::nullopt;
}

/// @brief Construct a lowering driver composed of specialised helper stages.
/// @details Instantiates the program-, procedure-, and statement-level lowering
///          helpers together with the IL emitter facade.  The @p boundsChecks
///          flag is threaded through to downstream helpers so they can reserve
///          additional metadata (for example, array bounds slots) when runtime
///          checking is enabled.
/// @param boundsChecks Enables generation of defensive array bounds logic when true.
Lowerer::Lowerer(bool boundsChecks)
    : programLowering(std::make_unique<ProgramLowering>(*this)),
      procedureLowering(std::make_unique<ProcedureLowering>(*this)),
      statementLowering(std::make_unique<StatementLowering>(*this)), boundsChecks(boundsChecks),
      emitter_(std::make_unique<lower::Emitter>(*this))
{
}

/// @brief Default destructor to anchor unique_ptr members in the translation unit.
/// @details The helpers own caches and refer back to the parent @ref Lowerer, so
///          providing the destructor here keeps ownership consistent even when
///          compiled as part of a shared library.
Lowerer::~Lowerer() = default;

/// @brief Lower an entire BASIC program into IL.
/// @details Constructs an empty @ref Module, delegates to the program-level
///          lowering helper to populate it, and returns the finished module by
///          value.  Per-procedure state is reset by the helper as each routine is
///          emitted, so the @ref Lowerer instance can be reused for subsequent
///          compilation phases.
/// @param prog Parsed BASIC program to translate.
/// @return Populated IL module representing @p prog.
Lowerer::Module Lowerer::lowerProgram(const Program &prog)
{
    Module module;
    programLowering->run(prog, module);
    return module;
}

/// @brief Convenience wrapper that lowers an entire program.
/// @details Present to keep the legacy API surface intact.  Forwarding to
///          @ref lowerProgram allows embedders to keep calling @ref lower while
///          the implementation details reside in a single code path.
/// @param prog Parsed BASIC program to translate.
/// @return Populated IL module representing @p prog.
Lowerer::Module Lowerer::lower(const Program &prog)
{
    return lowerProgram(prog);
}

/// @brief Lower a single procedure into IL using the configured helpers.
/// @details Materialises a lowering context, recomputes procedure metadata,
///          allocates required basic blocks, and finally emits IL for the
///          procedure body.  The @p config callbacks allow callers to inject
///          custom prologue/epilogue behaviour (for example, specialised return
///          handling) without reimplementing the lowering pipeline.
/// @param name Procedure identifier.
/// @param params Parameter declarations describing the procedure signature.
/// @param body Ordered statement list comprising the procedure body.
/// @param config Behavioural hooks that customise prologue/epilogue emission.
void Lowerer::lowerProcedure(const std::string &name,
                             const std::vector<Param> &params,
                             const std::vector<StmtPtr> &body,
                             const ProcedureConfig &config)
{
    auto ctx = procedureLowering->makeContext(name, params, body, config);
    procedureLowering->resetContext(ctx);
    procedureLowering->collectProcedureInfo(ctx);
    procedureLowering->scheduleBlocks(ctx);
    procedureLowering->emitProcedureIL(ctx);
}

} // namespace il::frontends::basic
