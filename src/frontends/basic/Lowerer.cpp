//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/RuntimeStatementLowerer.hpp"
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
/// @brief Implements if functionality.
/// @param BasicType::String Parameter description needed.
/// @return Return value description needed.
    if (hint == BasicType::String)
        return Type(K::Str);
/// @brief Implements if functionality.
/// @param BasicType::Float Parameter description needed.
/// @return Return value description needed.
    if (hint == BasicType::Float)
        return Type(K::F64);
/// @brief Implements if functionality.
/// @param BasicType::Int Parameter description needed.
/// @return Return value description needed.
    if (hint == BasicType::Int)
        return Type(K::I64);
/// @brief Implements if functionality.
/// @param BasicType::Bool Parameter description needed.
/// @return Return value description needed.
    if (hint == BasicType::Bool)
        return Type(K::I1);
/// @brief Implements if functionality.
/// @param BasicType::Void Parameter description needed.
/// @return Return value description needed.
    if (hint == BasicType::Void)
        return Type(K::Void);
/// @brief Implements if functionality.
/// @param !fnName.empty( Parameter description needed.
/// @return Return value description needed.
    if (!fnName.empty())
    {
        char c = fnName.back();
/// @brief Implements if functionality.
/// @param '$' Parameter description needed.
/// @return Return value description needed.
        if (c == '$')
            return Type(K::Str);
/// @brief Implements if functionality.
/// @param '#' Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param nsStack_.empty( Parameter description needed.
/// @return Return value description needed.
    if (count == 0 || nsStack_.empty())
        return;
/// @brief Implements if functionality.
/// @param nsStack_.size( Parameter description needed.
/// @return Return value description needed.
    if (count > nsStack_.size())
        count = nsStack_.size();
    nsStack_.erase(nsStack_.end() - static_cast<std::ptrdiff_t>(count), nsStack_.end());
}

