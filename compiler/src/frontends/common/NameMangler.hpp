//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/NameMangler.hpp
// Purpose: Generates deterministic, unique names for IL symbols during lowering.
//
// This header provides a NameMangler class that generates unique names for
// temporaries and basic blocks during AST-to-IL lowering. It is shared across
// multiple language frontends (BASIC, Pascal, etc.).
//
// Name mangling is essential for translating source language identifiers
// into IL's internal representation while ensuring:
// - Uniqueness: No name collisions between user variables, temporaries, blocks
// - Determinism: Identical programs always produce identical IL names
// - Readability: Generated names remain somewhat human-readable for debugging
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace il::frontends::common
{

//===----------------------------------------------------------------------===//
// OOP Name Mangling Functions
//===----------------------------------------------------------------------===//

/// @brief Join two identifiers with a dot separator: "Class.Member"
/// @param lhs Class portion of the identifier
/// @param rhs Member portion (method, constructor, destructor name)
/// @return Joined identifier string
inline std::string mangleMethod(std::string_view className, std::string_view methodName)
{
    std::string result;
    result.reserve(className.size() + methodName.size() + 1);
    result.append(className);
    result.push_back('.');
    result.append(methodName);
    return result;
}

/// @brief Mangle a constructor name: "ClassName.CtorName"
/// @details Pascal uses explicit constructor names (e.g., Create),
///          while BASIC uses a fixed ".__ctor" suffix.
inline std::string mangleConstructor(std::string_view className, std::string_view ctorName)
{
    return mangleMethod(className, ctorName);
}

/// @brief Mangle a destructor name: "ClassName.DtorName"
/// @details Pascal uses explicit destructor names (e.g., Destroy),
///          while BASIC uses a fixed ".__dtor" suffix.
inline std::string mangleDestructor(std::string_view className, std::string_view dtorName)
{
    return mangleMethod(className, dtorName);
}

/// @brief Mangle a BASIC-style constructor: "ClassName.__ctor"
inline std::string mangleClassCtor(std::string_view className)
{
    std::string result;
    result.reserve(className.size() + 7);
    result.append(className);
    result.append(".__ctor");
    return result;
}

/// @brief Mangle a BASIC-style destructor: "ClassName.__dtor"
inline std::string mangleClassDtor(std::string_view className)
{
    std::string result;
    result.reserve(className.size() + 7);
    result.append(className);
    result.append(".__dtor");
    return result;
}

/// @brief Sanitize dots in a qualified name by replacing with '$'
/// @details Used for interface thunk naming where dots aren't allowed
inline std::string sanitizeDots(std::string_view qualifiedName)
{
    std::string out;
    out.reserve(qualifiedName.size());
    for (char c : qualifiedName)
        out.push_back(c == '.' ? '$' : c);
    return out;
}

/// @brief Produce a stable name for an interface registration thunk.
/// @details Example: __iface_reg$A$B$I for interface A.B.I
inline std::string mangleIfaceRegThunk(std::string_view qualifiedIface)
{
    std::string s = sanitizeDots(qualifiedIface);
    return std::string("__iface_reg$") + s;
}

/// @brief Produce a stable name for a class->interface bind thunk.
/// @details Example: __iface_bind$A$C$A$B$I for class A.C binding A.B.I
inline std::string mangleIfaceBindThunk(std::string_view qualifiedClass,
                                        std::string_view qualifiedIface)
{
    std::string cs = sanitizeDots(qualifiedClass);
    std::string is = sanitizeDots(qualifiedIface);
    return std::string("__iface_bind$") + cs + "$" + is;
}

/// @brief Name for a BASIC-style OOP module initializer: "__mod_init$oop"
inline std::string mangleOopModuleInit()
{
    return "__mod_init$oop";
}

/// @brief Name for a Pascal-style OOP module initializer: "__pas_oop_init"
inline std::string manglePascalOopInit()
{
    return "__pas_oop_init";
}

/// @brief Generates deterministic names for temporaries and blocks.
/// @details Used during AST-to-IL lowering to create unique names.
/// @invariant Temp IDs increase sequentially; block names gain numeric suffixes on collision.
/// @ownership Pure utility; no external ownership.
class NameMangler
{
  public:
    /// @brief Construct a NameMangler with default temp prefix "%t".
    NameMangler() = default;

    /// @brief Construct a NameMangler with a custom temp prefix.
    /// @param tempPrefix Prefix for temporary names (e.g., "%t", "$tmp")
    explicit NameMangler(std::string tempPrefix) : tempPrefix_(std::move(tempPrefix)) {}

    /// @brief Return next temporary name (e.g., "%t0", "%t1", ...).
    /// @return A unique temporary name using the configured prefix.
    std::string nextTemp()
    {
        return tempPrefix_ + std::to_string(tempCounter_++);
    }

    /// @brief Return a block label based on @p hint ("entry", "then", ...).
    /// @details If the hint was used before, a numeric suffix is appended.
    /// @param hint The semantic hint for the block name.
    /// @return A unique block name, possibly with a numeric suffix.
    std::string block(const std::string &hint)
    {
        auto &count = blockCounters_[hint];
        std::string name = hint;
        if (count > 0)
            name += std::to_string(count);
        ++count;
        return name;
    }

    /// @brief Reset all counters for a new compilation unit.
    void reset()
    {
        tempCounter_ = 0;
        blockCounters_.clear();
    }

    /// @brief Get the current temp counter value (for debugging/testing).
    unsigned tempCount() const
    {
        return tempCounter_;
    }

  private:
    /// @brief Prefix for temporary names (default: "%t").
    std::string tempPrefix_ = "%t";

    /// @brief Monotonically increasing ID for temporary names.
    unsigned tempCounter_ = 0;

    /// @brief Map of block name hints to the number of times they've been used.
    std::unordered_map<std::string, unsigned> blockCounters_;
};

} // namespace il::frontends::common
