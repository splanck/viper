//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_deflate.c
// Purpose: DEFLATE compression (RFC 1951) + gzip framing: LZ77 match finder,
//   fixed/stored Huffman block emission, and the bit writer. Inflate lives in
//   rt_compress.c.
//
// Links: rt_compress_internal.h (shared trees), rt_compress.c
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt_compress_internal.h"


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
void *deflate_data(const uint8_t *data, size_t len, int level) {
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
void *gzip_data(const uint8_t *data, size_t len, int level) {
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
