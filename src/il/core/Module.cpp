//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::Module` helpers.
/// @details `Module` currently defines all behaviour inline. Keeping this file in
/// the build advertises the sanctioned location for future cross-cutting helpers
/// (verification adapters, serialization glue, etc.) while maintaining stable
/// build dependencies for downstream projects.

#include "il/core/Module.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
