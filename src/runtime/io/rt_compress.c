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

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

//=============================================================================
// Constants
//=============================================================================

/// @brief DEFLATE block types (RFC 1951 §3.2.3).
typedef enum {
    RT_HUFFMAN_STORED = 0,  ///< No compression (stored block).
    RT_HUFFMAN_FIXED = 1,   ///< Fixed Huffman codes.
    RT_HUFFMAN_DYNAMIC = 2, ///< Dynamic Huffman codes.
} rt_huffman_type_t;

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

extern void rt_trap_set_recovery(jmp_buf *buf);
extern void rt_trap_clear_recovery(void);

// Fixed Huffman code lengths (RFC 1951)
#define FIXED_LIT_CODES 288
#define FIXED_DIST_CODES 32

//=============================================================================
// Internal Bytes Access
//=============================================================================

/// @brief Get raw pointer to bytes data
static inline uint8_t *bytes_data(void *obj) {
    return rt_bytes_data(obj);
}

/// @brief Get bytes length
static inline int64_t bytes_len(void *obj) {
    return rt_bytes_len(obj);
}

/// @brief Release a temporary GC object that is no longer needed.
static void compress_release_temp_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

//=============================================================================
// Bit Stream Reader (for decompression)
//=============================================================================

/// @brief LSB-first bit-stream reader used by the DEFLATE decompressor.
typedef struct {
    const uint8_t *data; ///< Source byte buffer (borrowed, caller keeps alive).
    size_t len;          ///< Total length of `data` in bytes.
    size_t pos;          ///< Current byte offset into `data`.
    int bit_pos;         ///< Unused — superseded by `bits_in_buf` (kept for alignment).
    uint32_t buffer;     ///< Rolling bit accumulator (LSB = next bit).
    int bits_in_buf;     ///< Number of valid bits currently in `buffer`.
    bool error;          ///< Set to true after a truncated or invalid bit read.
} bit_reader_t;

/// @brief Initialize a bit reader over a byte buffer.
///
/// Resets the rolling buffer and bit-position state. The reader holds
/// `data` by pointer, so the caller must keep the buffer alive for the
/// reader's lifetime. Bit reads pull from the LSB of `buffer`,
/// refilled in 8-bit chunks from `data` on demand.
static void br_init(bit_reader_t *br, const uint8_t *data, size_t len) {
    br->data = data;
    br->len = len;
    br->pos = 0;
    br->bit_pos = 0;
    br->buffer = 0;
    br->bits_in_buf = 0;
    br->error = false;
}

/// @brief Ensure at least n bits in buffer
/// @note At end-of-stream, zero-fills remaining bits if some data exists (valid
///       since DEFLATE padding is zeros). Fails if no bits available at all.
static bool br_fill(bit_reader_t *br, int n) {
    while (br->bits_in_buf < n) {
        if (br->pos >= br->len) {
            return br->bits_in_buf > 0;
        }
        br->buffer |= ((uint32_t)br->data[br->pos++]) << br->bits_in_buf;
        br->bits_in_buf += 8;
    }
    return true;
}

/// @brief Read n bits (LSB first)
static uint32_t br_read(bit_reader_t *br, int n) {
    if (!br_fill(br, n) || br->bits_in_buf < n) {
        br->error = true;
        return 0;
    }
    uint32_t val = br->buffer & ((1U << n) - 1);
    br->buffer >>= n;
    br->bits_in_buf -= n;
    return val;
}

/// @brief Peek n bits without consuming
static uint32_t br_peek(bit_reader_t *br, int n) {
    if (!br_fill(br, n))
        return 0;
    return br->buffer & ((1U << n) - 1);
}

/// @brief Consume n bits
static void br_consume(bit_reader_t *br, int n) {
    if (n > br->bits_in_buf) {
        br->bits_in_buf = 0;
        br->buffer = 0;
        br->error = true;
        return;
    }
    br->buffer >>= n;
    br->bits_in_buf -= n;
}

/// @brief Align to byte boundary
static void br_align(bit_reader_t *br) {
    br->buffer = 0;
    br->bits_in_buf = 0;
}

/// @brief Check if more data available
static bool br_has_data(bit_reader_t *br) {
    return br->pos < br->len || br->bits_in_buf > 0;
}

//=============================================================================
// Bit Stream Writer (for compression)
//=============================================================================

/// @brief LSB-first bit-stream writer used by the DEFLATE compressor.
typedef struct {
    uint8_t *data;   ///< Output byte buffer (heap-allocated, grown by bw_ensure).
    size_t capacity; ///< Allocated capacity of `data` in bytes.
    size_t len;      ///< Number of complete bytes written to `data`.
    uint32_t buffer; ///< Pending bit accumulator (LSB = next bit to emit).
    int bits_in_buf; ///< Number of valid bits currently in `buffer`.
} bit_writer_t;

/// @brief Initialize a bit writer with a starting buffer capacity.
///
/// Allocates `initial_cap` bytes (clamped to a 256-byte minimum) and
/// zeros the bit accumulator. Traps on OOM.
static int bw_init(bit_writer_t *bw, size_t initial_cap) {
    bw->capacity = initial_cap > 256 ? initial_cap : 256;
    bw->data = (uint8_t *)malloc(bw->capacity);
    if (!bw->data) {
        rt_trap("rt_compress: memory allocation failed");
        bw->capacity = 0;
        bw->len = 0;
        bw->buffer = 0;
        bw->bits_in_buf = 0;
        return 0;
    }
    bw->len = 0;
    bw->buffer = 0;
    bw->bits_in_buf = 0;
    return 1;
}

/// @brief Grow the bit writer's byte buffer so `need` more bytes will fit.
///
/// Doubles the capacity (with overflow guard) or jumps to
/// `len + need + 256`, whichever is larger. Traps on OOM.
static int bw_ensure(bit_writer_t *bw, size_t need) {
    if (need > SIZE_MAX - bw->len) {
        rt_trap("Compress: output size overflow");
        return 0;
    }
    size_t required = bw->len + need;
    if (required <= bw->capacity)
        return 1;
    size_t new_cap = bw->capacity ? bw->capacity : 256;
    while (new_cap < required) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = required;
            break;
        }
        new_cap *= 2;
    }
    uint8_t *new_data = (uint8_t *)realloc(bw->data, new_cap);
    if (!new_data) {
        rt_trap("Compress: out of memory");
        return 0;
    }
    bw->data = new_data;
    bw->capacity = new_cap;
    return 1;
}

/// @brief Write n bits (LSB first)
static int bw_write(bit_writer_t *bw, uint32_t val, int n) {
    bw->buffer |= val << bw->bits_in_buf;
    bw->bits_in_buf += n;
    while (bw->bits_in_buf >= 8) {
        if (!bw_ensure(bw, 1))
            return 0;
        bw->data[bw->len++] = bw->buffer & 0xFF;
        bw->buffer >>= 8;
        bw->bits_in_buf -= 8;
    }
    return 1;
}

/// @brief Flush remaining bits (pad with zeros)
static int bw_flush(bit_writer_t *bw) {
    if (bw->bits_in_buf > 0) {
        if (!bw_ensure(bw, 1))
            return 0;
        bw->data[bw->len++] = bw->buffer & 0xFF;
        bw->buffer = 0;
        bw->bits_in_buf = 0;
    }
    return 1;
}

