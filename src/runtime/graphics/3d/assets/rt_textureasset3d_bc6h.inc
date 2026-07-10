//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_textureasset3d_bc6h.inc
// Purpose: BC6H (BPTC HDR) block decoding — clean-room implementation from the
//   Khronos Data Format Specification 1.3 §20.2 (tables 114/118/121/122 are
//   machine-extracted from the published spec text). Decodes signed and
//   unsigned blocks to half-float RGB, validated bit-exact against an
//   independent reference decoder across all 14 modes.
// Key invariants:
//   - Reserved modes (19/23/27/31) decode to opaque black per the spec.
//   - The KTX2 fallback path clamps half floats to [0, 1] into RGBA8 Pixels
//     (no native GPU retention; HDR range beyond 1 saturates).
// Ownership/Lifetime:
//   - Decoded Pixels are owned by the caller (KTX2 mip fallback table).
// Links: rt_textureasset3d_ktx2.inc, rt_textureasset3d_codecs.inc
//
//===----------------------------------------------------------------------===//

/* Generated from the Khronos Data Format Specification 1.3 (tables 114, 118,
 * 121, 122); see scripts/gen_bc6h_tables.py-style extraction notes in the
 * enclosing file header. Do not hand-edit the tables. */
static const uint8_t bc6h_partition2[32][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0},
    {0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
};

static const uint8_t bc6h_anchor2[32] = {15, 15, 15, 15, 15, 15, 15, 15,
                                        15, 15, 15, 15, 15, 15, 15, 15,
                                        15, 2,  8,  2,  2,  8,  8,  15,
                                        2,  8,  2,  2,  8,  8,  2,  2};

typedef struct {
    uint8_t var; /* 0=M, 1..12=R0,G0,B0,R1,G1,B1,R2,G2,B2,R3,G3,B3, 13=PB */
    uint8_t hi;
    uint8_t lo; /* hi < lo: bits are stored reversed */
} bc6h_field_t;