std::string Lowerer::qualify(const std::string &klass) const
{
    // Empty name → return as-is.
/// @brief Implements if functionality.
/// @param klass.empty( Parameter description needed.
/// @return Return value description needed.
    if (klass.empty())
        return klass;

    // Fully-qualified name (contains '.') → return unchanged.
/// @brief Implements if functionality.
/// @param klass.find('.' Parameter description needed.
/// @return Return value description needed.
    if (klass.find('.') != std::string::npos)
        return klass;

    // No active namespace → return unqualified.
/// @brief Implements if functionality.
/// @param nsStack_.empty( Parameter description needed.
/// @return Return value description needed.
    if (nsStack_.empty())
        return klass;

    // Qualify with current namespace: currentNs + "." + klass
    std::string out;
    // Compute final size to reserve capacity conservatively.
    std::size_t size = klass.size();
/// @brief Implements for functionality.
/// @param nsStack_ Parameter description needed.
/// @return Return value description needed.
    for (const auto &s : nsStack_)
        size += s.size() + 1; // segment + dot
    out.reserve(size);
/// @brief Implements for functionality.
/// @param nsStack_.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 0; i < nsStack_.size(); ++i)
    {
/// @brief Implements if functionality.
/// @param i Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param className.empty( Parameter description needed.
/// @return Return value description needed.
    if (className.empty())
        return std::nullopt;

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
/// @brief Implements if functionality.
/// @param !info Parameter description needed.
/// @return Return value description needed.
    if (!info)
        return std::nullopt;

    auto it = info->methods.find(std::string(methodName));
/// @brief Implements if functionality.
/// @param info->methods.end( Parameter description needed.
/// @return Return value description needed.
    if (it == info->methods.end())
        return std::nullopt;
/// @brief Implements if functionality.
/// @param it->second.sig.returnType Parameter description needed.
/// @return Return value description needed.
    if (it->second.sig.returnType)
        return it->second.sig.returnType;

/// @brief Implements if functionality.
/// @param inferAstTypeFromSuffix(methodName Parameter description needed.
/// @return Return value description needed.
    if (auto suffixType = inferAstTypeFromSuffix(methodName))
        return suffixType;

    return std::nullopt;
}

std::string Lowerer::findMethodReturnClassName(std::string_view className,
                                               std::string_view methodName) const
{
/// @brief Implements if functionality.
/// @param className.empty( Parameter description needed.
/// @return Return value description needed.
    if (className.empty())
        return {};

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
/// @brief Implements if functionality.
/// @param !info Parameter description needed.
/// @return Return value description needed.
    if (!info)
        return {};

    auto it = info->methods.find(std::string(methodName));
/// @brief Implements if functionality.
/// @param info->methods.end( Parameter description needed.
/// @return Return value description needed.
    if (it == info->methods.end())
        return {};

    // BUG-099 fix: Return the object class name if method returns an object
/// @brief Implements if functionality.
/// @param !it->second.sig.returnClassName.empty( Parameter description needed.
/// @return Return value description needed.
    if (!it->second.sig.returnClassName.empty())
        return it->second.sig.returnClassName;

    return {};
}

std::optional<::il::frontends::basic::Type> Lowerer::findFieldType(std::string_view className,
                                                                   std::string_view fieldName) const
{
/// @brief Implements if functionality.
/// @param className.empty( Parameter description needed.
/// @return Return value description needed.
    if (className.empty())
        return std::nullopt;

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
/// @brief Implements if functionality.
/// @param !info Parameter description needed.
/// @return Return value description needed.
    if (!info)
        return std::nullopt;

    // Search through fields (case-insensitive comparison)
/// @brief Implements fieldNameUpper functionality.
/// @param fieldName Parameter description needed.
/// @return Return value description needed.
    std::string fieldNameUpper(fieldName);
    std::transform(fieldNameUpper.begin(), fieldNameUpper.end(), fieldNameUpper.begin(), ::toupper);
/// @brief Implements for functionality.
/// @param info->fields Parameter description needed.
/// @return Return value description needed.
    for (const auto &field : info->fields)
    {
        std::string storedNameUpper = field.name;
        std::transform(
            storedNameUpper.begin(), storedNameUpper.end(), storedNameUpper.begin(), ::toupper);
/// @brief Implements if functionality.
/// @param fieldNameUpper Parameter description needed.
/// @return Return value description needed.
        if (storedNameUpper == fieldNameUpper)
            return field.type;
    }

    return std::nullopt;
}

bool Lowerer::isFieldArray(std::string_view className, std::string_view fieldName) const
{
/// @brief Implements if functionality.
/// @param className.empty( Parameter description needed.
/// @return Return value description needed.
    if (className.empty())
        return false;

    const ClassInfo *info = oopIndex_.findClass(std::string(className));
/// @brief Implements if functionality.
/// @param !info Parameter description needed.
/// @return Return value description needed.
    if (!info)
        return false;

    // Search through fields (case-insensitive comparison)
/// @brief Implements fieldNameUpper functionality.
/// @param fieldName Parameter description needed.
/// @return Return value description needed.
    std::string fieldNameUpper(fieldName);
    std::transform(fieldNameUpper.begin(), fieldNameUpper.end(), fieldNameUpper.begin(), ::toupper);
/// @brief Implements for functionality.
/// @param info->fields Parameter description needed.
/// @return Return value description needed.
    for (const auto &field : info->fields)
    {
        std::string storedNameUpper = field.name;
        std::transform(
            storedNameUpper.begin(), storedNameUpper.end(), storedNameUpper.begin(), ::toupper);
/// @brief Implements if functionality.
/// @param fieldNameUpper Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements procedureLowering functionality.
/// @param std::make_unique<ProcedureLowering>(*this Parameter description needed.
/// @return Return value description needed.
      procedureLowering(std::make_unique<ProcedureLowering>(*this)),
/// @brief Implements statementLowering functionality.
/// @param std::make_unique<StatementLowering>(*this Parameter description needed.
/// @return Return value description needed.
      statementLowering(std::make_unique<StatementLowering>(*this)), boundsChecks(boundsChecks),
/// @brief Emits ter_.
/// @param std::make_unique<lower::Emitter>(*this Parameter description needed.
/// @return Return value description needed.
      emitter_(std::make_unique<lower::Emitter>(*this)),
/// @brief Implements ioStmtLowerer_ functionality.
/// @param std::make_unique<IoStatementLowerer>(*this Parameter description needed.
/// @return Return value description needed.
      ioStmtLowerer_(std::make_unique<IoStatementLowerer>(*this)),
/// @brief Implements ctrlStmtLowerer_ functionality.
/// @param std::make_unique<ControlStatementLowerer>(*this Parameter description needed.
/// @return Return value description needed.
      ctrlStmtLowerer_(std::make_unique<ControlStatementLowerer>(*this)),
/// @brief Implements runtimeStmtLowerer_ functionality.
/// @param std::make_unique<RuntimeStatementLowerer>(*this Parameter description needed.
/// @return Return value description needed.
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
