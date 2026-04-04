//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_theora.h
// Purpose: Theora video codec decoder (decode only, from Theora I spec).
//   Supports I-frames and P-frames in OGG container.
//
// Key invariants:
//   - Based on VP3: 8x8 DCT blocks, Huffman coding, YCbCr 4:2:0.
//   - No B-frames (only I and P frames).
//   - Three headers: identification (0x80), comment (0x81), setup (0x82).
//   - Output: YCbCr 4:2:0 planes, caller converts via rt_ycbcr.h.
//
// Links: rt_ycbcr.h, rt_videoplayer.h, Theora spec: https://www.theora.org/doc/Theora.pdf
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THEORA_MAX_HUFFMAN_TABLES 80

/// @brief Theora decoder context.
typedef struct {
    /* Identification header fields */
    uint8_t version_major, version_minor, version_sub;
    uint32_t frame_width, frame_height;       /* encoded frame (multiple of 16) */
    uint32_t pic_width, pic_height;           /* visible picture */
    uint32_t pic_x, pic_y;                    /* picture offset within frame */
    uint32_t fps_num, fps_den;                /* frame rate fraction */
    uint32_t aspect_num, aspect_den;          /* pixel aspect ratio */
    uint8_t color_space;                      /* 0=unspecified, 1=Rec470M, 2=Rec470BG */
    uint8_t pixel_format;                     /* 0=4:2:0, 2=4:2:2, 3=4:4:4 */
    uint32_t quality_hint;
    uint32_t keyframe_granule_shift;

    /* Setup header fields */
    uint8_t loop_filter_limits[64];           /* per-QI loop filter limit */
    /* Quantization matrices: [inter/intra][plane][qi][coeff] */
    uint16_t qmat[2][3][64][64];              /* simplified: base matrices */
    int32_t qmat_count;

    /* Decode state */
    int8_t headers_complete;                  /* 1 after all 3 headers parsed */
    int32_t superblock_cols, superblock_rows; /* superblock grid dimensions */
    int32_t macro_cols, macro_rows;           /* macroblock grid */
    int32_t block_cols, block_rows;           /* 8x8 block grid */

    /* Reference frames (YCbCr 4:2:0 planes) */
    uint8_t *ref_y, *ref_cb, *ref_cr;        /* last decoded reference */
    uint8_t *gold_y, *gold_cb, *gold_cr;     /* golden frame reference */
    int32_t y_stride, c_stride;              /* plane strides */
    int32_t y_height, c_height;              /* plane heights */

    /* Current decode output */
    uint8_t *cur_y, *cur_cb, *cur_cr;        /* current frame being decoded */

    /* Internal/private decoder state. */
    void *priv;
} theora_decoder_t;

/// @brief Initialize a Theora decoder context.
void theora_decoder_init(theora_decoder_t *dec);

/// @brief Free decoder resources.
void theora_decoder_free(theora_decoder_t *dec);

/// @brief Decode a Theora header packet (identification, comment, or setup).
/// @param data Raw OGG packet data.
/// @param len Packet length.
/// @return 0 on success, -1 on error, 1 if this is a data packet (not a header).
int theora_decode_header(theora_decoder_t *dec, const uint8_t *data, size_t len);

/// @brief Decode a Theora data (video frame) packet.
/// @param data Raw OGG packet data.
/// @param len Packet length.
/// @param out_y  Output Y plane pointer (set on success).
/// @param out_cb Output Cb plane pointer (set on success).
/// @param out_cr Output Cr plane pointer (set on success).
/// @return 0 on success, -1 on error.
int theora_decode_frame(theora_decoder_t *dec, const uint8_t *data, size_t len,
                         const uint8_t **out_y, const uint8_t **out_cb,
                         const uint8_t **out_cr);

/// @brief Check if a packet is a Theora header (starts with 0x80-0x82 + "theora").
int theora_is_header_packet(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
