//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief BASIC-frontend alias for the shared string table.
/// @details This header re-exports the common string interning table from
///          `frontends/common/StringTable.hpp` so existing BASIC code can keep
///          including the legacy path. New code should include the common
///          header directly to avoid redundant aliases.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/common/StringTable.hpp"

namespace il::frontends::basic
{

/// @brief Backward-compatible alias for the shared string interning table.
/// @details The aliased type manages string literal deduplication and
///          deterministic label generation for IL globals. Prefer the common
///          namespace in new code to keep cross-frontend dependencies explicit.
using ::il::frontends::common::StringTable;

} // namespace il::frontends::basic
