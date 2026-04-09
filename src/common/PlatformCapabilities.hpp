//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/common/PlatformCapabilities.hpp
// Purpose: Shared host/build capability vocabulary for C++ code outside the
//          C runtime platform layer.
// Key invariants:
//   - Normal C++ code should prefer these macros/constants over raw host
//     preprocessor probes.
//   - Host OS/compiler and build capability flags are generated once by CMake.
// Ownership/Lifetime:
//   - Header-only constants; no runtime state.
// Links: src/runtime/rt_platform.h, docs/cross-platform/platform-checklist.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "viper/platform/Capabilities.hpp"
