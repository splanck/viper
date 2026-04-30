//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/SchedulerPass.hpp
// Purpose: Declare the x86-64 post-RA scheduling pass.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes {

class SchedulerPass final : public Pass {
  public:
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::x64::passes

