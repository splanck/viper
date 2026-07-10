//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_gltf_draco.inc  (#included from rt_gltf.c;
// not a standalone translation unit)
// Purpose: KHR_draco_mesh_compression decoding — clean-room implementation from the
//   published Draco Bitstream Specification (google.github.io/draco/spec). Covers the
//   sequential mesh encoding (compressed and uncompressed indices) with the rANS
//   entropy decoder, difference prediction, wrap and canonicalized-octahedron
//   transforms, quantization and octahedral-normal dequantization.
// Key invariants:
//   - Every read is bounds-checked; malformed payloads fail the primitive load with a
//     recoverable diagnostic, never a trap.
//   - Edgebreaker connectivity is not implemented yet and fails with a named
//     UNSUPPORTED diagnostic.
//===----------------------------------------------------------------------===//

#define DRACO_MAX_COMPONENTS 4
#define DRACO_MAX_ATTRIBUTES 16

/* Sequential attribute encoder types */
#define DRACO_SEQ_GENERIC 0
#define DRACO_SEQ_INTEGER 1
#define DRACO_SEQ_QUANTIZATION 2
#define DRACO_SEQ_NORMALS 3

/* Prediction schemes / transforms */
#define DRACO_PREDICTION_NONE (-2)
#define DRACO_PREDICTION_DIFFERENCE 0
#define DRACO_TRANSFORM_WRAP 1
#define DRACO_TRANSFORM_NORMAL_OCT_CANON 3

/// @brief Bounded little-endian byte reader for one Draco payload.
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int failed;
} draco_buf;

static uint8_t draco_read_u8(draco_buf *b) {
    if (b->pos >= b->size) {
        b->failed = 1;
        return 0;
    }
    return b->data[b->pos++];
}

static uint16_t draco_read_u16(draco_buf *b) {
    uint16_t lo = draco_read_u8(b);
    uint16_t hi = draco_read_u8(b);
    return (uint16_t)(lo | (hi << 8));
}

static uint32_t draco_read_u32(draco_buf *b) {
    uint32_t v = draco_read_u16(b);
    v |= (uint32_t)draco_read_u16(b) << 16;
    return v;
}

