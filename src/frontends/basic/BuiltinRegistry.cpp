// File: src/frontends/basic/BuiltinRegistry.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements registry of BASIC built-ins for semantic analysis and
//          lowering dispatch.
// Key invariants: Registry entries correspond 1:1 with BuiltinCallExpr::Builtin
//                 enum order.
// Ownership/Lifetime: Static data only.
// Links: docs/codemap.md

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/builtins/MathBuiltins.hpp"
#include <array>
#include <cstdint>
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

constexpr std::size_t kBuiltinCount = static_cast<std::size_t>(B::Loc) + 1;

constexpr std::size_t idx(B b) noexcept
{
    return static_cast<std::size_t>(b);
}

using HandlerMap = std::unordered_map<std::string_view, BuiltinHandler>;

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

/// @brief Declarative builtin table used for name/enum lookups.
/// Names remain uppercase because lookupBuiltin() performs exact, case-sensitive
/// matches on normalized BASIC keywords. The arity bounds mirror
/// SemanticAnalyzer::builtinSignature() so diagnostics continue to enforce the
/// same argument requirements, and the type masks communicate the possible
/// result categories to inference helpers.
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

constexpr std::array<BuiltinInfo, kBuiltinCount> makeBuiltinInfos()
{
    std::array<BuiltinInfo, kBuiltinCount> infos{};
    for (const auto &desc : kBuiltinDescriptors)
        infos[idx(desc.builtin)] = BuiltinInfo{desc.name, desc.analyze};
    return infos;
}

constexpr auto kBuiltins = makeBuiltinInfos();

static const std::array<LowerRule, kBuiltinCount> kBuiltinLoweringRules = []
{
    std::array<LowerRule, kBuiltinCount> rules{};
#define BUILTIN(NAME, DESCRIPTOR_EXPR, LOWER_EXPR, SCAN_EXPR) rules[idx(B::NAME)] = LOWER_EXPR;
#include "frontends/basic/builtin_registry.inc"
#undef BUILTIN
    builtins::registerMathBuiltinLoweringRules(rules);
    return rules;
}();

static const std::array<BuiltinScanRule, kBuiltinCount> kBuiltinScanRules = []
{
    std::array<BuiltinScanRule, kBuiltinCount> rules{};
#define BUILTIN(NAME, DESCRIPTOR_EXPR, LOWER_EXPR, SCAN_EXPR) rules[idx(B::NAME)] = SCAN_EXPR;
#include "frontends/basic/builtin_registry.inc"
#undef BUILTIN
    builtins::registerMathBuiltinScanRules(rules);
    return rules;
}();

/// @brief Access the lazily-initialized name-to-enum map.
/// @details Entries are stored with canonical uppercase spellings so callers
/// should normalize BASIC identifiers before lookup.
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

void register_builtin(std::string_view name, BuiltinHandler fn)
{
    auto &registry = builtinHandlerRegistry();
    if (fn)
        registry.insert_or_assign(name, fn);
    else
        registry.erase(name);
}

BuiltinHandler find_builtin(std::string_view name)
{
    auto &registry = builtinHandlerRegistry();
    if (auto it = registry.find(name); it != registry.end())
        return it->second;
    return nullptr;
}

} // namespace il::frontends::basic
