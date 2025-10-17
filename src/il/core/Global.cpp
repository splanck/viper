//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::Global` helpers.
/// @details All global declaration behaviour currently lives inline in the
/// header. Retaining this file signals where future utilities (metadata
/// materialisation, verification glue, etc.) belong without disrupting build
/// dependencies when they are introduced.

#include "il/core/Global.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