/// @brief Write raw bytes (must be byte-aligned)
static int bw_write_bytes(bit_writer_t *bw, const uint8_t *data, size_t len) {
    if (!bw_ensure(bw, len))
        return 0;
    memcpy(bw->data + bw->len, data, len);
    bw->len += len;
    return 1;
}

/// @brief Release the bit writer's heap buffer.
///
/// Should be called once the assembled bytes have been copied out
/// (see `deflate_data`). The struct itself is caller-owned.
static void bw_free(bit_writer_t *bw) {
    free(bw->data);
    bw->data = NULL;
    bw->capacity = 0;
    bw->len = 0;
}

//=============================================================================
// Huffman Tree
//=============================================================================

/// @brief A single Huffman code assignment (symbol → code + length).
typedef struct {
    uint16_t symbol; ///< Symbol value (literal, length code, or distance code).
    uint16_t code;   ///< Assigned bit pattern (MSB-aligned canonical code).
    uint8_t len;     ///< Code length in bits.
} huffman_code_t;

/// @brief Direct-mapped Huffman decode table for O(1) symbol lookup.
typedef struct {
    int max_code;      ///< Highest valid code index in the flat lookup table.
    uint16_t *symbols; ///< Flat table indexed by (bit-reversed) code prefix.
    int table_bits;    ///< Number of bits consumed per table lookup.
    size_t table_size; ///< Number of entries in `symbols`.
} huffman_tree_t;

