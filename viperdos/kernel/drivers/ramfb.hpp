//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/ramfb.hpp
// Purpose: QEMU RAM framebuffer (ramfb) driver interface.
// Key invariants: Framebuffer at fixed address; configured via fw_cfg.
// Ownership/Lifetime: Global framebuffer; initialized once.
// Links: kernel/drivers/ramfb.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

/**
 * @file ramfb.hpp
 * @brief QEMU RAM framebuffer (ramfb) driver interface.
 *
 * @details
 * QEMU's `ramfb` device provides a simple linear framebuffer backed by guest
 * RAM. The guest configures the device by writing a small configuration
 * structure via fw_cfg, which specifies the framebuffer address, pixel format,
 * and dimensions.
 *
 * This module:
 * - Configures the framebuffer to a requested resolution.
 * - Exposes basic drawing primitives (put pixel, fill rectangle, clear).
 *
 * The framebuffer is currently placed at a fixed physical address and assumed
 * to be directly accessible (identity-mapped).
 */
namespace ramfb {

// Framebuffer information
/**
 * @brief Describes the configured framebuffer properties.
 *
 * @details
 * `address` is the base address of the framebuffer in memory. The current
 * implementation uses a fixed address and treats it as both physical and
 * virtual under the identity map.
 */
struct FramebufferInfo {
    u64 address; // Physical/virtual address of framebuffer
    u32 width;   // Width in pixels
    u32 height;  // Height in pixels
    u32 pitch;   // Bytes per line
    u32 bpp;     // Bits per pixel
};

/**
 * @brief Initialize and configure the RAM framebuffer.
 *
 * @details
 * Locates the `etc/ramfb` fw_cfg file, computes stride/size for the requested
 * resolution, clears the framebuffer memory, and writes the configuration via
 * the fw_cfg DMA interface.
 *
 * @param width Desired width in pixels.
 * @param height Desired height in pixels.
 * @return `true` on success, otherwise `false`.
 */
bool init(u32 width, u32 height);

/**
 * @brief Initialize the ramfb module with an external framebuffer.
 *
 * @details
 * Used when the framebuffer is already configured by an external source
 * (e.g., UEFI GOP). This skips the fw_cfg configuration and just stores
 * the provided framebuffer parameters.
 *
 * @param address Physical address of the framebuffer.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param pitch Bytes per scanline.
 * @param bpp Bits per pixel.
 * @return `true` on success.
 */
bool init_external(u64 address, u32 width, u32 height, u32 pitch, u32 bpp);

/**
 * @brief Get the current framebuffer configuration.
 *
 * @return Reference to the framebuffer info structure.
 */
const FramebufferInfo &get_info();

/**
 * @brief Get a pointer to the framebuffer pixel memory.
 *
 * @return Pointer to a linear array of 32-bit pixels, or `nullptr` if not initialized.
 */
u32 *get_framebuffer();

/**
 * @brief Draw a single pixel.
 *
 * @details
 * Writes a pixel value at (x,y) if the framebuffer is initialized and the
 * coordinates are within bounds.
 *
 * @param x X coordinate in pixels.
 * @param y Y coordinate in pixels.
 * @param color Pixel color value (expected format is XRGB8888 in current setup).
 */
void put_pixel(u32 x, u32 y, u32 color);

/**
 * @brief Fill a rectangle region with a solid color.
 *
 * @param x Left coordinate.
 * @param y Top coordinate.
 * @param w Width in pixels.
 * @param h Height in pixels.
 * @param color Pixel color value.
 */
void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);

/**
 * @brief Clear the entire framebuffer to a color.
 *
 * @param color Pixel color value.
 */
void clear(u32 color);

} // namespace ramfb