static float draco_read_f32(draco_buf *b) {
    uint32_t bits = draco_read_u32(b);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/// @brief Unsigned LEB128 varint (capped at 64 bits).
static uint64_t draco_read_varint(draco_buf *b) {
    uint64_t result = 0;
    int shift = 0;
    for (int i = 0; i < 10; i++) {
        uint8_t byte = draco_read_u8(b);
        if (b->failed)
            return 0;
        result |= (uint64_t)(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0)
            return result;
        shift += 7;
    }
    b->failed = 1;
    return 0;
}

//===----------------------------------------------------------------------===//
// rANS entropy decoding (spec "Rans Decoding")
//===----------------------------------------------------------------------===//

typedef struct {
    const uint8_t *buf;
    size_t buf_offset;
    uint32_t state;
} draco_rans;

typedef struct {
    uint32_t prob;
    uint32_t cum_prob;
} draco_rans_sym;

/// @brief Initialize a rANS decoder from the tail of @p buf (spec RansInitDecoder).
static int draco_rans_init(draco_rans *ans, const uint8_t *buf, size_t size, uint32_t l_base) {
    uint32_t x;
    if (size == 0)
        return 0;
    ans->buf = buf;
    x = buf[size - 1] >> 6;
    if (x == 0) {
        ans->buf_offset = size - 1;
        ans->state = buf[size - 1] & 0x3Fu;
    } else if (x == 1) {
        if (size < 2)
            return 0;
        ans->buf_offset = size - 2;
        ans->state = ((uint32_t)buf[size - 2] | ((uint32_t)buf[size - 1] << 8)) & 0x3FFFu;
    } else if (x == 2) {
        if (size < 3)
            return 0;
        ans->buf_offset = size - 3;
        ans->state = ((uint32_t)buf[size - 3] | ((uint32_t)buf[size - 2] << 8) |
                      ((uint32_t)buf[size - 1] << 16)) &
                     0x3FFFFFu;
    } else {
        if (size < 4)
            return 0;
        ans->buf_offset = size - 4;
        ans->state = ((uint32_t)buf[size - 4] | ((uint32_t)buf[size - 3] << 8) |
                      ((uint32_t)buf[size - 2] << 16) | ((uint32_t)buf[size - 1] << 24)) &
                     0x3FFFFFFFu;
    }
    ans->state += l_base;
    return 1;
}

/// @brief Read one symbol (spec RansRead) using the lookup/probability tables.
static int draco_rans_read(draco_rans *ans,
                           uint32_t l_base,
                           uint32_t precision,
                           const uint32_t *lut,
                           const draco_rans_sym *probs,
                           uint32_t *out_val) {
    uint32_t quo;
    uint32_t rem;
    uint32_t symbol;
    while (ans->state < l_base && ans->buf_offset > 0)
        ans->state = ans->state * 256u + ans->buf[--ans->buf_offset];
    if (ans->state < l_base)
        return 0;
    quo = ans->state / precision;
    rem = ans->state % precision;
    symbol = lut[rem];
    ans->state = quo * probs[symbol].prob + rem - probs[symbol].cum_prob;
    *out_val = symbol;
    return 1;
}

/// @brief Binary rABS reader (spec RabsDescRead) for prediction/seam flag streams.
/// @note Reserved for the edgebreaker path (attribute seams, constrained-multi
///       parallelogram crease flags); unused while only sequential meshes decode.
#if defined(__clang__) || defined(__GNUC__)
__attribute__((unused))
#endif
static int draco_rabs_read(draco_rans *ans, uint32_t prob_zero, uint32_t *out_val) {
    uint32_t p = 256u - prob_zero;
    uint32_t x;
    uint32_t quot;
    uint32_t rem;
    uint32_t xn;
    uint32_t val;
    if (ans->state < 4096u && ans->buf_offset > 0)
        ans->state = ans->state * 256u + ans->buf[--ans->buf_offset];
    x = ans->state;
    quot = x / 256u;
    rem = x % 256u;
    xn = quot * p;
    val = rem < p;
    ans->state = val ? xn + rem : x - xn - p;
    *out_val = val;
    return 1;
}

/// @brief Parse the run-length coded probability table and build the rANS lookup
///        tables (spec BuildSymbolTables + rans_build_look_up_table).
/// @return 1 on success; tables are heap allocations owned by the caller.
static int draco_build_symbol_tables(draco_buf *b,
                                     uint32_t num_symbols,
                                     uint32_t precision,
                                     uint32_t **out_lut,
                                     draco_rans_sym **out_probs) {
    uint32_t *token_probs;
    uint32_t *lut = NULL;
    draco_rans_sym *probs = NULL;
    uint64_t cum_prob = 0;
    *out_lut = NULL;
    *out_probs = NULL;
    if (num_symbols == 0 || num_symbols > (1u << 18))
        return 0;
    token_probs = (uint32_t *)calloc(num_symbols, sizeof(uint32_t));
    if (!token_probs)
        return 0;
    for (uint32_t i = 0; i < num_symbols; i++) {
        uint8_t prob_data = draco_read_u8(b);
        uint32_t token = prob_data & 3u;
        if (b->failed)
            goto fail;
        if (token == 3u) {
            uint32_t offset = prob_data >> 2;
            if (i + offset >= num_symbols)
                goto fail;
            /* zero-probability run: token_probs entries stay zero */
            i += offset;
        } else {
            uint32_t prob = prob_data >> 2;
            for (uint32_t j = 0; j < token; j++) {
                uint8_t eb = draco_read_u8(b);
                if (b->failed)
                    goto fail;
                prob |= (uint32_t)eb << (8u * (j + 1u) - 2u);
            }
            token_probs[i] = prob;
        }
    }
    probs = (draco_rans_sym *)calloc(num_symbols, sizeof(draco_rans_sym));
    lut = (uint32_t *)calloc(precision, sizeof(uint32_t));
    if (!probs || !lut)
        goto fail;
    for (uint32_t i = 0; i < num_symbols; i++) {
        probs[i].prob = token_probs[i];
        probs[i].cum_prob = (uint32_t)cum_prob;
        cum_prob += token_probs[i];
        if (cum_prob > precision)
            goto fail;
        for (uint64_t j = probs[i].cum_prob; j < cum_prob; j++)
            lut[j] = i;
    }
    if (cum_prob != precision)
        goto fail;
    free(token_probs);
    *out_lut = lut;
    *out_probs = probs;
    return 1;
fail:
    free(token_probs);
    free(lut);
    free(probs);
    return 0;
}

/// @brief Decode @p num_values entropy-coded symbols (spec DecodeSymbols: both the
///        TAGGED and RAW schemes).
/// @return 1 on success; @p out_values (caller-allocated) receives the symbols.
static int draco_decode_symbols(draco_buf *b,
                                uint32_t num_values,
                                uint32_t num_components,
                                uint32_t *out_values) {
    uint8_t scheme = draco_read_u8(b);
    if (b->failed || num_components == 0)
        return 0;
    if (num_values == 0)
        return 1;
    if (scheme == 0) {
        /* TAGGED_SYMBOLS: rANS-coded bit lengths + raw bits per value. */
        uint32_t num_symbols = (uint32_t)draco_read_varint(b);
        uint32_t *lut = NULL;
        draco_rans_sym *probs = NULL;
        uint64_t size;
        draco_rans ans;
        int ok = 0;
        if (b->failed ||
            !draco_build_symbol_tables(b, num_symbols, 4096u /* TAGGED_RANS_PRECISION */, &lut,
                                       &probs))
            return 0;
        size = draco_read_varint(b);
        if (b->failed || size == 0 || size > b->size - b->pos)
            goto tagged_done;
        if (!draco_rans_init(&ans, b->data + b->pos, (size_t)size, 16384u /* TAGGED_RANS_BASE */))
            goto tagged_done;
        {
            /* Values are stored as contiguously packed raw bits (LSB-first) following
             * the tag stream; the reader realigns to a byte boundary once at the end.
             * (Verified against reference-encoder output; per-group realignment does
             * not match the emitted bitstream.) */
            size_t bit_byte = b->pos + (size_t)size;
            size_t bit_end = b->size;
            uint64_t bit_pos = 0;
            for (uint32_t i = 0; i < num_values; i += num_components) {
                uint32_t bit_length;
                if (!draco_rans_read(&ans, 16384u, 4096u, lut, probs, &bit_length))
                    goto tagged_done;
                if (bit_length > 32u)
                    goto tagged_done;
                for (uint32_t c = 0; c < num_components && i + c < num_values; c++) {
                    uint32_t val = 0;
                    for (uint32_t k = 0; k < bit_length; k++) {
                        uint64_t idx = bit_pos + k;
                        size_t byte = bit_byte + (size_t)(idx >> 3);
                        if (byte >= bit_end)
                            goto tagged_done;
                        val |= (uint32_t)((b->data[byte] >> (idx & 7u)) & 1u) << k;
                    }
                    bit_pos += bit_length;
                    out_values[i + c] = val;
                }
            }
            b->pos = bit_byte + (size_t)((bit_pos + 7u) >> 3);
            ok = b->pos <= b->size;
        }
    tagged_done:
        free(lut);
        free(probs);
        return ok;
    }
    if (scheme == 1) {
        /* RAW_SYMBOLS: one rANS symbol per value with adaptive precision. */
        uint8_t max_bit_length = draco_read_u8(b);
        uint32_t num_symbols = (uint32_t)draco_read_varint(b);
        uint32_t precision_bits = (3u * max_bit_length) / 2u;
        uint32_t precision;
        uint32_t l_base;
        uint32_t *lut = NULL;
        draco_rans_sym *probs = NULL;
        uint64_t size;
        draco_rans ans;
        int ok = 0;
        if (precision_bits > 20u)
            precision_bits = 20u;
        if (precision_bits < 12u)
            precision_bits = 12u;
        precision = 1u << precision_bits;
        l_base = precision * 4u;
        if (b->failed || !draco_build_symbol_tables(b, num_symbols, precision, &lut, &probs))
            return 0;
        size = draco_read_varint(b);
        if (b->failed || size == 0 || size > b->size - b->pos)
            goto raw_done;
        if (!draco_rans_init(&ans, b->data + b->pos, (size_t)size, l_base))
            goto raw_done;
        for (uint32_t i = 0; i < num_values; i++) {
            if (!draco_rans_read(&ans, l_base, precision, lut, probs, &out_values[i]))
                goto raw_done;
        }
        b->pos += (size_t)size;
        ok = 1;
    raw_done:
        free(lut);
        free(probs);
        return ok;
    }
    return 0;
}

//===----------------------------------------------------------------------===//
// Draco mesh state
//===----------------------------------------------------------------------===//

/// @brief One decoded attribute: identification plus decoded value storage.
typedef struct {
    uint8_t att_type;   /* 0 pos, 1 normal, 2 color, 3 tex coord, 4 generic */
    uint8_t data_type;  /* draco DT_* */
    uint8_t components; /* 1..4 */
    uint8_t normalized;
    uint32_t unique_id;
    uint8_t decoder_type; /* DRACO_SEQ_* */
    /* portable (integer) decode state */
    int8_t prediction_scheme;
    int8_t prediction_transform;
    int32_t *values; /* num_values * components, post-prediction integers */
    /* transform data */
    int32_t wrap_min;
    int32_t wrap_max;
    int32_t normal_max_q;
    uint8_t quantization_bits;
    float quant_min[DRACO_MAX_COMPONENTS];
    float quant_range;
    /* final output */
    float *out_floats;  /* dequantized / normal-decoded values, or NULL */
    int32_t *out_ints;  /* integer values, or NULL */
    uint32_t num_values;
} draco_attribute;

/// @brief Full decode state for one Draco payload.
typedef struct {
    uint32_t num_faces;
    uint32_t num_points;
    uint32_t *face_to_vertex; /* 3 * num_faces, interleaved v0 v1 v2 */
    draco_attribute attributes[DRACO_MAX_ATTRIBUTES];
    uint32_t num_attributes;
} draco_mesh;

static void draco_mesh_free(draco_mesh *mesh) {
    free(mesh->face_to_vertex);
    for (uint32_t i = 0; i < mesh->num_attributes && i < DRACO_MAX_ATTRIBUTES; i++) {
        free(mesh->attributes[i].values);
        free(mesh->attributes[i].out_floats);
        free(mesh->attributes[i].out_ints);
    }
    memset(mesh, 0, sizeof(*mesh));
}

/// @brief Decode sequential connectivity (spec DecodeSequentialConnectivityData).
static int draco_decode_sequential_connectivity(draco_buf *b, draco_mesh *mesh) {
    uint64_t num_faces = draco_read_varint(b);
    uint64_t num_points = draco_read_varint(b);
    uint8_t method = draco_read_u8(b);
    if (b->failed || num_faces > (uint64_t)UINT32_MAX / 3u || num_points > UINT32_MAX)
        return 0;
    mesh->num_faces = (uint32_t)num_faces;
    mesh->num_points = (uint32_t)num_points;
    if (num_faces == 0 || num_points == 0)
        return 0;
    if ((uint64_t)mesh->num_faces * 3u > SIZE_MAX / sizeof(uint32_t))
        return 0;
    mesh->face_to_vertex = (uint32_t *)malloc((size_t)mesh->num_faces * 3u * sizeof(uint32_t));
    if (!mesh->face_to_vertex)
        return 0;
    if (method == 0) {
        /* SEQUENTIAL_COMPRESSED_INDICES: zigzag deltas over entropy-coded symbols. */
        uint32_t *symbols =
            (uint32_t *)malloc((size_t)mesh->num_faces * 3u * sizeof(uint32_t));
        int64_t last = 0;
        if (!symbols)
            return 0;
        if (!draco_decode_symbols(b, mesh->num_faces * 3u, 1, symbols)) {
            free(symbols);
            return 0;
        }
        for (uint32_t i = 0; i < mesh->num_faces * 3u; i++) {
            uint32_t encoded = symbols[i];
            int64_t diff = (int64_t)(encoded >> 1);
            if (encoded & 1u)
                diff = -diff;
            last += diff;
            if (last < 0 || last >= (int64_t)mesh->num_points) {
                free(symbols);
                return 0;
            }
            mesh->face_to_vertex[i] = (uint32_t)last;
        }
        free(symbols);
        return 1;
    }
    if (method == 1) {
        /* SEQUENTIAL_UNCOMPRESSED_INDICES: width depends on the point count. */
        for (uint32_t i = 0; i < mesh->num_faces * 3u; i++) {
            uint32_t v;
            if (mesh->num_points < 256u)
                v = draco_read_u8(b);
            else if (mesh->num_points < (1u << 16))
                v = draco_read_u16(b);
            else if (mesh->num_points < (1u << 21))
                v = (uint32_t)draco_read_varint(b);
            else
                v = draco_read_u32(b);
            if (b->failed || v >= mesh->num_points)
                return 0;
            mesh->face_to_vertex[i] = v;
        }
        return 1;
    }
    return 0;
}

/// @brief Skip one metadata element (spec ParseMetadataElement, recursive layout).
static int draco_skip_metadata_element(draco_buf *b, int depth) {
    uint64_t num_entries = draco_read_varint(b);
    uint64_t num_sub;
    if (b->failed || depth > 8 || num_entries > 100000u)
        return 0;
    for (uint64_t i = 0; i < num_entries; i++) {
        uint8_t key_size = draco_read_u8(b);
        b->pos += key_size;
        {
            uint8_t value_size = draco_read_u8(b);
            b->pos += value_size;
        }
        if (b->pos > b->size)
            b->failed = 1;
        if (b->failed)
            return 0;
    }
    num_sub = draco_read_varint(b);
    if (b->failed || num_sub > 10000u)
        return 0;
    for (uint64_t i = 0; i < num_sub; i++) {
        uint8_t key_size = draco_read_u8(b);
        b->pos += key_size;
        if (b->pos > b->size)
            b->failed = 1;
        if (b->failed || !draco_skip_metadata_element(b, depth + 1))
            return 0;
    }
    return 1;
}

/// @brief Skip the optional metadata section (spec DecodeMetadata).
static int draco_skip_metadata(draco_buf *b) {
    uint64_t num_att_metadata = draco_read_varint(b);
    if (b->failed || num_att_metadata > 10000u)
        return 0;
    for (uint64_t i = 0; i < num_att_metadata; i++) {
        (void)draco_read_varint(b); /* attribute metadata id */
        if (b->failed || !draco_skip_metadata_element(b, 0))
            return 0;
    }
    return draco_skip_metadata_element(b, 0);
}

/// @brief Wrap-transform inverse (spec PredictionSchemeWrapDecodingTransform).
static void draco_wrap_compute_original(const draco_attribute *att,
                                        const int32_t *pred,
                                        const int32_t *corr,
                                        int32_t *out,
                                        int components) {
    int64_t max_dif = 1 + (int64_t)att->wrap_max - att->wrap_min;
    for (int c = 0; c < components; c++) {
        int64_t clamped = pred[c];
        int64_t value;
        if (clamped > att->wrap_max)
            clamped = att->wrap_max;
        else if (clamped < att->wrap_min)
            clamped = att->wrap_min;
        value = clamped + corr[c];
        if (value > att->wrap_max)
            value -= max_dif;
        else if (value < att->wrap_min)
            value += max_dif;
        out[c] = (int32_t)value;
    }
}

/* --- Canonicalized octahedron normal transform (spec Prediction Normal Transform) --- */

static int32_t draco_mod_max(int32_t x, int32_t center, int32_t max_q) {
    if (x > center)
        return x - max_q;
    if (x < -center)
        return x + max_q;
    return x;
}

static void draco_invert_diamond(int32_t *s, int32_t *t, int32_t center) {
    int32_t sign_s = 0;
    int32_t sign_t = 0;
    int32_t corner_s;
    int32_t corner_t;
    int32_t ss = *s;
    int32_t tt = *t;
    if (ss >= 0 && tt >= 0) {
        sign_s = 1;
        sign_t = 1;
    } else if (ss <= 0 && tt <= 0) {
        sign_s = -1;
        sign_t = -1;
    } else {
        sign_s = ss > 0 ? 1 : -1;
        sign_t = tt > 0 ? 1 : -1;
    }
    corner_s = sign_s * center;
    corner_t = sign_t * center;
    ss = 2 * ss - corner_s;
    tt = 2 * tt - corner_t;
    if (sign_s * sign_t >= 0) {
        int32_t temp = ss;
        ss = -tt;
        tt = -temp;
    } else {
        int32_t temp = ss;
        ss = tt;
        tt = temp;
    }
    ss = (ss + corner_s) / 2;
    tt = (tt + corner_t) / 2;
    *s = ss;
    *t = tt;
}

static int draco_rotation_count(const int32_t *pred) {
    int32_t sign_x = pred[0];
    int32_t sign_y = pred[1];
    if (sign_x == 0)
        return sign_y == 0 ? 0 : (sign_y > 0 ? 3 : 1);
    if (sign_x > 0)
        return sign_y >= 0 ? 2 : 1;
    return sign_y <= 0 ? 0 : 3;
}

static void draco_rotate_point(const int32_t *p, int rotation, int32_t *out) {
    switch (rotation) {
    case 1:
        out[0] = p[1];
        out[1] = -p[0];
        return;
    case 2:
        out[0] = -p[0];
        out[1] = -p[1];
        return;
    case 3:
        out[0] = -p[1];
        out[1] = p[0];
        return;
    default:
        out[0] = p[0];
        out[1] = p[1];
        return;
    }
}

static int draco_is_in_bottom_left(const int32_t *p) {
    if (p[0] == 0 && p[1] == 0)
        return 1;
    return p[0] < 0 && p[1] <= 0;
}

static int draco_msb(uint32_t n) {
    int msb = -1;
    while (n != 0) {
        msb++;
        n >>= 1;
    }
    return msb;
}

/// @brief Octahedron-canonicalized transform inverse (spec ComputeOriginalValue/2).
static void draco_normal_oct_compute_original(const draco_attribute *att,
                                              const int32_t *pred_vals,
                                              const int32_t *corr_vals,
                                              int32_t *out_vals) {
    int32_t max_q = (1 << (draco_msb((uint32_t)att->normal_max_q) + 1)) - 1;
    int32_t max_value = max_q - 1;
    int32_t center = max_value / 2;
    int32_t pred[2];
    int32_t orig[2];
    int pred_in_diamond;
    int pred_in_bottom_left;
    int rotation;
    pred[0] = pred_vals[0] - center;
    pred[1] = pred_vals[1] - center;
    pred_in_diamond = (pred[0] < 0 ? -pred[0] : pred[0]) + (pred[1] < 0 ? -pred[1] : pred[1]) <=
                      center;
    if (!pred_in_diamond)
        draco_invert_diamond(&pred[0], &pred[1], center);
    pred_in_bottom_left = draco_is_in_bottom_left(pred);
    rotation = draco_rotation_count(pred);
    if (!pred_in_bottom_left) {
        int32_t rotated[2];
        draco_rotate_point(pred, rotation, rotated);
        pred[0] = rotated[0];
        pred[1] = rotated[1];
    }
    orig[0] = draco_mod_max(pred[0] + corr_vals[0], center, max_q);
    orig[1] = draco_mod_max(pred[1] + corr_vals[1], center, max_q);
    if (!pred_in_bottom_left) {
        int reverse = (4 - rotation) % 4;
        int32_t rotated[2];
        draco_rotate_point(orig, reverse, rotated);
        orig[0] = rotated[0];
        orig[1] = rotated[1];
    }
    if (!pred_in_diamond)
        draco_invert_diamond(&orig[0], &orig[1], center);
    out_vals[0] = orig[0] + center;
    out_vals[1] = orig[1] + center;
}

/// @brief Apply the parsed prediction transform to one value tuple.
static void draco_transform_compute_original(const draco_attribute *att,
                                             const int32_t *pred,
                                             const int32_t *corr,
                                             int32_t *out,
                                             int components) {
    if (att->prediction_transform == DRACO_TRANSFORM_NORMAL_OCT_CANON)
        draco_normal_oct_compute_original(att, pred, corr, out);
    else
        draco_wrap_compute_original(att, pred, corr, out, components);
}

/// @brief Difference prediction inverse (spec PredictionSchemeDifference).
static void draco_difference_compute_original(draco_attribute *att, uint32_t num_values) {
    int components = att->components;
    int32_t zero_vals[DRACO_MAX_COMPONENTS] = {0, 0, 0, 0};
    if (num_values == 0)
        return;
    draco_transform_compute_original(att, zero_vals, att->values, att->values, components);
    for (uint32_t i = 1; i < num_values; i++) {
        draco_transform_compute_original(att,
                                         att->values + (size_t)(i - 1) * components,
                                         att->values + (size_t)i * components,
                                         att->values + (size_t)i * components,
                                         components);
    }
}

/// @brief Decode one attribute's portable integer values (spec
///        SequentialIntegerAttributeDecoder_DecodeIntegerValues, sequential meshes).
static int draco_decode_portable_attribute(draco_buf *b, draco_attribute *att) {
    uint32_t num_values = att->num_values * att->components;
    uint32_t *symbols;
    att->prediction_scheme = (int8_t)draco_read_u8(b);
    if (b->failed)
        return 0;
    if (att->prediction_scheme != DRACO_PREDICTION_NONE) {
        uint8_t compressed;
        att->prediction_transform = (int8_t)draco_read_u8(b);
        compressed = draco_read_u8(b);
        if (b->failed || compressed == 0)
            return 0; /* raw (uncompressed) integer storage is not emitted by encoders */
        if (att->prediction_scheme != DRACO_PREDICTION_DIFFERENCE)
            return 0; /* mesh prediction schemes need edgebreaker traversal state */
        if (att->prediction_transform != DRACO_TRANSFORM_WRAP &&
            att->prediction_transform != DRACO_TRANSFORM_NORMAL_OCT_CANON)
            return 0;
    } else {
        return 0; /* PREDICTION_NONE carries no value payload per the spec */
    }
    if (num_values == 0)
        return 1;
    if ((uint64_t)num_values > SIZE_MAX / sizeof(uint32_t))
        return 0;
    symbols = (uint32_t *)calloc(num_values, sizeof(uint32_t));
    att->values = (int32_t *)calloc(num_values, sizeof(int32_t));
    if (!symbols || !att->values) {
        free(symbols);
        return 0;
    }
    if (!draco_decode_symbols(b, num_values, att->components, symbols)) {
        free(symbols);
        return 0;
    }
    if (att->prediction_transform == DRACO_TRANSFORM_NORMAL_OCT_CANON) {
        for (uint32_t i = 0; i < num_values; i++)
            att->values[i] = (int32_t)symbols[i];
    } else {
        for (uint32_t i = 0; i < num_values; i++) {
            uint32_t val = symbols[i];
            int32_t signed_val;
            if (val & 1u)
                signed_val = -(int32_t)(val >> 1) - 1;
            else
                signed_val = (int32_t)(val >> 1);
            att->values[i] = signed_val;
        }
    }
    free(symbols);
    /* prediction data: transform parameters follow the symbol payload */
    if (att->prediction_transform == DRACO_TRANSFORM_WRAP) {
        att->wrap_min = (int32_t)draco_read_u32(b);
        att->wrap_max = (int32_t)draco_read_u32(b);
    } else {
        att->normal_max_q = (int32_t)draco_read_u32(b);
        (void)draco_read_u32(b); /* unused center value */
    }
    if (b->failed)
        return 0;
    draco_difference_compute_original(att, att->num_values);
    return 1;
}

/// @brief Dequantize one attribute (spec SequentialQuantizationAttributeDecoder).
static int draco_dequantize_attribute(draco_attribute *att) {
    uint32_t max_quantized = (1u << att->quantization_bits) - 1u;
    float factor;
    if (att->quantization_bits == 0 || att->quantization_bits > 30)
        return 0;
    factor = 1.0f / (float)max_quantized;
    att->out_floats =
        (float *)calloc((size_t)att->num_values * att->components, sizeof(float));
    if (!att->out_floats)
        return 0;
    for (uint32_t i = 0; i < att->num_values; i++) {
        for (int c = 0; c < att->components; c++) {
            int32_t raw = att->values[(size_t)i * att->components + c];
            int neg = raw < 0;
            float norm = (float)(neg ? -raw : raw) * factor;
            if (neg)
                norm = -norm;
            att->out_floats[(size_t)i * att->components + c] =
                norm * att->quant_range + att->quant_min[c];
        }
    }
    return 1;
}

/// @brief Convert octahedral normal coordinates to unit vectors (spec
///        OctaherdalCoordsToUnitVector and callers).
static int draco_decode_normals_attribute(draco_attribute *att) {
    int32_t max_q = (1 << (draco_msb((uint32_t)att->normal_max_q) + 1)) - 1;
    int32_t max_value = max_q - 1;
    float scale;
    if (att->normal_max_q <= 0 || max_value <= 0)
        return 0;
    scale = 1.0f / (float)max_value;
    att->out_floats = (float *)calloc((size_t)att->num_values * 3u, sizeof(float));
    if (!att->out_floats)
        return 0;
    for (uint32_t i = 0; i < att->num_values; i++) {
        float in_s = (float)att->values[(size_t)i * 2u] * scale;
        float in_t = (float)att->values[(size_t)i * 2u + 1u] * scale;
        float s = in_s;
        float t = in_t;
        float spt = s + t;
        float smt = s - t;
        float x_sign = 1.0f;
        float x;
        float y;
        float z;
        float norm_squared;
        if (spt >= 0.5f && spt <= 1.5f && smt >= -0.5f && smt <= 0.5f) {
            /* right hemisphere */
        } else {
            x_sign = -1.0f;
            if (spt <= 0.5f) {
                s = 0.5f - in_t;
                t = 0.5f - in_s;
            } else if (spt >= 1.5f) {
                s = 1.5f - in_t;
                t = 1.5f - in_s;
            } else if (smt <= -0.5f) {
                s = in_t - 0.5f;
                t = in_s + 0.5f;
            } else {
                s = in_t + 0.5f;
                t = in_s - 0.5f;
            }
            spt = s + t;
            smt = s - t;
        }
        y = 2.0f * s - 1.0f;
        z = 2.0f * t - 1.0f;
        {
            float a = 2.0f * spt - 1.0f;
            float bb = 3.0f - 2.0f * spt;
            float cc = 2.0f * smt + 1.0f;
            float dd = 1.0f - 2.0f * smt;
            float m = a < bb ? a : bb;
            float n = cc < dd ? cc : dd;
            x = (m < n ? m : n) * x_sign;
        }
        norm_squared = x * x + y * y + z * z;
        if (norm_squared < 1e-6f) {
            att->out_floats[(size_t)i * 3u] = 0;
            att->out_floats[(size_t)i * 3u + 1u] = 0;
            att->out_floats[(size_t)i * 3u + 2u] = 0;
        } else {
            float d = (float)(1.0 / sqrt((double)norm_squared));
            att->out_floats[(size_t)i * 3u] = x * d;
            att->out_floats[(size_t)i * 3u + 1u] = y * d;
            att->out_floats[(size_t)i * 3u + 2u] = z * d;
        }
    }
    return 1;
}

/// @brief Decode a complete Draco payload (sequential meshes; spec Decode()).
/// @return 1 on success, 0 with @p out_unsupported set when the payload uses the
///         edgebreaker encoding (not yet implemented) rather than being corrupt.
static int draco_decode_mesh(const uint8_t *data,
                             size_t size,
                             draco_mesh *mesh,
                             int *out_unsupported) {
    draco_buf b;
    uint8_t encoder_type;
    uint8_t encoder_method;
    uint16_t flags;
    memset(mesh, 0, sizeof(*mesh));
    *out_unsupported = 0;
    b.data = data;
    b.size = size;
    b.pos = 0;
    b.failed = 0;
    if (size < 11 || memcmp(data, "DRACO", 5) != 0)
        return 0;
    b.pos = 5;
    (void)draco_read_u8(&b); /* major version */
    (void)draco_read_u8(&b); /* minor version */
    encoder_type = draco_read_u8(&b);
    encoder_method = draco_read_u8(&b);
    flags = draco_read_u16(&b);
    if (b.failed || encoder_type != 1 /* TRIANGULAR_MESH */)
        return 0;
    if ((flags & 0x8000u) && !draco_skip_metadata(&b))
        return 0;
    if (encoder_method != 0 /* MESH_SEQUENTIAL_ENCODING */) {
        *out_unsupported = 1;
        return 0;
    }
    if (!draco_decode_sequential_connectivity(&b, mesh))
        goto fail;

    /* Attribute decoders (spec ParseAttributeDecodersData, sequential layout). */
    {
        uint8_t num_decoders = draco_read_u8(&b);
        uint32_t total_attributes = 0;
        if (b.failed || num_decoders != 1)
            goto fail; /* sequential meshes always use one attributes decoder */
        {
            uint64_t num_attributes = draco_read_varint(&b);
            if (b.failed || num_attributes == 0 || num_attributes > DRACO_MAX_ATTRIBUTES)
                goto fail;
            total_attributes = (uint32_t)num_attributes;
            for (uint32_t j = 0; j < total_attributes; j++) {
                draco_attribute *att = &mesh->attributes[j];
                att->att_type = draco_read_u8(&b);
                att->data_type = draco_read_u8(&b);
                att->components = draco_read_u8(&b);
                att->normalized = draco_read_u8(&b);
                att->unique_id = (uint32_t)draco_read_varint(&b);
                att->num_values = mesh->num_points;
                if (b.failed || att->components == 0 ||
                    att->components > DRACO_MAX_COMPONENTS)
                    goto fail;
            }
            for (uint32_t j = 0; j < total_attributes; j++)
                mesh->attributes[j].decoder_type = draco_read_u8(&b);
            if (b.failed)
                goto fail;
        }
        mesh->num_attributes = total_attributes;

        /* DecodePortableAttributes + DecodeDataNeededByPortableTransforms +
         * TransformAttributesToOriginalFormat, in spec order. */
        for (uint32_t j = 0; j < total_attributes; j++) {
            if (!draco_decode_portable_attribute(&b, &mesh->attributes[j]))
                goto fail;
        }
        for (uint32_t j = 0; j < total_attributes; j++) {
            draco_attribute *att = &mesh->attributes[j];
            if (att->decoder_type == DRACO_SEQ_NORMALS) {
                att->quantization_bits = draco_read_u8(&b);
                if (b.failed)
                    goto fail;
            } else if (att->decoder_type == DRACO_SEQ_QUANTIZATION) {
                for (int c = 0; c < att->components; c++)
                    att->quant_min[c] = draco_read_f32(&b);
                att->quant_range = draco_read_f32(&b);
                att->quantization_bits = draco_read_u8(&b);
                if (b.failed)
                    goto fail;
            }
        }
        for (uint32_t j = 0; j < total_attributes; j++) {
            draco_attribute *att = &mesh->attributes[j];
            if (att->decoder_type == DRACO_SEQ_NORMALS) {
                if (att->prediction_transform == DRACO_TRANSFORM_NORMAL_OCT_CANON) {
                    if (!draco_decode_normals_attribute(att))
                        goto fail;
                } else {
                    goto fail;
                }
            } else if (att->decoder_type == DRACO_SEQ_QUANTIZATION) {
                if (!draco_dequantize_attribute(att))
                    goto fail;
            } else {
                att->out_ints =
                    (int32_t *)calloc((size_t)att->num_values * att->components,
                                      sizeof(int32_t));
                if (!att->out_ints)
                    goto fail;
                memcpy(att->out_ints,
                       att->values,
                       (size_t)att->num_values * att->components * sizeof(int32_t));
            }
        }
    }
    return 1;
fail:
    draco_mesh_free(mesh);
    return 0;
}

/// @brief Find a decoded Draco attribute by its glTF-mapped unique id.
static const draco_attribute *draco_find_attribute(const draco_mesh *mesh, int64_t unique_id) {
    if (unique_id < 0)
        return NULL;
    for (uint32_t i = 0; i < mesh->num_attributes; i++) {
        if (mesh->attributes[i].unique_id == (uint32_t)unique_id)
            return &mesh->attributes[i];
    }
    return NULL;
}
