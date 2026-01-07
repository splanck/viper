//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: src/frontends/basic/sem/RuntimeMethodIndex.hpp
// Purpose: Index runtime class methods with parsed signatures for lookup.
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BasicTypes.hpp"
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

struct RuntimeMethodInfo
{
    BasicType ret{BasicType::Unknown};
    std::vector<BasicType> args; // excludes receiver (arg0)
    std::string target;          // Canonical extern name
};

class RuntimeMethodIndex
{
  public:
    void seed(const std::vector<il::runtime::RuntimeClass> &classes);

    [[nodiscard]] std::optional<RuntimeMethodInfo> find(std::string_view classQName,
                                                        std::string_view method,
                                                        std::size_t arity) const;

    /// @brief List available candidates for a class+method (all arities).
    [[nodiscard]] std::vector<std::string> candidates(std::string_view classQName,
                                                      std::string_view method) const;

  private:
    static std::string toLower(std::string_view s);
    static std::string keyFor(std::string_view cls, std::string_view method, std::size_t arity);
    static BasicType mapIlToken(std::string_view tok);
    static bool parseSignature(std::string_view sig, RuntimeMethodInfo &out);
    std::unordered_map<std::string, RuntimeMethodInfo> map_;
};

RuntimeMethodIndex &runtimeMethodIndex();

} // namespace il::frontends::basic
