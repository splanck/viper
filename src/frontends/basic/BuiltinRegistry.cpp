//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the declarative BASIC builtin registry used by the semantic
// analyser and lowering pipeline.  The translation unit owns the static tables
// that map source spellings to enumerators, provide lowering/scan rules, and
// expose hooks for dynamically registered intrinsics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Declarative metadata and lookup utilities for BASIC builtins.
/// @details The registry centralises builtin descriptors so semantic analysis
///          and the lowering stages can agree on signatures, runtime feature
///          requirements, and lowering strategies.  Helper functions in this
///          unit perform lazy initialisation of lookup tables and expose a
///          stable API for both static and dynamically registered builtins.

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/builtins/MathBuiltins.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace il::frontends::basic
{
namespace
{
using B = BuiltinCallExpr::Builtin;
using LowerRule = BuiltinLoweringRule;
using ResultSpec = LowerRule::ResultSpec;
using Variant = LowerRule::Variant;
using Condition = Variant::Condition;
using VariantKind = Variant::Kind;
using Argument = LowerRule::Argument;
using Transform = LowerRule::ArgTransform;
using TransformKind = LowerRule::ArgTransform::Kind;
using Feature = LowerRule::Feature;
using FeatureAction = LowerRule::Feature::Action;

constexpr std::size_t kBuiltinCount = static_cast<std::size_t>(B::Timer) + 1;

/// @brief Convert a builtin enumerator into a dense array index.
/// @details All tables in this file store entries densely in declaration order
///          so the enumerator's numeric value doubles as the correct array
///          index.  Using a helper keeps the casting logic centralised.
/// @param b Builtin enumerator being indexed.
/// @return Zero-based array index corresponding to @p b.
constexpr std::size_t idx(B b) noexcept
{
    return static_cast<std::size_t>(b);
}

struct TransparentStringHash
{
    using is_transparent = void;

    std::size_t operator()(std::string_view sv) const noexcept
    {
        return std::hash<std::string_view>{}(sv);
    }

    std::size_t operator()(const char *sv) const noexcept
    {
        return std::hash<std::string_view>{}(sv);
    }

    std::size_t operator()(const std::string &s) const noexcept
    {
        return std::hash<std::string_view>{}(s);
    }
};

struct TransparentStringEqual
{
    using is_transparent = void;

    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept
    {
        return lhs == rhs;
    }
};

using HandlerMap =
    std::unordered_map<std::string, BuiltinHandler, TransparentStringHash, TransparentStringEqual>;

/// @brief Access the singleton map storing dynamically registered builtins.
/// @details The registry persists for the lifetime of the process, allowing
///          tools or tests to inject custom builtin implementations at runtime.
///          It is initialised on first use and thereafter reused without further
///          allocation.
/// @return Reference to the mutable handler registry.
HandlerMap &builtinHandlerRegistry()
{
    static HandlerMap registry{};
    return registry;
}

/// @brief Describes broad type categories produced by a builtin.
enum class TypeMask : std::uint8_t
{
    None = 0,
    I64 = 1U << 0U,
    F64 = 1U << 1U,
    Str = 1U << 2U,
};

/// @brief Combine two @ref TypeMask values.
/// @details Enables ergonomic composition of descriptor masks without verbose
///          static casts at every call site.
/// @param lhs First mask operand.
/// @param rhs Second mask operand.
/// @return Bitwise OR of the supplied masks.
constexpr TypeMask operator|(TypeMask lhs, TypeMask rhs) noexcept
{
    return static_cast<TypeMask>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

struct Arity
{
    std::uint8_t minArgs;
    std::uint8_t maxArgs;
};

struct BuiltinDescriptor
{
    const char *name;
    B builtin;
    Arity arity;
    TypeMask typeMask;
    SemanticAnalyzer::BuiltinAnalyzer analyze;
};

/// @brief Build the compile-time descriptor table for each builtin.
/// @details Names remain uppercase because @ref lookupBuiltin performs exact,
///          case-sensitive matches on normalized BASIC keywords.  The arity
///          bounds mirror @ref SemanticAnalyzer::builtinSignature so
///          diagnostics continue to enforce the same argument requirements, and
///          the type masks communicate the possible result categories to
///          inference helpers.  The helper remains constexpr so the resulting
///          table can populate other constant data structures without runtime
///          overhead.
/// @return Fully populated descriptor table.
constexpr std::array<BuiltinDescriptor, kBuiltinCount> makeBuiltinDescriptors()
{
    std::array<BuiltinDescriptor, kBuiltinCount> descriptors{};
#define BUILTIN(NAME, DESCRIPTOR_EXPR, LOWER_EXPR, SCAN_EXPR)                                      \
    descriptors[idx(B::NAME)] = DESCRIPTOR_EXPR;
#include "frontends/basic/builtin_registry.inc"
#undef BUILTIN
    return descriptors;
}

constexpr auto kBuiltinDescriptors = makeBuiltinDescriptors();

/// @brief Create the lightweight info table consumed by semantic analysis.
/// @details Extracts the name and analyser callback from the descriptor table
///          so the semantic analyser can perform lookups without depending on
///          lowering metadata.
/// @return Array mapping builtins to @ref BuiltinInfo records.
constexpr std::array<BuiltinInfo, kBuiltinCount> makeBuiltinInfos()
{
    std::array<BuiltinInfo, kBuiltinCount> infos{};
    for (const auto &desc : kBuiltinDescriptors)
        infos[idx(desc.builtin)] = BuiltinInfo{desc.name, desc.analyze};
    return infos;
}

constexpr auto kBuiltins = makeBuiltinInfos();

/// @brief Generate the lowering rule table referenced by lowering passes.
/// @details Uses an immediately-invoked lambda so additional registration steps
///          (such as math builtins) can run after the table is initialised while
///          still keeping the storage static-duration.
static const std::array<LowerRule, kBuiltinCount> kBuiltinLoweringRules = []
{
    std::array<LowerRule, kBuiltinCount> rules{};
#define BUILTIN(NAME, DESCRIPTOR_EXPR, LOWER_EXPR, SCAN_EXPR) rules[idx(B::NAME)] = LOWER_EXPR;
#include "frontends/basic/builtin_registry.inc"
#undef BUILTIN
    builtins::registerMathBuiltinLoweringRules(rules);
    return rules;
}();

/// @brief Populate the scan rule table for builtin argument traversal.
/// @details Similar to the lowering table, this lambda initialises the scan
///          metadata at static initialisation time while still allowing the math
///          builtin helpers to inject additional patterns.
static const std::array<BuiltinScanRule, kBuiltinCount> kBuiltinScanRules = []
{
    std::array<BuiltinScanRule, kBuiltinCount> rules{};
#define BUILTIN(NAME, DESCRIPTOR_EXPR, LOWER_EXPR, SCAN_EXPR) rules[idx(B::NAME)] = SCAN_EXPR;
#include "frontends/basic/builtin_registry.inc"
#undef BUILTIN
    builtins::registerMathBuiltinScanRules(rules);
    return rules;
}();

/// @brief Access the lazily constructed map from builtin names to enumerators.
/// @details Entries are stored with canonical uppercase spellings so callers
///          should normalize BASIC identifiers before lookup.  The map is built
///          on first use using descriptor data and cached for subsequent
///          lookups.
/// @return Reference to the immutable name-to-enum map.
const std::unordered_map<std::string_view, B> &builtinNameIndex()
{
    static const auto index = []
    {
        std::unordered_map<std::string_view, B> map;
        map.reserve(kBuiltinDescriptors.size());
        for (const auto &desc : kBuiltinDescriptors)
            map.emplace(desc.name, desc.builtin);
        return map;
    }();
    return index;
}
} // namespace

/// @brief Fetch metadata for a BASIC built-in represented by its enum value.
/// @param b Builtin enumerator to inspect.
/// @return Reference to the metadata describing the semantic analysis hook.
/// @pre @p b must be a valid BuiltinCallExpr::Builtin enumerator; passing an
///      out-of-range value results in undefined behavior due to direct array
///      indexing.
const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b)
{
    return kBuiltins[static_cast<std::size_t>(b)];
}

/// @brief Retrieve the argument arity constraints for a builtin.
/// @param b Builtin enumerator to inspect.
/// @return Structure containing minimum and maximum argument counts.
/// @pre @p b must be a valid BuiltinCallExpr::Builtin enumerator.
BuiltinArity getBuiltinArity(BuiltinCallExpr::Builtin b)
{
    const auto &desc = kBuiltinDescriptors[static_cast<std::size_t>(b)];
    return BuiltinArity{desc.arity.minArgs, desc.arity.maxArgs};
}

/// @brief Resolve a BASIC built-in enum from its source spelling.
/// @param name Canonical BASIC keyword spelling to look up. The string is
///        matched exactly; callers must provide the normalized uppercase form
///        including any suffix markers.
/// @return Built-in enumerator on success; std::nullopt when the name is not
///         registered.
std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name)
{
    const auto &index = builtinNameIndex();
    auto it = index.find(name);
    if (it == index.end())
        return std::nullopt;
    return it->second;
}

