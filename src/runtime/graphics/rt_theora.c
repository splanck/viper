//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_theora.c
// Purpose: In-tree Theora decoder for OGG video playback.
//
//===----------------------------------------------------------------------===//

#include "rt_theora.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static const uint8_t theora_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t sb_hilbert_x[16] = {
    0, 1, 1, 0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 2, 2, 3
};

static const uint8_t sb_hilbert_y[16] = {
    0, 0, 1, 1, 2, 3, 3, 2, 2, 3, 3, 2, 1, 1, 0, 0
};

static const uint8_t mb_hilbert_x[4] = {0, 0, 1, 1};
static const uint8_t mb_hilbert_y[4] = {0, 1, 1, 0};

static const uint8_t mb_mode_scheme[7][8] = {
    {3, 4, 2, 0, 1, 5, 6, 7},
    {3, 4, 0, 2, 1, 5, 6, 7},
    {3, 2, 4, 0, 1, 5, 6, 7},
    {3, 2, 0, 4, 1, 5, 6, 7},
    {0, 3, 4, 2, 1, 5, 6, 7},
    {0, 5, 3, 4, 2, 1, 6, 7},
    {0, 1, 2, 3, 4, 5, 6, 7}
};

typedef struct {
    int16_t w[4];
    int16_t div;
} theora_dc_weight_t;

static const theora_dc_weight_t dc_weights[16] = {
    {{0, 0, 0, 0}, 1},
    {{1, 0, 0, 0}, 1},
    {{0, 1, 0, 0}, 1},
    {{1, 0, 0, 0}, 1},
    {{0, 0, 1, 0}, 1},
    {{1, 0, 1, 0}, 2},
    {{0, 0, 1, 0}, 1},
    {{29, -26, 29, 0}, 32},
    {{0, 0, 0, 1}, 1},
    {{75, 0, 0, 53}, 128},
    {{0, 1, 0, 1}, 2},
    {{75, 0, 0, 53}, 128},
    {{0, 0, 1, 0}, 1},
    {{75, 0, 0, 53}, 128},
    {{0, 3, 10, 3}, 16},
    {{29, -26, 29, 0}, 32}
};

static void br_init(bitreader_t *br, const uint8_t *data, size_t len) {
    br->data = data;
    br->len = len;
    br->byte_pos = 0;
    br->bit_pos = 8;
    br->failed = 0;
}

static uint32_t br_read(bitreader_t *br, int nbits) {
    uint32_t value = 0;
    if (!br || nbits < 0 || nbits > 25) {
        if (br)
            br->failed = 1;
        return 0;
    }
    while (nbits-- > 0) {
        if (br->byte_pos >= br->len) {
            br->failed = 1;
            return 0;
        }
        value <<= 1;
        value |= (uint32_t)((br->data[br->byte_pos] >> (br->bit_pos - 1)) & 1u);
        br->bit_pos--;
        if (br->bit_pos == 0) {
            br->byte_pos++;
            br->bit_pos = 8;
        }
    }
    return value;
}

static int br_read1(bitreader_t *br) {
    return (int)br_read(br, 1);
}

