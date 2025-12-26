//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: src/frontends/basic/sem/RuntimePropertyIndex.hpp
// Purpose: Index runtime class properties for case-insensitive lookup.
// Invariants: Keys are case-insensitive; values reference canonical strings.
// Ownership: Index stores copies of strings for stable access.
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::runtime
{
struct RuntimeClass; // fwd
}

namespace il::frontends::basic
{

struct RuntimePropertyInfo
{
    std::string type;     ///< IL scalar type string (e.g., "i64", "i1").
    std::string getter;   ///< Canonical extern target for getter.
    std::string setter;   ///< Canonical extern target for setter (empty if none).
    bool readonly{false}; ///< True if setter is empty.
};

class RuntimePropertyIndex
{
  public:
    void seed(const std::vector<il::runtime::RuntimeClass> &classes);

    /// @brief Find property info for class + property name.
    [[nodiscard]] std::optional<RuntimePropertyInfo> find(std::string_view classQName,
                                                          std::string_view propName) const;

  private:
    static std::string toLower(std::string_view s);
    static std::string keyFor(std::string_view cls, std::string_view prop);
    std::unordered_map<std::string, RuntimePropertyInfo> map_;
};

/// @brief Access singleton property index.
RuntimePropertyIndex &runtimePropertyIndex();

} // namespace il::frontends::basic
