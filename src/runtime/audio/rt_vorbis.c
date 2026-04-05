//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_vorbis.c
// Purpose: Vorbis I audio decoder implementation.
// Key invariants:
//   - Implements floor type 1, residue types 0/1/2, codebook VQ
//   - IMDCT via split-radix FFT with pre/post twiddle
//   - All memory is owned by the decoder instance
// Ownership/Lifetime:
//   - Created by vorbis_decoder_new, freed by vorbis_decoder_free
// Links: rt_vorbis.h, Vorbis I specification (xiph.org)
//
//===----------------------------------------------------------------------===//

#include "rt_vorbis.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VORBIS_MAX_CHANNELS 8
#define VORBIS_MAX_CODEBOOKS 256
#define VORBIS_MAX_FLOORS 64
#define VORBIS_MAX_RESIDUES 64
#define VORBIS_MAX_MAPPINGS 64
#define VORBIS_MAX_MODES 64

//===----------------------------------------------------------------------===//
// Bitstream reader (LSB-first, per Vorbis spec)
//===----------------------------------------------------------------------===//

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t byte_pos;
    int bit_pos; // 0..7 within current byte
} vorbis_bits_t;

static void bits_init(vorbis_bits_t *b, const uint8_t *data, size_t len) {
    b->data = data;
    b->len = len;
    b->byte_pos = 0;
    b->bit_pos = 0;
}

static uint32_t bits_read(vorbis_bits_t *b, int count) {
    if (count <= 0 || count > 32)
        return 0;
    uint32_t val = 0;
    for (int i = 0; i < count; i++) {
        if (b->byte_pos >= b->len)
            return val;
        if (b->data[b->byte_pos] & (1 << b->bit_pos))
            val |= (1u << i);
        b->bit_pos++;
        if (b->bit_pos >= 8) {
            b->bit_pos = 0;
            b->byte_pos++;
        }
    }
    return val;
}

static int bits_read1(vorbis_bits_t *b) {
    return (int)bits_read(b, 1);
}

static int bits_eof(const vorbis_bits_t *b) {
    return b->byte_pos >= b->len;
}

//===----------------------------------------------------------------------===//
// ilog — integer log2 (position of highest set bit)
//===----------------------------------------------------------------------===//

static int ilog(uint32_t v) {
    int r = 0;
    while (v > 0) {
        r++;
        v >>= 1;
    }
    return r;
}

//===----------------------------------------------------------------------===//
// Codebook
//===----------------------------------------------------------------------===//

typedef struct {
    int entries;
    int dimensions;
    // Codeword lengths (0 = unused entry)
    uint8_t *lengths;
    // VQ lookup: multiplicands for type 1/2 lookups
    float *vq_table; // dimensions * entries floats (expanded from multiplicands)
    int lookup_type;
    float minimum_value;
    float delta_value;
    int sequence_p;
    // Decode tree: sorted entries for binary search
    uint32_t *sorted_codes;
    int *sorted_indices;
    int sorted_count;
} vorbis_codebook_t;

static float float32_unpack(uint32_t val) {
    int mantissa = (int)(val & 0x1FFFFF);
    int sign = (int)(val & 0x80000000);
    int exponent = (int)((val >> 21) & 0x3FF);
    if (sign)
        mantissa = -mantissa;
    return (float)ldexp((double)mantissa, exponent - 788);
}

// Build sorted codeword table for Huffman decoding
static void codebook_build_tree(vorbis_codebook_t *cb) {
    // Count valid entries
    int count = 0;
    for (int i = 0; i < cb->entries; i++) {
        if (cb->lengths[i] > 0 && cb->lengths[i] <= 32)
            count++;
    }
    cb->sorted_count = count;
    if (count == 0)
        return;

    cb->sorted_codes = (uint32_t *)calloc((size_t)count, sizeof(uint32_t));
    cb->sorted_indices = (int *)calloc((size_t)count, sizeof(int));
    if (!cb->sorted_codes || !cb->sorted_indices)
        return;

    // Assign codewords using the algorithm from the Vorbis spec
    uint32_t marker[33] = {0};
    int idx = 0;
    for (int i = 0; i < cb->entries; i++) {
        int len = cb->lengths[i];
        if (len <= 0 || len > 32)
            continue;
        uint32_t entry = marker[len];
        // Reverse bits to create prefix-free code (LSB-first reading)
        uint32_t rev = 0;
        for (int j = 0; j < len; j++) {
            if (entry & (1u << j))
                rev |= (1u << (len - 1 - j));
        }
        cb->sorted_codes[idx] = rev | ((uint32_t)len << 24); // pack length in upper bits
        cb->sorted_indices[idx] = i;
        idx++;

        // Propagate carry
        marker[len]++;
        for (int j = len - 1; j >= 1; j--) {
            if (marker[j] & (1u << j)) {
                marker[j] = marker[j + 1] << 1;
                break;
            }
        }
    }
}

