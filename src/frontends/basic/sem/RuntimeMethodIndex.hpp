//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: src/frontends/basic/sem/RuntimeMethodIndex.hpp
// Purpose: Index runtime class methods with parsed signatures for lookup.
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BasicTypes.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{

/// @brief Convert ILScalarType to BasicType.
/// @param t The IL scalar type from the RuntimeRegistry.
/// @return Corresponding BasicType for the BASIC frontend.
BasicType toBasicType(il::runtime::ILScalarType t);

struct RuntimeMethodInfo
{
    BasicType ret{BasicType::Unknown};
    std::vector<BasicType> args; // excludes receiver (arg0)
    std::string target;          // Canonical extern name
};

class RuntimeMethodIndex
{
  public:
    /// @brief Seed is now a no-op; RuntimeRegistry handles indexing.
    void seed();

    [[nodiscard]] std::optional<RuntimeMethodInfo> find(std::string_view classQName,
                                                        std::string_view method,
                                                        std::size_t arity) const;

    /// @brief List available candidates for a class+method (all arities).
    [[nodiscard]] std::vector<std::string> candidates(std::string_view classQName,
                                                      std::string_view method) const;
};

RuntimeMethodIndex &runtimeMethodIndex();

} // namespace il::frontends::basic