static int clampi(int v, int lo, int hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int16_t trunc_i16(int32_t v) {
    return (int16_t)(uint16_t)v;
}

static int ilog_u32(uint32_t v) {
    int ret = 0;
    while (v) {
        ret++;
        v >>= 1;
    }
    return ret;
}

static int round_div_s32(int v, int d) {
    if (d <= 0)
        return 0;
    if (v >= 0)
        return (v + d / 2) / d;
    return -(((-v) + d / 2) / d);
}

static int theora_ref_index_for_mode(int mode) {
    switch (mode) {
    case 1:
        return 0;
    case 5:
    case 6:
        return 2;
    default:
        return 1;
    }
}

static int plane_mb_block_w(const theora_decoder_t *dec, int plane) {
    if (plane == 0)
        return 2;
    if (dec->pixel_format == 3)
        return 2;
    return 1;
}

static int plane_mb_block_h(const theora_decoder_t *dec, int plane) {
    if (plane == 0)
        return 2;
    if (dec->pixel_format == 0)
        return 1;
    return 2;
}

static void theora_priv_free(theora_decoder_t *dec) {
    theora_priv_t *priv = (theora_priv_t *)dec->priv;
    if (!priv)
        return;
    free(priv->plane_raster_to_coded[0]);
    free(priv->plane_raster_to_coded[1]);
    free(priv->plane_raster_to_coded[2]);
    free(priv->mb_raster_to_coded);
    free(priv->blocks);
    free(priv->mbs);
    free(priv->bcoded);
    free(priv->sb_partcoded);
    free(priv->sb_fullcoded);
    free(priv->qiis);
    free(priv->mbmodes);
    free(priv->tis);
    free(priv->ncoeffs);
    free(priv->mvects);
    free(priv->coeffs);
    free(priv);
    dec->priv = NULL;
}

static int check_header_sig(const uint8_t *data, size_t len, uint8_t type) {
    if (!data || len < 7 || data[0] != type)
        return 0;
    return data[1] == 't' && data[2] == 'h' && data[3] == 'e' &&
           data[4] == 'o' && data[5] == 'r' && data[6] == 'a';
}

static int parse_id_header(theora_decoder_t *dec, const uint8_t *data, size_t len) {
    bitreader_t br;
    if (!check_header_sig(data, len, 0x80) || len < 42)
        return -1;

    dec->version_major = data[7];
    dec->version_minor = data[8];
    dec->version_sub = data[9];
    dec->frame_width = (uint32_t)((((uint16_t)data[10] << 8) | data[11]) * 16u);
    dec->frame_height = (uint32_t)((((uint16_t)data[12] << 8) | data[13]) * 16u);
    dec->pic_width = ((uint32_t)data[14] << 16) | ((uint32_t)data[15] << 8) | data[16];
    dec->pic_height = ((uint32_t)data[17] << 16) | ((uint32_t)data[18] << 8) | data[19];
    dec->pic_x = data[20];
    dec->pic_y = data[21];
    dec->fps_num = ((uint32_t)data[22] << 24) | ((uint32_t)data[23] << 16) |
                   ((uint32_t)data[24] << 8) | data[25];
    dec->fps_den = ((uint32_t)data[26] << 24) | ((uint32_t)data[27] << 16) |
                   ((uint32_t)data[28] << 8) | data[29];
    dec->aspect_num = ((uint32_t)data[30] << 24) | ((uint32_t)data[31] << 16) |
                      ((uint32_t)data[32] << 8) | data[33];
    dec->aspect_den = ((uint32_t)data[34] << 24) | ((uint32_t)data[35] << 16) |
                      ((uint32_t)data[36] << 8) | data[37];
    dec->color_space = data[38];

    br_init(&br, data + 40, len - 40);
    dec->quality_hint = br_read(&br, 6);
    dec->keyframe_granule_shift = br_read(&br, 5);
    dec->pixel_format = (uint8_t)br_read(&br, 2);
    if (br.failed)
        return -1;

    dec->block_cols = (int32_t)(dec->frame_width / 8);
    dec->block_rows = (int32_t)(dec->frame_height / 8);
    dec->superblock_cols = (dec->block_cols + 3) / 4;
    dec->superblock_rows = (dec->block_rows + 3) / 4;
    dec->macro_cols = (dec->block_cols + 1) / 2;
    dec->macro_rows = (dec->block_rows + 1) / 2;
    return 0;
}

static int parse_comment_header(theora_decoder_t *dec, const uint8_t *data, size_t len) {
    (void)dec;
    return check_header_sig(data, len, 0x81) ? 0 : -1;
}

static int huff_parse_node(theora_huff_table_t *tab, bitreader_t *br, int depth) {
    int idx;
    if (!tab || !br || depth > 64 || tab->node_count >= 64)
        return -1;
    idx = tab->node_count++;
    memset(&tab->nodes[idx], 0, sizeof(tab->nodes[idx]));
    if (br_read1(br)) {
        tab->nodes[idx].leaf = 1;
        tab->nodes[idx].token = (int8_t)br_read(br, 5);
    } else {
        int left = huff_parse_node(tab, br, depth + 1);
        int right = huff_parse_node(tab, br, depth + 1);
        if (left < 0 || right < 0)
            return -1;
        tab->nodes[idx].left = (int16_t)left;
        tab->nodes[idx].right = (int16_t)right;
    }
    return br->failed ? -1 : idx;
}

static int huff_decode_token(const theora_huff_table_t *tab, bitreader_t *br) {
    int idx = 0;
    if (!tab || !br || tab->node_count <= 0)
        return -1;
    while (!tab->nodes[idx].leaf) {
        int bit = br_read1(br);
        if (br->failed)
            return -1;
        idx = bit ? tab->nodes[idx].right : tab->nodes[idx].left;
        if (idx < 0 || idx >= tab->node_count)
            return -1;
    }
    return tab->nodes[idx].token;
}

static int theora_alloc_layout(theora_decoder_t *dec, theora_priv_t *priv) {
    int plane;
    int32_t block_offset = 0;
    int32_t sb_offset = 0;
    int32_t total_blocks = 0;
    int32_t total_sbs = 0;
    int32_t mb_count;

    if (!dec || !priv)
        return -1;

    priv->plane_width[0] = (int32_t)dec->frame_width;
    priv->plane_height[0] = (int32_t)dec->frame_height;
    switch (dec->pixel_format) {
    case 0:
        priv->plane_width[1] = priv->plane_width[2] = (int32_t)(dec->frame_width / 2);
        priv->plane_height[1] = priv->plane_height[2] = (int32_t)(dec->frame_height / 2);
        break;
    case 2:
        priv->plane_width[1] = priv->plane_width[2] = (int32_t)(dec->frame_width / 2);
        priv->plane_height[1] = priv->plane_height[2] = (int32_t)dec->frame_height;
        break;
    case 3:
        priv->plane_width[1] = priv->plane_width[2] = (int32_t)dec->frame_width;
        priv->plane_height[1] = priv->plane_height[2] = (int32_t)dec->frame_height;
        break;
    default:
        return -1;
    }

    for (plane = 0; plane < 3; plane++) {
        priv->plane_block_cols[plane] = priv->plane_width[plane] / 8;
        priv->plane_block_rows[plane] = priv->plane_height[plane] / 8;
        priv->plane_sb_cols[plane] = (priv->plane_block_cols[plane] + 3) / 4;
        priv->plane_sb_rows[plane] = (priv->plane_block_rows[plane] + 3) / 4;
        priv->plane_block_offsets[plane] = block_offset;
        priv->plane_sb_offsets[plane] = sb_offset;
        priv->plane_block_counts[plane] =
            priv->plane_block_cols[plane] * priv->plane_block_rows[plane];
        priv->plane_sb_counts[plane] =
            priv->plane_sb_cols[plane] * priv->plane_sb_rows[plane];
        block_offset += priv->plane_block_counts[plane];
        sb_offset += priv->plane_sb_counts[plane];
    }

    total_blocks = block_offset;
    total_sbs = sb_offset;
    mb_count = dec->macro_cols * dec->macro_rows;
    priv->total_blocks = total_blocks;
    priv->total_superblocks = total_sbs;
    priv->total_macroblocks = mb_count;

    priv->plane_raster_to_coded[0] =
        (int32_t *)malloc((size_t)priv->plane_block_counts[0] * sizeof(int32_t));
    priv->plane_raster_to_coded[1] =
        (int32_t *)malloc((size_t)priv->plane_block_counts[1] * sizeof(int32_t));
    priv->plane_raster_to_coded[2] =
        (int32_t *)malloc((size_t)priv->plane_block_counts[2] * sizeof(int32_t));
    priv->mb_raster_to_coded =
        (int32_t *)malloc((size_t)mb_count * sizeof(int32_t));
    priv->blocks =
        (theora_block_info_t *)calloc((size_t)total_blocks, sizeof(theora_block_info_t));
    priv->mbs =
        (theora_mb_info_t *)calloc((size_t)mb_count, sizeof(theora_mb_info_t));
    priv->bcoded = (uint8_t *)calloc((size_t)total_blocks, 1);
    priv->sb_partcoded = (uint8_t *)calloc((size_t)total_sbs, 1);
    priv->sb_fullcoded = (uint8_t *)calloc((size_t)total_sbs, 1);
    priv->qiis = (uint8_t *)calloc((size_t)total_blocks, 1);
    priv->mbmodes = (uint8_t *)calloc((size_t)mb_count, 1);
    priv->tis = (uint8_t *)calloc((size_t)total_blocks, 1);
    priv->ncoeffs = (uint8_t *)calloc((size_t)total_blocks, 1);
    priv->mvects = (int8_t(*)[2])calloc((size_t)total_blocks, sizeof(int8_t[2]));
    priv->coeffs = (int16_t(*)[64])calloc((size_t)total_blocks, sizeof(int16_t[64]));

    if (!priv->plane_raster_to_coded[0] || !priv->plane_raster_to_coded[1] ||
        !priv->plane_raster_to_coded[2] || !priv->mb_raster_to_coded ||
        !priv->blocks || !priv->mbs || !priv->bcoded || !priv->sb_partcoded ||
        !priv->sb_fullcoded || !priv->qiis || !priv->mbmodes || !priv->tis ||
        !priv->ncoeffs || !priv->mvects || !priv->coeffs) {
        return -1;
    }

    for (int32_t mbi = 0; mbi < mb_count; mbi++) {
        for (int i = 0; i < 4; i++) {
            priv->mbs[mbi].luma[i] = -1;
            priv->mbs[mbi].chroma[0][i] = -1;
            priv->mbs[mbi].chroma[1][i] = -1;
        }
    }

    {
        int32_t mbi = 0;
        for (int32_t sby = 0; sby < dec->macro_rows; sby += 2) {
            for (int32_t sbx = 0; sbx < dec->macro_cols; sbx += 2) {
                for (int i = 0; i < 4; i++) {
                    int32_t mbx = sbx + mb_hilbert_x[i];
                    int32_t mby = sby + mb_hilbert_y[i];
                    if (mbx < dec->macro_cols && mby < dec->macro_rows)
                        priv->mb_raster_to_coded[mby * dec->macro_cols + mbx] = mbi++;
                }
            }
        }
    }

    {
        int32_t bi = 0;
        for (plane = 0; plane < 3; plane++) {
            for (int32_t sby = 0; sby < priv->plane_sb_rows[plane]; sby++) {
                for (int32_t sbx = 0; sbx < priv->plane_sb_cols[plane]; sbx++) {
                    for (int i = 0; i < 16; i++) {
                        int32_t bx = sbx * 4 + sb_hilbert_x[i];
                        int32_t by = sby * 4 + sb_hilbert_y[i];
                        int32_t raster;
                        int32_t mbx;
                        int32_t mby;
                        int32_t local_x;
                        int32_t local_y;
                        if (bx >= priv->plane_block_cols[plane] ||
                            by >= priv->plane_block_rows[plane])
                            continue;
                        raster = by * priv->plane_block_cols[plane] + bx;
                        priv->plane_raster_to_coded[plane][raster] = bi;
                        priv->blocks[bi].plane = plane;
                        priv->blocks[bi].bx = bx;
                        priv->blocks[bi].by = by;
                        priv->blocks[bi].plane_raster = raster;

                        mbx = bx / plane_mb_block_w(dec, plane);
                        mby = by / plane_mb_block_h(dec, plane);
                        priv->blocks[bi].mb =
                            priv->mb_raster_to_coded[mby * dec->macro_cols + mbx];
                        local_x = bx % plane_mb_block_w(dec, plane);
                        local_y = by % plane_mb_block_h(dec, plane);
                        if (plane == 0) {
                            int slot = local_y * 2 + local_x;
                            if (slot == 2)
                                slot = 2;
                            priv->mbs[priv->blocks[bi].mb].luma[slot] = bi;
                        } else if (dec->pixel_format == 0) {
                            priv->mbs[priv->blocks[bi].mb].chroma[plane - 1][0] = bi;
                        } else if (dec->pixel_format == 2) {
                            int slot = local_y;
                            priv->mbs[priv->blocks[bi].mb].chroma[plane - 1][slot] = bi;
                        } else {
                            int slot = local_y * 2 + local_x;
                            priv->mbs[priv->blocks[bi].mb].chroma[plane - 1][slot] = bi;
                        }
                        bi++;
                    }
                }
            }
        }
    }

    return 0;
}

static uint16_t theora_compute_qscale(const theora_priv_t *priv, int qti, int pli, int qi, int ci) {
    uint16_t qscale;
    uint16_t bm = 0;
    int qri;
    int qi_start = 0;
    if (ci == 0)
        qscale = priv->dc_scale[qi];
    else
        qscale = priv->ac_scale[qi];
    qri = 0;
    while (qri + 1 < priv->nqrs[qti][pli] &&
           qi >= qi_start + priv->qrsizes[qti][pli][qri]) {
        qi_start += priv->qrsizes[qti][pli][qri];
        qri++;
    }
    bm = priv->bms[priv->qrbmis[qti][pli][qri]][ci];
    {
        uint32_t q = (uint32_t)qscale * (uint32_t)bm / 100u;
        uint16_t floorv = (ci == 0) ? 4 : 2;
        uint16_t minv = (ci == 0) ? 8 : ((ci < 3) ? 2 : 1);
        uint32_t scaled = qti == 0 ? q * 4u : q * 3u;
        uint32_t clamped = scaled < floorv ? floorv : scaled;
        if (clamped > 4096u)
            clamped = 4096u;
        if (clamped < minv)
            clamped = minv;
        return (uint16_t)clamped;
    }
}

static int theora_finish_setup(theora_decoder_t *dec, theora_priv_t *priv) {
    int32_t fw, fh;
    size_t y_size, c_size;
    for (int qti = 0; qti < 2; qti++) {
        for (int pli = 0; pli < 3; pli++) {
            for (int qi = 0; qi < 64; qi++) {
                for (int ci = 0; ci < 64; ci++)
                    dec->qmat[qti][pli][qi][ci] =
                        theora_compute_qscale(priv, qti, pli, qi, ci);
            }
        }
    }
    dec->qmat_count = 2 * 3 * 64;

    if (theora_alloc_layout(dec, priv) != 0)
        return -1;

    fw = (int32_t)dec->frame_width;
    fh = (int32_t)dec->frame_height;
    dec->y_stride = fw;
    dec->c_stride = priv->plane_width[1];
    dec->y_height = fh;
    dec->c_height = priv->plane_height[1];
    y_size = (size_t)dec->y_stride * (size_t)dec->y_height;
    c_size = (size_t)dec->c_stride * (size_t)dec->c_height;

    dec->ref_y = (uint8_t *)calloc(y_size, 1);
    dec->ref_cb = (uint8_t *)calloc(c_size, 1);
    dec->ref_cr = (uint8_t *)calloc(c_size, 1);
    dec->gold_y = (uint8_t *)calloc(y_size, 1);
    dec->gold_cb = (uint8_t *)calloc(c_size, 1);
    dec->gold_cr = (uint8_t *)calloc(c_size, 1);
    dec->cur_y = (uint8_t *)calloc(y_size, 1);
    dec->cur_cb = (uint8_t *)calloc(c_size, 1);
    dec->cur_cr = (uint8_t *)calloc(c_size, 1);
    if (!dec->ref_y || !dec->ref_cb || !dec->ref_cr ||
        !dec->gold_y || !dec->gold_cb || !dec->gold_cr ||
        !dec->cur_y || !dec->cur_cb || !dec->cur_cr)
        return -1;
    dec->headers_complete = 1;
    return 0;
}

static int parse_setup_header(theora_decoder_t *dec, const uint8_t *data, size_t len) {
    theora_priv_t *priv;
    bitreader_t br;
    if (!check_header_sig(data, len, 0x82))
        return -1;

    priv = (theora_priv_t *)calloc(1, sizeof(*priv));
    if (!priv)
        return -1;
    dec->priv = priv;

    br_init(&br, data + 7, len - 7);
    {
        int nbits = (int)br_read(&br, 3);
        for (int qi = 0; qi < 64; qi++)
            dec->loop_filter_limits[qi] = (uint8_t)br_read(&br, nbits);
    }

    {
        int nbits = (int)br_read(&br, 4) + 1;
        for (int qi = 0; qi < 64; qi++)
            priv->ac_scale[qi] = (uint16_t)br_read(&br, nbits);
    }

    {
        int nbits = (int)br_read(&br, 4) + 1;
        for (int qi = 0; qi < 64; qi++)
            priv->dc_scale[qi] = (uint16_t)br_read(&br, nbits);
    }

    priv->nbms = (int)br_read(&br, 9) + 1;
    if (priv->nbms <= 0 || priv->nbms > 384 || br.failed)
        return -1;

    for (int bmi = 0; bmi < priv->nbms; bmi++) {
        for (int ci = 0; ci < 64; ci++)
            priv->bms[bmi][ci] = (uint8_t)br_read(&br, 8);
    }

    for (int qti = 0; qti < 2; qti++) {
        for (int pli = 0; pli < 3; pli++) {
            int newqr = (qti == 0 && pli == 0) ? 1 : br_read1(&br);
            if (newqr) {
                int qi = 0;
                int qri = 0;
                while (1) {
                    int nbits = ilog_u32((uint32_t)(priv->nbms - 1));
                    priv->qrbmis[qti][pli][qri] = (uint16_t)br_read(&br, nbits);
                    if (qi >= 63) {
                        priv->qrsizes[qti][pli][qri] = 0;
                        priv->nqrs[qti][pli] = (uint8_t)(qri + 1);
                        break;
                    }
                    nbits = ilog_u32((uint32_t)(62 - qi));
                    priv->qrsizes[qti][pli][qri] = (uint8_t)(br_read(&br, nbits) + 1);
                    qi += priv->qrsizes[qti][pli][qri];
                    qri++;
                    if (qri >= 64 || qi > 63)
                        return -1;
                }
            } else {
                if (qti > 0 && br_read1(&br)) {
                    memcpy(priv->qrbmis[qti][pli], priv->qrbmis[qti - 1][pli],
                           sizeof(priv->qrbmis[qti][pli]));
                    memcpy(priv->qrsizes[qti][pli], priv->qrsizes[qti - 1][pli],
                           sizeof(priv->qrsizes[qti][pli]));
                    priv->nqrs[qti][pli] = priv->nqrs[qti - 1][pli];
                } else {
                    int src_qti = (3 * qti + pli - 1) / 3;
                    int src_pli = (pli + 2) % 3;
                    memcpy(priv->qrbmis[qti][pli], priv->qrbmis[src_qti][src_pli],
                           sizeof(priv->qrbmis[qti][pli]));
                    memcpy(priv->qrsizes[qti][pli], priv->qrsizes[src_qti][src_pli],
                           sizeof(priv->qrsizes[qti][pli]));
                    priv->nqrs[qti][pli] = priv->nqrs[src_qti][src_pli];
                }
            }
        }
    }

    for (int hti = 0; hti < 80; hti++) {
        priv->huff[hti].node_count = 0;
        if (huff_parse_node(&priv->huff[hti], &br, 0) < 0 || br.failed)
            return -1;
    }

    return theora_finish_setup(dec, priv);
}

void theora_decoder_init(theora_decoder_t *dec) {
    memset(dec, 0, sizeof(*dec));
}

void theora_decoder_free(theora_decoder_t *dec) {
    if (!dec)
        return;
    free(dec->ref_y);
    free(dec->ref_cb);
    free(dec->ref_cr);
    free(dec->gold_y);
    free(dec->gold_cb);
    free(dec->gold_cr);
    free(dec->cur_y);
    free(dec->cur_cb);
    free(dec->cur_cr);
    theora_priv_free(dec);
    memset(dec, 0, sizeof(*dec));
}

int theora_is_header_packet(const uint8_t *data, size_t len) {
    if (!data || len < 7)
        return 0;
    if (data[0] != 0x80 && data[0] != 0x81 && data[0] != 0x82)
        return 0;
    return data[1] == 't' && data[2] == 'h' && data[3] == 'e' &&
           data[4] == 'o' && data[5] == 'r' && data[6] == 'a';
}

int theora_decode_header(theora_decoder_t *dec, const uint8_t *data, size_t len) {
    if (!dec || !data || len < 1)
        return -1;
    if (!theora_is_header_packet(data, len))
        return 1;
    switch (data[0]) {
    case 0x80:
        return parse_id_header(dec, data, len);
    case 0x81:
        return parse_comment_header(dec, data, len);
    case 0x82:
        return parse_setup_header(dec, data, len);
    default:
        return -1;
    }
}

typedef struct {
    int frame_type;
    int nqi;
    uint8_t qi[3];
} theora_frame_header_t;

static int decode_frame_header(theora_decoder_t *dec,
                               bitreader_t *br,
                               theora_frame_header_t *fh) {
    int has_more;
    if (br_read1(br) != 0)
        return -1;
    fh->frame_type = br_read1(br);
    fh->qi[0] = (uint8_t)br_read(br, 6);
    fh->nqi = 1;
    has_more = br_read1(br);
    if (has_more) {
        fh->qi[1] = (uint8_t)br_read(br, 6);
        fh->nqi = 2;
        has_more = br_read1(br);
        if (has_more) {
            fh->qi[2] = (uint8_t)br_read(br, 6);
            fh->nqi = 3;
        }
    }
    if (fh->frame_type == 0)
        (void)br_read(br, 3);
    if (br->failed)
        return -1;
    if (fh->frame_type != 0 && fh->frame_type != 1)
        return -1;
    if (!((theora_priv_t *)dec->priv)->first_frame_decoded && fh->frame_type != 0)
        return -1;
    return 0;
}

static int decode_long_run_length(bitreader_t *br) {
    int bit0 = br_read1(br);
    if (bit0 == 0)
        return 1;
    if (br_read1(br) == 0)
        return (int)br_read(br, 1) + 2;
    if (br_read1(br) == 0)
        return (int)br_read(br, 2) + 4;
    if (br_read1(br) == 0)
        return (int)br_read(br, 3) + 8;
    if (br_read1(br) == 0)
        return (int)br_read(br, 4) + 16;
    return (int)br_read(br, 12) + 32;
}

static int decode_short_run_length(bitreader_t *br) {
    int bit0 = br_read1(br);
    if (bit0 == 0)
        return 1;
    if (br_read1(br) == 0)
        return (int)br_read(br, 1) + 2;
    if (br_read1(br) == 0)
        return (int)br_read(br, 2) + 4;
    return (int)br_read(br, 4) + 8;
}

static int decode_rle_bits(bitreader_t *br, uint8_t *out, int count, int long_runs) {
    int filled = 0;
    while (filled < count) {
        int bit = br_read1(br);
        int run = long_runs ? decode_long_run_length(br) : decode_short_run_length(br);
        if (br->failed || run <= 0)
            return -1;
        while (run-- > 0 && filled < count)
            out[filled++] = (uint8_t)bit;
    }
    return filled == count ? 0 : -1;
}

static int sb_block_count(const theora_priv_t *priv, int sbi) {
    int count = 0;
    for (int bi = 0; bi < priv->total_blocks; bi++) {
        int plane = priv->blocks[bi].plane;
        int sbx = priv->blocks[bi].bx / 4;
        int sby = priv->blocks[bi].by / 4;
        int sb = priv->plane_sb_offsets[plane] + sby * priv->plane_sb_cols[plane] + sbx;
        if (sb == sbi)
            count++;
    }
    return count;
}

static int decode_block_flags(theora_decoder_t *dec,
                              theora_priv_t *priv,
                              bitreader_t *br,
                              int frame_type) {
    memset(priv->bcoded, frame_type == 0 ? 1 : 0, (size_t)priv->total_blocks);
    if (frame_type == 0)
        return 0;

    if (decode_rle_bits(br, priv->sb_partcoded, priv->total_superblocks, 1) != 0)
        return -1;

    {
        int nbits = 0;
        uint8_t *bits;
        for (int sbi = 0; sbi < priv->total_superblocks; sbi++) {
            if (!priv->sb_partcoded[sbi])
                nbits++;
        }
        bits = (uint8_t *)malloc((size_t)nbits);
        if (!bits)
            return -1;
        if (decode_rle_bits(br, bits, nbits, 1) != 0) {
            free(bits);
            return -1;
        }
        nbits = 0;
        for (int sbi = 0; sbi < priv->total_superblocks; sbi++) {
            if (!priv->sb_partcoded[sbi])
                priv->sb_fullcoded[sbi] = bits[nbits++];
        }
        free(bits);
    }

    for (int bi = 0; bi < priv->total_blocks; bi++) {
        int plane = priv->blocks[bi].plane;
        int sbx = priv->blocks[bi].bx / 4;
        int sby = priv->blocks[bi].by / 4;
        int sbi = priv->plane_sb_offsets[plane] + sby * priv->plane_sb_cols[plane] + sbx;
        if (!priv->sb_partcoded[sbi])
            priv->bcoded[bi] = priv->sb_fullcoded[sbi];
    }

    {
        int nbits = 0;
        uint8_t *bits;
        int cursor = 0;
        for (int sbi = 0; sbi < priv->total_superblocks; sbi++) {
            if (priv->sb_partcoded[sbi])
                nbits += sb_block_count(priv, sbi);
        }
        bits = (uint8_t *)malloc((size_t)nbits);
        if (!bits)
            return -1;
        if (decode_rle_bits(br, bits, nbits, 0) != 0) {
            free(bits);
            return -1;
        }
        for (int bi = 0; bi < priv->total_blocks; bi++) {
            int plane = priv->blocks[bi].plane;
            int sbx = priv->blocks[bi].bx / 4;
            int sby = priv->blocks[bi].by / 4;
            int sbi = priv->plane_sb_offsets[plane] + sby * priv->plane_sb_cols[plane] + sbx;
            if (priv->sb_partcoded[sbi])
                priv->bcoded[bi] = bits[cursor++];
        }
        free(bits);
    }
    return 0;
}

static int decode_mb_mode_huff(bitreader_t *br) {
    int code = 0;
    for (int len = 1; len <= 8; len++) {
        code = (code << 1) | br_read1(br);
        if (br->failed)
            return -1;
        if (len == 1 && code == 0) return 0;
        if (len == 2 && code == 2) return 1;
        if (len == 3 && code == 6) return 2;
        if (len == 4 && code == 14) return 3;
        if (len == 5 && code == 30) return 4;
        if (len == 6 && code == 62) return 5;
        if (len == 7 && code == 126) return 6;
        if (len == 8 && code == 255) return 7;
    }
    return -1;
}

static int mb_has_coded_luma(const theora_priv_t *priv, int mbi) {
    for (int i = 0; i < 4; i++) {
        int bi = priv->mbs[mbi].luma[i];
        if (bi >= 0 && priv->bcoded[bi])
            return 1;
    }
    return 0;
}

static int decode_mb_modes(theora_decoder_t *dec,
                           theora_priv_t *priv,
                           bitreader_t *br,
                           int frame_type) {
    (void)dec;
    if (frame_type == 0) {
        memset(priv->mbmodes, 1, (size_t)priv->total_macroblocks);
        return 0;
    }

    {
        int mscheme = (int)br_read(br, 3);
        uint8_t alphabet[8];
        if (br->failed)
            return -1;
        if (mscheme == 0) {
            for (int mode = 0; mode < 8; mode++) {
                int mi = (int)br_read(br, 3);
                if (mi < 0 || mi > 7 || br->failed)
                    return -1;
                alphabet[mi] = (uint8_t)mode;
            }
        } else if (mscheme != 7) {
            memcpy(alphabet, mb_mode_scheme[mscheme - 1], sizeof(alphabet));
        } else {
            memset(alphabet, 0, sizeof(alphabet));
        }

        for (int mbi = 0; mbi < priv->total_macroblocks; mbi++) {
            if (!mb_has_coded_luma(priv, mbi)) {
                priv->mbmodes[mbi] = 0;
            } else if (mscheme == 7) {
                priv->mbmodes[mbi] = (uint8_t)br_read(br, 3);
            } else {
                int mi = decode_mb_mode_huff(br);
                if (mi < 0)
                    return -1;
                priv->mbmodes[mbi] = alphabet[mi];
            }
            if (priv->mbmodes[mbi] > 7 || br->failed)
                return -1;
        }
    }
    return 0;
}

static int decode_mv_component_huff(bitreader_t *br) {
    int code = 0;
    for (int len = 1; len <= 8; len++) {
        code = (code << 1) | br_read1(br);
        if (br->failed)
            return 0;
        switch (len) {
        case 3:
            if (code == 0) return 0;
            if (code == 1) return 1;
            if (code == 2) return -1;
            break;
        case 4:
            if (code == 6) return 2;
            if (code == 7) return -2;
            if (code == 8) return 3;
            if (code == 9) return -3;
            break;
        case 6:
            if (code >= 40 && code <= 55) {
                int mag = ((code - 40) >> 1) + 4;
                return ((code - 40) & 1) ? -mag : mag;
            }
            break;
        case 7:
            if (code >= 96 && code <= 127) {
                int mag = ((code - 96) >> 1) + 8;
                return ((code - 96) & 1) ? -mag : mag;
            }
            break;
        case 8:
            if (code >= 224 && code <= 255) {
                int mag = ((code - 224) >> 1) + 24;
                return ((code - 224) & 1) ? -mag : mag;
            } else if (code >= 192 && code <= 223) {
                int mag = ((code - 192) >> 1) + 16;
                return ((code - 192) & 1) ? -mag : mag;
            }
            break;
        default:
            break;
        }
    }
    br->failed = 1;
    return 0;
}

static void assign_mv_to_coded_blocks(const theora_priv_t *priv, int mbi, int mvx, int mvy) {
    for (int i = 0; i < 4; i++) {
        int bi = priv->mbs[mbi].luma[i];
        if (bi >= 0 && priv->bcoded[bi]) {
            priv->mvects[bi][0] = (int8_t)mvx;
            priv->mvects[bi][1] = (int8_t)mvy;
        }
    }
    for (int plane = 0; plane < 2; plane++) {
        for (int i = 0; i < 4; i++) {
            int bi = priv->mbs[mbi].chroma[plane][i];
            if (bi >= 0 && priv->bcoded[bi]) {
                priv->mvects[bi][0] = (int8_t)mvx;
                priv->mvects[bi][1] = (int8_t)mvy;
            }
        }
    }
}

static int decode_motion_vectors(theora_decoder_t *dec,
                                 theora_priv_t *priv,
                                 bitreader_t *br,
                                 int frame_type) {
    int mvmode = 0;
    int last1x = 0, last1y = 0, last2x = 0, last2y = 0;

    memset(priv->mvects, 0, (size_t)priv->total_blocks * sizeof(priv->mvects[0]));
    if (frame_type == 0)
        return 0;

    mvmode = br_read1(br);
    if (br->failed)
        return -1;

    for (int mbi = 0; mbi < priv->total_macroblocks; mbi++) {
        int mode = priv->mbmodes[mbi];
        int mvx = 0;
        int mvy = 0;
        if (mode == 7) {
            int a = priv->mbs[mbi].luma[0];
            int b = priv->mbs[mbi].luma[1];
            int c = priv->mbs[mbi].luma[2];
            int d = priv->mbs[mbi].luma[3];
            int last_mv_x = 0;
            int last_mv_y = 0;
            int order[4] = {a, b, c, d};
            for (int i = 0; i < 4; i++) {
                int bi = order[i];
                if (bi >= 0 && priv->bcoded[bi]) {
                    if (mvmode == 0) {
                        mvx = decode_mv_component_huff(br);
                        mvy = decode_mv_component_huff(br);
                    } else {
                        mvx = (int)br_read(br, 5);
                        if (br_read1(br))
                            mvx = -mvx;
                        mvy = (int)br_read(br, 5);
                        if (br_read1(br))
                            mvy = -mvy;
                    }
                    if (br->failed)
                        return -1;
                    priv->mvects[bi][0] = (int8_t)mvx;
                    priv->mvects[bi][1] = (int8_t)mvy;
                    last_mv_x = mvx;
                    last_mv_y = mvy;
                }
            }

            if (dec->pixel_format == 0) {
                int cb = priv->mbs[mbi].chroma[0][0];
                int cr = priv->mbs[mbi].chroma[1][0];
                int sumx = 0, sumy = 0;
                for (int i = 0; i < 4; i++) {
                    int bi = order[i];
                    if (bi >= 0) {
                        sumx += priv->mvects[bi][0];
                        sumy += priv->mvects[bi][1];
                    }
                }
                if (cb >= 0) {
                    priv->mvects[cb][0] = (int8_t)round_div_s32(sumx, 4);
                    priv->mvects[cb][1] = (int8_t)round_div_s32(sumy, 4);
                }
                if (cr >= 0) {
                    priv->mvects[cr][0] = (int8_t)round_div_s32(sumx, 4);
                    priv->mvects[cr][1] = (int8_t)round_div_s32(sumy, 4);
                }
            } else if (dec->pixel_format == 2) {
                int cb0 = priv->mbs[mbi].chroma[0][0];
                int cb1 = priv->mbs[mbi].chroma[0][1];
                int cr0 = priv->mbs[mbi].chroma[1][0];
                int cr1 = priv->mbs[mbi].chroma[1][1];
                int botx = round_div_s32(priv->mvects[a][0] + priv->mvects[b][0], 2);
                int boty = round_div_s32(priv->mvects[a][1] + priv->mvects[b][1], 2);
                int topx = round_div_s32(priv->mvects[c][0] + priv->mvects[d][0], 2);
                int topy = round_div_s32(priv->mvects[c][1] + priv->mvects[d][1], 2);
                if (cb0 >= 0) { priv->mvects[cb0][0] = (int8_t)botx; priv->mvects[cb0][1] = (int8_t)boty; }
                if (cr0 >= 0) { priv->mvects[cr0][0] = (int8_t)botx; priv->mvects[cr0][1] = (int8_t)boty; }
                if (cb1 >= 0) { priv->mvects[cb1][0] = (int8_t)topx; priv->mvects[cb1][1] = (int8_t)topy; }
                if (cr1 >= 0) { priv->mvects[cr1][0] = (int8_t)topx; priv->mvects[cr1][1] = (int8_t)topy; }
            } else {
                for (int i = 0; i < 4; i++) {
                    int cb = priv->mbs[mbi].chroma[0][i];
                    int cr = priv->mbs[mbi].chroma[1][i];
                    int src = order[i];
                    if (cb >= 0) {
                        priv->mvects[cb][0] = priv->mvects[src][0];
                        priv->mvects[cb][1] = priv->mvects[src][1];
                    }
                    if (cr >= 0) {
                        priv->mvects[cr][0] = priv->mvects[src][0];
                        priv->mvects[cr][1] = priv->mvects[src][1];
                    }
                }
            }
            last2x = last1x;
            last2y = last1y;
            last1x = last_mv_x;
            last1y = last_mv_y;
        } else {
            if (mode == 6 || mode == 2) {
                if (mvmode == 0) {
                    mvx = decode_mv_component_huff(br);
                    mvy = decode_mv_component_huff(br);
                } else {
                    mvx = (int)br_read(br, 5);
                    if (br_read1(br))
                        mvx = -mvx;
                    mvy = (int)br_read(br, 5);
                    if (br_read1(br))
                        mvy = -mvy;
                }
                if (mode == 2) {
                    last2x = last1x;
                    last2y = last1y;
                    last1x = mvx;
                    last1y = mvy;
                }
            } else if (mode == 4) {
                mvx = last2x;
                mvy = last2y;
                last2x = last1x;
                last2y = last1y;
                last1x = mvx;
                last1y = mvy;
            } else if (mode == 3) {
                mvx = last1x;
                mvy = last1y;
            }
            if (br->failed)
                return -1;
            assign_mv_to_coded_blocks(priv, mbi, mvx, mvy);
        }
    }
    return 0;
}

static int decode_qiis(theora_priv_t *priv, bitreader_t *br, int nqi) {
    memset(priv->qiis, 0, (size_t)priv->total_blocks);
    for (int qii = 0; qii < nqi - 1; qii++) {
        int nbits = 0;
        uint8_t *bits;
        int cursor = 0;
        for (int bi = 0; bi < priv->total_blocks; bi++) {
            if (priv->bcoded[bi] && priv->qiis[bi] == qii)
                nbits++;
        }
        bits = (uint8_t *)malloc((size_t)nbits);
        if (!bits)
            return -1;
        if (decode_rle_bits(br, bits, nbits, 1) != 0) {
            free(bits);
            return -1;
        }
        for (int bi = 0; bi < priv->total_blocks; bi++) {
            if (priv->bcoded[bi] && priv->qiis[bi] == qii)
                priv->qiis[bi] = (uint8_t)(priv->qiis[bi] + bits[cursor++]);
        }
        free(bits);
    }
    return 0;
}

static int decode_eob_token(theora_priv_t *priv,
                            bitreader_t *br,
                            int token,
                            int bi,
                            int ti,
                            int *eobs) {
    int run = 0;
    switch (token) {
    case 0: run = 1; break;
    case 1: run = 2; break;
    case 2: run = 3; break;
    case 3: run = (int)br_read(br, 2) + 4; break;
    case 4: run = (int)br_read(br, 3) + 8; break;
    case 5: run = (int)br_read(br, 4) + 16; break;
    case 6:
        run = (int)br_read(br, 12);
        if (run == 0) {
            run = 0;
            for (int bj = 0; bj < priv->total_blocks; bj++) {
                if (priv->bcoded[bj] && priv->tis[bj] < 64)
                    run++;
            }
        }
        break;
    default:
        return -1;
    }
    if (br->failed || run <= 0)
        return -1;
    for (int tj = ti; tj < 64; tj++)
        priv->coeffs[bi][tj] = 0;
    priv->ncoeffs[bi] = priv->tis[bi];
    priv->tis[bi] = 64;
    *eobs = run - 1;
    return 0;
}

static int decode_coeff_token(theora_priv_t *priv,
                              bitreader_t *br,
                              int token,
                              int bi,
                              int ti) {
    int sign = 0;
    int mag = 0;
    int rlen = 0;
    if (token == 7 || token == 8) {
        rlen = (int)br_read(br, token == 7 ? 3 : 6) + 1;
        if (br->failed || ti + rlen > 64)
            return -1;
        for (int tj = ti; tj < ti + rlen; tj++)
            priv->coeffs[bi][tj] = 0;
        priv->tis[bi] = (uint8_t)(priv->tis[bi] + rlen);
        return 0;
    }
    if (ti >= 64)
        return -1;

    switch (token) {
    case 9: priv->coeffs[bi][ti] = 1; priv->tis[bi]++; break;
    case 10: priv->coeffs[bi][ti] = -1; priv->tis[bi]++; break;
    case 11: priv->coeffs[bi][ti] = 2; priv->tis[bi]++; break;
    case 12: priv->coeffs[bi][ti] = -2; priv->tis[bi]++; break;
    case 13: case 14: case 15: case 16:
        sign = br_read1(br);
        if (br->failed)
            return -1;
        priv->coeffs[bi][ti] = (int16_t)((sign ? -1 : 1) * (token - 10));
        priv->tis[bi]++;
        break;
    case 17:
        sign = br_read1(br);
        mag = (int)br_read(br, 1) + 7;
        if (br->failed)
            return -1;
        priv->coeffs[bi][ti] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi]++;
        break;
    case 18:
        sign = br_read1(br);
        mag = (int)br_read(br, 2) + 9;
        if (br->failed)
            return -1;
        priv->coeffs[bi][ti] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi]++;
        break;
    case 19:
        sign = br_read1(br);
        mag = (int)br_read(br, 3) + 13;
        if (br->failed)
            return -1;
        priv->coeffs[bi][ti] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi]++;
        break;
    case 20:
        sign = br_read1(br);
        mag = (int)br_read(br, 4) + 21;
        if (br->failed)
            return -1;
        priv->coeffs[bi][ti] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi]++;
        break;
    case 21:
        sign = br_read1(br);
        mag = (int)br_read(br, 5) + 37;
        if (br->failed)
            return -1;
        priv->coeffs[bi][ti] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi]++;
        break;
    case 22:
        sign = br_read1(br);
        mag = (int)br_read(br, 9) + 69;
        if (br->failed)
            return -1;
        priv->coeffs[bi][ti] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi]++;
        break;
    case 23:
        sign = br_read1(br);
        if (br->failed || ti + 1 >= 64)
            return -1;
        priv->coeffs[bi][ti] = 0;
        priv->coeffs[bi][ti + 1] = (int16_t)(sign ? -1 : 1);
        priv->tis[bi] += 2;
        break;
    case 24:
    case 25:
    case 26:
    case 27:
        sign = br_read1(br);
        rlen = token - 22;
        if (br->failed || ti + rlen >= 64)
            return -1;
        for (int tj = ti; tj < ti + rlen; tj++)
            priv->coeffs[bi][tj] = 0;
        priv->coeffs[bi][ti + rlen] = (int16_t)(sign ? -1 : 1);
        priv->tis[bi] += (uint8_t)(rlen + 1);
        break;
    case 28:
        sign = br_read1(br);
        rlen = (int)br_read(br, 2) + 6;
        if (br->failed || ti + rlen >= 64)
            return -1;
        for (int tj = ti; tj < ti + rlen; tj++)
            priv->coeffs[bi][tj] = 0;
        priv->coeffs[bi][ti + rlen] = (int16_t)(sign ? -1 : 1);
        priv->tis[bi] += (uint8_t)(rlen + 1);
        break;
    case 29:
        sign = br_read1(br);
        rlen = (int)br_read(br, 3) + 10;
        if (br->failed || ti + rlen >= 64)
            return -1;
        for (int tj = ti; tj < ti + rlen; tj++)
            priv->coeffs[bi][tj] = 0;
        priv->coeffs[bi][ti + rlen] = (int16_t)(sign ? -1 : 1);
        priv->tis[bi] += (uint8_t)(rlen + 1);
        break;
    case 30:
        sign = br_read1(br);
        mag = (int)br_read(br, 1) + 2;
        if (br->failed || ti + 1 >= 64)
            return -1;
        priv->coeffs[bi][ti] = 0;
        priv->coeffs[bi][ti + 1] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi] += 2;
        break;
    case 31:
        sign = br_read1(br);
        mag = (int)br_read(br, 1) + 2;
        rlen = (int)br_read(br, 1) + 2;
        if (br->failed || ti + rlen >= 64)
            return -1;
        for (int tj = ti; tj < ti + rlen; tj++)
            priv->coeffs[bi][tj] = 0;
        priv->coeffs[bi][ti + rlen] = (int16_t)(sign ? -mag : mag);
        priv->tis[bi] += (uint8_t)(rlen + 1);
        break;
    default:
        return -1;
    }

    priv->ncoeffs[bi] = priv->tis[bi];
    return 0;
}

