//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SelectCaseRange.hpp
// Purpose: Shared helpers for SELECT CASE range handling across semantic analysis and lowering. 
// Key invariants: Maintains consistent 32-bit numeric limits for CASE labels.
// Ownership/Lifetime: Header-only utilities without state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace il::frontends::basic
{

inline constexpr int64_t kCaseLabelMin = static_cast<int64_t>(std::numeric_limits<int32_t>::min());
inline constexpr int64_t kCaseLabelMax = static_cast<int64_t>(std::numeric_limits<int32_t>::max());

inline std::string makeSelectCaseLabelRangeMessage(int64_t raw)
{
    std::string msg = "CASE label ";
    msg += std::to_string(raw);
    msg += " is outside 32-bit signed range";
    return msg;
}

} // namespace il::frontends::basic
