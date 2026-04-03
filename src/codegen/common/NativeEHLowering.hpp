//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/NativeEHLowering.hpp
// Purpose: Lower IL EH markers into native-friendly control flow before backend
// lowering.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Module.hpp"

namespace viper::codegen::common {

/// @brief Rewrite structured EH into ordinary IL calls/branches for native codegen.
/// @return True when the module was changed.
bool lowerNativeEh(il::core::Module &module);

} // namespace viper::codegen::common
