// File: src/il/transform/PassManager.hpp
// Purpose: Declares a minimal pass manager for IL transformations.
// Key invariants: Passes run sequentially; verifier checks after each in debug builds.
// Ownership/Lifetime: PassManager holds no state beyond registered callbacks.
// Links: docs/class-catalog.md
#pragma once

#include "il/core/Module.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::transform
{

using PassFn = std::function<void(core::Module &)>;

/// @brief Simple pass manager running named passes on a module.
class PassManager
{
  public:
    /// @brief Register pass @p name invoking @p fn.
    void addPass(const std::string &name, PassFn fn);

    /// @brief Run passes in order specified by @p names on @p m.
    void run(core::Module &m, const std::vector<std::string> &names) const;

  private:
    std::unordered_map<std::string, PassFn> passes_;
};

} // namespace il::transform
