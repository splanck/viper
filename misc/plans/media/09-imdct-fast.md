# Plan 09: Fast IMDCT (O(n log n) Upgrade)

## Context

Both the MP3 and Vorbis decoders use O(n²) direct-computation IMDCT. For MP3 (n=36), this
is 648 multiply-adds per subband — acceptable. For Vorbis long blocks (n=8192), it's
16.7M multiply-adds — ~100x slower than an FFT-based O(n log n) approach (~53K ops).

## Problem

The O(n²) Vorbis IMDCT is the primary performance bottleneck for OGG playback. MP3's
36-point IMDCT is small enough that O(n²) is tolerable, but upgrading it costs nothing
since the same algorithm works for both.

## Approach: Split-Radix IMDCT via N/4-point FFT

The standard fast IMDCT decomposes into:
1. **Pre-twiddle**: multiply N/2 input values by complex exponentials
2. **N/4-point complex FFT**: Cooley-Tukey radix-2 DIT
3. **Post-twiddle**: multiply FFT output by complex exponentials

This reduces IMDCT(N) from O(N²) to O(N log N).

### Algorithm

```
Input: X[0..N/4-1] (frequency coefficients)
Output: x[0..N/2-1] (time-domain samples)

Step 1 — Pre-twiddle (N/4 complex multiplies):
  for k = 0..N/4-1:
    Z[k] = X[2k] + j*X[2k+1]  (interleave real/imag)
    Z[k] *= exp(-j * 2π/N * (k + 1/8))

Step 2 — N/4-point complex FFT (in-place Cooley-Tukey):
  Standard radix-2 DIT butterfly

Step 3 — Post-twiddle (N/4 complex multiplies):
  for k = 0..N/4-1:
    Z[k] *= exp(-j * 2π/N * (k + 1/8))

Step 4 — Reorder to IMDCT output:
  Shuffle real/imaginary parts into the output array with sign flips
```

### Twiddle Factor Precomputation

For each block size, precompute `cos` and `sin` tables:
```c
float *cos_tw, *sin_tw;  // N/4 entries each
for (k = 0; k < N/4; k++) {
    double angle = 2.0 * M_PI / N * (k + 0.125);
    cos_tw[k] = (float)cos(angle);
    sin_tw[k] = (float)sin(angle);
}
```

MP3: precompute for N=36 and N=12 (tiny tables — 9 and 3 entries).
Vorbis: precompute for blocksize_0 and blocksize_1 on decoder init.

### Radix-2 FFT

```c
static void fft_radix2(float *re, float *im, int n) {
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) { swap(re[i], re[j]); swap(im[i], im[j]); }
    }
    // Butterfly passes
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * M_PI / len;
        float wre = (float)cos(ang), wim = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cur_re = 1.0f, cur_im = 0.0f;
            for (int j = 0; j < len/2; j++) {
                float tre = re[i+j+len/2]*cur_re - im[i+j+len/2]*cur_im;
                float tim = re[i+j+len/2]*cur_im + im[i+j+len/2]*cur_re;
                re[i+j+len/2] = re[i+j] - tre;
                im[i+j+len/2] = im[i+j] - tim;
                re[i+j] += tre;
                im[i+j] += tim;
                float new_re = cur_re*wre - cur_im*wim;
                cur_im = cur_re*wim + cur_im*wre;
                cur_re = new_re;
            }
        }
    }
}
```

### Handling Non-Power-of-2 (MP3 N=36)

MP3's 36-point IMDCT doesn't decompose cleanly into a power-of-2 FFT. Options:
1. **Keep the O(n²) for N=36/12** — only 648/36 ops, fast enough
2. **Use a 9-point DFT** with Winograd or Rader — adds complexity for negligible gain

Recommendation: keep O(n²) for MP3 (N=36 is trivial), upgrade only Vorbis.

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/audio/rt_vorbis.c` | Replace `imdct()` with FFT-based fast IMDCT |
| `src/runtime/audio/rt_vorbis.c` | Add twiddle precomputation in `decode_identification` |
| `src/runtime/audio/rt_vorbis.h` | No changes needed (internal function) |

## Files to Create

None — the FFT and twiddle functions are static helpers within `rt_vorbis.c`.

## Estimated LOC

| Component | LOC |
|-----------|-----|
| Radix-2 FFT | ~40 |
| Pre/post twiddle | ~30 |
| Twiddle table precomputation | ~20 |
| Fast IMDCT wrapper | ~30 |
| Remove old O(n²) IMDCT | -12 |
| Total | ~120 net |

## Verification

- Decode a Vorbis file with long blocks (2048 samples), compare PCM output before/after
- Verify output matches O(n²) implementation within ±1 LSB (float rounding)
- Benchmark: time 100 IMDCT calls at N=2048, verify >10x speedup
