//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/PassManager.hpp
// Purpose: Target-independent pass manager templated on backend Module type.
// Key invariants: Passes run sequentially, short-circuiting on failure.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/Diagnostics.hpp"

#include <memory>
#include <vector>

namespace viper::codegen::common
{

/// @brief Abstract interface implemented by individual pipeline passes.
/// @tparam ModuleT The backend-specific module state type.
template <typename ModuleT>
class Pass
{
  public:
    virtual ~Pass() = default;
    /// @brief Execute the pass over @p module, emitting diagnostics to @p diags.
    virtual bool run(ModuleT &module, Diagnostics &diags) = 0;
};

/// @brief Container sequencing registered passes for execution.
/// @tparam ModuleT The backend-specific module state type.
template <typename ModuleT>
class PassManager
{
  public:
    /// @brief Add a pass to the manager; ownership is transferred.
    void addPass(std::unique_ptr<Pass<ModuleT>> pass)
    {
        passes_.push_back(std::move(pass));
    }

    /// @brief Execute all registered passes in order.
    /// @return False when a pass signals failure; true otherwise.
    bool run(ModuleT &module, Diagnostics &diags) const
    {
        for (const auto &pass : passes_)
        {
            if (!pass->run(module, diags))
            {
                return false;
            }
        }
        return true;
    }

  private:
    std::vector<std::unique_ptr<Pass<ModuleT>>> passes_{};
};

} // namespace viper::codegen::common
