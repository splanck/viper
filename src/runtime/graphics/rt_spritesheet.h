//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_spritesheet.h
// Purpose: Sprite sheet/atlas for named region extraction from a single Pixels texture, enabling
// efficient storage of multiple sprites in one image.
//
// Key invariants:
//   - Regions are stored by name; duplicate names overwrite the previous region.
//   - rt_spritesheet_get_region returns coordinates into the atlas; it does not copy pixels.
//   - All regions must reference valid coordinates within the atlas bounds.
//   - The atlas Pixels object must remain valid while the spritesheet is in use.
//
// Ownership/Lifetime:
//   - Spritesheet objects are heap-allocated opaque pointers.
//   - The atlas Pixels is not retained; caller must ensure its lifetime exceeds the spritesheet.
//
// Links: src/runtime/graphics/rt_spritesheet.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new sprite sheet from an atlas Pixels buffer.
    /// @param atlas_pixels Pixels object containing the full atlas.
    /// @return Opaque sprite sheet handle.
    void *rt_spritesheet_new(void *atlas_pixels);

    /// @brief Create a sprite sheet with uniform grid layout.
    /// @param atlas_pixels Pixels object.
    /// @param frame_w Width of each cell.
    /// @param frame_h Height of each cell.
    /// @return Sprite sheet with auto-named regions ("0", "1", ...).
    void *rt_spritesheet_from_grid(void *atlas_pixels, int64_t frame_w, int64_t frame_h);

    /// @brief Define a named region within the atlas.
    /// @param sheet Sprite sheet handle.
    /// @param name Region name (e.g., "walk_0").
    /// @param x Left edge of region.
    /// @param y Top edge of region.
    /// @param w Width of region.
    /// @param h Height of region.
    void rt_spritesheet_set_region(
        void *sheet, rt_string name, int64_t x, int64_t y, int64_t w, int64_t h);

    /// @brief Extract a named region as a new Pixels buffer.
    /// @param sheet Sprite sheet handle.
    /// @param name Region name.
    /// @return New Pixels object with the region data, or NULL if not found.
    void *rt_spritesheet_get_region(void *sheet, rt_string name);

    /// @brief Check if a region name exists.
    /// @param sheet Sprite sheet handle.
    /// @param name Region name.
    /// @return 1 if exists, 0 otherwise.
    int8_t rt_spritesheet_has_region(void *sheet, rt_string name);

    /// @brief Get the number of defined regions.
    /// @param sheet Sprite sheet handle.
    /// @return Number of regions.
    int64_t rt_spritesheet_region_count(void *sheet);

    /// @brief Get the width of the underlying atlas.
    /// @param sheet Sprite sheet handle.
    /// @return Width in pixels.
    int64_t rt_spritesheet_width(void *sheet);

    /// @brief Get the height of the underlying atlas.
    /// @param sheet Sprite sheet handle.
    /// @return Height in pixels.
    int64_t rt_spritesheet_height(void *sheet);

    /// @brief Get all region names as a Seq.
    /// @param sheet Sprite sheet handle.
    /// @return Seq of region name strings.
    void *rt_spritesheet_region_names(void *sheet);

    /// @brief Remove a named region.
    /// @param sheet Sprite sheet handle.
    /// @param name Region name.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_spritesheet_remove_region(void *sheet, rt_string name);

#ifdef __cplusplus
}
#endif
