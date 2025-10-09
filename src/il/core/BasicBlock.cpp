//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//
// This translation unit anchors out-of-line definitions for
// il::core::BasicBlock. The class currently exposes only inline operations,
// but keeping the dedicated implementation file in the project tree makes it
// trivial to extend block-specific behavior without reshaping build scripts or
// increasing header churn across the compiler.
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"

namespace il::core
{

// No out-of-line methods.
} // namespace il::core
