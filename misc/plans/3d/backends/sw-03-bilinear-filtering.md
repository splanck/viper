# SW-03: Bilinear Texture Filtering in Software Rasterizer

## Context
`sample_texture()` uses nearest-neighbor (point) sampling. This produces visible pixelation when textures are magnified. Bilinear filtering interpolates between 4 neighboring texels for smooth results.

## Current State
```c
// In sample_texture() — point sampling:
int x = (int)(u * (float)tex->width);
int y = (int)(v * (float)tex->height);
```
Truncates to integer → nearest texel. No interpolation. Already wraps via `u = u - floorf(u)` (fract).

## Implementation

Replace `sample_texture` with bilinear version:
```c
static void sample_texture(
    const sw_pixels_view *tex, float u, float v, float *r, float *g, float *b, float *a) {
    u = u - floorf(u);
    v = v - floorf(v);

    float fx = u * (float)tex->width - 0.5f;
    float fy = v * (float)tex->height - 0.5f;
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    float xf = fx - (float)x0;  // fractional part [0,1)
    float yf = fy - (float)y0;

    // Wrap coordinates for repeat mode
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int w = (int)tex->width, h = (int)tex->height;
    if (w == 0 || h == 0) { *r = *g = *b = 1.0f; *a = 1.0f; return; }
    x0 = ((x0 % w) + w) % w;
    y0 = ((y0 % h) + h) % h;
    x1 = ((x1 % w) + w) % w;
    y1 = ((y1 % h) + h) % h;

    // Sample 4 texels
    uint32_t p00 = tex->data[y0 * w + x0];
    uint32_t p10 = tex->data[y0 * w + x1];
    uint32_t p01 = tex->data[y1 * w + x0];
    uint32_t p11 = tex->data[y1 * w + x1];

    // Unpack and bilinear interpolate
    float w00 = (1-xf)*(1-yf), w10 = xf*(1-yf), w01 = (1-xf)*yf, w11 = xf*yf;
    *r = (((p00>>24)&0xFF)*w00 + ((p10>>24)&0xFF)*w10 +
          ((p01>>24)&0xFF)*w01 + ((p11>>24)&0xFF)*w11) / 255.0f;
    *g = (((p00>>16)&0xFF)*w00 + ((p10>>16)&0xFF)*w10 +
          ((p01>>16)&0xFF)*w01 + ((p11>>16)&0xFF)*w11) / 255.0f;
    *b = (((p00>>8)&0xFF)*w00 + ((p10>>8)&0xFF)*w10 +
          ((p01>>8)&0xFF)*w01 + ((p11>>8)&0xFF)*w11) / 255.0f;
    *a = ((p00&0xFF)*w00 + (p10&0xFF)*w10 +
          (p01&0xFF)*w01 + (p11&0xFF)*w11) / 255.0f;
}
```

This also fixes the wrapping to be proper repeat (modulo) instead of clamp-after-fract.

## Performance
Bilinear samples 4 texels instead of 1. On the software rasterizer this is ~4x more memory reads per pixel. For 1080p at 60fps this is acceptable — the rasterizer is already memory-bound.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_sw.c` — `sample_texture()` rewrite

## Testing
- Magnify a 16x16 checkerboard texture → should produce smooth gradients at edges (not hard pixel boundaries)
- Minification: a large texture on a small triangle → should produce averaged color (not flickering)