static int huffman_group_for_ti(int ti) {
    if (ti == 0) return 0;
    if (ti <= 5) return 1;
    if (ti <= 14) return 2;
    if (ti <= 27) return 3;
    return 4;
}

static int decode_coefficients(theora_priv_t *priv, bitreader_t *br) {
    int hti_l = 0;
    int hti_c = 0;
    int nlbs = priv->total_macroblocks * 4;
    int eobs = 0;

    memset(priv->tis, 0, (size_t)priv->total_blocks);
    memset(priv->ncoeffs, 0, (size_t)priv->total_blocks);
    memset(priv->coeffs, 0, (size_t)priv->total_blocks * sizeof(priv->coeffs[0]));

    for (int ti = 0; ti < 64; ti++) {
        if (ti == 0 || ti == 1) {
            hti_l = (int)br_read(br, 4);
            hti_c = (int)br_read(br, 4);
            if (br->failed)
                return -1;
        }
        for (int bi = 0; bi < priv->total_blocks; bi++) {
            if (!priv->bcoded[bi] || priv->tis[bi] != ti)
                continue;
            priv->ncoeffs[bi] = (uint8_t)ti;
            if (eobs > 0) {
                for (int tj = ti; tj < 64; tj++)
                    priv->coeffs[bi][tj] = 0;
                priv->tis[bi] = 64;
                eobs--;
            } else {
                int hg = huffman_group_for_ti(ti);
                int hti = (bi < nlbs) ? (16 * hg + hti_l) : (16 * hg + hti_c);
                int token = huff_decode_token(&priv->huff[hti], br);
                if (token < 0)
                    return -1;
                if (token < 7) {
                    if (decode_eob_token(priv, br, token, bi, ti, &eobs) != 0)
                        return -1;
                } else {
                    if (decode_coeff_token(priv, br, token, bi, ti) != 0)
                        return -1;
                }
            }
        }
    }
    if (eobs != 0)
        return -1;
    for (int bi = 0; bi < priv->total_blocks; bi++) {
        if (priv->bcoded[bi] && priv->tis[bi] != 64)
            return -1;
    }
    return 0;
}

