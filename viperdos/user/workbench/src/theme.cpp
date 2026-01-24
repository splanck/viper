//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/workbench/src/theme.cpp
// Purpose: Theme system implementation.
//
//===----------------------------------------------------------------------===//

#include "../include/theme.hpp"

namespace workbench {

// Default to Classic Amiga theme
const Theme *g_currentTheme = &themes::ClassicAmiga;

void setTheme(const Theme *theme) {
    if (theme) {
        g_currentTheme = theme;
    }
}

} // namespace workbench