static const bc6h_field_t bc6h_mode0_fields[] = {
    {0, 1, 0}, {8, 4, 4}, {9, 4, 4}, {12, 4, 4}, {1, 9, 0}, {2, 9, 0},
    {3, 9, 0}, {4, 4, 0}, {11, 4, 4}, {8, 3, 0}, {5, 4, 0}, {12, 0, 0},
    {11, 3, 0}, {6, 4, 0}, {12, 1, 1}, {9, 3, 0}, {7, 4, 0}, {12, 2, 2},
    {10, 4, 0}, {12, 3, 3}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode1_fields[] = {
    {0, 1, 0}, {8, 5, 5}, {11, 4, 4}, {11, 5, 5}, {1, 6, 0}, {12, 0, 0},
    {12, 1, 1}, {9, 4, 4}, {2, 6, 0}, {9, 5, 5}, {12, 2, 2}, {8, 4, 4},
    {3, 6, 0}, {12, 3, 3}, {12, 5, 5}, {12, 4, 4}, {4, 5, 0}, {8, 3, 0},
    {5, 5, 0}, {11, 3, 0}, {6, 5, 0}, {9, 3, 0}, {7, 5, 0}, {10, 5, 0},
    {13, 4, 0},
};
static const bc6h_field_t bc6h_mode2_fields[] = {
    {0, 4, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 4, 0}, {1, 10, 10},
    {8, 3, 0}, {5, 3, 0}, {2, 10, 10}, {12, 0, 0}, {11, 3, 0}, {6, 3, 0},
    {3, 10, 10}, {12, 1, 1}, {9, 3, 0}, {7, 4, 0}, {12, 2, 2}, {10, 4, 0},
    {12, 3, 3}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode6_fields[] = {
    {0, 4, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 3, 0}, {1, 10, 10},
    {11, 4, 4}, {8, 3, 0}, {5, 4, 0}, {2, 10, 10}, {11, 3, 0}, {6, 3, 0},
    {3, 10, 10}, {12, 1, 1}, {9, 3, 0}, {7, 3, 0}, {12, 0, 0}, {12, 2, 2},
    {10, 3, 0}, {8, 4, 4}, {12, 3, 3}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode10_fields[] = {
    {0, 4, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 3, 0}, {1, 10, 10},
    {9, 4, 4}, {8, 3, 0}, {5, 3, 0}, {2, 10, 10}, {12, 0, 0}, {11, 3, 0},
    {6, 4, 0}, {3, 10, 10}, {9, 3, 0}, {7, 3, 0}, {12, 1, 1}, {12, 2, 2},
    {10, 3, 0}, {12, 4, 4}, {12, 3, 3}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode14_fields[] = {
    {0, 4, 0}, {1, 8, 0}, {9, 4, 4}, {2, 8, 0}, {8, 4, 4}, {3, 8, 0},
    {12, 4, 4}, {4, 4, 0}, {11, 4, 4}, {8, 3, 0}, {5, 4, 0}, {12, 0, 0},
    {11, 3, 0}, {6, 4, 0}, {12, 1, 1}, {9, 3, 0}, {7, 4, 0}, {12, 2, 2},
    {10, 4, 0}, {12, 3, 3}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode18_fields[] = {
    {0, 4, 0}, {1, 7, 0}, {11, 4, 4}, {9, 4, 4}, {2, 7, 0}, {12, 2, 2},
    {8, 4, 4}, {3, 7, 0}, {12, 3, 3}, {12, 4, 4}, {4, 5, 0}, {8, 3, 0},
    {5, 4, 0}, {12, 0, 0}, {11, 3, 0}, {6, 4, 0}, {12, 1, 1}, {9, 3, 0},
    {7, 5, 0}, {10, 5, 0}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode22_fields[] = {
    {0, 4, 0}, {1, 7, 0}, {12, 0, 0}, {9, 4, 4}, {2, 7, 0}, {8, 5, 5},
    {8, 4, 4}, {3, 7, 0}, {11, 5, 5}, {12, 4, 4}, {4, 4, 0}, {11, 4, 4},
    {8, 3, 0}, {5, 5, 0}, {11, 3, 0}, {6, 4, 0}, {12, 1, 1}, {9, 3, 0},
    {7, 4, 0}, {12, 2, 2}, {10, 4, 0}, {12, 3, 3}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode26_fields[] = {
    {0, 4, 0}, {1, 7, 0}, {12, 1, 1}, {9, 4, 4}, {2, 7, 0}, {9, 5, 5},
    {8, 4, 4}, {3, 7, 0}, {12, 5, 5}, {12, 4, 4}, {4, 4, 0}, {11, 4, 4},
    {8, 3, 0}, {5, 4, 0}, {12, 0, 0}, {11, 3, 0}, {6, 5, 0}, {9, 3, 0},
    {7, 4, 0}, {12, 2, 2}, {10, 4, 0}, {12, 3, 3}, {13, 4, 0},
};
static const bc6h_field_t bc6h_mode30_fields[] = {
    {0, 4, 0}, {1, 5, 0}, {11, 4, 4}, {12, 0, 0}, {12, 1, 1}, {9, 4, 4},
    {2, 5, 0}, {8, 5, 5}, {9, 5, 5}, {12, 2, 2}, {8, 4, 4}, {3, 5, 0},
    {11, 5, 5}, {12, 3, 3}, {12, 5, 5}, {12, 4, 4}, {4, 5, 0}, {8, 3, 0},
    {5, 5, 0}, {11, 3, 0}, {6, 5, 0}, {9, 3, 0}, {7, 5, 0}, {10, 5, 0},
    {13, 4, 0},
};
static const bc6h_field_t bc6h_mode3_fields[] = {
    {0, 4, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 9, 0}, {5, 9, 0},
    {6, 9, 0},
};
static const bc6h_field_t bc6h_mode7_fields[] = {
    {0, 4, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 8, 0}, {1, 10, 10},
    {5, 8, 0}, {2, 10, 10}, {6, 8, 0}, {3, 10, 10},
};
static const bc6h_field_t bc6h_mode11_fields[] = {
    {0, 4, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 7, 0}, {1, 10, 11},
    {5, 7, 0}, {2, 10, 11}, {6, 7, 0}, {3, 10, 11},
};
static const bc6h_field_t bc6h_mode15_fields[] = {
    {0, 4, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 3, 0}, {1, 10, 15},
    {5, 3, 0}, {2, 10, 15}, {6, 3, 0}, {3, 10, 15},
};

typedef struct {
    int8_t mode;
    const bc6h_field_t *fields;
    uint8_t field_count;
    uint8_t epb;         /* endpoint precision bits */
    uint8_t transformed; /* endpoints 1..3 are deltas from endpoint 0 */
    uint8_t two_subset;
} bc6h_mode_info_t;

static const bc6h_mode_info_t bc6h_modes[] = {
    {0, bc6h_mode0_fields, (uint8_t)(sizeof(bc6h_mode0_fields) / sizeof(bc6h_field_t)), 10, 1, 1},
    {1, bc6h_mode1_fields, (uint8_t)(sizeof(bc6h_mode1_fields) / sizeof(bc6h_field_t)), 7, 1, 1},
    {2, bc6h_mode2_fields, (uint8_t)(sizeof(bc6h_mode2_fields) / sizeof(bc6h_field_t)), 11, 1, 1},
    {6, bc6h_mode6_fields, (uint8_t)(sizeof(bc6h_mode6_fields) / sizeof(bc6h_field_t)), 11, 1, 1},
    {10, bc6h_mode10_fields, (uint8_t)(sizeof(bc6h_mode10_fields) / sizeof(bc6h_field_t)), 11, 1, 1},
    {14, bc6h_mode14_fields, (uint8_t)(sizeof(bc6h_mode14_fields) / sizeof(bc6h_field_t)), 9, 1, 1},
    {18, bc6h_mode18_fields, (uint8_t)(sizeof(bc6h_mode18_fields) / sizeof(bc6h_field_t)), 8, 1, 1},
    {22, bc6h_mode22_fields, (uint8_t)(sizeof(bc6h_mode22_fields) / sizeof(bc6h_field_t)), 8, 1, 1},
    {26, bc6h_mode26_fields, (uint8_t)(sizeof(bc6h_mode26_fields) / sizeof(bc6h_field_t)), 8, 1, 1},
    {30, bc6h_mode30_fields, (uint8_t)(sizeof(bc6h_mode30_fields) / sizeof(bc6h_field_t)), 6, 0, 1},
    {3, bc6h_mode3_fields, (uint8_t)(sizeof(bc6h_mode3_fields) / sizeof(bc6h_field_t)), 10, 0, 0},
    {7, bc6h_mode7_fields, (uint8_t)(sizeof(bc6h_mode7_fields) / sizeof(bc6h_field_t)), 11, 1, 0},
    {11, bc6h_mode11_fields, (uint8_t)(sizeof(bc6h_mode11_fields) / sizeof(bc6h_field_t)), 12, 1, 0},
    {15, bc6h_mode15_fields, (uint8_t)(sizeof(bc6h_mode15_fields) / sizeof(bc6h_field_t)), 16, 1, 0},
};

static const uint8_t bc6h_weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
static const uint8_t bc6h_weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30,
                                          34, 38, 43, 47, 51, 55, 60, 64};

typedef struct {
    const uint8_t *data;
    uint32_t bit;
} bc6h_reader_t;

static uint32_t bc6h_read_bits(bc6h_reader_t *r, uint32_t count) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t bit = r->bit + i;
        value |= (uint32_t)((r->data[bit >> 3] >> (bit & 7u)) & 1u) << i;
    }
    r->bit += count;
    return value;
}

static int32_t bc6h_sign_extend(int32_t value, int bits) {
    int32_t mask = 1 << (bits - 1);
    return (value ^ mask) - mask;
}

static int32_t bc6h_unquantize_unsigned(int32_t x, int epb) {
    if (epb >= 15)
        return x;
    if (x == 0)
        return 0;
    if (x == ((1 << epb) - 1))
        return 0xFFFF;
    return ((x << 15) + 0x4000) >> (epb - 1);
}

static int32_t bc6h_unquantize_signed(int32_t x, int epb) {
    int s = 0;
    int32_t unq;
    if (epb >= 16)
        return x;
    if (x < 0) {
        s = 1;
        x = -x;
    }
    if (x == 0)
        unq = 0;
    else if (x >= ((1 << (epb - 1)) - 1))
        unq = 0x7FFF;
    else
        unq = ((x << 15) + 0x4000) >> (epb - 1);
    return s ? -unq : unq;
}

static uint16_t bc6h_finish_unsigned(int32_t i) {
    return (uint16_t)((i * 31) >> 6);
}

static uint16_t bc6h_finish_signed(int32_t i) {
    return i < 0 ? (uint16_t)((((-i) * 31) >> 5) | 0x8000) : (uint16_t)((i * 31) >> 5);
}

/// Decode one 16-byte BC6H block into 16 RGB half-float bit triples.
void bc6h_decode_block(const uint8_t block[16], int is_signed, uint16_t out_halves[16][3]) {
    bc6h_reader_t reader = {block, 0};
    const bc6h_mode_info_t *info = 0;
    int32_t vars[14];
    int mode;

    memset(out_halves, 0, 16 * 3 * sizeof(uint16_t));
    mode = block[0] & 3;
    if (mode >= 2)
        mode = block[0] & 31;
    for (size_t i = 0; i < sizeof(bc6h_modes) / sizeof(bc6h_modes[0]); i++) {
        if (bc6h_modes[i].mode == mode) {
            info = &bc6h_modes[i];
            break;
        }
    }
    if (!info)
        return; /* reserved mode: all-zero texels */

    memset(vars, 0, sizeof(vars));
    for (int f = 0; f < info->field_count; f++) {
        const bc6h_field_t *field = &info->fields[f];
        if (field->hi >= field->lo) {
            uint32_t width = (uint32_t)(field->hi - field->lo + 1);
            uint32_t bits = bc6h_read_bits(&reader, width);
            vars[field->var] |= (int32_t)(bits << field->lo);
        } else {
            /* Reversed storage: first block bit lands in the highest variable bit. */
            uint32_t width = (uint32_t)(field->lo - field->hi + 1);
            uint32_t bits = bc6h_read_bits(&reader, width);
            for (uint32_t i = 0; i < width; i++) {
                if (bits & (1u << i))
                    vars[field->var] |= 1 << (field->lo - (int)i);
            }
        }
    }

    {
        int epb = info->epb;
        int endpoint_count = info->two_subset ? 4 : 2;
        int32_t endpoints[4][3];
        int32_t mask = epb >= 32 ? -1 : ((1 << epb) - 1);
        int pb = vars[13] & 31;
        for (int e = 0; e < endpoint_count; e++) {
            for (int c = 0; c < 3; c++)
                endpoints[e][c] = vars[1 + e * 3 + c];
        }
        /* Sign extension: E0 when signed; deltas when signed or transformed. */
        for (int c = 0; c < 3; c++) {
            if (is_signed)
                endpoints[0][c] = bc6h_sign_extend(endpoints[0][c], epb);
        }
        for (int e = 1; e < endpoint_count; e++) {
            for (int c = 0; c < 3; c++) {
                int width = epb;
                if (is_signed || info->transformed) {
                    /* Delta width = highest bit this mode stores for the var. */
                    int max_bit = 0;
                    for (int f = 0; f < info->field_count; f++) {
                        const bc6h_field_t *field = &info->fields[f];
                        if (field->var == 1 + e * 3 + c) {
                            int top = field->hi > field->lo ? field->hi : field->lo;
                            if (top + 1 > max_bit)
                                max_bit = top + 1;
                        }
                    }
                    width = max_bit;
                    endpoints[e][c] = bc6h_sign_extend(endpoints[e][c], width);
                }
            }
        }
        if (info->transformed) {
            for (int e = 1; e < endpoint_count; e++) {
                for (int c = 0; c < 3; c++) {
                    endpoints[e][c] = (endpoints[0][c] + endpoints[e][c]) & mask;
                    if (is_signed)
                        endpoints[e][c] = bc6h_sign_extend(endpoints[e][c], epb);
                }
            }
        }
        for (int e = 0; e < endpoint_count; e++) {
            for (int c = 0; c < 3; c++) {
                endpoints[e][c] = is_signed
                                      ? bc6h_unquantize_signed(endpoints[e][c], epb)
                                      : bc6h_unquantize_unsigned(endpoints[e][c], epb);
            }
        }

        for (int t = 0; t < 16; t++) {
            int subset = info->two_subset ? bc6h_partition2[pb][t] : 0;
            int anchor = (t == 0) || (info->two_subset && t == bc6h_anchor2[pb]);
            int index_bits = (info->two_subset ? 3 : 4) - (anchor ? 1 : 0);
            uint32_t index = bc6h_read_bits(&reader, (uint32_t)index_bits);
            int weight = info->two_subset ? bc6h_weights3[index & 7]
                                          : bc6h_weights4[index & 15];
            for (int c = 0; c < 3; c++) {
                int32_t e0 = endpoints[subset * 2][c];
                int32_t e1 = endpoints[subset * 2 + 1][c];
                int32_t interpolated = (e0 * (64 - weight) + e1 * weight + 32) >> 6;
                out_halves[t][c] = is_signed ? bc6h_finish_signed(interpolated)
                                             : bc6h_finish_unsigned(interpolated);
            }
        }
    }
}


/// @brief Half-float bit pattern to float.
static float textureasset3d_half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;
    uint32_t bits;
    float out;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            /* subnormal: normalize */
            int shift = 0;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                shift++;
            }
            mantissa &= 0x3FFu;
            bits = sign | ((uint32_t)(127 - 15 - shift) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }
    memcpy(&out, &bits, sizeof(out));
    return out;
}

/// @brief Decode one BC6H mip level to an RGBA8 Pixels fallback (halves clamped
///   to [0, 1]; alpha forced opaque).
static void *textureasset3d_decode_bc6h_fallback(const uint8_t *data,
                                                 size_t size,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint64_t offset,
                                                 uint64_t length,
                                                 int is_signed) {
    rt_pixels_impl *pixels;
    uint32_t blocks_x = (width + 3u) / 4u;
    uint32_t blocks_y = (height + 3u) / 4u;
    uint64_t needed = (uint64_t)blocks_x * (uint64_t)blocks_y * 16u;
    uint64_t rgba_bytes = 0;
    if (width == 0 || height == 0)
        return NULL;
    if (!textureasset3d_rgba8_byte_count(width, height, &rgba_bytes))
        return NULL;
    if (rgba_bytes > TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES)
        return NULL;
    if (length < needed || offset > (uint64_t)size || needed > (uint64_t)size - offset)
        return NULL;
    pixels = (rt_pixels_impl *)rt_pixels_new((int64_t)width, (int64_t)height);
    if (!pixels) {
        rt_trap("TextureAsset3D.LoadKTX2: BC6H Pixels fallback allocation failed");
        return NULL;
    }
    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            uint16_t halves[16][3];
            const uint8_t *block = data + offset + ((uint64_t)by * blocks_x + bx) * 16u;
            bc6h_decode_block(block, is_signed, halves);
            for (int ty = 0; ty < 4; ty++) {
                for (int tx = 0; tx < 4; tx++) {
                    uint32_t px = bx * 4u + (uint32_t)tx;
                    uint32_t py = by * 4u + (uint32_t)ty;
                    uint32_t rgba = 0x000000FFu;
                    if (px >= width || py >= height)
                        continue;
                    for (int c = 0; c < 3; c++) {
                        float v = textureasset3d_half_to_float(halves[ty * 4 + tx][c]);
                        int b;
                        if (!(v > 0.0f))
                            v = 0.0f;
                        if (v > 1.0f)
                            v = 1.0f;
                        b = (int)(v * 255.0f + 0.5f);
                        rgba |= (uint32_t)b << (24 - c * 8);
                    }
                    textureasset3d_write_fallback_rgba(pixels, px, py, rgba);
                }
            }
        }
    }
    pixels_touch(pixels);
    return pixels;
}

/// @brief Decode all BC6H mips into a Pixels array (per-mip misses allowed).
static void **textureasset3d_decode_bc6h_mips(const uint8_t *data,
                                              size_t size,
                                              const textureasset3d_mip *mips,
                                              int64_t mip_count,
                                              int is_signed) {
    void **mip_pixels;
    int decoded_count = 0;
    if (!data || !mips || mip_count <= 0)
        return NULL;
    mip_pixels = (void **)calloc((size_t)mip_count, sizeof(void *));
    if (!mip_pixels)
        return NULL;
    for (int64_t i = 0; i < mip_count; i++) {
        mip_pixels[i] = textureasset3d_decode_bc6h_fallback(
            data, size, mips[i].width, mips[i].height, mips[i].offset, mips[i].length,
            is_signed);
        if (mip_pixels[i])
            decoded_count++;
    }
    if (decoded_count <= 0) {
        free(mip_pixels);
        return NULL;
    }
    return mip_pixels;
}
