#pragma once

#include "../include/types.hpp"

/**
 * @file gcon.hpp
 * @brief Simple framebuffer-backed graphics console.
 *
 * @details
 * The graphics console ("gcon") renders monospaced text into the RAM
 * framebuffer provided by the `ramfb` driver. It is primarily used to show
 * boot status and basic diagnostics when a graphical output device is
 * available, while keeping the implementation small enough for early boot.
 *
 * The console tracks a cursor in character-cell coordinates and supports a
 * minimal set of control characters (`\\n`, `\\r`, `\\t`, `\\b`). Scrolling is
 * implemented by moving framebuffer contents upward and clearing the last line.
 *
 * Color values are specified as 32-bit pixels in the same packed format used
 * by the framebuffer driver (typically XRGB8888/ARGB8888 depending on the
 * platform configuration).
 */
namespace gcon
{

/**
 * @brief Convenience color constants used by the boot UI.
 *
 * @details
 * These constants encode a small palette in packed 32-bit pixel format.
 * The exact channel ordering depends on the framebuffer format expected by
 * `ramfb::put_pixel`, but the values are chosen to look reasonable on the
 * QEMU virt RAM framebuffer configuration.
 */
namespace colors
{
constexpr u32 VIPER_GREEN = 0xFF00AA44;
constexpr u32 VIPER_DARK_BROWN = 0xFF1A1208;
constexpr u32 VIPER_YELLOW = 0xFFFFDD00;
constexpr u32 VIPER_WHITE = 0xFFEEEEEE;
constexpr u32 VIPER_RED = 0xFFCC3333;
constexpr u32 BLACK = 0xFF000000;
} // namespace colors

/**
 * @brief Initialize the graphics console.
 *
 * @details
 * Binds the console to the RAM framebuffer provided by `ramfb`, computes the
 * available number of character cells based on the font metrics, clears the
 * screen to the default background color, and resets the cursor to the origin.
 *
 * @return `true` if a framebuffer is available and initialization succeeds,
 *         otherwise `false`.
 */
bool init();

/**
 * @brief Determine whether the graphics console is ready for use.
 *
 * @details
 * The graphics console becomes available after a successful call to @ref init.
 * Callers can use this to decide whether to mirror output to the framebuffer.
 *
 * @return `true` if initialized, otherwise `false`.
 */
bool is_available();

/**
 * @brief Output a single character to the graphics console.
 *
 * @details
 * Renders one printable ASCII character at the current cursor location and
 * advances the cursor. For basic control characters:
 * - `\\n` moves to the next line (with scrolling as needed).
 * - `\\r` moves to column 0 of the current line.
 * - `\\t` advances to the next 8-column tab stop.
 * - `\\b` erases the previous character cell on the current line.
 *
 * If the console is not available, the call is a no-op.
 *
 * @param c The character to output.
 */
void putc(char c);

/**
 * @brief Output a NUL-terminated string to the graphics console.
 *
 * @details
 * Writes characters sequentially using @ref putc. If the console is not
 * available the call is a no-op.
 *
 * @param s Pointer to a NUL-terminated string.
 */
void puts(const char *s);

/**
 * @brief Clear the graphics console.
 *
 * @details
 * Fills the framebuffer with the current background color and resets the
 * cursor to the origin (0,0). If the console is not available, the call is a
 * no-op.
 */
void clear();

/**
 * @brief Set the active foreground and background colors.
 *
 * @details
 * These colors are used for subsequent text rendering. Existing framebuffer
 * contents are not retroactively recolored.
 *
 * @param fg Foreground (text) color value.
 * @param bg Background color value.
 */
void set_colors(u32 fg, u32 bg);

/**
 * @brief Get the current cursor position in character cells.
 *
 * @param x Output: current column.
 * @param y Output: current row.
 */
void get_cursor(u32 &x, u32 &y);

/**
 * @brief Set the current cursor position in character cells.
 *
 * @details
 * Positions outside the current console bounds are ignored/clamped by the
 * implementation. This is primarily intended for simple boot UI layout.
 *
 * @param x Desired column.
 * @param y Desired row.
 */
void set_cursor(u32 x, u32 y);

/**
 * @brief Get the console dimensions in character cells.
 *
 * @param cols Output: number of character columns.
 * @param rows Output: number of character rows.
 */
void get_size(u32 &cols, u32 &rows);

} // namespace gcon
