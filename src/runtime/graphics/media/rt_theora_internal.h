//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/media/rt_theora_internal.h
// Purpose: Shared decoder state types, lookup tables, and helper declarations
//          for the Theora video decoder, split between rt_theora.c (bitstream +
//          header + frame decode) and rt_theora_recon.c (DC prediction, IDCT,
//          loop filter, motion-comp reconstruction).
//
// Key invariants:
//   - Engine-internal; included only by the graphics/media/ theora TUs.
//   - theora_priv_t holds all per-decoder state; the recon TU reads decoded
//     coefficients/modes from it and writes reconstructed planes back.
//
// Ownership/Lifetime:
//   - theora_priv_t and its buffers are owned by the enclosing rt_theora object.
//
// Links: src/runtime/graphics/media/rt_theora.c (decode),
//        src/runtime/graphics/media/rt_theora_recon.c (reconstruction)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_theora.h"

#include <stddef.h>
#include <stdint.h>

// --- Core decoder state + bitstream/huffman/block types ---
typedef struct {
    const uint8_t *data;
    size_t len;
    size_t byte_pos;
    int bit_pos;
    int failed;
} bitreader_t;

typedef struct {
    int16_t left;
    int16_t right;
    uint8_t leaf;
    int8_t token;
} theora_huff_node_t;

typedef struct {
    theora_huff_node_t nodes[64];
    int node_count;
} theora_huff_table_t;

typedef struct {
    int32_t plane;
    int32_t bx;
    int32_t by;
    int32_t plane_raster;
    int32_t mb;
} theora_block_info_t;

typedef struct {
    int32_t luma[4];
    int32_t chroma[2][4];
} theora_mb_info_t;

typedef struct {
    int32_t plane_width[3];
    int32_t plane_height[3];
    int32_t plane_block_cols[3];
    int32_t plane_block_rows[3];
    int32_t plane_sb_cols[3];
    int32_t plane_sb_rows[3];
    int32_t plane_block_offsets[3];
    int32_t plane_sb_offsets[3];
    int32_t plane_block_counts[3];
    int32_t plane_sb_counts[3];
    int32_t total_blocks;
    int32_t total_superblocks;
    int32_t total_macroblocks;

    int32_t *plane_raster_to_coded[3];
    int32_t *mb_raster_to_coded;
    theora_block_info_t *blocks;
    theora_mb_info_t *mbs;

    uint8_t *bcoded;
    uint8_t *sb_partcoded;
    uint8_t *sb_fullcoded;
    uint8_t *qiis;
    uint8_t *mbmodes;
    uint8_t *tis;
    uint8_t *ncoeffs;
    int8_t (*mvects)[2];
    int16_t (*coeffs)[64];

    uint16_t dc_scale[64];
    uint16_t ac_scale[64];
    uint8_t bms[384][64];
    uint8_t nqrs[2][3];
    uint8_t qrsizes[2][3][64];
    uint16_t qrbmis[2][3][64];
    int nbms;

    theora_huff_table_t huff[80];
    uint8_t first_frame_decoded;
} theora_priv_t;

// --- DC prediction weight table entry ---
typedef struct {
    int16_t w[4];
    int16_t div;
} theora_dc_weight_t;

// --- Per-frame header (parsed from each frame packet) ---
typedef struct {
    int frame_type;
    int nqi;
    uint8_t qi[3];
} theora_frame_header_t;

//=============================================================================
// Shared lookup table + decode helpers (defined in rt_theora.c)
//=============================================================================

extern const uint8_t theora_zigzag[64];

void br_init(bitreader_t *br, const uint8_t *data, size_t len);
int clampi(int v, int lo, int hi);
int16_t trunc_i16(int32_t v);
int theora_ref_index_for_mode(int mode);
int decode_frame_header(theora_decoder_t *dec, bitreader_t *br, theora_frame_header_t *fh);
int decode_block_flags(theora_decoder_t *dec, theora_priv_t *priv, bitreader_t *br, int frame_type);
int decode_mb_modes(theora_decoder_t *dec, theora_priv_t *priv, bitreader_t *br, int frame_type);
int decode_motion_vectors(theora_decoder_t *dec, theora_priv_t *priv, bitreader_t *br, int frame_type);
int decode_qiis(theora_priv_t *priv, bitreader_t *br, int nqi);
int decode_coefficients(theora_priv_t *priv, bitreader_t *br);
int compute_dc_pred(const theora_priv_t *priv, const int16_t lastdc[3], int bi);
