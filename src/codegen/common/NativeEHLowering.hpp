//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

#include <optional>
#include <string>

namespace zanna::codegen::common {

/// @brief Rewrite structured EH into ordinary IL calls/branches for native codegen.
/// @return True when the module was changed.
bool lowerNativeEh(il::core::Module &module);

/// @brief Report the first structured EH marker that survived native EH lowering.
/// @details Native backends expect @ref lowerNativeEh to erase all structured EH
///          markers and handler resume-token block shapes before IL optimization
///          and MIR lowering. A non-empty result is a backend-lowering bug.
[[nodiscard]] std::optional<std::string> findResidualStructuredEh(const il::core::Module &module);

} // namespace zanna::codegen::common
