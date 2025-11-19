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
#include "frontends/basic/ControlStatementLowerer.hpp"
#include "frontends/basic/IoStatementLowerer.hpp"
#include "frontends/basic/RuntimeStatementLowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include <algorithm>
#include <cctype>

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
    if (hint == BasicType::Bool)
        return Type(K::I1);
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

void Lowerer::pushNamespace(const std::vector<std::string> &path)
{
    nsStack_.insert(nsStack_.end(), path.begin(), path.end());
}

void Lowerer::popNamespace(std::size_t count)
{
    if (count == 0 || nsStack_.empty())
        return;
    if (count > nsStack_.size())
        count = nsStack_.size();
    nsStack_.erase(nsStack_.end() - static_cast<std::ptrdiff_t>(count), nsStack_.end());
}

std::string Lowerer::qualify(const std::string &klass) const
{
    // Empty name → return as-is.
    if (klass.empty())
        return klass;

    // Fully-qualified name (contains '.') → return unchanged.
    if (klass.find('.') != std::string::npos)
        return klass;

    // No active namespace → return unqualified.
    if (nsStack_.empty())
        return klass;

    // Qualify with current namespace: currentNs + "." + klass
    std::string out;
    // Compute final size to reserve capacity conservatively.
    std::size_t size = klass.size();
    for (const auto &s : nsStack_)
        size += s.size() + 1; // segment + dot
    out.reserve(size);
    for (std::size_t i = 0; i < nsStack_.size(); ++i)
    {
        if (i)
            out.push_back('.');
        out.append(nsStack_[i]);
    }
    out.push_back('.');
    out.append(klass);
    return out;
}

std::optional<::il::frontends::basic::Type> Lowerer::findMethodReturnType(
    std::string_view className, std::string_view methodName) const
{
    if (className.empty())
        return std::nullopt;

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
    if (!info)
        return std::nullopt;

    auto it = info->methods.find(std::string(methodName));
    if (it == info->methods.end())
        return std::nullopt;
    if (it->second.sig.returnType)
        return it->second.sig.returnType;

    if (auto suffixType = inferAstTypeFromSuffix(methodName))
        return suffixType;

    return std::nullopt;
}

std::string Lowerer::findMethodReturnClassName(std::string_view className,
                                                 std::string_view methodName) const
{
    if (className.empty())
        return {};

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
    if (!info)
        return {};

    auto it = info->methods.find(std::string(methodName));
    if (it == info->methods.end())
        return {};

    // BUG-099 fix: Return the object class name if method returns an object
    if (!it->second.sig.returnClassName.empty())
        return it->second.sig.returnClassName;

    return {};
}

std::optional<::il::frontends::basic::Type> Lowerer::findFieldType(std::string_view className,
                                                                   std::string_view fieldName) const
{
    if (className.empty())
        return std::nullopt;

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
    if (!info)
        return std::nullopt;

    // Search through fields (case-insensitive comparison)
    std::string fieldNameUpper(fieldName);
    std::transform(fieldNameUpper.begin(), fieldNameUpper.end(), fieldNameUpper.begin(), ::toupper);
    for (const auto &field : info->fields)
    {
        std::string storedNameUpper = field.name;
        std::transform(
            storedNameUpper.begin(), storedNameUpper.end(), storedNameUpper.begin(), ::toupper);
        if (storedNameUpper == fieldNameUpper)
            return field.type;
    }

    return std::nullopt;
}

bool Lowerer::isFieldArray(std::string_view className, std::string_view fieldName) const
{
    if (className.empty())
        return false;

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
    if (!info)
        return false;

    // Search through fields (case-insensitive comparison)
    std::string fieldNameUpper(fieldName);
    std::transform(fieldNameUpper.begin(), fieldNameUpper.end(), fieldNameUpper.begin(), ::toupper);
    for (const auto &field : info->fields)
    {
        std::string storedNameUpper = field.name;
        std::transform(
            storedNameUpper.begin(), storedNameUpper.end(), storedNameUpper.begin(), ::toupper);
        if (storedNameUpper == fieldNameUpper)
            return field.isArray;
    }

    return false;
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
      emitter_(std::make_unique<lower::Emitter>(*this)),
      ioStmtLowerer_(std::make_unique<IoStatementLowerer>(*this)),
      ctrlStmtLowerer_(std::make_unique<ControlStatementLowerer>(*this)),
      runtimeStmtLowerer_(std::make_unique<RuntimeStatementLowerer>(*this))
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