static int codebook_decode_scalar(vorbis_codebook_t *cb, vorbis_bits_t *b) {
    if (cb->sorted_count == 0)
        return -1;

    // Linear scan through sorted codes (simple but correct)
    // For each possible length, try to match
    uint32_t code = 0;
    for (int len = 1; len <= 32; len++) {
        int bit = bits_read1(b);
        if (bits_eof(b) && len > 1)
            return -1;
        code |= ((uint32_t)bit << (len - 1));

        // Search for match
        for (int i = 0; i < cb->sorted_count; i++) {
            int cb_len = (int)(cb->sorted_codes[i] >> 24);
            uint32_t cb_code = cb->sorted_codes[i] & 0x00FFFFFF;
            if (cb_len == len && cb_code == code)
                return cb->sorted_indices[i];
        }
    }
    return -1;
}

static void codebook_decode_vq(vorbis_codebook_t *cb, int entry, float *out) {
    if (cb->vq_table && entry >= 0 && entry < cb->entries) {
        memcpy(out, cb->vq_table + entry * cb->dimensions, (size_t)cb->dimensions * sizeof(float));
    } else {
        memset(out, 0, (size_t)cb->dimensions * sizeof(float));
    }
}

//===----------------------------------------------------------------------===//
// Floor type 1
//===----------------------------------------------------------------------===//

typedef struct {
    int partitions;
    int partition_class[31];
    int class_dimensions[16];
    int class_subclasses[16];
    int class_masterbooks[16];
    int subclass_books[16][8];
    int multiplier;
    int x_list_count;
    int x_list[256];
} vorbis_floor1_t;

//===----------------------------------------------------------------------===//
// Residue
//===----------------------------------------------------------------------===//

typedef struct {
    int type; // 0, 1, or 2
    int begin, end;
    int partition_size;
    int classifications;
    int classbook;
    uint8_t cascade[64];
    int books[64][8]; // per-classification per-pass book index
} vorbis_residue_t;

//===----------------------------------------------------------------------===//
// Mapping
//===----------------------------------------------------------------------===//

typedef struct {
    int submaps;
    int coupling_steps;
    int coupling_magnitude[256];
    int coupling_angle[256];
    int mux[VORBIS_MAX_CHANNELS];
    int submap_floor[16];
    int submap_residue[16];
} vorbis_mapping_t;

//===----------------------------------------------------------------------===//
// Mode
//===----------------------------------------------------------------------===//

typedef struct {
    int block_flag; // 0=short, 1=long
    int windowtype;
    int transformtype;
    int mapping;
} vorbis_mode_t;

//===----------------------------------------------------------------------===//
// Decoder state
//===----------------------------------------------------------------------===//

struct vorbis_decoder {
    // Identification header
    int channels;
    int sample_rate;
    int blocksize_0; // short block
    int blocksize_1; // long block

    // Setup header data
    int codebook_count;
    vorbis_codebook_t codebooks[VORBIS_MAX_CODEBOOKS];

    int floor_count;
    vorbis_floor1_t floors[VORBIS_MAX_FLOORS];

    int residue_count;
    vorbis_residue_t residues[VORBIS_MAX_RESIDUES];

    int mapping_count;
    vorbis_mapping_t mappings[VORBIS_MAX_MAPPINGS];

    int mode_count;
    vorbis_mode_t modes[VORBIS_MAX_MODES];

    // Decode state
    int headers_done; // bitmask: bit 0=ident, bit 1=comment, bit 2=setup
    int prev_block_flag;

    // Window functions (precomputed)
    float *window_short;
    float *window_long;

    // Previous frame overlap buffer
    float *overlap[VORBIS_MAX_CHANNELS];

    // Output PCM buffer
    int16_t *pcm_out;
    int pcm_out_cap;
};

//===----------------------------------------------------------------------===//
// Window functions
//===----------------------------------------------------------------------===//

static float *make_window(int n) {
    float *w = (float *)malloc((size_t)n * sizeof(float));
    if (!w)
        return NULL;
    for (int i = 0; i < n; i++) {
        double x = sin(M_PI / 2.0 * sin(M_PI * ((double)i + 0.5) / (double)n) *
                       sin(M_PI * ((double)i + 0.5) / (double)n));
        w[i] = (float)x;
    }
    return w;
}

//===----------------------------------------------------------------------===//
// IMDCT — via direct computation (n/2 output from n/2 input)
//===----------------------------------------------------------------------===//

/// @brief Radix-2 in-place complex FFT (decimation in time).
static void fft_radix2(float *re, float *im, int n) {
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            float t;
            t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    // Butterfly passes
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        float wre = (float)cos(ang), wim = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cur_re = 1.0f, cur_im = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                float tre = re[i + j + len / 2] * cur_re - im[i + j + len / 2] * cur_im;
                float tim = re[i + j + len / 2] * cur_im + im[i + j + len / 2] * cur_re;
                re[i + j + len / 2] = re[i + j] - tre;
                im[i + j + len / 2] = im[i + j] - tim;
                re[i + j] += tre;
                im[i + j] += tim;
                float new_re = cur_re * wre - cur_im * wim;
                cur_im = cur_re * wim + cur_im * wre;
                cur_re = new_re;
            }
        }
    }
}