/// @brief Access the declarative scan rule for a BASIC builtin.
/// @param b Builtin enumerator whose rule is requested.
/// @return Reference to the rule describing argument traversal and runtime features.
const BuiltinScanRule &getBuiltinScanRule(BuiltinCallExpr::Builtin b)
{
    return kBuiltinScanRules[static_cast<std::size_t>(b)];
}

/// @brief Access the lowering rule for a BASIC builtin.
/// @param b Builtin enumerator identifying the builtin.
/// @return Reference to the declarative lowering description.
const BuiltinLoweringRule &getBuiltinLoweringRule(BuiltinCallExpr::Builtin b)
{
    return kBuiltinLoweringRules[static_cast<std::size_t>(b)];
}

/// @brief Register or remove a dynamically provided builtin handler.
/// @details Installing a non-null handler either adds a new entry or replaces
///          an existing one keyed by @p name.  Passing @c nullptr removes the
///          handler, allowing tests to restore the default behaviour.
/// @param name Canonical builtin name in uppercase form.
/// @param fn Handler function to associate with @p name, or nullptr to remove.
void register_builtin(std::string_view name, BuiltinHandler fn)
{
    auto &registry = builtinHandlerRegistry();
    if (fn)
        registry.insert_or_assign(std::string{name}, fn);
    else
        registry.erase(std::string{name});
}

/// @brief Locate a dynamically registered builtin handler.
/// @details Performs an exact lookup against the canonical uppercase spelling.
///          When no handler exists the function returns @c nullptr so callers
///          can fall back to the static registry.
/// @param name Canonical builtin name to query.
/// @return Installed handler or nullptr when none is registered.
BuiltinHandler find_builtin(std::string_view name)
{
    auto &registry = builtinHandlerRegistry();
    if (auto it = registry.find(name); it != registry.end())
        return it->second;
    return nullptr;
}

} // namespace il::frontends::basic
