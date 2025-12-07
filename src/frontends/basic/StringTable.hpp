//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/StringTable.hpp
// Purpose: String literal interning and deduplication for BASIC frontend.
//
// This header now re-exports the common StringTable from frontends/common/.
// The implementation has been moved to the common library to be shared
// by all language frontends (BASIC, Pascal, etc.).
//
// For new code, prefer using frontends/common/StringTable.hpp directly.
//
// Links: docs/architecture.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/common/StringTable.hpp"

namespace il::frontends::basic
{

// Re-export StringTable from common library for backward compatibility
using ::il::frontends::common::StringTable;

} // namespace il::frontends::basic