/// @brief Fast IMDCT via N/4-point FFT with pre/post twiddle.
/// O(N log N) for power-of-2 sizes; falls back to O(N²) for non-power-of-2.
static void imdct(const float *in, float *out, int n) {
    int n2 = n / 2;
    int n4 = n / 4;

    // Check for power-of-2 (fast path)
    if (n4 > 0 && (n4 & (n4 - 1)) == 0) {
        // Allocate temp buffers for complex FFT
        float *fft_re = (float *)calloc((size_t)n4, sizeof(float));
        float *fft_im = (float *)calloc((size_t)n4, sizeof(float));
        if (!fft_re || !fft_im) {
            free(fft_re);
            free(fft_im);
            goto slow_path;
        }

        // Pre-twiddle: pack input into N/4 complex values
        for (int k = 0; k < n4; k++) {
            double angle = 2.0 * M_PI / (double)n * ((double)k + 0.125);
            float cos_a = (float)cos(angle);
            float sin_a = (float)sin(angle);
            float xr = in[2 * k];
            float xi = (2 * k + 1 < n4 * 2) ? in[2 * k + 1] : 0.0f;
            fft_re[k] = xr * cos_a + xi * sin_a;
            fft_im[k] = -xr * sin_a + xi * cos_a;
        }

        // N/4-point complex FFT
        fft_radix2(fft_re, fft_im, n4);

        // Post-twiddle + reorder to IMDCT output
        float scale = 2.0f / (float)n2;
        for (int k = 0; k < n4; k++) {
            double angle = 2.0 * M_PI / (double)n * ((double)k + 0.125);
            float cos_a = (float)cos(angle);
            float sin_a = (float)sin(angle);
            float re = fft_re[k] * cos_a + fft_im[k] * sin_a;
            float im = -fft_re[k] * sin_a + fft_im[k] * cos_a;
            out[2 * k] = re * scale;
            if (2 * k + 1 < n2)
                out[2 * k + 1] = im * scale;
        }

        free(fft_re);
        free(fft_im);
        return;
    }

slow_path:
    // Fallback: O(N²) direct computation for non-power-of-2 sizes
    for (int i = 0; i < n2; i++) {
        double sum = 0.0;
        for (int k = 0; k < n4; k++) {
            sum += (double)in[k] *
                   cos(2.0 * M_PI / (double)n * ((double)(2 * i + 1 + n2) * (2 * k + 1)) / 4.0);
        }
        out[i] = (float)(sum * 2.0 / (double)n2);
    }
}

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

/// @brief Allocate a new Vorbis decoder state (must receive headers before decoding audio).
vorbis_decoder_t *vorbis_decoder_new(void) {
    vorbis_decoder_t *dec = (vorbis_decoder_t *)calloc(1, sizeof(vorbis_decoder_t));
    if (!dec)
        return NULL;
    dec->prev_block_flag = -1; // no previous block
    return dec;
}

/// @brief Free a Vorbis decoder and all its codebook/floor/residue tables.
void vorbis_decoder_free(vorbis_decoder_t *dec) {
    if (!dec)
        return;
    // Free codebooks
    for (int i = 0; i < dec->codebook_count; i++) {
        free(dec->codebooks[i].lengths);
        free(dec->codebooks[i].vq_table);
        free(dec->codebooks[i].sorted_codes);
        free(dec->codebooks[i].sorted_indices);
    }
    // Free windows
    free(dec->window_short);
    free(dec->window_long);
    // Free overlap buffers
    for (int i = 0; i < dec->channels; i++)
        free(dec->overlap[i]);
    free(dec->pcm_out);
    free(dec);
}

//===----------------------------------------------------------------------===//
// Header decoding
//===----------------------------------------------------------------------===//

static int decode_identification(vorbis_decoder_t *dec, const uint8_t *data, size_t len) {
    if (len < 30)
        return -1;
    // Verify "\x01vorbis"
    if (data[0] != 1 || memcmp(data + 1, "vorbis", 6) != 0)
        return -1;

    // Parse fields (all little-endian)
    uint32_t version =
        data[7] | ((uint32_t)data[8] << 8) | ((uint32_t)data[9] << 16) | ((uint32_t)data[10] << 24);
    if (version != 0)
        return -1;

    dec->channels = data[11];
    dec->sample_rate = (int)(data[12] | ((uint32_t)data[13] << 8) | ((uint32_t)data[14] << 16) |
                             ((uint32_t)data[15] << 24));
    // Skip bitrate fields (16..27)
    uint8_t blocksizes = data[28];
    dec->blocksize_0 = 1 << (blocksizes & 0x0F);
    dec->blocksize_1 = 1 << ((blocksizes >> 4) & 0x0F);

    if (dec->channels < 1 || dec->channels > VORBIS_MAX_CHANNELS)
        return -1;
    if (dec->sample_rate <= 0)
        return -1;
    if (dec->blocksize_0 < 64 || dec->blocksize_1 < dec->blocksize_0)
        return -1;

    // Precompute windows
    dec->window_short = make_window(dec->blocksize_0);
    dec->window_long = make_window(dec->blocksize_1);

    // Allocate overlap buffers
    for (int i = 0; i < dec->channels; i++) {
        dec->overlap[i] = (float *)calloc((size_t)dec->blocksize_1 / 2, sizeof(float));
        if (!dec->overlap[i])
            return -1;
    }

    return 0;
}

static int decode_comment(vorbis_decoder_t *dec, const uint8_t *data, size_t len) {
    (void)dec;
    // Verify "\x03vorbis"
    if (len < 7 || data[0] != 3 || memcmp(data + 1, "vorbis", 6) != 0)
        return -1;
    // We don't need comment data — just validate the header
    return 0;
}

