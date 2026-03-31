# Plan 07: MP3 Huffman Tree Decoding

## Context

The MP3 decoder's Huffman stage (`rt_mp3.c:560-600`) uses a simplified approach that reads
`ceil(log2(max_val+1))` fixed-width bits per value instead of walking the spec's variable-
length Huffman trees. This produces incorrect frequency data for most MP3 files — the
audio pipeline is structurally complete but the spectral input is wrong.

## Problem

ISO 11172-3 defines 32 Huffman pair tables (0-31) plus two quad tables (A/B). Each table
maps variable-length bit patterns to (x, y) value pairs. The current code reads fixed-
width fields, which is only correct when all codewords happen to be the same length — a
rare degenerate case. Real MP3 files use highly variable code lengths (1-19 bits).

## Approach

Replace the simplified decode with a proper tree-walk decoder. Each Huffman table is stored
as a binary tree in a flat array. Decoding reads one bit at a time and walks left/right
until reaching a leaf node.

### Tree Storage Format

Each tree node is a 16-bit value:
- Positive: leaf node. `x = value >> 4`, `y = value & 0x0F`.
- Negative: branch node. `left_child = -value`, `right_child = -value + 1`.

### Tables to Add (`rt_mp3_tables.h`)

The 32 pair tables from ISO 11172-3 Annex B.7. Key tables by frequency of use:

| Table | Max (x,y) | Linbits | Entries | Used For |
|-------|-----------|---------|---------|----------|
| 0 | 0 | 0 | 0 | Zero region (no decode needed) |
| 1 | 1 | 0 | 7 | Low-energy pairs |
| 2-3 | 2 | 0 | 17 | |
| 5-6 | 3 | 0 | 31 | |
| 7-9 | 5 | 0 | 63 | Mid-energy pairs |
| 10-12 | 7 | 0 | 127 | |
| 13,15 | 15 | 0 | 511 | High-energy pairs |
| 16-31 | 15+ | 1-13 | 511 | ESC tables (linbits extension) |

Total static data: ~4000 int16_t entries (~8 KB).

### Quad Tables (count1 region)

Table A (hcod): 4 values {v,w,x,y} ∈ {0,1}, encoded in 1-6 bits.
Table B: each value is 1 bit (v,w,x,y directly). Current implementation is already correct
for Table B. Table A needs a small 16-entry lookup.

### Decode Algorithm

```c
static int mp3_huff_decode_pair(mp3_bits_t *bits, const int16_t *tree, int *x, int *y) {
    int node = 0;
    while (tree[node] < 0) {
        int bit = mp3_bits_read(bits, 1);
        node = -tree[node] + bit; // left=0, right=1
    }
    *x = tree[node] >> 4;
    *y = tree[node] & 0x0F;
    return 0;
}
```

After decoding (x, y), apply linbits and sign:
```c
if (linbits > 0 && x == 15) x += mp3_bits_read(bits, linbits);
if (x != 0) { if (mp3_bits_read(bits, 1)) x = -x; } // sign
// same for y
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/audio/rt_mp3_tables.h` | Add 32 tree arrays (~4000 entries) + quad table A |
| `src/runtime/audio/rt_mp3.c` | Replace lines 560-600 with tree-walk decoder |

## Files to Create

None — changes are in existing files.

## Verification

- Existing `test_mp3_decode` tests continue to pass (rejection tests)
- Decode a real 128kbps 44.1kHz stereo MP3 and verify non-silence output
- Compare first 1024 samples against a reference decoder (ffmpeg) within ±2 LSB tolerance