/// @brief Build a canonical-Huffman lookup table from per-symbol code lengths.
///
/// Follows RFC 1951 §3.2.2: from the length histogram it derives the
/// starting code for each bit-length, assigns codes in symbol order,
/// then bit-reverses each code to LSB-first order (since DEFLATE reads
/// bits LSB-first). The resulting table is a 2^max_len array where each
/// slot holds a packed `(len << 12) | symbol` entry — one lookup decodes
/// any symbol using `tree->table_bits` bits peeked from the stream.
/// Short codes are replicated across all matching prefix slots so the
/// table is a direct-mapped decoder. Returns false on invalid input
/// (code length >15) or allocation failure.
static bool build_huffman_tree(huffman_tree_t *tree, const uint8_t *lengths, int num_codes) {
    // Count code lengths
    int bl_count[MAX_BITS + 1] = {0};
    for (int i = 0; i < num_codes; i++) {
        if (lengths[i] > MAX_BITS)
            return false;
        bl_count[lengths[i]]++;
    }
    bl_count[0] = 0;

    int left = 1;
    for (int bits = 1; bits <= MAX_BITS; bits++) {
        left <<= 1;
        left -= bl_count[bits];
        if (left < 0)
            return false;
    }

    // Calculate next code for each length
    uint16_t next_code[MAX_BITS + 1];
    uint16_t code = 0;
    for (int bits = 1; bits <= MAX_BITS; bits++) {
        code = (uint16_t)((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }

    // Determine table size
    int max_len = 0;
    for (int i = 0; i < num_codes; i++) {
        if (lengths[i] > max_len)
            max_len = lengths[i];
    }
    if (max_len == 0)
        max_len = 1;

    tree->table_bits = max_len; /* Support full 15-bit Huffman codes per RFC 1951 */
    tree->table_size = (size_t)1 << tree->table_bits;
    tree->max_code = num_codes;

    // Allocate symbol table
    tree->symbols = (uint16_t *)calloc(tree->table_size + num_codes * 2, sizeof(uint16_t));
    if (!tree->symbols)
        return false;

    // Build direct lookup table for short codes
    for (int i = 0; i < num_codes; i++) {
        int len = lengths[i];
        if (len == 0)
            continue;
        if (len < 0 || len > MAX_BITS)
            return false;

        uint16_t sym_code = next_code[len]++;

        if (len <= tree->table_bits) {
            // Direct table entry
            // Reverse the code bits for LSB-first reading
            uint16_t rev_code = 0;
            for (int b = 0; b < len; b++) {
                if (sym_code & (1 << b))
                    rev_code |= 1 << (len - 1 - b);
            }

            // Fill in all entries that match this prefix
            int fill = 1 << (tree->table_bits - len);
            for (int j = 0; j < fill; j++) {
                int idx = rev_code | (j << len);
                if ((size_t)idx >= tree->table_size) {
                    free(tree->symbols);
                    tree->symbols = NULL;
                    return false;
                }
                // Pack length and symbol: high bits = length, low bits = symbol
                tree->symbols[idx] = (uint16_t)((len << 12) | i);
            }
        }
    }

    return true;
}

/// @brief Decode a single Huffman-coded symbol from the bit stream.
///
/// Peeks `table_bits` bits, uses them as a direct index into the
/// precomputed lookup table, and consumes only the actual code length
/// stored in the high 4 bits of the entry. Returns -1 if the stream
/// runs dry or the entry is zero (invalid code — the canonical
/// construction leaves unassigned prefixes as zero entries).
static int decode_symbol(huffman_tree_t *tree, bit_reader_t *br) {
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
    if (br->error)
        return -1;
    return symbol;
}

/// @brief Release a Huffman tree's symbol table.
///
/// The tree struct itself is caller-owned; only the dynamically
/// allocated `symbols` buffer is freed.
static void free_huffman_tree(huffman_tree_t *tree) {
    free(tree->symbols);
    tree->symbols = NULL;
}

//=============================================================================
// Fixed Huffman Trees (for block type 1)
//=============================================================================

static huffman_tree_t fixed_lit_tree;
static huffman_tree_t fixed_dist_tree;
#ifdef _WIN32
static INIT_ONCE fixed_trees_once = INIT_ONCE_STATIC_INIT;
#else
static pthread_once_t fixed_trees_once = PTHREAD_ONCE_INIT;
#endif

/// @brief Lazily build the fixed-Huffman literal/distance trees.
///
/// RFC 1951 defines a single canonical set of fixed Huffman codes
/// used by block type 1. Building the lookup tables once and reusing
/// them across calls saves work but introduces a one-time race the
/// `fixed_trees_init` flag mitigates (single-threaded init is fine
/// since runtime startup is serialized — no concurrent inflate calls
/// can occur before the runtime is up).
static void init_fixed_trees_impl(void) {
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

    if (!build_huffman_tree(&fixed_lit_tree, lit_lengths, FIXED_LIT_CODES)) {
        rt_trap("Inflate: failed to initialize fixed literal tree");
        return;
    }

    // Fixed distance code lengths (all 5 bits)
    uint8_t dist_lengths[FIXED_DIST_CODES];
    for (int i = 0; i < FIXED_DIST_CODES; i++)
        dist_lengths[i] = 5;

    if (!build_huffman_tree(&fixed_dist_tree, dist_lengths, FIXED_DIST_CODES)) {
        rt_trap("Inflate: failed to initialize fixed distance tree");
        return;
    }
}

#ifdef _WIN32
/// @brief `InitOnce` callback that builds the fixed Huffman trees (Windows).
static BOOL CALLBACK init_fixed_trees_once_cb(PINIT_ONCE InitOnce,
                                              PVOID Parameter,
                                              PVOID *Context) {
    (void)InitOnce;
    (void)Parameter;
    (void)Context;
    init_fixed_trees_impl();
    return TRUE;
}
#endif

/// @brief Ensure the fixed Huffman trees are built exactly once (thread-safe).
static void init_fixed_trees(void) {
#ifdef _WIN32
    InitOnceExecuteOnce(&fixed_trees_once, init_fixed_trees_once_cb, NULL, NULL);
#else
    pthread_once(&fixed_trees_once, init_fixed_trees_impl);
#endif
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

/// @brief Growable byte buffer used to accumulate DEFLATE decompressor output.
typedef struct {
    uint8_t *data;     ///< Heap-allocated output buffer (grown by out_ensure).
    size_t len;        ///< Number of valid bytes written to `data`.
    size_t capacity;   ///< Allocated capacity of `data` in bytes.
    size_t max_output; ///< Maximum allowed decompressed bytes.
} output_buffer_t;

/// @brief Initialize a growable output buffer.
///
/// Allocates `initial_cap` bytes (clamped to a 256-byte minimum). Used
/// to accumulate inflated output, which can be larger than the input
/// by an arbitrary factor — `out_ensure` enforces a 256MB safety cap
/// to prevent decompression-bomb attacks. Traps on OOM.
static int out_init(output_buffer_t *out, size_t initial_cap, size_t max_output) {
    out->capacity = initial_cap > 256 ? initial_cap : 256;
    out->data = (uint8_t *)malloc(out->capacity);
    if (!out->data) {
        rt_trap("rt_compress: memory allocation failed");
        out->capacity = 0;
        out->len = 0;
        out->max_output = max_output;
        return 0;
    }
    out->len = 0;
    out->max_output = max_output;
    return 1;
}

/* S-20: Maximum decompressed output size (256 MB) to prevent decompression bombs */
#define INFLATE_DEFAULT_MAX_OUTPUT (256u * 1024u * 1024u)

/// @brief Grow the inflate output buffer enforcing the 256MB cap.
///
/// If the request would push total size past `INFLATE_MAX_OUTPUT`
/// (256MB), traps with a "decompression bomb" message — protects
/// against malicious inputs that inflate to many GB. Otherwise grows
/// geometrically (capped at the limit) and traps on OOM.
static int out_ensure(output_buffer_t *out, size_t need) {
    if (need > SIZE_MAX - out->len) {
        rt_trap("Inflate: output size overflow");
        return 0;
    }
    size_t required = out->len + need;
    if (required > out->max_output) {
        rt_trap("Inflate: decompressed output exceeds limit");
        return 0;
    }

    if (required <= out->capacity)
        return 1;
    size_t new_cap = out->capacity ? out->capacity : 256;
    while (new_cap < required) {
        if (new_cap > out->max_output / 2) {
            new_cap = required;
            break;
        }
        new_cap *= 2;
    }
    if (new_cap > out->max_output)
        new_cap = out->max_output;
    uint8_t *new_data = (uint8_t *)realloc(out->data, new_cap);
    if (!new_data) {
        rt_trap("Inflate: out of memory");
        return 0;
    }
    out->data = new_data;
    out->capacity = new_cap;
    return 1;
}

/// @brief Append a single literal byte to the output buffer.
static int out_byte(output_buffer_t *out, uint8_t b) {
    if (!out_ensure(out, 1))
        return 0;
    out->data[out->len++] = b;
    return 1;
}

/// @brief Copy `length` bytes from `distance` back in the output (LZ77 back-ref).
///
/// Implements the DEFLATE back-reference primitive: copy a run from
/// already-decompressed output. Crucially, copies must overlap correctly
/// when `length > distance` (e.g., RLE-style "AAAAA" expansion), so the
/// loop must walk byte-by-byte rather than `memcpy`. Caller has already
/// validated that `distance <= out->len`.
static int out_copy(output_buffer_t *out, int distance, int length) {
    if (!out_ensure(out, length))
        return 0;
    size_t src = out->len - distance;
    for (int i = 0; i < length; i++) {
        out->data[out->len++] = out->data[src++];
    }
    return 1;
}

static int out_append(output_buffer_t *out, const uint8_t *data, size_t len) {
    if (!out_ensure(out, len))
        return 0;
    if (len > 0)
        memcpy(out->data + out->len, data, len);
    out->len += len;
    return 1;
}

/// @brief Release the output buffer's heap allocation.
static void out_free(output_buffer_t *out) {
    free(out->data);
    out->data = NULL;
    out->capacity = 0;
    out->len = 0;
}

//=============================================================================
// DEFLATE Decompression
//=============================================================================

/// @brief Inflate a DEFLATE type-0 (stored/uncompressed) block.
///
/// Aligns the bit reader to a byte boundary, reads the 16-bit LEN and
/// its bitwise complement NLEN, verifies LEN ^ NLEN == 0xFFFF as
/// specified in RFC 1951 §3.2.4, then copies LEN bytes verbatim into
/// the output. Returns false on truncated data or the LEN/NLEN check
/// failing.
static bool inflate_stored(bit_reader_t *br, output_buffer_t *out) {
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

    if (!out_ensure(out, len))
        return false;
    memcpy(out->data + out->len, br->data + br->pos, len);
    out->len += len;
    br->pos += len;

    return true;
}

/// @brief Inflate a DEFLATE Huffman-coded block (type 1 fixed or type 2 dynamic).
///
/// Runs the literal/length/end-of-block symbol loop: symbols 0..255
/// are literal bytes written straight to output; 256 ends the block;
/// 257..285 are length codes that pair with a following distance code
/// to form an LZ77 back-reference. Extra bits follow length/distance
/// codes per RFC 1951 §3.2.5. Validates the back-reference distance
/// against the current output length before calling `out_copy` (which
/// handles overlapping copies for RLE-style expansion). The two tree
/// arguments are shared between fixed and dynamic callers — same loop,
/// different trees.
static bool inflate_huffman(bit_reader_t *br,
                            output_buffer_t *out,
                            huffman_tree_t *lit_tree,
                            huffman_tree_t *dist_tree) {
    while (true) {
        int sym = decode_symbol(lit_tree, br);
        if (sym < 0)
            return false;

        if (sym < 256) {
            // Literal byte
            if (!out_byte(out, (uint8_t)sym))
                return false;
        } else if (sym == 256) {
            // End of block
            return true;
        } else if (sym <= 285) {
            // Length code
            int len_idx = sym - 257;
            int length = length_base[len_idx];
            int extra = length_extra_bits[len_idx];
            if (extra > 0) {
                length += br_read(br, extra);
                if (br->error)
                    return false;
            }

            // Distance code
            int dist_sym = decode_symbol(dist_tree, br);
            if (dist_sym < 0 || dist_sym >= 30)
                return false;

            int distance = dist_base[dist_sym];
            extra = dist_extra_bits[dist_sym];
            if (extra > 0) {
                distance += br_read(br, extra);
                if (br->error)
                    return false;
            }

            // Validate distance
            if (distance > (int)out->len)
                return false;

            // Copy from output buffer
            if (!out_copy(out, distance, length))
                return false;
        } else {
            return false; // Invalid symbol
        }
    }
}

/// @brief Inflate a DEFLATE type-2 (dynamic Huffman) block.
///
/// Dynamic blocks ship their own Huffman trees, built via a three-level
/// scheme (RFC 1951 §3.2.7):
///   1. Read HLIT/HDIST/HCLEN counts, then HCLEN 3-bit lengths in the
///      permuted `code_length_order` sequence to build a meta-tree for
///      decoding code-length values.
///   2. Use the meta-tree to decode HLIT+HDIST code lengths, with
///      special symbols 16/17/18 encoding run-length-compressed
///      repeats of prior/zero lengths.
///   3. Split the decoded lengths into literal/length and distance
///      trees, then delegate the symbol loop to `inflate_huffman`.
/// Frees all transient trees on every exit path — traps aren't used
/// here because the caller may need to fall back cleanly.
static bool inflate_dynamic(bit_reader_t *br, output_buffer_t *out) {
    // Read header
    int hlit = br_read(br, 5) + 257; // Number of literal/length codes
    int hdist = br_read(br, 5) + 1;  // Number of distance codes
    int hclen = br_read(br, 4) + 4;  // Number of code length codes
    if (br->error)
        return false;

    if (hlit > MAX_LIT_CODES || hdist > MAX_DIST_CODES)
        return false;

    // Read code length code lengths
    uint8_t cl_lengths[MAX_CODE_LEN_CODES] = {0};
    for (int i = 0; i < hclen; i++) {
        cl_lengths[code_length_order[i]] = (uint8_t)br_read(br, 3);
        if (br->error)
            return false;
    }

    // Build code length tree
    huffman_tree_t cl_tree = {0};
    if (!build_huffman_tree(&cl_tree, cl_lengths, MAX_CODE_LEN_CODES))
        return false;

    // Decode literal/length and distance code lengths
    uint8_t lengths[MAX_LIT_CODES + MAX_DIST_CODES] = {0};
    int total_codes = hlit + hdist;
    int i = 0;

    while (i < total_codes) {
        int sym = decode_symbol(&cl_tree, br);
        if (sym < 0) {
            free_huffman_tree(&cl_tree);
            return false;
        }

        if (sym < 16) {
            // Literal length
            lengths[i++] = (uint8_t)sym;
        } else if (sym == 16) {
            // Repeat previous
            if (i == 0) {
                free_huffman_tree(&cl_tree);
                return false;
            }
            int repeat = br_read(br, 2) + 3;
            if (br->error) {
                free_huffman_tree(&cl_tree);
                return false;
            }
            uint8_t prev = lengths[i - 1];
            if (repeat > total_codes - i) {
                free_huffman_tree(&cl_tree);
                return false;
            }
            while (repeat-- > 0 && i < total_codes)
                lengths[i++] = prev;
        } else if (sym == 17) {
            // Repeat 0, 3-10 times
            int repeat = br_read(br, 3) + 3;
            if (br->error) {
                free_huffman_tree(&cl_tree);
                return false;
            }
            if (repeat > total_codes - i) {
                free_huffman_tree(&cl_tree);
                return false;
            }
            while (repeat-- > 0 && i < total_codes)
                lengths[i++] = 0;
        } else if (sym == 18) {
            // Repeat 0, 11-138 times
            int repeat = br_read(br, 7) + 11;
            if (br->error) {
                free_huffman_tree(&cl_tree);
                return false;
            }
            if (repeat > total_codes - i) {
                free_huffman_tree(&cl_tree);
                return false;
            }
            while (repeat-- > 0 && i < total_codes)
                lengths[i++] = 0;
        } else {
            free_huffman_tree(&cl_tree);
            return false;
        }
    }

    free_huffman_tree(&cl_tree);

    // Build literal/length and distance trees
    huffman_tree_t lit_tree = {0}, dist_tree = {0};

    if (hlit <= 256 || lengths[256] == 0)
        return false;

    if (!build_huffman_tree(&lit_tree, lengths, hlit))
        return false;

    if (!build_huffman_tree(&dist_tree, lengths + hlit, hdist)) {
        free_huffman_tree(&lit_tree);
        return false;
    }

    // Decode block
    bool ok = inflate_huffman(br, out, &lit_tree, &dist_tree);

    free_huffman_tree(&lit_tree);
    free_huffman_tree(&dist_tree);

    return ok;
}

/// @brief Top-level DEFLATE decompression driver.
///
/// Walks a stream of DEFLATE blocks until BFINAL=1 is seen. Each block
/// is decoded via `inflate_stored`, `inflate_huffman` (with shared
/// fixed trees), or `inflate_dynamic` based on its 2-bit type. Traps
/// on truncated input, invalid block types, or output exceeding the
/// 256MB decompression-bomb limit. After the final block, any residual
/// non-zero bits in the byte-aligned tail are a stream corruption —
/// not just padding — and also trap.
static uint8_t *inflate_raw_limited_ex(const uint8_t *data,
                                       size_t len,
                                       size_t max_output,
                                       size_t *out_len,
                                       size_t *consumed_bytes,
                                       bool allow_trailing) {
    init_fixed_trees();

    bit_reader_t br;
    br_init(&br, data, len);

    output_buffer_t out;
    size_t estimate = len > max_output / 4 ? max_output : len * 4;
    if (!out_init(&out, estimate, max_output))
        return NULL; // Estimate 4x expansion without overflowing.

    bool last_block = false;

    while (!last_block) {
        if (!br_has_data(&br)) {
            out_free(&out);
            rt_trap("Inflate: unexpected end of data");
        }

        // Read block header
        last_block = br_read(&br, 1);
        int block_type = br_read(&br, 2);
        if (br.error) {
            out_free(&out);
            rt_trap("Inflate: unexpected end of data");
        }

        bool ok = false;
        switch (block_type) {
            case RT_HUFFMAN_STORED:
                ok = inflate_stored(&br, &out);
                break;
            case RT_HUFFMAN_FIXED:
                ok = inflate_huffman(&br, &out, &fixed_lit_tree, &fixed_dist_tree);
                break;
            case RT_HUFFMAN_DYNAMIC:
                ok = inflate_dynamic(&br, &out);
                break;
            default:
                out_free(&out);
                rt_trap("Inflate: invalid block type");
        }

        if (!ok) {
            out_free(&out);
            rt_trap("Inflate: invalid compressed data");
        }
    }

    size_t consumed_bits = br.pos * 8u - (size_t)br.bits_in_buf;
    int byte_padding_bits = (int)((8u - (consumed_bits & 7u)) & 7u);
    if (byte_padding_bits > 0) {
        uint32_t padding_mask = (1U << byte_padding_bits) - 1U;
        if ((br.buffer & padding_mask) != 0) {
            out_free(&out);
            rt_trap("Inflate: trailing data after final block");
        }
    }

    size_t compressed_bytes = (consumed_bits + 7u) / 8u;
    if (compressed_bytes > len) {
        out_free(&out);
        rt_trap("Inflate: invalid compressed data");
    }
    if (consumed_bytes)
        *consumed_bytes = compressed_bytes;

    if (!allow_trailing && compressed_bytes < len) {
        out_free(&out);
        rt_trap("Inflate: trailing data after final block");
    }

    if (!allow_trailing && br.bits_in_buf > 0) {
        int padding_bits = br.bits_in_buf > 7 ? 7 : br.bits_in_buf;
        uint32_t padding_mask = (1U << padding_bits) - 1U;
        if ((br.buffer & padding_mask) != 0 || br.bits_in_buf > 7) {
            out_free(&out);
            rt_trap("Inflate: trailing data after final block");
        }
    }

    if (out_len)
        *out_len = out.len;
    return out.data;
}

static void *inflate_data_limited_ex(const uint8_t *data,
                                     size_t len,
                                     size_t max_output,
                                     size_t *consumed_bytes,
                                     bool allow_trailing) {
    size_t raw_len = 0;
    uint8_t *raw =
        inflate_raw_limited_ex(data, len, max_output, &raw_len, consumed_bytes, allow_trailing);
    if (!raw)
        return NULL;
    void *result = rt_bytes_new(raw_len);
    if (!result) {
        free(raw);
        return NULL;
    }
    if (raw_len > 0)
        memcpy(bytes_data(result), raw, raw_len);
    free(raw);
    return result;
}

static void *inflate_data_limited(const uint8_t *data, size_t len, size_t max_output) {
    return inflate_data_limited_ex(data, len, max_output, NULL, false);
}

static void *inflate_data(const uint8_t *data, size_t len) {
    return inflate_data_limited(data, len, INFLATE_DEFAULT_MAX_OUTPUT);
}

//=============================================================================
// DEFLATE Compression
//=============================================================================

/// @brief Hash-chain tables for the LZ77 sliding-window match finder.
typedef struct {
    int *head;      ///< Maps each 3-byte-hash to the most recent matching position.
    int *prev;      ///< Back-chain linking older positions with the same hash.
    int window_pos; ///< Current write position within the 32KB sliding window.
} lz77_state_t;

#define HASH_BITS 15
#define HASH_SIZE (1 << HASH_BITS)
#define HASH_MASK (HASH_SIZE - 1)
#define NIL (-1)

/// @brief Compute hash for 3 bytes
static inline int compute_hash(const uint8_t *data) {
    return ((data[0] << 10) ^ (data[1] << 5) ^ data[2]) & HASH_MASK;
}

/// @brief Allocate the LZ77 hash-chain tables for match finding.
///
/// Two arrays back the sliding-window match search: `head` (HASH_SIZE
/// slots) maps a 3-byte-prefix hash to the most recent position seen,
/// and `prev` (WINDOW_SIZE slots) chains older positions sharing the
/// same hash. NIL (-1) marks unused slots. Together they form a
/// hash-chained lookup that `find_match` walks to locate the longest
/// back-reference. Traps on OOM.
static bool lz77_init(lz77_state_t *lz) {
    lz->head = (int *)malloc(HASH_SIZE * sizeof(int));
    lz->prev = (int *)malloc(WINDOW_SIZE * sizeof(int));
    if (!lz->head || !lz->prev) {
        free(lz->head);
        free(lz->prev);
        lz->head = NULL;
        lz->prev = NULL;
        rt_trap("Compress: memory allocation failed");
        return false;
    }
    for (int i = 0; i < HASH_SIZE; i++)
        lz->head[i] = NIL;
    for (int i = 0; i < WINDOW_SIZE; i++)
        lz->prev[i] = NIL;
    lz->window_pos = 0;
    return true;
}

/// @brief Release LZ77 hash-chain memory (head + prev arrays).
static void lz77_free(lz77_state_t *lz) {
    free(lz->head);
    free(lz->prev);
}

/// @brief Find the longest LZ77 back-reference match at `pos`.
///
/// Hashes the 3 bytes at `pos`, walks the hash chain of prior positions
/// with the same prefix, and returns the longest match found (up to
/// `MAX_MATCH_LEN = 258`). `max_chain` caps the walk depth — higher
/// compression levels use longer chains (`4 << level`) for better
/// matches at the cost of speed. Refuses matches with distance
/// exceeding 32KB or below `MIN_MATCH_LEN = 3` (short matches cost
/// more bits than the literals they replace).
static int find_match(
    lz77_state_t *lz, const uint8_t *data, size_t pos, size_t len, int max_chain, int *match_dist) {
    if (pos + MIN_MATCH_LEN > len)
        return 0;

    int hash = compute_hash(data + pos);
    int chain_pos = lz->head[hash];
    int best_len = MIN_MATCH_LEN - 1;
    int best_dist = 0;

    int limit = (int)pos - MAX_DISTANCE;
    if (limit < 0)
        limit = 0;

    while (chain_pos >= limit && max_chain-- > 0) {
        // Check match
        int match_len = 0;
        size_t a = pos, b = chain_pos;
        size_t max_len = len - pos;
        if (max_len > MAX_MATCH_LEN)
            max_len = MAX_MATCH_LEN;

        while (match_len < (int)max_len && data[a] == data[b]) {
            match_len++;
            a++;
            b++;
        }

        if (match_len > best_len) {
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

/// @brief Insert `pos` at the head of the hash chain for its 3-byte prefix.
///
/// Maintains the `prev[pos] = head[hash]; head[hash] = pos` invariant
/// that lets `find_match` walk back through older occurrences. Called
/// after emitting a literal or (per byte) inside a match so later
/// positions can find it.
static void update_hash(lz77_state_t *lz, const uint8_t *data, size_t pos) {
    int hash = compute_hash(data + pos);
    lz->prev[pos & WINDOW_MASK] = lz->head[hash];
    lz->head[hash] = (int)pos;
}

/// @brief Map an LZ77 match length (3..258) to the DEFLATE length code (257..285).
///
/// Scans the `length_base` table for the first entry whose next slot
/// exceeds `length`, which identifies the code whose range covers it.
/// Lengths of exactly 258 are encoded as code 285 (the sentinel final
/// entry, which has zero extra bits).
static int get_length_code(int length) {
    for (int i = 0; i < 29; i++) {
        if (i == 28)
            return 285;
        if (length < length_base[i + 1])
            return 257 + i;
    }
    return 285;
}

/// @brief Map a back-reference distance (1..32768) to the DEFLATE distance code (0..29).
///
/// Linear scan of `dist_base`; the extra-bits table on the matching
/// code handles the offset within each code's range. 30 distance
/// codes cover the full 32KB window.
static int get_dist_code(int dist) {
    for (int i = 0; i < 30; i++) {
        if (i == 29 || dist < dist_base[i + 1])
            return i;
    }
    return 29;
}

/// @brief Emit a canonical Huffman code with DEFLATE's LSB-first bit order.
///
/// Canonical Huffman codes are conceptually MSB-first, but DEFLATE's
/// bit stream reads LSB-first — so the code must be bit-reversed
/// before `bw_write` emits it LSB-first and the decoder's
/// `decode_symbol` reassembles the original MSB-first prefix.
static int write_code(bit_writer_t *bw, uint16_t code, int len) {
    // Reverse the code bits
    uint16_t rev = 0;
    for (int i = 0; i < len; i++) {
        if (code & (1 << i))
            rev |= 1 << (len - 1 - i);
    }
    return bw_write(bw, rev, len);
}

/// @brief Emit a sequence of DEFLATE type-0 (stored/uncompressed) blocks.
///
/// Used at level 1 or for very small payloads where Huffman overhead
/// would exceed any savings. Each block carries up to 65535 bytes (LEN
/// is a u16), so large inputs are chunked across multiple back-to-back
/// stored blocks with BFINAL=0 until the last one. An empty input
/// still needs one final block so the stream is well-formed — handled
/// as a special case up front.
static int deflate_stored(bit_writer_t *bw, const uint8_t *data, size_t len) {
    // Handle empty data - still need a final block
    if (len == 0) {
        // Block header
        if (!bw_write(bw, 1, 1) || !bw_write(bw, 0, 2))
            return 0;

        // Align to byte
        if (!bw_flush(bw))
            return 0;

        // LEN = 0 and NLEN = 0xFFFF
        if (!bw_write(bw, 0x00, 8) || !bw_write(bw, 0x00, 8) || !bw_write(bw, 0xFF, 8) ||
            !bw_write(bw, 0xFF, 8))
            return 0;
        return 1;
    }

    size_t pos = 0;
    while (pos < len) {
        size_t block_len = len - pos;
        if (block_len > 65535)
            block_len = 65535;

        bool last = (pos + block_len >= len);

        // Block header
        if (!bw_write(bw, last ? 1 : 0, 1) || !bw_write(bw, 0, 2))
            return 0;

        // Align to byte
        if (!bw_flush(bw))
            return 0;

        // LEN and NLEN
        uint16_t nlen = ~(uint16_t)block_len;
        if (!bw_write(bw, block_len & 0xFF, 8) || !bw_write(bw, (block_len >> 8) & 0xFF, 8) ||
            !bw_write(bw, nlen & 0xFF, 8) || !bw_write(bw, (nlen >> 8) & 0xFF, 8))
            return 0;

        // Data
        if (!bw_flush(bw) || !bw_write_bytes(bw, data + pos, block_len))
            return 0;

        pos += block_len;
    }
    return 1;
}

/// @brief Emit a single DEFLATE type-1 (fixed Huffman) block with LZ77 matching.
///
/// At each input position, queries `find_match` for the longest
/// back-reference. If the match meets the minimum length, emits a
/// length-code + distance-code pair (with their extra-bits suffixes)
/// using the canonical fixed Huffman assignment — literals 0..143
/// use 8-bit codes 0x30..0xBF, 144..255 use 9-bit codes, end-of-block
/// is 7 bits, etc. Otherwise falls back to a literal. Hash chain is
/// advanced one byte per input regardless so future positions can
/// still find matches even inside earlier matches.
static int deflate_fixed(bit_writer_t *bw, const uint8_t *data, size_t len, int level) {
    init_fixed_trees();

    lz77_state_t lz;
    if (!lz77_init(&lz))
        return 0;

    int max_chain = 4 << level;

    // Block header
    if (!bw_write(bw, 1, 1) || !bw_write(bw, 1, 2)) { // BFINAL=1, BTYPE=fixed
        lz77_free(&lz);
        return 0;
    }

    // Fixed literal/length codes
    // 0-143: 8 bits, 144-255: 9 bits, 256-279: 7 bits, 280-287: 8 bits

    size_t pos = 0;
    while (pos < len) {
        int match_dist = 0;
        int match_len = 0;

        if (pos + MIN_MATCH_LEN <= len) {
            match_len = find_match(&lz, data, pos, len, max_chain, &match_dist);
        }

        if (match_len >= MIN_MATCH_LEN) {
            // Write length code
            int len_code = get_length_code(match_len);
            int len_idx = len_code - 257;

            // Encode length code with fixed Huffman
            if (len_code <= 279) {
                // 7 bits: codes 256-279 map to 0000000-0010111
                if (!write_code(bw, (uint16_t)(len_code - 256), 7)) {
                    lz77_free(&lz);
                    return 0;
                }
            } else {
                // 8 bits: codes 280-287 map to 11000000-11000111
                if (!write_code(bw, (uint16_t)(0xC0 + (len_code - 280)), 8)) {
                    lz77_free(&lz);
                    return 0;
                }
            }

            // Extra length bits
            if (length_extra_bits[len_idx] > 0) {
                if (!bw_write(bw, match_len - length_base[len_idx], length_extra_bits[len_idx])) {
                    lz77_free(&lz);
                    return 0;
                }
            }

            // Distance code (5 bits for fixed)
            int dist_code = get_dist_code(match_dist);
            if (!write_code(bw, (uint16_t)dist_code, 5)) {
                lz77_free(&lz);
                return 0;
            }

            // Extra distance bits
            if (dist_extra_bits[dist_code] > 0) {
                if (!bw_write(bw, match_dist - dist_base[dist_code], dist_extra_bits[dist_code])) {
                    lz77_free(&lz);
                    return 0;
                }
            }

            // Update hash for all matched bytes
            for (int i = 0; i < match_len; i++) {
                if (pos + i + MIN_MATCH_LEN <= len)
                    update_hash(&lz, data, pos + i);
            }
            pos += match_len;
        } else {
            // Literal byte
            uint8_t lit = data[pos];
            if (lit <= 143) {
                // 8 bits: 00110000-10111111
                if (!write_code(bw, 0x30 + lit, 8)) {
                    lz77_free(&lz);
                    return 0;
                }
            } else {
                // 9 bits: 110010000-111111111
                if (!write_code(bw, 0x190 + (lit - 144), 9)) {
                    lz77_free(&lz);
                    return 0;
                }
            }

            if (pos + MIN_MATCH_LEN <= len)
                update_hash(&lz, data, pos);
            pos++;
        }
    }

    // End of block (code 256, 7 bits)
    if (!write_code(bw, 0, 7)) {
        lz77_free(&lz);
        return 0;
    }

    lz77_free(&lz);
    return 1;
}

/// @brief Top-level DEFLATE compression driver.
///
/// Picks the block strategy: inputs of ≤64 bytes or level=1 use stored
/// blocks (no Huffman savings worth the overhead); everything else uses
/// a single fixed-Huffman block with LZ77. Dynamic Huffman (block type
/// 2) is not currently emitted — decoder supports it for
/// interoperability, but the encoder keeps encoding simple. Level is
/// clamped to [1..9].
static void *deflate_data(const uint8_t *data, size_t len, int level) {
    if (level < DEFLATE_MIN_LEVEL)
        level = DEFLATE_MIN_LEVEL;
    if (level > DEFLATE_MAX_LEVEL)
        level = DEFLATE_MAX_LEVEL;

    bit_writer_t bw;
    if (!bw_init(&bw, len))
        return NULL;

    // For small data or level 1, use stored blocks
    // For larger data, use fixed Huffman
    if (len <= 64 || level == 1) {
        if (!deflate_stored(&bw, data, len)) {
            bw_free(&bw);
            return NULL;
        }
    } else {
        if (!deflate_fixed(&bw, data, len, level)) {
            bw_free(&bw);
            return rt_bytes_new(0);
        }
    }

    if (!bw_flush(&bw)) {
        bw_free(&bw);
        return NULL;
    }

    // Create output Bytes object
    void *result = rt_bytes_new(bw.len);
    if (!result) {
        bw_free(&bw);
        return NULL;
    }
    memcpy(bytes_data(result), bw.data, bw.len);
    bw_free(&bw);

    return result;
}

//=============================================================================
// GZIP Wrapper
//=============================================================================

/// @brief Wrap a DEFLATE payload in the RFC 1952 GZIP envelope.
///
/// Layout: 10-byte header (magic 0x1F 0x8B, method=8 deflate, flags=0,
/// zeroed MTIME, XFL, OS=0xFF unknown) + DEFLATE payload + 8-byte
/// trailer (CRC32 of the original uncompressed data + ISIZE =
/// uncompressed length mod 2^32, both little-endian). CRC32 is computed
/// over the raw input, not the compressed bytes.
static void *gzip_data(const uint8_t *data, size_t len, int level) {
    // First compress with DEFLATE
    void *deflated = deflate_data(data, len, level);
    if (!deflated)
        return NULL;
    size_t deflated_len = bytes_len(deflated);
    uint8_t *deflated_data = bytes_data(deflated);

    // Calculate CRC32
    uint32_t crc = rt_crc32_compute(data, len);

    // Create output: header (10) + deflated + trailer (8)
    if (deflated_len > SIZE_MAX - 18) {
        compress_release_temp_object(deflated);
        rt_trap("Gzip: output size overflow");
        return NULL;
    }
    size_t total_len = 10 + deflated_len + 8;
    void *result = rt_bytes_new(total_len);
    if (!result) {
        compress_release_temp_object(deflated);
        return NULL;
    }
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

    compress_release_temp_object(deflated);
    return result;
}

/// @brief Decode one GZIP-wrapped DEFLATE member per RFC 1952.
///
/// Validates magic bytes (0x1F 0x8B), checks method=deflate, then walks
/// the optional header fields selected by the flags byte: FEXTRA (2-byte
/// length + payload), FNAME (NUL-terminated), FCOMMENT (NUL-terminated),
/// FHCRC (2-byte CRC16 of the header so far). After the DEFLATE stream
/// ends, validates the 8-byte trailer (CRC32 of inflated data + ISIZE)
/// against the actual inflated result. Traps on any mismatch.
static void *gunzip_member_data(const uint8_t *data, size_t len, size_t *member_len) {
    if (len < 18) {
        rt_trap("Gunzip: data too short");
        return NULL;
    }

    // Verify magic
    if (data[0] != 0x1F || data[1] != 0x8B) {
        rt_trap("Gunzip: invalid magic number");
        return NULL;
    }

    // Check compression method
    if (data[2] != 0x08) {
        rt_trap("Gunzip: unsupported compression method");
        return NULL;
    }

    uint8_t flags = data[3];
    if (flags & 0xE0) {
        rt_trap("Gunzip: reserved flags set");
        return NULL;
    }

    // Skip header
    size_t pos = 10;

    // Skip FEXTRA
    if (flags & 0x04) {
        if (pos + 2 > len) {
            rt_trap("Gunzip: truncated header");
            return NULL;
        }
        size_t xlen = data[pos] | (data[pos + 1] << 8);
        if (xlen > len - pos - 2) {
            rt_trap("Gunzip: truncated extra field");
            return NULL;
        }
        pos += 2 + xlen;
    }

    // Skip FNAME
    if (flags & 0x08) {
        while (pos < len && data[pos] != 0)
            pos++;
        if (pos >= len) {
            rt_trap("Gunzip: truncated filename");
            return NULL;
        }
        pos++; // Skip null terminator
    }

    // Skip FCOMMENT
    if (flags & 0x10) {
        while (pos < len && data[pos] != 0)
            pos++;
        if (pos >= len) {
            rt_trap("Gunzip: truncated comment");
            return NULL;
        }
        pos++;
    }

    // Skip FHCRC
    if (flags & 0x02) {
        if (pos + 2 > len) {
            rt_trap("Gunzip: truncated header CRC");
            return NULL;
        }
        uint16_t expected_hcrc = (uint16_t)(data[pos] | (data[pos + 1] << 8));
        uint16_t actual_hcrc = (uint16_t)(rt_crc32_compute(data, pos) & 0xFFFFu);
        if (actual_hcrc != expected_hcrc) {
            rt_trap("Gunzip: header CRC mismatch");
            return NULL;
        }
        pos += 2;
    }

    if (pos >= len - 8) {
        rt_trap("Gunzip: truncated data");
        return NULL;
    }

    size_t deflate_consumed = 0;
    void *result = inflate_data_limited_ex(
        data + pos, len - pos, INFLATE_DEFAULT_MAX_OUTPUT, &deflate_consumed, true);

    if (deflate_consumed > len - pos || len - pos - deflate_consumed < 8) {
        compress_release_temp_object(result);
        rt_trap("Gunzip: truncated trailer");
        return NULL;
    }

    // Extract trailer at the byte immediately after the DEFLATE member.
    size_t trailer_pos = pos + deflate_consumed;
    uint32_t expected_crc = data[trailer_pos] | (data[trailer_pos + 1] << 8) |
                            (data[trailer_pos + 2] << 16) | (data[trailer_pos + 3] << 24);
    uint32_t expected_size = data[trailer_pos + 4] | (data[trailer_pos + 5] << 8) |
                             (data[trailer_pos + 6] << 16) | (data[trailer_pos + 7] << 24);

    // Verify CRC
    uint32_t actual_crc = rt_crc32_compute(bytes_data(result), bytes_len(result));
    if (actual_crc != expected_crc) {
        compress_release_temp_object(result);
        rt_trap("Gunzip: CRC mismatch");
        return NULL;
    }

    // Verify size (mod 2^32)
    if ((bytes_len(result) & 0xFFFFFFFF) != expected_size) {
        compress_release_temp_object(result);
        rt_trap("Gunzip: size mismatch");
        return NULL;
    }

    if (member_len)
        *member_len = trailer_pos + 8;
    return result;
}

/// @brief Decode a possibly concatenated GZIP stream.
static void *gunzip_data(const uint8_t *data, size_t len) {
    if (len < 18) {
        rt_trap("Gunzip: data too short");
        return NULL;
    }

    output_buffer_t out;
    size_t estimate = len > INFLATE_DEFAULT_MAX_OUTPUT / 2 ? INFLATE_DEFAULT_MAX_OUTPUT : len * 2;
    if (!out_init(&out, estimate, INFLATE_DEFAULT_MAX_OUTPUT))
        return NULL;

    size_t pos = 0;
    while (pos < len) {
        size_t member_len = 0;
        void *member = gunzip_member_data(data + pos, len - pos, &member_len);
        if (member_len == 0 || member_len > len - pos) {
            compress_release_temp_object(member);
            out_free(&out);
            rt_trap("Gunzip: invalid member length");
            return NULL;
        }
        if (!out_append(&out, bytes_data(member), (size_t)bytes_len(member))) {
            compress_release_temp_object(member);
            out_free(&out);
            return NULL;
        }
        compress_release_temp_object(member);
        pos += member_len;
    }

    void *result = rt_bytes_new(out.len);
    if (!result) {
        out_free(&out);
        return NULL;
    }
    if (out.len > 0)
        memcpy(bytes_data(result), out.data, out.len);
    out_free(&out);
    return result;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief `Compress.Deflate(data)` — RFC 1951 raw DEFLATE at default level (6).
///
/// Produces a raw DEFLATE bitstream with no GZIP/zlib wrapper. Use
/// `Gzip` if you need a `.gz`-compatible output. Traps on NULL input.
///
/// @param data Source `rt_bytes`.
/// @return Owned `rt_bytes` containing the compressed stream.
void *rt_compress_deflate(void *data) {
    if (!data) {
        rt_trap("Compress.Deflate: data is null");
        return NULL;
    }
    return deflate_data(bytes_data(data), bytes_len(data), DEFLATE_DEFAULT_LEVEL);
}

/// @brief `Compress.DeflateLvl(data, level)` — explicit-level DEFLATE.
///
/// `level` is 1 (fastest, stored blocks) through 9 (best compression).
/// Levels 2-9 currently all use the same fixed-Huffman path; the level
/// only changes the LZ77 hash-chain depth (`max_chain = 4 << level`).
/// Traps on NULL data or out-of-range level.
///
/// @param data  Source `rt_bytes`.
/// @param level Compression level (1..9).
/// @return Owned `rt_bytes` containing the compressed stream.
void *rt_compress_deflate_lvl(void *data, int64_t level) {
    if (!data) {
        rt_trap("Compress.DeflateLvl: data is null");
        return NULL;
    }
    if (level < DEFLATE_MIN_LEVEL || level > DEFLATE_MAX_LEVEL) {
        rt_trap("Compress.DeflateLvl: level must be 1-9");
        return NULL;
    }
    return deflate_data(bytes_data(data), bytes_len(data), (int)level);
}

/// @brief `Compress.Inflate(data)` — decompress a raw DEFLATE stream.
///
/// Accepts only the bare RFC 1951 bitstream — see `Gunzip` for
/// GZIP-wrapped data. Traps on NULL input or any structural error
/// in the compressed stream.
///
/// @param data Compressed `rt_bytes`.
/// @return Owned `rt_bytes` containing the original payload.
void *rt_compress_inflate(void *data) {
    if (!data) {
        rt_trap("Compress.Inflate: data is null");
        return NULL;
    }
    return inflate_data(bytes_data(data), bytes_len(data));
}

void *rt_compress_inflate_limit(void *data, int64_t max_output) {
    if (!data) {
        rt_trap("Compress.InflateLimit: data is null");
        return NULL;
    }
    if (max_output < 0) {
        rt_trap("Compress.InflateLimit: max output is negative");
        return NULL;
    }
    return inflate_data_limited(bytes_data(data), bytes_len(data), (size_t)max_output);
}

int rt_compress_inflate_raw(
    const uint8_t *data, size_t len, size_t max_output, uint8_t **out_data, size_t *out_len) {
    jmp_buf recovery;
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    int ok = 0;
    if (out_data)
        *out_data = NULL;
    if (out_len)
        *out_len = 0;
    if (!data || !out_data || !out_len)
        return 0;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        raw = inflate_raw_limited_ex(data, len, max_output, &raw_len, NULL, false);
        ok = raw != NULL || raw_len == 0;
    }
    rt_trap_clear_recovery();
    if (!ok) {
        free(raw);
        return 0;
    }
    *out_data = raw;
    *out_len = raw_len;
    return 1;
}

/// @brief `Compress.Gzip(data)` — RFC 1952 GZIP wrap at default level.
///
/// Emits a 10-byte GZIP header + DEFLATE payload + 8-byte trailer
/// (CRC32 + uncompressed size mod 2^32). Suitable for writing `.gz`
/// files or HTTP `Content-Encoding: gzip`.
///
/// @param data Source `rt_bytes`.
/// @return Owned `rt_bytes` containing the GZIP-wrapped stream.
void *rt_compress_gzip(void *data) {
    if (!data) {
        rt_trap("Compress.Gzip: data is null");
        return NULL;
    }
    return gzip_data(bytes_data(data), bytes_len(data), DEFLATE_DEFAULT_LEVEL);
}

/// @brief `Compress.GzipLvl(data, level)` — explicit-level GZIP wrap.
///
/// Like `Gzip` but uses the supplied compression level (1..9) for the
/// inner DEFLATE pass.
///
/// @param data  Source `rt_bytes`.
/// @param level Compression level (1..9).
/// @return Owned `rt_bytes` containing the GZIP-wrapped stream.
void *rt_compress_gzip_lvl(void *data, int64_t level) {
    if (!data) {
        rt_trap("Compress.GzipLvl: data is null");
        return NULL;
    }
    if (level < DEFLATE_MIN_LEVEL || level > DEFLATE_MAX_LEVEL) {
        rt_trap("Compress.GzipLvl: level must be 1-9");
        return NULL;
    }
    return gzip_data(bytes_data(data), bytes_len(data), (int)level);
}

/// @brief `Compress.Gunzip(data)` — decode a GZIP-wrapped DEFLATE stream.
///
/// Validates each member's magic bytes, optional FEXTRA/FNAME/FCOMMENT/FHCRC
/// header fields, and trailing CRC32 + size. Concatenated GZIP members are
/// inflated in order and returned as one Bytes payload.
///
/// @param data GZIP-wrapped `rt_bytes`.
/// @return Owned `rt_bytes` containing the inflated payload.
void *rt_compress_gunzip(void *data) {
    if (!data) {
        rt_trap("Compress.Gunzip: data is null");
        return NULL;
    }
    return gunzip_data(bytes_data(data), bytes_len(data));
}

/// @brief `Compress.DeflateStr(text)` — string convenience wrapper.
///
/// Encodes `text` as UTF-8, deflates at the default level, and
/// returns the bytes. Useful for compressing JSON / log payloads.
///
/// @param text Source string.
/// @return Owned `rt_bytes` of the compressed stream.
void *rt_compress_deflate_str(rt_string text) {
    if (!text) {
        rt_trap("Compress.DeflateStr: text is null");
        return NULL;
    }
    void *bytes = rt_bytes_from_str(text);
    if (!bytes)
        return NULL;
    void *result = deflate_data(bytes_data(bytes), bytes_len(bytes), DEFLATE_DEFAULT_LEVEL);
    compress_release_temp_object(bytes);
    return result;
}

/// @brief `Compress.InflateStr(data)` — inflate then decode as UTF-8 string.
///
/// Use only when the original payload is known to be valid UTF-8;
/// `rt_bytes_to_str` validates and traps on malformed sequences.
///
/// @param data Compressed `rt_bytes`.
/// @return Owned `rt_string` with the inflated text.
rt_string rt_compress_inflate_str(void *data) {
    if (!data) {
        rt_trap("Compress.InflateStr: data is null");
        return rt_str_empty();
    }
    void *result = inflate_data(bytes_data(data), bytes_len(data));
    if (!result)
        return rt_str_empty();
    rt_string str = rt_bytes_to_str(result);
    compress_release_temp_object(result);
    return str;
}

/// @brief `Compress.GzipStr(text)` — string convenience wrapper around Gzip.
///
/// @param text Source string.
/// @return Owned `rt_bytes` of the GZIP-wrapped stream.
void *rt_compress_gzip_str(rt_string text) {
    if (!text) {
        rt_trap("Compress.GzipStr: text is null");
        return NULL;
    }
    void *bytes = rt_bytes_from_str(text);
    if (!bytes)
        return NULL;
    void *result = gzip_data(bytes_data(bytes), bytes_len(bytes), DEFLATE_DEFAULT_LEVEL);
    compress_release_temp_object(bytes);
    return result;
}

/// @brief `Compress.GunzipStr(data)` — gunzip and decode as UTF-8 string.
///
/// @param data GZIP-wrapped `rt_bytes`.
/// @return Owned `rt_string` with the inflated text.
rt_string rt_compress_gunzip_str(void *data) {
    if (!data) {
        rt_trap("Compress.GunzipStr: data is null");
        return rt_str_empty();
    }
    void *result = gunzip_data(bytes_data(data), bytes_len(data));
    if (!result)
        return rt_str_empty();
    rt_string str = rt_bytes_to_str(result);
    compress_release_temp_object(result);
    return str;
}