static int compute_dc_pred(const theora_priv_t *priv, const int16_t lastdc[3], int bi) {
    static const int dx[4] = {-1, -1, 0, 1};
    static const int dy[4] = {0, -1, -1, -1};
    const theora_block_info_t *b = &priv->blocks[bi];
    int rfi = theora_ref_index_for_mode(priv->mbmodes[b->mb]);
    int present[4] = {0, 0, 0, 0};
    int pbi[4] = {-1, -1, -1, -1};
    int mask = 0;

    for (int i = 0; i < 4; i++) {
        int nbx = b->bx + dx[i];
        int nby = b->by + dy[i];
        if (nbx < 0 || nby < 0 || nbx >= priv->plane_block_cols[b->plane] ||
            nby >= priv->plane_block_rows[b->plane])
            continue;
        pbi[i] = priv->plane_raster_to_coded[b->plane][nby * priv->plane_block_cols[b->plane] + nbx];
        if (pbi[i] >= 0 && priv->bcoded[pbi[i]] &&
            theora_ref_index_for_mode(priv->mbmodes[priv->blocks[pbi[i]].mb]) == rfi) {
            present[i] = 1;
            mask |= 1 << i;
        }
    }

    if (mask == 0)
        return lastdc[rfi];

    {
        int sum = 0;
        const theora_dc_weight_t *w = &dc_weights[mask];
        for (int i = 0; i < 4; i++) {
            if (present[i])
                sum += w->w[i] * priv->coeffs[pbi[i]][0];
        }
        sum /= w->div;
        if (present[0] && present[1] && present[2]) {
            if (abs(sum - priv->coeffs[pbi[2]][0]) > 128)
                sum = priv->coeffs[pbi[2]][0];
            else if (abs(sum - priv->coeffs[pbi[0]][0]) > 128)
                sum = priv->coeffs[pbi[0]][0];
            else if (abs(sum - priv->coeffs[pbi[1]][0]) > 128)
                sum = priv->coeffs[pbi[1]][0];
        }
        return sum;
    }
}