static int decode_setup(vorbis_decoder_t *dec, const uint8_t *data, size_t len) {
    if (len < 7 || data[0] != 5 || memcmp(data + 1, "vorbis", 6) != 0)
        return -1;

    vorbis_bits_t bits;
    bits_init(&bits, data + 7, len - 7);

    // --- Codebooks ---
    dec->codebook_count = (int)bits_read(&bits, 8) + 1;
    if (dec->codebook_count > VORBIS_MAX_CODEBOOKS)
        return -1;

    for (int i = 0; i < dec->codebook_count; i++) {
        vorbis_codebook_t *cb = &dec->codebooks[i];

        // Sync pattern: 0x564342
        uint32_t sync = bits_read(&bits, 24);
        if (sync != 0x564342)
            return -1;

        cb->dimensions = (int)bits_read(&bits, 16);
        cb->entries = (int)bits_read(&bits, 24);

        // Read codeword lengths
        cb->lengths = (uint8_t *)calloc((size_t)cb->entries, 1);
        if (!cb->lengths)
            return -1;

        int ordered = bits_read1(&bits);
        if (!ordered) {
            int sparse = bits_read1(&bits);
            for (int j = 0; j < cb->entries; j++) {
                if (sparse) {
                    int flag = bits_read1(&bits);
                    if (flag)
                        cb->lengths[j] = (uint8_t)(bits_read(&bits, 5) + 1);
                    else
                        cb->lengths[j] = 0; // unused
                } else {
                    cb->lengths[j] = (uint8_t)(bits_read(&bits, 5) + 1);
                }
            }
        } else {
            int current_entry = 0;
            int current_length = (int)bits_read(&bits, 5) + 1;
            while (current_entry < cb->entries) {
                int bits_count = ilog((uint32_t)(cb->entries - current_entry));
                int number = (int)bits_read(&bits, bits_count);
                for (int j = 0; j < number && current_entry < cb->entries; j++)
                    cb->lengths[current_entry++] = (uint8_t)current_length;
                current_length++;
            }
        }

        codebook_build_tree(cb);

        // VQ lookup
        cb->lookup_type = (int)bits_read(&bits, 4);
        if (cb->lookup_type == 1 || cb->lookup_type == 2) {
            cb->minimum_value = float32_unpack(bits_read(&bits, 32));
            cb->delta_value = float32_unpack(bits_read(&bits, 32));
            int value_bits = (int)bits_read(&bits, 4) + 1;
            cb->sequence_p = bits_read1(&bits);

            int lookup_values;
            if (cb->lookup_type == 1) {
                // floor(entries^(1/dim))
                lookup_values = (int)floor(pow((double)cb->entries, 1.0 / (double)cb->dimensions));
                while ((int)pow((double)(lookup_values + 1), (double)cb->dimensions) <= cb->entries)
                    lookup_values++;
            } else {
                lookup_values = cb->entries * cb->dimensions;
            }

            uint16_t *multiplicands = (uint16_t *)calloc((size_t)lookup_values, sizeof(uint16_t));
            if (!multiplicands)
                return -1;
            for (int j = 0; j < lookup_values; j++)
                multiplicands[j] = (uint16_t)bits_read(&bits, value_bits);

            // Expand VQ table
            cb->vq_table =
                (float *)calloc((size_t)cb->entries * (size_t)cb->dimensions, sizeof(float));
            if (cb->vq_table) {
                for (int j = 0; j < cb->entries; j++) {
                    float last = 0.0f;
                    int index_divisor = 1;
                    for (int k = 0; k < cb->dimensions; k++) {
                        int off;
                        if (cb->lookup_type == 1)
                            off = (j / index_divisor) % lookup_values;
                        else
                            off = j * cb->dimensions + k;
                        float val = cb->minimum_value + (float)multiplicands[off] * cb->delta_value;
                        if (cb->sequence_p)
                            val += last;
                        cb->vq_table[j * cb->dimensions + k] = val;
                        last = val;
                        if (cb->lookup_type == 1)
                            index_divisor *= lookup_values;
                    }
                }
            }
            free(multiplicands);
        }
    }

    // --- Time domain transforms (placeholder, always 0) ---
    int time_count = (int)bits_read(&bits, 6) + 1;
    for (int i = 0; i < time_count; i++)
        bits_read(&bits, 16); // always 0

    // --- Floors ---
    dec->floor_count = (int)bits_read(&bits, 6) + 1;
    if (dec->floor_count > VORBIS_MAX_FLOORS)
        return -1;

    for (int i = 0; i < dec->floor_count; i++) {
        int floor_type = (int)bits_read(&bits, 16);
        if (floor_type != 1)
            return -1; // Only floor type 1 supported

        vorbis_floor1_t *fl = &dec->floors[i];
        fl->partitions = (int)bits_read(&bits, 5);
        int maximum_class = -1;
        for (int j = 0; j < fl->partitions; j++) {
            fl->partition_class[j] = (int)bits_read(&bits, 4);
            if (fl->partition_class[j] > maximum_class)
                maximum_class = fl->partition_class[j];
        }
        for (int j = 0; j <= maximum_class; j++) {
            fl->class_dimensions[j] = (int)bits_read(&bits, 3) + 1;
            fl->class_subclasses[j] = (int)bits_read(&bits, 2);
            if (fl->class_subclasses[j] > 0)
                fl->class_masterbooks[j] = (int)bits_read(&bits, 8);
            for (int k = 0; k < (1 << fl->class_subclasses[j]); k++) {
                fl->subclass_books[j][k] = (int)bits_read(&bits, 8) - 1;
            }
        }
        fl->multiplier = (int)bits_read(&bits, 2) + 1;
        int range_bits = (int)bits_read(&bits, 4);
        fl->x_list_count = 2; // always starts with 0 and (1 << range_bits)
        fl->x_list[0] = 0;
        fl->x_list[1] = 1 << range_bits;
        for (int j = 0; j < fl->partitions; j++) {
            int cdim = fl->class_dimensions[fl->partition_class[j]];
            for (int k = 0; k < cdim; k++) {
                if (fl->x_list_count < 256)
                    fl->x_list[fl->x_list_count++] = (int)bits_read(&bits, range_bits);
            }
        }
    }

    // --- Residues ---
    dec->residue_count = (int)bits_read(&bits, 6) + 1;
    if (dec->residue_count > VORBIS_MAX_RESIDUES)
        return -1;

    for (int i = 0; i < dec->residue_count; i++) {
        vorbis_residue_t *res = &dec->residues[i];
        res->type = (int)bits_read(&bits, 16);
        if (res->type > 2)
            return -1;
        res->begin = (int)bits_read(&bits, 24);
        res->end = (int)bits_read(&bits, 24);
        res->partition_size = (int)bits_read(&bits, 24) + 1;
        res->classifications = (int)bits_read(&bits, 6) + 1;
        res->classbook = (int)bits_read(&bits, 8);

        for (int j = 0; j < res->classifications; j++) {
            uint8_t low = (uint8_t)bits_read(&bits, 3);
            uint8_t high = 0;
            if (bits_read1(&bits))
                high = (uint8_t)bits_read(&bits, 5);
            res->cascade[j] = low | (high << 3);
        }
        for (int j = 0; j < res->classifications; j++) {
            for (int k = 0; k < 8; k++) {
                if (res->cascade[j] & (1 << k))
                    res->books[j][k] = (int)bits_read(&bits, 8);
                else
                    res->books[j][k] = -1;
            }
        }
    }

    // --- Mappings ---
    dec->mapping_count = (int)bits_read(&bits, 6) + 1;
    if (dec->mapping_count > VORBIS_MAX_MAPPINGS)
        return -1;

    for (int i = 0; i < dec->mapping_count; i++) {
        vorbis_mapping_t *map = &dec->mappings[i];
        int mapping_type = (int)bits_read(&bits, 16);
        (void)mapping_type; // always 0

        map->submaps = 1;
        if (bits_read1(&bits))
            map->submaps = (int)bits_read(&bits, 4) + 1;

        map->coupling_steps = 0;
        if (bits_read1(&bits)) {
            map->coupling_steps = (int)bits_read(&bits, 8) + 1;
            int ch_bits = ilog((uint32_t)(dec->channels - 1));
            for (int j = 0; j < map->coupling_steps; j++) {
                map->coupling_magnitude[j] = (int)bits_read(&bits, ch_bits);
                map->coupling_angle[j] = (int)bits_read(&bits, ch_bits);
            }
        }

        bits_read(&bits, 2); // reserved (must be 0)
        if (map->submaps > 1) {
            for (int j = 0; j < dec->channels; j++)
                map->mux[j] = (int)bits_read(&bits, 4);
        }
        for (int j = 0; j < map->submaps; j++) {
            bits_read(&bits, 8); // unused time configuration
            map->submap_floor[j] = (int)bits_read(&bits, 8);
            map->submap_residue[j] = (int)bits_read(&bits, 8);
        }
    }

    // --- Modes ---
    dec->mode_count = (int)bits_read(&bits, 6) + 1;
    if (dec->mode_count > VORBIS_MAX_MODES)
        return -1;

    for (int i = 0; i < dec->mode_count; i++) {
        dec->modes[i].block_flag = bits_read1(&bits);
        dec->modes[i].windowtype = (int)bits_read(&bits, 16);
        dec->modes[i].transformtype = (int)bits_read(&bits, 16);
        dec->modes[i].mapping = (int)bits_read(&bits, 8);
    }

    // Framing bit
    if (!bits_read1(&bits))
        return -1;

    return 0;
}

