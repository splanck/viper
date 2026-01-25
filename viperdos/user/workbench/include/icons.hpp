#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file icons.hpp
 * @brief Icon pixel data declarations for Workbench.
 *
 * This file declares the icon pixel data arrays used by the Workbench
 * desktop and file browser. Icons are 24x24 pixels in ARGB32 format.
 *
 * ## Pixel Format
 *
 * Each icon is stored as an array of 576 (24*24) uint32_t values.
 * Each value is an ARGB color:
 * - 0x00000000 = Transparent (not rendered)
 * - 0xFFRRGGBB = Opaque pixel with RGB color
 *
 * ## Icon Rendering
 *
 * Icons are rendered pixel-by-pixel, skipping transparent (zero) values.
 * This allows icons to have non-rectangular shapes.
 *
 * ```cpp
 * for (int py = 0; py < 24; py++) {
 *     for (int px = 0; px < 24; px++) {
 *         uint32_t color = icon[py * 24 + px];
 *         if (color != 0) {
 *             fb[y + py][x + px] = color;
 *         }
 *     }
 * }
 * ```
 *
 * ## Icon Design
 *
 * Icons use a limited palette matching the Workbench theme:
 * - WB_BLUE (0xFF0055AA) - Primary color
 * - WB_WHITE (0xFFFFFFFF) - Highlights
 * - WB_BLACK (0xFF000000) - Outlines
 * - WB_GRAY_LIGHT (0xFFAAAAAA) - Fills
 *
 * @see icons.cpp for the actual pixel data
 * @see desktop.cpp for icon rendering
 */
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace workbench::icons {

//===----------------------------------------------------------------------===//
// Desktop Icons (24x24)
//===----------------------------------------------------------------------===//

/**
 * @defgroup DesktopIcons Desktop Icons
 * @brief Icons displayed on the Workbench desktop.
 * @{
 */

/**
 * @brief Disk/volume icon for mounted filesystems.
 *
 * A floppy disk icon used for SYS:, C:, and other volume icons.
 */
extern const uint32_t disk_24[24 * 24];

/**
 * @brief Shell/terminal icon.
 *
 * A console/terminal icon for launching the shell.
 */
extern const uint32_t shell_24[24 * 24];

/**
 * @brief Settings/preferences icon.
 *
 * A gear/cog icon for the preferences application.
 */
extern const uint32_t settings_24[24 * 24];

/**
 * @brief About/help icon.
 *
 * A question mark or info icon for the About dialog.
 */
extern const uint32_t about_24[24 * 24];

/** @} */ // end DesktopIcons

//===----------------------------------------------------------------------===//
// File Browser Icons (24x24)
//===----------------------------------------------------------------------===//

/**
 * @defgroup FileBrowserIcons File Browser Icons
 * @brief Icons displayed in file browser windows.
 * @{
 */

/**
 * @brief Closed folder icon.
 *
 * Standard folder icon for directories.
 */
extern const uint32_t folder_24[24 * 24];

/**
 * @brief Open folder icon.
 *
 * Folder icon showing open state (currently unused).
 */
extern const uint32_t folder_open_24[24 * 24];

/**
 * @brief Generic file icon.
 *
 * Default icon for unknown file types.
 */
extern const uint32_t file_24[24 * 24];

/**
 * @brief Executable file icon.
 *
 * Icon for .sys and .prg files.
 */
extern const uint32_t file_exe_24[24 * 24];

/**
 * @brief Text file icon.
 *
 * Icon for .txt, .c, .h, .cpp, .hpp, .md files.
 */
extern const uint32_t file_text_24[24 * 24];

/**
 * @brief Parent directory icon.
 *
 * Icon for ".." entry to navigate up (currently unused).
 */
extern const uint32_t parent_24[24 * 24];

/** @} */ // end FileBrowserIcons

} // namespace workbench::icons