static void undo_dc_prediction(theora_priv_t *priv) {
    int plane;
    for (plane = 0; plane < 3; plane++) {
        int16_t lastdc[3] = {0, 0, 0};
        int count = priv->plane_block_counts[plane];
        for (int raster = 0; raster < count; raster++) {
            int bi = priv->plane_raster_to_coded[plane][raster];
            if (!priv->bcoded[bi])
                continue;
            {
                int dcpred = compute_dc_pred(priv, lastdc, bi);
                int32_t dc = (int32_t)priv->coeffs[bi][0] + dcpred;
                int rfi = theora_ref_index_for_mode(priv->mbmodes[priv->blocks[bi].mb]);
                priv->coeffs[bi][0] = trunc_i16(dc);
                lastdc[rfi] = priv->coeffs[bi][0];
            }
        }
    }
}

#define TH_FIX(x) ((int32_t)((x) * 4096.0 + 0.5))
#define TH_DESCALE(x, n) (((x) + (1 << ((n) - 1))) >> (n))

static void idct_row(int32_t *row) {
    int32_t x0 = row[0], x1 = row[1], x2 = row[2], x3 = row[3];
    int32_t x4 = row[4], x5 = row[5], x6 = row[6], x7 = row[7];
    int32_t s0 = x0 + x4;
    int32_t s1 = x0 - x4;
    int32_t s2 = TH_DESCALE(x2 * TH_FIX(0.541196100) + x6 * TH_FIX(0.541196100 - 1.847759065), 12);
    int32_t s3 = TH_DESCALE(x2 * TH_FIX(0.541196100 + 0.765366865) + x6 * TH_FIX(0.541196100), 12);
    int32_t e0 = s0 + s3;
    int32_t e1 = s1 + s2;
    int32_t e2 = s1 - s2;
    int32_t e3 = s0 - s3;
    int32_t t0 = x1 + x7;
    int32_t t1 = x5 + x3;
    int32_t t2 = x1 + x3;
    int32_t t3 = x5 + x7;
    int32_t z5 = TH_DESCALE((t2 - t3) * TH_FIX(1.175875602), 12);
    t0 = TH_DESCALE(t0 * TH_FIX(-0.899976223), 12);
    t1 = TH_DESCALE(t1 * TH_FIX(-2.562915447), 12);
    t2 = TH_DESCALE(t2 * TH_FIX(-1.961570560), 12) + z5;
    t3 = TH_DESCALE(t3 * TH_FIX(-0.390180644), 12) + z5;
    row[0] = e0 + TH_DESCALE(x1 * TH_FIX(1.501321110), 12) + t0 + t3;
    row[1] = e1 + TH_DESCALE(x3 * TH_FIX(3.072711026), 12) + t1 + t2;
    row[2] = e2 + TH_DESCALE(x5 * TH_FIX(2.053119869), 12) + t1 + t3;
    row[3] = e3 + TH_DESCALE(x7 * TH_FIX(0.298631336), 12) + t0 + t2;
    row[4] = e3 - (TH_DESCALE(x7 * TH_FIX(0.298631336), 12) + t0 + t2);
    row[5] = e2 - (TH_DESCALE(x5 * TH_FIX(2.053119869), 12) + t1 + t3);
    row[6] = e1 - (TH_DESCALE(x3 * TH_FIX(3.072711026), 12) + t1 + t2);
    row[7] = e0 - (TH_DESCALE(x1 * TH_FIX(1.501321110), 12) + t0 + t3);
}

