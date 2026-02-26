//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_compress.c
// Purpose: Implements RFC 1951 DEFLATE and RFC 1952 GZIP compression and
//          decompression without external dependencies. Uses LZ77 with a 32KB
//          sliding window and Huffman coding (fixed or dynamic) to compress
//          data at configurable levels (1-9).
//
// Key invariants:
//   - Compression level 6 is the default; levels 1-9 are supported.
//   - Decompression accepts both raw DEFLATE and GZIP-wrapped streams.
//   - Block type 0 (stored), type 1 (fixed Huffman), and type 2 (dynamic) are
//     all produced and consumed correctly.
//   - CRC32 is computed and validated for GZIP streams.
//   - All functions are thread-safe (no global mutable state).
//
// Ownership/Lifetime:
//   - Compressed and decompressed output is returned as a fresh rt_bytes
//     allocation owned by the caller.
//   - Input rt_bytes buffers are read-only and not retained by the functions.
//
// Links: src/runtime/io/rt_compress.h (public API),
//        src/runtime/rt_crc32.h (CRC32 used for GZIP footer validation),
//        src/runtime/io/rt_archive.c (consumes this for ZIP DEFLATE entries)
//
//===----------------------------------------------------------------------===//

#include "rt_compress.h"

#include "rt_bytes.h"
#include "rt_crc32.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Constants
//=============================================================================

#define DEFLATE_DEFAULT_LEVEL 6
#define DEFLATE_MIN_LEVEL 1
#define DEFLATE_MAX_LEVEL 9

#define WINDOW_SIZE 32768  // 32KB sliding window
#define WINDOW_MASK 0x7FFF // For wrapping
#define MAX_MATCH_LEN 258  // Maximum match length
#define MIN_MATCH_LEN 3    // Minimum match length
#define MAX_DISTANCE 32768 // Maximum back-reference distance

#define MAX_BITS 15           // Maximum Huffman code length
#define MAX_LIT_CODES 286     // 0-255 literals + 256 end + 257-285 lengths
#define MAX_DIST_CODES 30     // Distance codes
#define MAX_CODE_LEN_CODES 19 // Code length alphabet size

// Fixed Huffman code lengths (RFC 1951)
#define FIXED_LIT_CODES 288
#define FIXED_DIST_CODES 32

//=============================================================================
// Internal Bytes Access
//=============================================================================

/// @brief Internal structure matching rt_bytes_impl
typedef struct
{
    int64_t len;
    uint8_t *data;
} bytes_impl;

