# Plan 13: Vorbis Residue Multi-Pass Decode

## Context

The Vorbis residue decoder (`rt_vorbis.c:856-911`) uses a simplified single-pass approach.
The Vorbis I spec requires up to 8 passes per partition, where each pass decodes
increasingly fine detail from the residue signal. Missing passes means losing spectral
detail, causing muddy or distorted audio on complex Vorbis streams.

## Problem

The current implementation:
1. Reads a single classbook entry per partition
2. Decodes residue from the first book of the classification only (pass 0)
3. Ignores passes 1-7 entirely

The spec requires:
1. For each partition, decode a classification value from the classbook
2. For each pass (0..7), check if the classification's cascade bit is set for that pass
3. If set, decode residue vectors from the pass-specific book

## Approach

### Replace Single-Pass with Full Multi-Pass (~100 LOC change)

```c
// Per-partition classifications (decoded once, used across all passes)
int *classifications = calloc(parts_to_decode * ch_count, sizeof(int));

// Pass 0: decode all classbook entries to get classifications
for (int part = 0; part < parts_to_decode; part++) {
    int class_val = codebook_decode_scalar(&codebooks[classbook], &bits);
    // Unpack classifications for each channel
    int temp = class_val;
    for (int c = ch_count - 1; c >= 0; c--) {
        classifications[part * ch_count + c] = temp % res->classifications;
        temp /= res->classifications;
    }
}

// Passes 0..7: decode residue vectors for each pass that has data
for (int pass = 0; pass < 8; pass++) {
    for (int part = 0; part < parts_to_decode; part++) {
        for (int c = 0; c < ch_count; c++) {
            int cls = classifications[part * ch_count + c];
            if (!(res->cascade[cls] & (1 << pass)))
                continue; // no data for this pass
            int book = res->books[cls][pass];
            if (book < 0)
                continue;
            // Decode residue vectors from this book
            int offset = res->begin + part * res->partition_size;
            decode_residue_vectors(&codebooks[book], &bits,
                                   residue_buf[ch_list[c]], offset,
                                   res->partition_size, res->type);
        }
    }
}
```

### Residue Type Differences

| Type | Vector Layout |
|------|--------------|
| 0 | Interleaved: each VQ vector spans all channels |
| 1 | Non-interleaved: VQ vectors within one channel's partition |
| 2 | Coupled: all channels packed into one big vector, then de-interleaved |

Current code treats all types the same (type 1 style). Need to add type-specific handling:

**Type 0** (interleaved): rarely used in practice, but spec-compliant decoders must handle it.

**Type 2** (coupled): concatenate all channels' partitions into one long vector, decode as
one stream, then de-interleave. This is the most common type for stereo Vorbis.

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/audio/rt_vorbis.c` | Replace residue decode section (~lines 856-911) |

## Estimated LOC

~100 lines net change (replace ~50 simplified lines with ~150 correct lines, add type dispatch).

## Verification

- Decode the same OGG test file before/after, verify improved audio quality
- Confirm spectral analysis shows more high-frequency detail (the missing passes)
- Existing `test_ogg_vorbis` header tests still pass
- No memory leaks (classifications array freed after decode)