static void idct_col(int32_t *workspace, int col) {
    int32_t x0 = workspace[col + 0 * 8], x1 = workspace[col + 1 * 8];
    int32_t x2 = workspace[col + 2 * 8], x3 = workspace[col + 3 * 8];
    int32_t x4 = workspace[col + 4 * 8], x5 = workspace[col + 5 * 8];
    int32_t x6 = workspace[col + 6 * 8], x7 = workspace[col + 7 * 8];
    int32_t s0 = x0 + x4;
    int32_t s1 = x0 - x4;
    int32_t s2 = TH_DESCALE(x2 * TH_FIX(0.541196100) + x6 * TH_FIX(0.541196100 - 1.847759065), 12);
    int32_t s3 = TH_DESCALE(x2 * TH_FIX(0.541196100 + 0.765366865) + x6 * TH_FIX(0.541196100), 12);
    int32_t e0 = s0 + s3;
    int32_t e1 = s1 + s2;
    int32_t e2 = s1 - s2;
    int32_t e3 = s0 - s3;
    int32_t t0 = x1 + x7;
    int32_t t1 = x5 + x3;
    int32_t t2 = x1 + x3;
    int32_t t3 = x5 + x7;
    int32_t z5 = TH_DESCALE((t2 - t3) * TH_FIX(1.175875602), 12);
    t0 = TH_DESCALE(t0 * TH_FIX(-0.899976223), 12);
    t1 = TH_DESCALE(t1 * TH_FIX(-2.562915447), 12);
    t2 = TH_DESCALE(t2 * TH_FIX(-1.961570560), 12) + z5;
    t3 = TH_DESCALE(t3 * TH_FIX(-0.390180644), 12) + z5;
    workspace[col + 0 * 8] = TH_DESCALE(e0 + TH_DESCALE(x1 * TH_FIX(1.501321110), 12) + t0 + t3, 5);
    workspace[col + 1 * 8] = TH_DESCALE(e1 + TH_DESCALE(x3 * TH_FIX(3.072711026), 12) + t1 + t2, 5);
    workspace[col + 2 * 8] = TH_DESCALE(e2 + TH_DESCALE(x5 * TH_FIX(2.053119869), 12) + t1 + t3, 5);
    workspace[col + 3 * 8] = TH_DESCALE(e3 + TH_DESCALE(x7 * TH_FIX(0.298631336), 12) + t0 + t2, 5);
    workspace[col + 4 * 8] = TH_DESCALE(e3 - (TH_DESCALE(x7 * TH_FIX(0.298631336), 12) + t0 + t2), 5);
    workspace[col + 5 * 8] = TH_DESCALE(e2 - (TH_DESCALE(x5 * TH_FIX(2.053119869), 12) + t1 + t3), 5);
    workspace[col + 6 * 8] = TH_DESCALE(e1 - (TH_DESCALE(x3 * TH_FIX(3.072711026), 12) + t1 + t2), 5);
    workspace[col + 7 * 8] = TH_DESCALE(e0 - (TH_DESCALE(x1 * TH_FIX(1.501321110), 12) + t0 + t3), 5);
}