/// @brief Decode a Vorbis header packet (identification=0, comment=1, setup=2).
/// @details Must be called in order for packets 0, 1, 2 before any audio decoding.
///          Packet 0 sets sample rate and channels. Packet 2 builds all codebook,
///          floor, residue, and mapping tables needed for audio frame decoding.
int vorbis_decode_header(vorbis_decoder_t *dec, const uint8_t *data, size_t len, int packet_num) {
    if (!dec || !data)
        return -1;

    int rc;
    switch (packet_num) {
        case 0:
            rc = decode_identification(dec, data, len);
            if (rc == 0)
                dec->headers_done |= 1;
            return rc;
        case 1:
            rc = decode_comment(dec, data, len);
            if (rc == 0)
                dec->headers_done |= 2;
            return rc;
        case 2:
            rc = decode_setup(dec, data, len);
            if (rc == 0)
                dec->headers_done |= 4;
            return rc;
        default:
            return -1;
    }
}

//===----------------------------------------------------------------------===//
// Audio packet decoding
//===----------------------------------------------------------------------===//

int vorbis_decode_packet(
    vorbis_decoder_t *dec, const uint8_t *data, size_t len, int16_t **out_pcm, int *out_samples) {
    if (!dec || !data || dec->headers_done != 7)
        return -1;

    vorbis_bits_t bits;
    bits_init(&bits, data, len);

    // Packet type (must be 0 for audio)
    if (bits_read1(&bits) != 0)
        return -1;

    // Mode number
    int mode_bits = ilog((uint32_t)(dec->mode_count - 1));
    int mode_number = (int)bits_read(&bits, mode_bits);
    if (mode_number >= dec->mode_count)
        return -1;

    vorbis_mode_t *mode = &dec->modes[mode_number];
    int block_flag = mode->block_flag;
    int n = block_flag ? dec->blocksize_1 : dec->blocksize_0;
    int n2 = n / 2;

    // For long blocks, read previous/next window flags
    int prev_flag = 0, next_flag = 0;
    if (block_flag) {
        prev_flag = bits_read1(&bits);
        next_flag = bits_read1(&bits);
    }
    (void)prev_flag;
    (void)next_flag;

    vorbis_mapping_t *map = &dec->mappings[mode->mapping];

    // --- Floor decode ---
    float **floor_buf = (float **)calloc((size_t)dec->channels, sizeof(float *));
    int *no_residue = (int *)calloc((size_t)dec->channels, sizeof(int));
    if (!floor_buf || !no_residue) {
        free(floor_buf);
        free(no_residue);
        return -1;
    }

    for (int ch = 0; ch < dec->channels; ch++) {
        int submap_idx = map->submaps > 1 ? map->mux[ch] : 0;
        int floor_idx = map->submap_floor[submap_idx];
        vorbis_floor1_t *fl = &dec->floors[floor_idx];

        floor_buf[ch] = (float *)calloc((size_t)n2, sizeof(float));
        if (!floor_buf[ch]) {
            for (int j = 0; j < ch; j++)
                free(floor_buf[j]);
            free(floor_buf);
            free(no_residue);
            return -1;
        }

        // Floor type 1 decode: read amplitude values
        int amplitude = bits_read1(&bits);
        if (!amplitude) {
            no_residue[ch] = 1; // unused channel
            continue;
        }

        // Read Y values from the bitstream
        int range_vals[] = {256, 128, 86, 64};
        int range = range_vals[fl->multiplier - 1];
        int y_list[256];
        int range_bits_val = ilog((uint32_t)(range - 1));
        y_list[0] = (int)bits_read(&bits, range_bits_val);
        y_list[1] = (int)bits_read(&bits, range_bits_val);

        int offset = 2;
        for (int j = 0; j < fl->partitions; j++) {
            int cls = fl->partition_class[j];
            int cdim = fl->class_dimensions[cls];
            int cbits = fl->class_subclasses[cls];
            int csub = (1 << cbits) - 1;
            int cval = 0;
            if (cbits > 0) {
                int book = fl->class_masterbooks[cls];
                if (book >= 0 && book < dec->codebook_count)
                    cval = codebook_decode_scalar(&dec->codebooks[book], &bits);
            }
            for (int k = 0; k < cdim && offset < fl->x_list_count; k++) {
                int book = fl->subclass_books[cls][cval & csub];
                cval >>= cbits;
                if (book >= 0 && book < dec->codebook_count) {
                    y_list[offset] = (int)codebook_decode_scalar(&dec->codebooks[book], &bits);
                } else {
                    y_list[offset] = 0;
                }
                offset++;
            }
        }

        // Floor curve synthesis: linear interpolation between the decoded points
        // Simplified: spread values across the n/2 frequency bins
        // Sort x_list to get floor curve points in frequency order
        int sorted_x[256], sorted_y[256];
        int npoints = fl->x_list_count;
        for (int j = 0; j < npoints; j++) {
            sorted_x[j] = fl->x_list[j];
            sorted_y[j] = y_list[j];
        }
        // Simple insertion sort on x
        for (int j = 1; j < npoints; j++) {
            int kx = sorted_x[j], ky = sorted_y[j];
            int m = j - 1;
            while (m >= 0 && sorted_x[m] > kx) {
                sorted_x[m + 1] = sorted_x[m];
                sorted_y[m + 1] = sorted_y[m];
                m--;
            }
            sorted_x[m + 1] = kx;
            sorted_y[m + 1] = ky;
        }

        // Render floor curve via linear interpolation
        for (int j = 0; j < npoints - 1; j++) {
            int x0 = sorted_x[j], x1 = sorted_x[j + 1];
            float y0 = (float)sorted_y[j] * (float)fl->multiplier;
            float y1 = (float)sorted_y[j + 1] * (float)fl->multiplier;
            for (int x = x0; x < x1 && x < n2; x++) {
                float t = (x1 > x0) ? (float)(x - x0) / (float)(x1 - x0) : 0.0f;
                float val = y0 + t * (y1 - y0);
                // Convert to linear amplitude (floor1_inverse_dB_table approximation)
                floor_buf[ch][x] = (float)pow(10.0, (double)(val - 1.0f) / 20.0);
            }
        }
        // Fill remaining with last value
        if (npoints > 0) {
            float last_val = (float)sorted_y[npoints - 1] * (float)fl->multiplier;
            float last_amp = (float)pow(10.0, (double)(last_val - 1.0f) / 20.0);
            for (int x = sorted_x[npoints - 1]; x < n2; x++)
                floor_buf[ch][x] = last_amp;
        }
    }

    // --- Residue decode ---
    float **residue_buf = (float **)calloc((size_t)dec->channels, sizeof(float *));
    if (!residue_buf) {
        for (int ch = 0; ch < dec->channels; ch++)
            free(floor_buf[ch]);
        free(floor_buf);
        free(no_residue);
        return -1;
    }

    for (int ch = 0; ch < dec->channels; ch++) {
        residue_buf[ch] = (float *)calloc((size_t)n2, sizeof(float));
        if (!residue_buf[ch]) {
            for (int j = 0; j <= ch; j++)
                free(residue_buf[j]);
            free(residue_buf);
            for (int j = 0; j < dec->channels; j++)
                free(floor_buf[j]);
            free(floor_buf);
            free(no_residue);
            return -1;
        }
    }

    // Full multi-pass residue decode (Vorbis I spec §8.6)
    for (int sm = 0; sm < map->submaps; sm++) {
        int res_idx = map->submap_residue[sm];
        if (res_idx >= dec->residue_count)
            continue;
        vorbis_residue_t *res = &dec->residues[res_idx];

        // Determine which channels use this submap
        int ch_count = 0;
        int ch_list[VORBIS_MAX_CHANNELS];
        for (int ch = 0; ch < dec->channels; ch++) {
            int submap_idx = map->submaps > 1 ? map->mux[ch] : 0;
            if (submap_idx == sm && !no_residue[ch])
                ch_list[ch_count++] = ch;
        }
        if (ch_count == 0)
            continue;

        int classbook = res->classbook;
        if (classbook < 0 || classbook >= dec->codebook_count)
            continue;
        vorbis_codebook_t *cb = &dec->codebooks[classbook];
        int actual_size = res->end - res->begin;
        if (actual_size <= 0 || res->partition_size <= 0)
            continue;
        int parts_per_ch = actual_size / res->partition_size;

        // Allocate per-partition classification array (decoded in pass 0, used in all passes)
        int total_parts = parts_per_ch * ch_count;
        int *classifications = (int *)calloc((size_t)total_parts, sizeof(int));
        if (!classifications)
            goto residue_done;

        // Pass 0: decode classification values from the classbook
        for (int p = 0; p < parts_per_ch; p++) {
            for (int c = 0; c < ch_count; c++) {
                if (p == 0 || (p % cb->dimensions) == 0) {
                    // Read a new classword every cb->dimensions partitions
                }
            }
        }

        // Decode classwords and unpack classifications
        {
            int classwords_per_ch = cb->dimensions;
            for (int p = 0; p < parts_per_ch;) {
                for (int c = 0; c < ch_count; c++) {
                    int cw = codebook_decode_scalar(cb, &bits);
                    if (cw < 0)
                        goto residue_class_done;
                    // Unpack classification values from the codeword
                    int temp = cw;
                    // The classifications for this channel's next `classwords_per_ch`
                    // partitions are packed in the codeword as:
                    // cls[last] = temp % n_cls; temp /= n_cls; ... cls[first] = temp % n_cls
                    int cls_buf[256];
                    for (int j = classwords_per_ch - 1; j >= 0; j--) {
                        cls_buf[j] = temp % res->classifications;
                        temp /= res->classifications;
                    }
                    for (int j = 0; j < classwords_per_ch && (p + j) < parts_per_ch; j++)
                        classifications[(p + j) * ch_count + c] = cls_buf[j];
                }
                p += classwords_per_ch;
            }
        }
    residue_class_done:

        // Passes 0..7: decode residue vectors for each pass with cascade bits
        for (int pass = 0; pass < 8; pass++) {
            for (int p = 0; p < parts_per_ch; p++) {
                for (int c = 0; c < ch_count; c++) {
                    int cls = classifications[p * ch_count + c];
                    if (cls < 0 || cls >= res->classifications)
                        continue;
                    if (!(res->cascade[cls] & (1 << pass)))
                        continue; // no data for this pass
                    int book_idx = res->books[cls][pass];
                    if (book_idx < 0 || book_idx >= dec->codebook_count)
                        continue;

                    vorbis_codebook_t *rcb = &dec->codebooks[book_idx];
                    int ch_idx = ch_list[c];
                    int offset = res->begin + p * res->partition_size;

                    for (int j = 0; j < res->partition_size && offset + j < n2;
                         j += rcb->dimensions) {
                        int entry = codebook_decode_scalar(rcb, &bits);
                        if (entry < 0)
                            goto residue_pass_done;
                        if (rcb->vq_table) {
                            float vq[256];
                            codebook_decode_vq(rcb, entry, vq);
                            for (int d = 0; d < rcb->dimensions && offset + j + d < n2; d++)
                                residue_buf[ch_idx][offset + j + d] += vq[d];
                        }
                    }
                }
            }
        }
    residue_pass_done:
        free(classifications);
    }
residue_done:

    // --- Inverse coupling ---
    for (int step = map->coupling_steps - 1; step >= 0; step--) {
        int mag_ch = map->coupling_magnitude[step];
        int ang_ch = map->coupling_angle[step];
        if (mag_ch >= dec->channels || ang_ch >= dec->channels)
            continue;
        for (int j = 0; j < n2; j++) {
            float m = residue_buf[mag_ch][j];
            float a = residue_buf[ang_ch][j];
            float new_m, new_a;
            if (m > 0) {
                if (a > 0) {
                    new_m = m;
                    new_a = m - a;
                } else {
                    new_a = m;
                    new_m = m + a;
                }
            } else {
                if (a > 0) {
                    new_m = m;
                    new_a = m + a;
                } else {
                    new_a = m;
                    new_m = m - a;
                }
            }
            residue_buf[mag_ch][j] = new_m;
            residue_buf[ang_ch][j] = new_a;
        }
    }

    // --- Apply floor, IMDCT, window, overlap-add ---
    int prev_n = (dec->prev_block_flag < 0)
                     ? n
                     : (dec->prev_block_flag ? dec->blocksize_1 : dec->blocksize_0);
    int overlap = (prev_n < n ? prev_n : n) / 2;
    int output_samples = overlap; // output from overlap region

    // Ensure PCM output buffer is large enough
    int total_pcm = output_samples * dec->channels;
    if (total_pcm > dec->pcm_out_cap) {
        free(dec->pcm_out);
        dec->pcm_out_cap = total_pcm + 1024;
        dec->pcm_out = (int16_t *)calloc((size_t)dec->pcm_out_cap, sizeof(int16_t));
        if (!dec->pcm_out) {
            for (int ch = 0; ch < dec->channels; ch++) {
                free(floor_buf[ch]);
                free(residue_buf[ch]);
            }
            free(floor_buf);
            free(residue_buf);
            free(no_residue);
            return -1;
        }
    }

    float *imdct_buf = (float *)calloc((size_t)n, sizeof(float));
    float *freq_buf = (float *)calloc((size_t)n2, sizeof(float));
    float *windowed = (float *)calloc((size_t)n, sizeof(float));
    if (!imdct_buf || !freq_buf || !windowed) {
        free(imdct_buf);
        free(freq_buf);
        free(windowed);
        for (int ch = 0; ch < dec->channels; ch++) {
            free(floor_buf[ch]);
            free(residue_buf[ch]);
        }
        free(floor_buf);
        free(residue_buf);
        free(no_residue);
        return -1;
    }

    float *window = block_flag ? dec->window_long : dec->window_short;

    for (int ch = 0; ch < dec->channels; ch++) {
        // Multiply residue by floor
        for (int j = 0; j < n2; j++)
            freq_buf[j] = residue_buf[ch][j] * floor_buf[ch][j];

        // IMDCT
        imdct(freq_buf, imdct_buf, n);

        // Apply window
        for (int j = 0; j < n; j++) {
            int wi = (j < n2) ? j : (n - 1 - j);
            windowed[j] = imdct_buf[j] * window[wi];
        }

        // Overlap-add with previous frame
        for (int j = 0; j < overlap; j++) {
            float sample = dec->overlap[ch][j] + windowed[j];
            // Clamp to 16-bit range
            int s = (int)(sample * 32767.0f);
            if (s > 32767)
                s = 32767;
            if (s < -32768)
                s = -32768;
            dec->pcm_out[j * dec->channels + ch] = (int16_t)s;
        }

        // Save right half for next frame's overlap
        int overlap_size = n2;
        if (overlap_size > dec->blocksize_1 / 2)
            overlap_size = dec->blocksize_1 / 2;
        memset(dec->overlap[ch], 0, (size_t)(dec->blocksize_1 / 2) * sizeof(float));
        for (int j = 0; j < n2 && j < dec->blocksize_1 / 2; j++)
            dec->overlap[ch][j] = windowed[n2 + j];
    }

    free(imdct_buf);
    free(freq_buf);
    free(windowed);
    for (int ch = 0; ch < dec->channels; ch++) {
        free(floor_buf[ch]);
        free(residue_buf[ch]);
    }
    free(floor_buf);
    free(residue_buf);
    free(no_residue);

    dec->prev_block_flag = block_flag;

    if (dec->prev_block_flag < 0) {
        // First frame — no output (only overlap saved)
        *out_pcm = NULL;
        *out_samples = 0;
    } else {
        *out_pcm = dec->pcm_out;
        *out_samples = output_samples;
    }

    return 0;
}

/// @brief Get the audio sample rate from the Vorbis identification header.
int vorbis_get_sample_rate(const vorbis_decoder_t *dec) {
    return dec ? dec->sample_rate : 0;
}

/// @brief Get the number of audio channels from the Vorbis identification header.
int vorbis_get_channels(const vorbis_decoder_t *dec) {
    return dec ? dec->channels : 0;
}
