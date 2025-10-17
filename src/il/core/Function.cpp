//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Placeholder translation unit for `il::core::Function` extensions.
/// @details All current `Function` members are defined inline in the header, yet
/// keeping this file in the build makes the extension point explicit for future
/// helpers (metadata attachment, verifier glue, etc.) and prevents disruptive
/// build churn when those helpers eventually materialise.


#include "il/core/Function.hpp"

namespace il::core
{

// No out-of-line methods yet.
} // namespace il::core
