//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gif.h
// Purpose: GIF87a/89a image decoder with LZW decompression and animation support.
// Key invariants:
//   - Supports both GIF87a and GIF89a formats
//   - Returns one Pixels object per frame in RGBA format (0xRRGGBBAA)
//   - Per-frame delay and disposal method extracted from Graphics Control Extension
//   - Transparency: transparent color index produces alpha=0, others alpha=0xFF
// Ownership/Lifetime:
//   - Caller owns the returned gif_frame_t array and each Pixels object within it
//   - Free the array with free(), release Pixels via normal GC
// Links: src/runtime/graphics/rt_pixels.h (Pixels public API),
//        src/runtime/graphics/rt_sprite.c (animated sprite integration)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief A single decoded GIF frame.
typedef struct {
    void *pixels;       ///< Pixels object (RGBA, via pixels_alloc)
    int delay_ms;       ///< Frame display time in milliseconds (from GCE, 0 if unset)
    int dispose_method; ///< 0=none, 1=keep, 2=restore-to-bg, 3=restore-to-prev
} gif_frame_t;

/// @brief Decode all frames from a GIF file.
/// @param filepath Path to the GIF file (C string).
/// @param out_frames Receives malloc'd array of gif_frame_t. Caller must free().
/// @param out_frame_count Receives number of frames decoded.
/// @param out_width Receives logical screen width.
/// @param out_height Receives logical screen height.
/// @return Frame count (>0) on success, 0 on failure.
int gif_decode_file(const char *filepath,
                    gif_frame_t **out_frames,
                    int *out_frame_count,
                    int *out_width,
                    int *out_height);

#ifdef __cplusplus
}
#endif