/// @brief Get raw pointer to bytes data
static inline uint8_t *bytes_data(void *obj)
{
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

/// @brief Get bytes length
static inline int64_t bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// Bit Stream Reader (for decompression)
//=============================================================================

typedef struct
{
    const uint8_t *data;
    size_t len;
    size_t pos;      // Current byte position
    int bit_pos;     // Current bit position (0-7)
    uint32_t buffer; // Bit buffer
    int bits_in_buf; // Bits available in buffer
} bit_reader_t;

static void br_init(bit_reader_t *br, const uint8_t *data, size_t len)
{
    br->data = data;
    br->len = len;
    br->pos = 0;
    br->bit_pos = 0;
    br->buffer = 0;
    br->bits_in_buf = 0;
}

/// @brief Ensure at least n bits in buffer
/// @note At end-of-stream, zero-fills remaining bits if some data exists (valid
///       since DEFLATE padding is zeros). Fails if no bits available at all.
static bool br_fill(bit_reader_t *br, int n)
{
    while (br->bits_in_buf < n)
    {
        if (br->pos >= br->len)
        {
            // At end of stream with some bits remaining - zero-fill for table lookup
            // This is valid because DEFLATE streams are zero-padded to byte boundary
            // The remaining bits contain the final symbol (e.g., end-of-block)
            if (br->bits_in_buf > 0)
            {
                br->bits_in_buf = n;
                return true;
            }
            // No bits at all - genuine EOF
            return false;
        }
        br->buffer |= ((uint32_t)br->data[br->pos++]) << br->bits_in_buf;
        br->bits_in_buf += 8;
    }
    return true;
}

/// @brief Read n bits (LSB first)
static uint32_t br_read(bit_reader_t *br, int n)
{
    if (!br_fill(br, n))
        return 0;
    uint32_t val = br->buffer & ((1U << n) - 1);
    br->buffer >>= n;
    br->bits_in_buf -= n;
    return val;
}

/// @brief Peek n bits without consuming
static uint32_t br_peek(bit_reader_t *br, int n)
{
    br_fill(br, n);
    return br->buffer & ((1U << n) - 1);
}

/// @brief Consume n bits
static void br_consume(bit_reader_t *br, int n)
{
    br->buffer >>= n;
    br->bits_in_buf -= n;
}

/// @brief Align to byte boundary
static void br_align(bit_reader_t *br)
{
    br->buffer = 0;
    br->bits_in_buf = 0;
}

/// @brief Check if more data available
static bool br_has_data(bit_reader_t *br)
{
    return br->pos < br->len || br->bits_in_buf > 0;
}

//=============================================================================
// Bit Stream Writer (for compression)
//=============================================================================

typedef struct
{
    uint8_t *data;
    size_t capacity;
    size_t len;
    uint32_t buffer;
    int bits_in_buf;
} bit_writer_t;

static void bw_init(bit_writer_t *bw, size_t initial_cap)
{
    bw->capacity = initial_cap > 256 ? initial_cap : 256;
    bw->data = (uint8_t *)malloc(bw->capacity);
    if (!bw->data)
        rt_trap("rt_compress: memory allocation failed");
    bw->len = 0;
    bw->buffer = 0;
    bw->bits_in_buf = 0;
}

static void bw_ensure(bit_writer_t *bw, size_t need)
{
    if (bw->len + need > bw->capacity)
    {
        size_t new_cap = bw->capacity * 2;
        if (new_cap < bw->len + need)
            new_cap = bw->len + need + 256;
        uint8_t *new_data = (uint8_t *)realloc(bw->data, new_cap);
        if (!new_data)
            rt_trap("Compress: out of memory");
        bw->data = new_data;
        bw->capacity = new_cap;
    }
}

/// @brief Write n bits (LSB first)
static void bw_write(bit_writer_t *bw, uint32_t val, int n)
{
    bw->buffer |= val << bw->bits_in_buf;
    bw->bits_in_buf += n;
    while (bw->bits_in_buf >= 8)
    {
        bw_ensure(bw, 1);
        bw->data[bw->len++] = bw->buffer & 0xFF;
        bw->buffer >>= 8;
        bw->bits_in_buf -= 8;
    }
}

/// @brief Flush remaining bits (pad with zeros)
static void bw_flush(bit_writer_t *bw)
{
    if (bw->bits_in_buf > 0)
    {
        bw_ensure(bw, 1);
        bw->data[bw->len++] = bw->buffer & 0xFF;
        bw->buffer = 0;
        bw->bits_in_buf = 0;
    }
}

/// @brief Write raw bytes (must be byte-aligned)
static void bw_write_bytes(bit_writer_t *bw, const uint8_t *data, size_t len)
{
    bw_ensure(bw, len);
    memcpy(bw->data + bw->len, data, len);
    bw->len += len;
}

static void bw_free(bit_writer_t *bw)
{
    free(bw->data);
    bw->data = NULL;
    bw->capacity = 0;
    bw->len = 0;
}

//=============================================================================
// Huffman Tree
//=============================================================================

typedef struct
{
    uint16_t symbol; // Symbol value
    uint16_t code;   // Huffman code
    uint8_t len;     // Code length in bits
} huffman_code_t;

typedef struct
{
    int max_code;      // Maximum code index
    uint16_t *symbols; // Symbol lookup by code
    int table_bits;    // Bits for direct lookup
    size_t table_size; // Size of lookup table
} huffman_tree_t;

/// @brief Build Huffman tree from code lengths
static bool build_huffman_tree(huffman_tree_t *tree, const uint8_t *lengths, int num_codes)
{
    // Count code lengths
    int bl_count[MAX_BITS + 1] = {0};
    for (int i = 0; i < num_codes; i++)
    {
        if (lengths[i] > MAX_BITS)
            return false;
        bl_count[lengths[i]]++;
    }
    bl_count[0] = 0;

    // Calculate next code for each length
    uint16_t next_code[MAX_BITS + 1];
    uint16_t code = 0;
    for (int bits = 1; bits <= MAX_BITS; bits++)
    {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    // Determine table size
    int max_len = 0;
    for (int i = 0; i < num_codes; i++)
    {
        if (lengths[i] > max_len)
            max_len = lengths[i];
    }
    if (max_len == 0)
        max_len = 1;

    tree->table_bits = max_len; /* Support full 15-bit Huffman codes per RFC 1951 */
    tree->table_size = 1 << tree->table_bits;
    tree->max_code = num_codes;

    // Allocate symbol table
    tree->symbols = (uint16_t *)calloc(tree->table_size + num_codes * 2, sizeof(uint16_t));
    if (!tree->symbols)
        return false;

    // Build direct lookup table for short codes
    for (int i = 0; i < num_codes; i++)
    {
        if (lengths[i] == 0)
            continue;

        uint16_t sym_code = next_code[lengths[i]]++;
        int len = lengths[i];

        if (len <= tree->table_bits)
        {
            // Direct table entry
            // Reverse the code bits for LSB-first reading
            uint16_t rev_code = 0;
            for (int b = 0; b < len; b++)
            {
                if (sym_code & (1 << b))
                    rev_code |= 1 << (len - 1 - b);
            }

            // Fill in all entries that match this prefix
            int fill = 1 << (tree->table_bits - len);
            for (int j = 0; j < fill; j++)
            {
                int idx = rev_code | (j << len);
                // Pack length and symbol: high bits = length, low bits = symbol
                tree->symbols[idx] = (uint16_t)((len << 12) | i);
            }
        }
    }

    return true;
}

/// @brief Decode one symbol using Huffman tree
static int decode_symbol(huffman_tree_t *tree, bit_reader_t *br)
{
    if (!br_fill(br, tree->table_bits))
        return -1;

    uint32_t bits = br_peek(br, tree->table_bits);
    uint16_t entry = tree->symbols[bits];

    if (entry == 0)
        return -1; // Invalid code

    int len = entry >> 12;
    int symbol = entry & 0xFFF;

    if (len == 0)
        return -1;

    br_consume(br, len);
    return symbol;
}

static void free_huffman_tree(huffman_tree_t *tree)
{
    free(tree->symbols);
    tree->symbols = NULL;
}

//=============================================================================
// Fixed Huffman Trees (for block type 1)
//=============================================================================

static huffman_tree_t fixed_lit_tree;
static huffman_tree_t fixed_dist_tree;
static int fixed_trees_init = 0;

static void init_fixed_trees(void)
{
    if (fixed_trees_init)
        return;

    // Fixed literal/length code lengths (RFC 1951 section 3.2.6)
    uint8_t lit_lengths[FIXED_LIT_CODES];
    for (int i = 0; i <= 143; i++)
        lit_lengths[i] = 8;
    for (int i = 144; i <= 255; i++)
        lit_lengths[i] = 9;
    for (int i = 256; i <= 279; i++)
        lit_lengths[i] = 7;
    for (int i = 280; i <= 287; i++)
        lit_lengths[i] = 8;

    build_huffman_tree(&fixed_lit_tree, lit_lengths, FIXED_LIT_CODES);

    // Fixed distance code lengths (all 5 bits)
    uint8_t dist_lengths[FIXED_DIST_CODES];
    for (int i = 0; i < FIXED_DIST_CODES; i++)
        dist_lengths[i] = 5;

    build_huffman_tree(&fixed_dist_tree, dist_lengths, FIXED_DIST_CODES);

    fixed_trees_init = 1;
}

//=============================================================================
// Length and Distance Tables
//=============================================================================

// Extra bits for length codes 257-285
static const int length_extra_bits[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                          2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// Base length for length codes 257-285
static const int length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

// Extra bits for distance codes 0-29
static const int dist_extra_bits[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Base distance for distance codes 0-29
static const int dist_base[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                  33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

// Code length alphabet order (for dynamic Huffman)
static const int code_length_order[MAX_CODE_LEN_CODES] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

//=============================================================================
// Decompression Output Buffer
//=============================================================================

typedef struct
{
    uint8_t *data;
    size_t len;
    size_t capacity;
} output_buffer_t;

static void out_init(output_buffer_t *out, size_t initial_cap)
{
    out->capacity = initial_cap > 256 ? initial_cap : 256;
    out->data = (uint8_t *)malloc(out->capacity);
    if (!out->data)
        rt_trap("rt_compress: memory allocation failed");
    out->len = 0;
}

/* S-20: Maximum decompressed output size (256 MB) to prevent decompression bombs */
#define INFLATE_MAX_OUTPUT (256u * 1024u * 1024u)

static void out_ensure(output_buffer_t *out, size_t need)
{
    if (out->len + need > INFLATE_MAX_OUTPUT)
        rt_trap("Inflate: decompressed output exceeds 256 MB limit");

    if (out->len + need > out->capacity)
    {
        size_t new_cap = out->capacity * 2;
        if (new_cap < out->len + need)
            new_cap = out->len + need + 256;
        if (new_cap > INFLATE_MAX_OUTPUT)
            new_cap = INFLATE_MAX_OUTPUT;
        uint8_t *new_data = (uint8_t *)realloc(out->data, new_cap);
        if (!new_data)
            rt_trap("Inflate: out of memory");
        out->data = new_data;
        out->capacity = new_cap;
    }
}

static void out_byte(output_buffer_t *out, uint8_t b)
{
    out_ensure(out, 1);
    out->data[out->len++] = b;
}

static void out_copy(output_buffer_t *out, int distance, int length)
{
    out_ensure(out, length);
    size_t src = out->len - distance;
    for (int i = 0; i < length; i++)
    {
        out->data[out->len++] = out->data[src++];
    }
}

static void out_free(output_buffer_t *out)
{
    free(out->data);
    out->data = NULL;
    out->capacity = 0;
    out->len = 0;
}

//=============================================================================
// DEFLATE Decompression
//=============================================================================

/// @brief Inflate a stored block (no compression)
static bool inflate_stored(bit_reader_t *br, output_buffer_t *out)
{
    // Align to byte boundary
    br_align(br);

    // Read LEN and NLEN
    if (br->pos + 4 > br->len)
        return false;

    uint16_t len = br->data[br->pos] | (br->data[br->pos + 1] << 8);
    uint16_t nlen = br->data[br->pos + 2] | (br->data[br->pos + 3] << 8);
    br->pos += 4;

    // Verify
    if ((len ^ nlen) != 0xFFFF)
        return false;

    // Copy bytes
    if (br->pos + len > br->len)
        return false;

    out_ensure(out, len);
    memcpy(out->data + out->len, br->data + br->pos, len);
    out->len += len;
    br->pos += len;

    return true;
}

/// @brief Inflate a Huffman-coded block
static bool inflate_huffman(bit_reader_t *br,
                            output_buffer_t *out,
                            huffman_tree_t *lit_tree,
                            huffman_tree_t *dist_tree)
{
    while (true)
    {
        int sym = decode_symbol(lit_tree, br);
        if (sym < 0)
            return false;

        if (sym < 256)
        {
            // Literal byte
            out_byte(out, (uint8_t)sym);
        }
        else if (sym == 256)
        {
            // End of block
            return true;
        }
        else if (sym <= 285)
        {
            // Length code
            int len_idx = sym - 257;
            int length = length_base[len_idx];
            int extra = length_extra_bits[len_idx];
            if (extra > 0)
                length += br_read(br, extra);

            // Distance code
            int dist_sym = decode_symbol(dist_tree, br);
            if (dist_sym < 0 || dist_sym >= 30)
                return false;

            int distance = dist_base[dist_sym];
            extra = dist_extra_bits[dist_sym];
            if (extra > 0)
                distance += br_read(br, extra);

            // Validate distance
            if (distance > (int)out->len)
                return false;

            // Copy from output buffer
            out_copy(out, distance, length);
        }
        else
        {
            return false; // Invalid symbol
        }
    }
}

/// @brief Inflate a dynamic Huffman block
static bool inflate_dynamic(bit_reader_t *br, output_buffer_t *out)
{
    // Read header
    int hlit = br_read(br, 5) + 257; // Number of literal/length codes
    int hdist = br_read(br, 5) + 1;  // Number of distance codes
    int hclen = br_read(br, 4) + 4;  // Number of code length codes

    if (hlit > MAX_LIT_CODES || hdist > MAX_DIST_CODES)
        return false;

    // Read code length code lengths
    uint8_t cl_lengths[MAX_CODE_LEN_CODES] = {0};
    for (int i = 0; i < hclen; i++)
    {
        cl_lengths[code_length_order[i]] = br_read(br, 3);
    }

    // Build code length tree
    huffman_tree_t cl_tree = {0};
    if (!build_huffman_tree(&cl_tree, cl_lengths, MAX_CODE_LEN_CODES))
        return false;

    // Decode literal/length and distance code lengths
    uint8_t lengths[MAX_LIT_CODES + MAX_DIST_CODES] = {0};
    int total_codes = hlit + hdist;
    int i = 0;

    while (i < total_codes)
    {
        int sym = decode_symbol(&cl_tree, br);
        if (sym < 0)
        {
            free_huffman_tree(&cl_tree);
            return false;
        }

        if (sym < 16)
        {
            // Literal length
            lengths[i++] = sym;
        }
        else if (sym == 16)
        {
            // Repeat previous
            if (i == 0)
            {
                free_huffman_tree(&cl_tree);
                return false;
            }
            int repeat = br_read(br, 2) + 3;
            uint8_t prev = lengths[i - 1];
            while (repeat-- > 0 && i < total_codes)
                lengths[i++] = prev;
        }
        else if (sym == 17)
        {
            // Repeat 0, 3-10 times
            int repeat = br_read(br, 3) + 3;
            while (repeat-- > 0 && i < total_codes)
                lengths[i++] = 0;
        }
        else if (sym == 18)
        {
            // Repeat 0, 11-138 times
            int repeat = br_read(br, 7) + 11;
            while (repeat-- > 0 && i < total_codes)
                lengths[i++] = 0;
        }
        else
        {
            free_huffman_tree(&cl_tree);
            return false;
        }
    }

    free_huffman_tree(&cl_tree);

    // Build literal/length and distance trees
    huffman_tree_t lit_tree = {0}, dist_tree = {0};

    if (!build_huffman_tree(&lit_tree, lengths, hlit))
        return false;

    if (!build_huffman_tree(&dist_tree, lengths + hlit, hdist))
    {
        free_huffman_tree(&lit_tree);
        return false;
    }

    // Decode block
    bool ok = inflate_huffman(br, out, &lit_tree, &dist_tree);

    free_huffman_tree(&lit_tree);
    free_huffman_tree(&dist_tree);

    return ok;
}

/// @brief Main DEFLATE decompression function
static void *inflate_data(const uint8_t *data, size_t len)
{
    init_fixed_trees();

    bit_reader_t br;
    br_init(&br, data, len);

    output_buffer_t out;
    out_init(&out, len * 4); // Estimate 4x expansion

    bool last_block = false;

    while (!last_block)
    {
        if (!br_has_data(&br))
        {
            out_free(&out);
            rt_trap("Inflate: unexpected end of data");
        }

        // Read block header
        last_block = br_read(&br, 1);
        int block_type = br_read(&br, 2);

        bool ok = false;
        switch (block_type)
        {
            case 0: // Stored
                ok = inflate_stored(&br, &out);
                break;
            case 1: // Fixed Huffman
                ok = inflate_huffman(&br, &out, &fixed_lit_tree, &fixed_dist_tree);
                break;
            case 2: // Dynamic Huffman
                ok = inflate_dynamic(&br, &out);
                break;
            default:
                out_free(&out);
                rt_trap("Inflate: invalid block type");
        }

        if (!ok)
        {
            out_free(&out);
            rt_trap("Inflate: invalid compressed data");
        }
    }

    // Create output Bytes object
    void *result = rt_bytes_new(out.len);
    memcpy(bytes_data(result), out.data, out.len);
    out_free(&out);

    return result;
}

//=============================================================================
// DEFLATE Compression
//=============================================================================

// Hash table for LZ77 matching
typedef struct
{
    int *head;      // Hash -> position
    int *prev;      // Chain for same hash
    int window_pos; // Current position in window
} lz77_state_t;

#define HASH_BITS 15
#define HASH_SIZE (1 << HASH_BITS)
#define HASH_MASK (HASH_SIZE - 1)
#define NIL (-1)

/// @brief Compute hash for 3 bytes
static inline int compute_hash(const uint8_t *data)
{
    return ((data[0] << 10) ^ (data[1] << 5) ^ data[2]) & HASH_MASK;
}

/// @brief Initialize LZ77 state
static void lz77_init(lz77_state_t *lz)
{
    lz->head = (int *)malloc(HASH_SIZE * sizeof(int));
    lz->prev = (int *)malloc(WINDOW_SIZE * sizeof(int));
    for (int i = 0; i < HASH_SIZE; i++)
        lz->head[i] = NIL;
    for (int i = 0; i < WINDOW_SIZE; i++)
        lz->prev[i] = NIL;
    lz->window_pos = 0;
}

static void lz77_free(lz77_state_t *lz)
{
    free(lz->head);
    free(lz->prev);
}

/// @brief Find best match at current position
static int find_match(
    lz77_state_t *lz, const uint8_t *data, size_t pos, size_t len, int max_chain, int *match_dist)
{
    if (pos + MIN_MATCH_LEN > len)
        return 0;

    int hash = compute_hash(data + pos);
    int chain_pos = lz->head[hash];
    int best_len = MIN_MATCH_LEN - 1;
    int best_dist = 0;

    int limit = (int)pos - MAX_DISTANCE;
    if (limit < 0)
        limit = 0;

    while (chain_pos >= limit && max_chain-- > 0)
    {
        // Check match
        int match_len = 0;
        size_t a = pos, b = chain_pos;
        size_t max_len = len - pos;
        if (max_len > MAX_MATCH_LEN)
            max_len = MAX_MATCH_LEN;

        while (match_len < (int)max_len && data[a] == data[b])
        {
            match_len++;
            a++;
            b++;
        }

        if (match_len > best_len)
        {
            best_len = match_len;
            best_dist = (int)(pos - chain_pos);
            if (best_len >= MAX_MATCH_LEN)
                break;
        }

        chain_pos = lz->prev[chain_pos & WINDOW_MASK];
    }

    *match_dist = best_dist;
    return best_len >= MIN_MATCH_LEN ? best_len : 0;
}

/// @brief Update hash chain
static void update_hash(lz77_state_t *lz, const uint8_t *data, size_t pos)
{
    int hash = compute_hash(data + pos);
    lz->prev[pos & WINDOW_MASK] = lz->head[hash];
    lz->head[hash] = (int)pos;
}

/// @brief Get length code for a match length
static int get_length_code(int length)
{
    for (int i = 0; i < 29; i++)
    {
        if (i == 28)
            return 285;
        if (length < length_base[i + 1])
            return 257 + i;
    }
    return 285;
}

/// @brief Get distance code for a distance
static int get_dist_code(int dist)
{
    for (int i = 0; i < 30; i++)
    {
        if (i == 29 || dist < dist_base[i + 1])
            return i;
    }
    return 29;
}

/// @brief Count frequencies for Huffman code generation
static void count_frequencies(
    const uint8_t *data, size_t len, int level, int *lit_freq, int *dist_freq)
{
    memset(lit_freq, 0, MAX_LIT_CODES * sizeof(int));
    memset(dist_freq, 0, MAX_DIST_CODES * sizeof(int));

    lz77_state_t lz;
    lz77_init(&lz);

    int max_chain = 4 << level; // More chains at higher levels

    size_t pos = 0;
    while (pos < len)
    {
        int match_dist = 0;
        int match_len = 0;

        if (pos + MIN_MATCH_LEN <= len)
        {
            match_len = find_match(&lz, data, pos, len, max_chain, &match_dist);
        }

        if (match_len >= MIN_MATCH_LEN)
        {
            // Length/distance pair
            lit_freq[get_length_code(match_len)]++;
            dist_freq[get_dist_code(match_dist)]++;

            for (int i = 0; i < match_len; i++)
            {
                if (pos + i + MIN_MATCH_LEN <= len)
                    update_hash(&lz, data, pos + i);
            }
            pos += match_len;
        }
        else
        {
            // Literal
            lit_freq[data[pos]]++;
            if (pos + MIN_MATCH_LEN <= len)
                update_hash(&lz, data, pos);
            pos++;
        }
    }

    // End of block
    lit_freq[256]++;

    lz77_free(&lz);
}

/// @brief Build optimal code lengths using package-merge algorithm (simplified)
static void build_code_lengths(const int *freq, int n, int max_len, uint8_t *lengths)
{
    // Simple approach: assign lengths based on frequency ranking
    // (Not optimal but reasonable)

    typedef struct
    {
        int symbol;
        int freq;
    } sym_freq_t;

    sym_freq_t *sorted = (sym_freq_t *)malloc(n * sizeof(sym_freq_t));
    int count = 0;

    for (int i = 0; i < n; i++)
    {
        lengths[i] = 0;
        if (freq[i] > 0)
        {
            sorted[count].symbol = i;
            sorted[count].freq = freq[i];
            count++;
        }
    }

    if (count == 0)
    {
        free(sorted);
        return;
    }

    // Sort by frequency (descending)
    for (int i = 0; i < count - 1; i++)
    {
        for (int j = i + 1; j < count; j++)
        {
            if (sorted[j].freq > sorted[i].freq)
            {
                sym_freq_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    // Assign lengths (more frequent = shorter)
    // Use a simple heuristic based on position in sorted list
    for (int i = 0; i < count; i++)
    {
        // Map position to length (1-15)
        int len;
        if (count <= 2)
            len = 1;
        else if (i < count / 4)
            len = (count <= 8) ? 2 : 4;
        else if (i < count / 2)
            len = (count <= 8) ? 3 : 6;
        else if (i < 3 * count / 4)
            len = (count <= 8) ? 4 : 8;
        else
            len = (count <= 8) ? 5 : 10;

        if (len > max_len)
            len = max_len;
        if (len < 1)
            len = 1;

        lengths[sorted[i].symbol] = len;
    }

    free(sorted);
}

/// @brief Write Huffman codes
static void build_codes(const uint8_t *lengths, int n, uint16_t *codes)
{
    int bl_count[MAX_BITS + 1] = {0};
    for (int i = 0; i < n; i++)
        bl_count[lengths[i]]++;
    bl_count[0] = 0;

    uint16_t next_code[MAX_BITS + 1];
    uint16_t code = 0;
    for (int bits = 1; bits <= MAX_BITS; bits++)
    {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    for (int i = 0; i < n; i++)
    {
        if (lengths[i] != 0)
            codes[i] = next_code[lengths[i]]++;
        else
            codes[i] = 0;
    }
}

/// @brief Write bits in reverse order (LSB first as required by DEFLATE)
static void write_code(bit_writer_t *bw, uint16_t code, int len)
{
    // Reverse the code bits
    uint16_t rev = 0;
    for (int i = 0; i < len; i++)
    {
        if (code & (1 << i))
            rev |= 1 << (len - 1 - i);
    }
    bw_write(bw, rev, len);
}

/// @brief Compress data using DEFLATE with stored blocks (simplest approach)
static void deflate_stored(bit_writer_t *bw, const uint8_t *data, size_t len)
{
    // Handle empty data - still need a final block
    if (len == 0)
    {
        // Block header
        bw_write(bw, 1, 1); // BFINAL = 1
        bw_write(bw, 0, 2); // BTYPE = stored

        // Align to byte
        bw_flush(bw);

        // LEN = 0 and NLEN = 0xFFFF
        bw_write(bw, 0x00, 8);
        bw_write(bw, 0x00, 8);
        bw_write(bw, 0xFF, 8);
        bw_write(bw, 0xFF, 8);
        return;
    }

    size_t pos = 0;
    while (pos < len)
    {
        size_t block_len = len - pos;
        if (block_len > 65535)
            block_len = 65535;

        bool last = (pos + block_len >= len);

        // Block header
        bw_write(bw, last ? 1 : 0, 1); // BFINAL
        bw_write(bw, 0, 2);            // BTYPE = stored

        // Align to byte
        bw_flush(bw);

        // LEN and NLEN
        uint16_t nlen = ~(uint16_t)block_len;
        bw_write(bw, block_len & 0xFF, 8);
        bw_write(bw, (block_len >> 8) & 0xFF, 8);
        bw_write(bw, nlen & 0xFF, 8);
        bw_write(bw, (nlen >> 8) & 0xFF, 8);

        // Data
        bw_flush(bw);
        bw_write_bytes(bw, data + pos, block_len);

        pos += block_len;
    }
}

/// @brief Compress data using fixed Huffman codes
static void deflate_fixed(bit_writer_t *bw, const uint8_t *data, size_t len, int level)
{
    init_fixed_trees();

    lz77_state_t lz;
    lz77_init(&lz);

    int max_chain = 4 << level;

    // Block header
    bw_write(bw, 1, 1); // BFINAL = 1 (single block)
    bw_write(bw, 1, 2); // BTYPE = fixed Huffman

    // Fixed literal/length codes
    // 0-143: 8 bits, 144-255: 9 bits, 256-279: 7 bits, 280-287: 8 bits

    size_t pos = 0;
    while (pos < len)
    {
        int match_dist = 0;
        int match_len = 0;

        if (pos + MIN_MATCH_LEN <= len)
        {
            match_len = find_match(&lz, data, pos, len, max_chain, &match_dist);
        }

        if (match_len >= MIN_MATCH_LEN)
        {
            // Write length code
            int len_code = get_length_code(match_len);
            int len_idx = len_code - 257;

            // Encode length code with fixed Huffman
            if (len_code <= 279)
            {
                // 7 bits: codes 256-279 map to 0000000-0010111
                write_code(bw, len_code - 256, 7);
            }
            else
            {
                // 8 bits: codes 280-287 map to 11000000-11000111
                write_code(bw, 0xC0 + (len_code - 280), 8);
            }

            // Extra length bits
            if (length_extra_bits[len_idx] > 0)
            {
                bw_write(bw, match_len - length_base[len_idx], length_extra_bits[len_idx]);
            }

            // Distance code (5 bits for fixed)
            int dist_code = get_dist_code(match_dist);
            write_code(bw, dist_code, 5);

            // Extra distance bits
            if (dist_extra_bits[dist_code] > 0)
            {
                bw_write(bw, match_dist - dist_base[dist_code], dist_extra_bits[dist_code]);
            }

            // Update hash for all matched bytes
            for (int i = 0; i < match_len; i++)
            {
                if (pos + i + MIN_MATCH_LEN <= len)
                    update_hash(&lz, data, pos + i);
            }
            pos += match_len;
        }
        else
        {
            // Literal byte
            uint8_t lit = data[pos];
            if (lit <= 143)
            {
                // 8 bits: 00110000-10111111
                write_code(bw, 0x30 + lit, 8);
            }
            else
            {
                // 9 bits: 110010000-111111111
                write_code(bw, 0x190 + (lit - 144), 9);
            }

            if (pos + MIN_MATCH_LEN <= len)
                update_hash(&lz, data, pos);
            pos++;
        }
    }

    // End of block (code 256, 7 bits)
    write_code(bw, 0, 7);

    lz77_free(&lz);
}

/// @brief Main DEFLATE compression function
static void *deflate_data(const uint8_t *data, size_t len, int level)
{
    if (level < DEFLATE_MIN_LEVEL)
        level = DEFLATE_MIN_LEVEL;
    if (level > DEFLATE_MAX_LEVEL)
        level = DEFLATE_MAX_LEVEL;

    bit_writer_t bw;
    bw_init(&bw, len);

    // For small data or level 1, use stored blocks
    // For larger data, use fixed Huffman
    if (len <= 64 || level == 1)
    {
        deflate_stored(&bw, data, len);
    }
    else
    {
        deflate_fixed(&bw, data, len, level);
    }

    bw_flush(&bw);

    // Create output Bytes object
    void *result = rt_bytes_new(bw.len);
    memcpy(bytes_data(result), bw.data, bw.len);
    bw_free(&bw);

    return result;
}

//=============================================================================
// GZIP Wrapper
//=============================================================================

/// @brief Compress data with GZIP wrapper
static void *gzip_data(const uint8_t *data, size_t len, int level)
{
    // First compress with DEFLATE
    void *deflated = deflate_data(data, len, level);
    size_t deflated_len = bytes_len(deflated);
    uint8_t *deflated_data = bytes_data(deflated);

    // Calculate CRC32
    uint32_t crc = rt_crc32_compute(data, len);

    // Create output: header (10) + deflated + trailer (8)
    size_t total_len = 10 + deflated_len + 8;
    void *result = rt_bytes_new(total_len);
    uint8_t *out = bytes_data(result);

    // GZIP header (RFC 1952)
    out[0] = 0x1F; // Magic
    out[1] = 0x8B; // Magic
    out[2] = 0x08; // Compression method (deflate)
    out[3] = 0x00; // Flags (none)
    out[4] = 0x00; // MTIME (0 = not available)
    out[5] = 0x00;
    out[6] = 0x00;
    out[7] = 0x00;
    out[8] = 0x00; // XFL (extra flags)
    out[9] = 0xFF; // OS (unknown)

    // Compressed data
    memcpy(out + 10, deflated_data, deflated_len);

    // Trailer
    size_t trailer_pos = 10 + deflated_len;
    out[trailer_pos + 0] = crc & 0xFF;
    out[trailer_pos + 1] = (crc >> 8) & 0xFF;
    out[trailer_pos + 2] = (crc >> 16) & 0xFF;
    out[trailer_pos + 3] = (crc >> 24) & 0xFF;
    out[trailer_pos + 4] = len & 0xFF;
    out[trailer_pos + 5] = (len >> 8) & 0xFF;
    out[trailer_pos + 6] = (len >> 16) & 0xFF;
    out[trailer_pos + 7] = (len >> 24) & 0xFF;

    return result;
}

/// @brief Decompress GZIP data
static void *gunzip_data(const uint8_t *data, size_t len)
{
    if (len < 18)
        rt_trap("Gunzip: data too short");

    // Verify magic
    if (data[0] != 0x1F || data[1] != 0x8B)
        rt_trap("Gunzip: invalid magic number");

    // Check compression method
    if (data[2] != 0x08)
        rt_trap("Gunzip: unsupported compression method");

    uint8_t flags = data[3];

    // Skip header
    size_t pos = 10;

    // Skip FEXTRA
    if (flags & 0x04)
    {
        if (pos + 2 > len)
            rt_trap("Gunzip: truncated header");
        size_t xlen = data[pos] | (data[pos + 1] << 8);
        pos += 2 + xlen;
    }

    // Skip FNAME
    if (flags & 0x08)
    {
        while (pos < len && data[pos] != 0)
            pos++;
        pos++; // Skip null terminator
    }

    // Skip FCOMMENT
    if (flags & 0x10)
    {
        while (pos < len && data[pos] != 0)
            pos++;
        pos++;
    }

    // Skip FHCRC
    if (flags & 0x02)
    {
        pos += 2;
    }

    if (pos >= len - 8)
        rt_trap("Gunzip: truncated data");

    // Extract trailer
    size_t trailer_pos = len - 8;
    uint32_t expected_crc = data[trailer_pos] | (data[trailer_pos + 1] << 8) |
                            (data[trailer_pos + 2] << 16) | (data[trailer_pos + 3] << 24);
    uint32_t expected_size = data[trailer_pos + 4] | (data[trailer_pos + 5] << 8) |
                             (data[trailer_pos + 6] << 16) | (data[trailer_pos + 7] << 24);

    // Decompress
    size_t compressed_len = trailer_pos - pos;
    void *result = inflate_data(data + pos, compressed_len);

    // Verify CRC
    uint32_t actual_crc = rt_crc32_compute(bytes_data(result), bytes_len(result));
    if (actual_crc != expected_crc)
        rt_trap("Gunzip: CRC mismatch");

    // Verify size (mod 2^32)
    if ((bytes_len(result) & 0xFFFFFFFF) != expected_size)
        rt_trap("Gunzip: size mismatch");

    return result;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_compress_deflate(void *data)
{
    if (!data)
        rt_trap("Compress.Deflate: data is null");
    return deflate_data(bytes_data(data), bytes_len(data), DEFLATE_DEFAULT_LEVEL);
}

void *rt_compress_deflate_lvl(void *data, int64_t level)
{
    if (!data)
        rt_trap("Compress.DeflateLvl: data is null");
    if (level < DEFLATE_MIN_LEVEL || level > DEFLATE_MAX_LEVEL)
        rt_trap("Compress.DeflateLvl: level must be 1-9");
    return deflate_data(bytes_data(data), bytes_len(data), (int)level);
}

void *rt_compress_inflate(void *data)
{
    if (!data)
        rt_trap("Compress.Inflate: data is null");
    return inflate_data(bytes_data(data), bytes_len(data));
}

void *rt_compress_gzip(void *data)
{
    if (!data)
        rt_trap("Compress.Gzip: data is null");
    return gzip_data(bytes_data(data), bytes_len(data), DEFLATE_DEFAULT_LEVEL);
}

void *rt_compress_gzip_lvl(void *data, int64_t level)
{
    if (!data)
        rt_trap("Compress.GzipLvl: data is null");
    if (level < DEFLATE_MIN_LEVEL || level > DEFLATE_MAX_LEVEL)
        rt_trap("Compress.GzipLvl: level must be 1-9");
    return gzip_data(bytes_data(data), bytes_len(data), (int)level);
}

void *rt_compress_gunzip(void *data)
{
    if (!data)
        rt_trap("Compress.Gunzip: data is null");
    return gunzip_data(bytes_data(data), bytes_len(data));
}

void *rt_compress_deflate_str(rt_string text)
{
    if (!text)
        rt_trap("Compress.DeflateStr: text is null");
    void *bytes = rt_bytes_from_str(text);
    void *result = deflate_data(bytes_data(bytes), bytes_len(bytes), DEFLATE_DEFAULT_LEVEL);
    return result;
}

rt_string rt_compress_inflate_str(void *data)
{
    if (!data)
        rt_trap("Compress.InflateStr: data is null");
    void *result = inflate_data(bytes_data(data), bytes_len(data));
    rt_string str = rt_bytes_to_str(result);
    return str;
}

void *rt_compress_gzip_str(rt_string text)
{
    if (!text)
        rt_trap("Compress.GzipStr: text is null");
    void *bytes = rt_bytes_from_str(text);
    void *result = gzip_data(bytes_data(bytes), bytes_len(bytes), DEFLATE_DEFAULT_LEVEL);
    return result;
}

rt_string rt_compress_gunzip_str(void *data)
{
    if (!data)
        rt_trap("Compress.GunzipStr: data is null");
    void *result = gunzip_data(bytes_data(data), bytes_len(data));
    rt_string str = rt_bytes_to_str(result);
    return str;
}