static void idct_block(const int16_t in[64], int16_t out[64]) {
    int32_t ws[64];
    for (int i = 0; i < 64; i++)
        ws[i] = in[i];
    for (int i = 0; i < 8; i++)
        idct_row(ws + i * 8);
    for (int i = 0; i < 8; i++)
        idct_col(ws, i);
    for (int i = 0; i < 64; i++)
        out[i] = trunc_i16(ws[i]);
}

static void build_residual_block(const theora_decoder_t *dec,
                                 const theora_priv_t *priv,
                                 int bi,
                                 const theora_frame_header_t *fh,
                                 int16_t out[64]) {
    const theora_block_info_t *b = &priv->blocks[bi];
    int qti = priv->mbmodes[b->mb] == 1 ? 0 : 1;
    int qi_dc = fh->qi[0];
    int qi_ac = fh->qi[priv->qiis[bi]];
    int16_t coeff_block[64];
    memset(out, 0, sizeof(int16_t) * 64);
    if (priv->ncoeffs[bi] == 0)
        return;
    if (priv->ncoeffs[bi] <= 1) {
        int dc = ((int)priv->coeffs[bi][0] * dec->qmat[qti][b->plane][qi_dc][0] + 15) >> 5;
        for (int i = 0; i < 64; i++)
            out[i] = (int16_t)dc;
        return;
    }
    memset(coeff_block, 0, sizeof(coeff_block));
    coeff_block[0] = (int16_t)(((int)priv->coeffs[bi][0] * dec->qmat[qti][b->plane][qi_dc][0] + 15) >> 5);
    for (int zi = 1; zi < priv->ncoeffs[bi] && zi < 64; zi++) {
        int idx = theora_zigzag[zi];
        int qi = zi == 0 ? qi_dc : qi_ac;
        coeff_block[idx] =
            (int16_t)(((int)priv->coeffs[bi][zi] * dec->qmat[qti][b->plane][qi][idx] + 15) >> 5);
    }
    idct_block(coeff_block, out);
}

static void select_plane_buffers(const theora_decoder_t *dec,
                                 int plane,
                                 uint8_t **cur,
                                 const uint8_t **ref,
                                 const uint8_t **gold,
                                 int *stride) {
    if (plane == 0) {
        *cur = dec->cur_y;
        *ref = dec->ref_y;
        *gold = dec->gold_y;
        *stride = dec->y_stride;
    } else if (plane == 1) {
        *cur = dec->cur_cb;
        *ref = dec->ref_cb;
        *gold = dec->gold_cb;
        *stride = dec->c_stride;
    } else {
        *cur = dec->cur_cr;
        *ref = dec->ref_cr;
        *gold = dec->gold_cr;
        *stride = dec->c_stride;
    }
}

static int loop_filter_limit_response(int r, int limit) {
    if (limit <= 0)
        return 0;
    if (r <= -2 * limit)
        return 0;
    if (r <= -limit)
        return -r - 2 * limit;
    if (r < limit)
        return r;
    if (r < 2 * limit)
        return -r + 2 * limit;
    return 0;
}

static void apply_horizontal_filter(uint8_t *plane, int stride, int fx, int fy, int limit) {
    for (int by = 0; by < 8; by++) {
        int row = fy + by;
        int r = (plane[row * stride + fx] -
                 3 * plane[row * stride + fx + 1] +
                 3 * plane[row * stride + fx + 2] -
                 plane[row * stride + fx + 3] + 4) >> 3;
        int delta = loop_filter_limit_response(r, limit);
        int p1 = plane[row * stride + fx + 1] + delta;
        int p2 = plane[row * stride + fx + 2] - delta;
        plane[row * stride + fx + 1] = (uint8_t)clampi(p1, 0, 255);
        plane[row * stride + fx + 2] = (uint8_t)clampi(p2, 0, 255);
    }
}

static void apply_vertical_filter(uint8_t *plane, int stride, int fx, int fy, int limit) {
    for (int bx = 0; bx < 8; bx++) {
        int r = (plane[fy * stride + fx + bx] -
                 3 * plane[(fy + 1) * stride + fx + bx] +
                 3 * plane[(fy + 2) * stride + fx + bx] -
                 plane[(fy + 3) * stride + fx + bx] + 4) >> 3;
        int delta = loop_filter_limit_response(r, limit);
        int p1 = plane[(fy + 1) * stride + fx + bx] + delta;
        int p2 = plane[(fy + 2) * stride + fx + bx] - delta;
        plane[(fy + 1) * stride + fx + bx] = (uint8_t)clampi(p1, 0, 255);
        plane[(fy + 2) * stride + fx + bx] = (uint8_t)clampi(p2, 0, 255);
    }
}

static void apply_loop_filter(theora_decoder_t *dec,
                              const theora_priv_t *priv,
                              const theora_frame_header_t *fh) {
    int limit;
    if (!dec || !priv || !fh)
        return;
    limit = dec->loop_filter_limits[fh->qi[0]];
    if (limit <= 0)
        return;

    for (int plane = 0; plane < 3; plane++) {
        uint8_t *plane_data = NULL;
        const uint8_t *unused_ref = NULL;
        const uint8_t *unused_gold = NULL;
        int stride = 0;
        int block_cols = priv->plane_block_cols[plane];
        int block_rows = priv->plane_block_rows[plane];
        int width = priv->plane_width[plane];
        int height = priv->plane_height[plane];

        select_plane_buffers(dec, plane, &plane_data, &unused_ref, &unused_gold, &stride);
        for (int by = 0; by < block_rows; by++) {
            for (int bx = 0; bx < block_cols; bx++) {
                int bi = priv->plane_raster_to_coded[plane][by * block_cols + bx];
                if (!priv->bcoded[bi])
                    continue;

                if (bx > 0) {
                    int fx = bx * 8 - 2;
                    int fy = by * 8;
                    apply_horizontal_filter(plane_data, stride, fx, fy, limit);
                }
                if (by > 0) {
                    int fx = bx * 8;
                    int fy = by * 8 - 2;
                    apply_vertical_filter(plane_data, stride, fx, fy, limit);
                }
                if ((bx + 1) * 8 < width) {
                    int bj = priv->plane_raster_to_coded[plane][by * block_cols + (bx + 1)];
                    if (!priv->bcoded[bj]) {
                        int fx = bx * 8 + 6;
                        int fy = by * 8;
                        apply_horizontal_filter(plane_data, stride, fx, fy, limit);
                    }
                }
                if ((by + 1) * 8 < height) {
                    int bj = priv->plane_raster_to_coded[plane][(by + 1) * block_cols + bx];
                    if (!priv->bcoded[bj]) {
                        int fx = bx * 8;
                        int fy = by * 8 + 6;
                        apply_vertical_filter(plane_data, stride, fx, fy, limit);
                    }
                }
            }
        }
    }
}

static void copy_pred_whole(uint8_t *dst,
                            const uint8_t *src,
                            int stride,
                            int bw,
                            int bh,
                            int sx,
                            int sy,
                            int max_w,
                            int max_h) {
    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            int px = clampi(sx + x, 0, max_w - 1);
            int py = clampi(sy + y, 0, max_h - 1);
            dst[y * 8 + x] = src[py * stride + px];
        }
    }
}

static void copy_pred_half(uint8_t *dst,
                           const uint8_t *src,
                           int stride,
                           int bw,
                           int bh,
                           int x0,
                           int y0,
                           int x1,
                           int y1,
                           int max_w,
                           int max_h) {
    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            int ax = clampi(x0 + x, 0, max_w - 1);
            int ay = clampi(y0 + y, 0, max_h - 1);
            int bx = clampi(x1 + x, 0, max_w - 1);
            int by = clampi(y1 + y, 0, max_h - 1);
            dst[y * 8 + x] = (uint8_t)((src[ay * stride + ax] + src[by * stride + bx] + 1) >> 1);
        }
    }
}

static int motion_trunc_toward_zero(int mv) {
    int s = mv < 0 ? -1 : 1;
    int a = mv < 0 ? -mv : mv;
    return s * (a / 2);
}

static int motion_trunc_away_zero(int mv) {
    int s = mv < 0 ? -1 : 1;
    int a = mv < 0 ? -mv : mv;
    return s * ((a + 1) / 2);
}

static void reconstruct_frame(theora_decoder_t *dec,
                              const theora_priv_t *priv,
                              const theora_frame_header_t *fh) {
    for (int bi = 0; bi < priv->total_blocks; bi++) {
        const theora_block_info_t *b = &priv->blocks[bi];
        uint8_t pred[64];
        int16_t resid[64];
        uint8_t *cur;
        const uint8_t *ref;
        const uint8_t *gold;
        int stride;
        int plane_w = priv->plane_width[b->plane];
        int plane_h = priv->plane_height[b->plane];
        int px = b->bx * 8;
        int py = b->by * 8;
        int bw = (px + 8 <= plane_w) ? 8 : (plane_w - px);
        int bh = (py + 8 <= plane_h) ? 8 : (plane_h - py);
        int mode = priv->mbmodes[b->mb];

        select_plane_buffers(dec, b->plane, &cur, &ref, &gold, &stride);
        memset(pred, 128, sizeof(pred));
        memset(resid, 0, sizeof(resid));

        if (!priv->bcoded[bi]) {
            copy_pred_whole(pred, ref, stride, bw, bh, px, py, plane_w, plane_h);
        } else if (mode != 1) {
            const uint8_t *src = theora_ref_index_for_mode(mode) == 2 ? gold : ref;
            int mvx = priv->mvects[bi][0];
            int mvy = priv->mvects[bi][1];
            if (((mvx | mvy) & 1) == 0) {
                copy_pred_whole(pred, src, stride, bw, bh,
                                px + mvx / 2, py + mvy / 2, plane_w, plane_h);
            } else {
                copy_pred_half(pred, src, stride, bw, bh,
                               px + motion_trunc_toward_zero(mvx),
                               py + motion_trunc_toward_zero(mvy),
                               px + motion_trunc_away_zero(mvx),
                               py + motion_trunc_away_zero(mvy),
                               plane_w, plane_h);
            }
        }

        if (priv->bcoded[bi])
            build_residual_block(dec, priv, bi, fh, resid);

        for (int y = 0; y < bh; y++) {
            for (int x = 0; x < bw; x++) {
                int idx = y * 8 + x;
                int val = pred[idx] + resid[idx];
                cur[(py + y) * stride + (px + x)] = (uint8_t)clampi(val, 0, 255);
            }
        }
    }
    apply_loop_filter(dec, priv, fh);
}

int theora_decode_frame(theora_decoder_t *dec,
                        const uint8_t *data,
                        size_t len,
                        const uint8_t **out_y,
                        const uint8_t **out_cb,
                        const uint8_t **out_cr) {
    theora_priv_t *priv;
    bitreader_t br;
    theora_frame_header_t fh;
    size_t y_size, c_size;

    if (!dec || !data || len < 1 || !dec->headers_complete || !dec->priv)
        return -1;

    priv = (theora_priv_t *)dec->priv;
    br_init(&br, data, len);
    memset(&fh, 0, sizeof(fh));

    if (decode_frame_header(dec, &br, &fh) != 0)
        return -1;
    if (decode_block_flags(dec, priv, &br, fh.frame_type) != 0)
        return -1;
    if (decode_mb_modes(dec, priv, &br, fh.frame_type) != 0)
        return -1;
    if (decode_motion_vectors(dec, priv, &br, fh.frame_type) != 0)
        return -1;
    if (decode_qiis(priv, &br, fh.nqi) != 0)
        return -1;
    if (decode_coefficients(priv, &br) != 0)
        return -1;
    undo_dc_prediction(priv);
    reconstruct_frame(dec, priv, &fh);

    y_size = (size_t)dec->y_stride * (size_t)dec->y_height;
    c_size = (size_t)dec->c_stride * (size_t)dec->c_height;
    memcpy(dec->ref_y, dec->cur_y, y_size);
    memcpy(dec->ref_cb, dec->cur_cb, c_size);
    memcpy(dec->ref_cr, dec->cur_cr, c_size);
    if (fh.frame_type == 0) {
        memcpy(dec->gold_y, dec->cur_y, y_size);
        memcpy(dec->gold_cb, dec->cur_cb, c_size);
        memcpy(dec->gold_cr, dec->cur_cr, c_size);
    }
    priv->first_frame_decoded = 1;

    if (out_y) *out_y = dec->cur_y;
    if (out_cb) *out_cb = dec->cur_cb;
    if (out_cr) *out_cr = dec->cur_cr;
    return 0;
}
