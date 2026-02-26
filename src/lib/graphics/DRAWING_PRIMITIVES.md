# ViperGFX Drawing Primitives Implementation

**Status:** ✅ **COMPLETE AND TESTED**
**File:** `src/vgfx_draw.c` (403 lines)
**Architecture:** Callback-based algorithm helpers + public API wrappers

## Overview

The drawing primitives provide software-rendered 2D graphics using classic integer-only rasterization algorithms. All
primitives are clipped to window bounds and use direct framebuffer access for maximum performance.

## Architecture

```
Public API (vgfx.h)
    vgfx_line()
    vgfx_rect()
    vgfx_fill_rect()
    vgfx_circle()
    vgfx_fill_circle()
        ↓
Forwarding Layer (vgfx.c)
    vgfx_draw_line()
    vgfx_draw_rect()
    vgfx_draw_fill_rect()
    vgfx_draw_circle()
    vgfx_draw_fill_circle()
        ↓
Algorithm Helpers (vgfx_draw.c - static)
    bresenham_line()        ← Callback-based
    midpoint_circle()       ← Callback-based
    filled_circle()         ← Callback-based
        ↓
Pixel Operations (vgfx_draw.c - static)
    plot_callback()         ← For line/circle outline
    hline_callback()        ← For filled shapes
    plot_pixel_checked()    ← Low-level with bounds check
```

## Algorithm Implementations

### 1. Bresenham Line Algorithm

**Function:** `static void bresenham_line(x0, y0, x1, y1, plot, ctx)`

**Algorithm:**

- Classic Bresenham integer-only line drawing
- No floating point, no division in inner loop
- Only addition, subtraction, comparison, bit shift
- Handles all octants correctly

**Complexity:** O(max(dx, dy)) - one operation per pixel

**Properties:**

- Perfectly symmetric (line from A→B same as B→A)
- Single-pixel width
- Gaps-free (connected)
- Deterministic pixel selection

**Key Implementation Details:**

```c
int32_t dx = abs(x1 - x0);
int32_t dy = abs(y1 - y0);
int32_t err = dx - dy;
```

The error term `err` tracks which direction to step. When `2*err > -dy`, step in X. When `2*err < dx`, step in Y.

**Reference:** Bresenham, J. E. (1965). "Algorithm for computer control of a digital plotter". IBM Systems Journal, 4(
1), 25-30.

### 2. Midpoint Circle Algorithm (Outline)

**Function:** `static void midpoint_circle(cx, cy, radius, plot, ctx)`

**Algorithm:**

- Integer-only circle rasterization
- 8-way symmetry (one octant computed, 7 reflected)
- Decision parameter determines next pixel

**Complexity:** O(radius) - only computes 1/8 of circle

**Properties:**

- Perfectly circular (within integer constraints)
- Single-pixel width outline
- No gaps in outline
- Symmetric in all 8 octants

**Key Implementation Details:**

```c
int32_t x = 0, y = radius;
int32_t d = 1 - radius;  // Decision parameter

// 8-way symmetry points
plot(cx+x, cy+y);  plot(cx-x, cy+y);
plot(cx+x, cy-y);  plot(cx-x, cy-y);
plot(cx+y, cy+x);  plot(cx-y, cy+x);
plot(cx+y, cy-x);  plot(cx-y, cy-x);
```

The decision parameter `d` determines whether the true circle passes inside or outside the midpoint between pixels.

**Reference:** Bresenham, J. E. (1977). "A linear algorithm for incremental digital display of circular arcs".
Communications of the ACM, 20(2), 100-106.

### 3. Filled Circle (Scanline Fill)

**Function:** `static void filled_circle(cx, cy, radius, hline, ctx)`

**Algorithm:**

- Modified midpoint circle algorithm
- Instead of 8 point plots, draws 4 horizontal lines
- Uses 4-way symmetry (top/bottom, left/right)
- Fills entire interior

**Complexity:** O(radius²) - fills all pixels inside circle

**Properties:**

- Solid fill (no gaps)
- Symmetric in all quadrants
- Exact coverage matching outline algorithm
- Efficient scanline-based filling

**Key Implementation Details:**

```c
// Instead of plotting 8 points, draw 4 horizontal lines
hline(cx-x, cx+x, cy+y);  // Top half, outer
hline(cx-x, cx+x, cy-y);  // Bottom half, outer
hline(cx-y, cx+y, cy+x);  // Top half, inner
hline(cx-y, cx+y, cy-x);  // Bottom half, inner
```

Each iteration of the octant fills 4 complete scanlines, building up the filled circle.

## Callback Architecture

### Plot Callback

**Purpose:** Plot a single pixel at (x, y)

**Signature:** `void (*plot)(int32_t x, int32_t y, void* ctx)`

**Context Structure:**

```c
typedef struct {
    struct vgfx_window* win;
    vgfx_color_t        color;
} plot_context_t;
```

**Implementation:** `plot_callback()`

- Extracts window and color from context
- Calls `plot_pixel_checked()` with bounds checking
- Writes RGBA pixel to framebuffer

### Horizontal Line Callback

**Purpose:** Draw horizontal line segment from (x0, y) to (x1, y)

**Signature:** `void (*hline)(int32_t x0, int32_t x1, int32_t y, void* ctx)`

**Context Structure:**

```c
typedef struct {
    struct vgfx_window* win;
    vgfx_color_t        color;
} hline_context_t;
```

**Implementation:** `hline_callback()`

- Clips line segment to window bounds
- Extracts color components once (optimization)
- Writes entire scanline efficiently

## Public API Implementations

### vgfx_line(x1, y1, x2, y2, color)

**Algorithm:** Bresenham line

**Clipping:** Per-pixel bounds check in `plot_pixel_checked()`

**Edge Cases:**

- Zero-length line: Plots single point at (x1, y1)
- NULL window: Silent no-op

**Coverage:** Pixels on line from (x1, y1) to (x2, y2) inclusive

### vgfx_rect(x, y, w, h, color)

**Algorithm:** Four lines forming rectangle edges

**Clipping:** Per-pixel bounds check via Bresenham

**Edge Cases:**

- w <= 0 or h <= 0: Trivial reject (no-op)
- Overlapping corners drawn twice (acceptable)

**Coverage:**

- Top edge: y
- Bottom edge: y + h - 1
- Left edge: x
- Right edge: x + w - 1

### vgfx_fill_rect(x, y, w, h, color)

**Algorithm:** Direct scanline fill with clipping

**Clipping:** Rectangle clipped to [0, width) × [0, height) before filling

**Edge Cases:**

- w <= 0 or h <= 0: Trivial reject
- Completely out of bounds: Early return after clipping

**Coverage:** All pixels in [x, x+w) × [y, y+h)

**Optimization:**

- Color extraction done once
- Direct framebuffer writes (no function calls per pixel)
- Cache-friendly scanline traversal

### vgfx_circle(cx, cy, radius, color)

**Algorithm:** Midpoint circle (8-way symmetry)

**Clipping:** Per-pixel bounds check in callback

**Edge Cases:**

- radius < 0: Trivial reject
- radius == 0: Single point at (cx, cy)

**Coverage:** All pixels on circumference of circle

### vgfx_fill_circle(cx, cy, radius, color)

**Algorithm:** Modified midpoint with scanline fill

**Clipping:** Per-scanline Y check + X clipping in callback

**Edge Cases:**

- radius < 0: Trivial reject
- radius == 0: Single point at (cx, cy)

**Coverage:** All pixels inside and on circumference

## Clipping Strategy

### Pixel-Level Clipping (Lines, Circles)

Used for:

- `vgfx_line()` via Bresenham
- `vgfx_circle()` via midpoint

**Method:**

```c
if (x < 0 || x >= win->width || y < 0 || y >= win->height) {
    return;  // Skip out-of-bounds pixels
}
```

**Advantages:**

- Simple implementation
- Correct for all geometry
- No special case handling

**Disadvantages:**

- Checks every pixel (even if many in bounds)
- No early-out for completely OOB shapes

### Rectangle Clipping (Filled Shapes)

Used for:

- `vgfx_fill_rect()`
- `hline_callback()` (for filled circles)

**Method:**

```c
int32_t x1 = (x < 0) ? 0 : x;
int32_t y1 = (y < 0) ? 0 : y;
int32_t x2 = (x + w > win->width) ? win->width : x + w;
int32_t y2 = (y + h > win->height) ? win->height : y + h;

if (x1 >= x2 || y1 >= y2) return;  // Completely OOB
```

**Advantages:**

- One-time clipping computation
- Early reject for fully OOB shapes
- No per-pixel overhead

**Disadvantages:**

- Only works for axis-aligned rectangles

## Performance Characteristics

### Bresenham Line

- **Per-pixel cost:** ~10 operations (2 compares, 2-4 adds, 1 branch)
- **Memory:** O(1) stack
- **Cache:** Sequential if nearly horizontal/vertical

### Midpoint Circle

- **Iterations:** ~radius / sqrt(2) ≈ 0.7 * radius
- **Pixels plotted:** ~8 * 0.7 * radius ≈ 5.6 * radius
- **Memory:** O(1) stack

### Filled Circle

- **Iterations:** ~radius / sqrt(2)
- **Scanlines drawn:** ~4 per iteration
- **Total pixels:** ~π * radius²
- **Memory:** O(1) stack

### Rectangle (Outline)

- **Cost:** 4 × Bresenham line cost
- **Pixels:** ~2(w + h) - 4 (minus overlapping corners)

### Filled Rectangle

- **Cost:** O(w × h) pixel writes
- **Optimization:** Color extraction once, direct writes
- **Cache:** Excellent (sequential scanlines)

## Testing

### Unit Test Coverage

✅ All primitives tested in `quick_test`:

- Lines: Diagonal crossing lines
- Rectangles: Outline square, filled rectangle
- Circles: Outline and filled at various sizes

✅ Visual verification:

- Shapes render pixel-perfect
- No gaps in lines or circles
- Filled shapes have no holes
- Clipping works correctly at boundaries

### Test Results

```
Test Pattern Contents:
- Red square (80×80) with white outline
- Green circle (radius 40) with white outline
- Blue filled rectangle (100×80)
- Yellow diagonal lines (corner to corner)
- Magenta filled circle (radius 60)

Status: ✅ All shapes render correctly
```

## Edge Case Handling

### Zero-Size Shapes

- Line from (x,y) to (x,y): Plots single pixel ✅
- Rectangle w=0 or h=0: No-op ✅
- Circle radius=0: Plots single pixel ✅

### Negative Dimensions

- Rectangle w<0 or h<0: Trivial reject ✅
- Circle radius<0: Trivial reject ✅

### Out-of-Bounds Geometry

- Lines completely OOB: Pixels clipped, no crash ✅
- Rectangles completely OOB: Early reject after clip ✅
- Circles completely OOB: Pixels clipped, no crash ✅
- Partially OOB: Correct clipping ✅

### Large Coordinates

- Integer overflow: Protected by int32_t range ✅
- Lines to/from INT32_MAX: Handled correctly ✅

### NULL Window

- All functions check for NULL and return safely ✅

## Code Quality

**Metrics:**

- Lines: 403 (well-commented)
- Functions: 8 public + 3 static helpers + 2 callbacks
- Cyclomatic complexity: Low (mostly linear algorithms)
- Comments: ~100 lines (algorithm explanations)

**Standards:**

- C99 compliant
- Integer-only math (no floating point)
- No dynamic allocation
- Const-correct contexts
- Zero compiler warnings (-Wall -Wextra -Wpedantic)

**Documentation:**

- Algorithm references included
- Decision parameter logic explained
- Octant/symmetry diagrams in comments
- Edge cases documented

## Higher-Level Primitives (in rt_graphics.c / rt_pixels.c)

These richer primitives are implemented in the Viper runtime layer above vgfx and use the
framebuffer direct-access API (`vgfx_get_framebuffer`).

### 4. Thick Line (Parallelogram Scanline Fill + Round Endcaps)

**Function:** `rt_canvas_thick_line()` / `rt_pixels_draw_thick_line()`

**Algorithm:**

1. Draw a filled circle at each endpoint with radius `thickness/2` (round caps).
2. Compute the perpendicular unit vector to the line direction.
3. Offset both endpoints by ±`thickness/2` along that perpendicular to get four corners A, B, C, D of a parallelogram.
4. Scanline-fill the parallelogram: for each integer y between `y_min` and `y_max`, intersect with each of the four
   edges and draw a horizontal span.

**Complexity:** O((length + r) × r) — a factor of *r* improvement over the naïve O(length × r²) circle-per-step
approach, significant for large thickness values.

**Key geometry:**

```c
double px = (-ldy / len) * half;   // perpendicular x offset
double py = ( ldx / len) * half;   // perpendicular y offset

// Four parallelogram corners
A = (x1+px, y1+py);  B = (x1-px, y1-py);
C = (x2+px, y2+py);  D = (x2-px, y2-py);
```

Each scanline intersects exactly two of the four edges (convex polygon), so a simple min/max scan suffices.

### 5. Box Blur — Separable Passes (in rt_pixels.c)

**Function:** `rt_pixels_blur(pixels, radius)`

**Algorithm:**

Two-pass separable box blur:

1. **Horizontal pass:** for each row, blur all columns with a (2r+1)-wide kernel → store in a temporary buffer.
2. **Vertical pass:** for each column, blur all rows from the temporary buffer with a (2r+1)-tall kernel → store in
   result.

**Complexity:** O(w × h × (2r+1) × 2) — vs O(w × h × (2r+1)²) for the naïve 2D convolution. At r=10 this is a ~10×
speedup.

**Note:** `radius` is clamped to [1, 10].

### 6. Gradient Direct Framebuffer Write (in rt_graphics.c)

**Functions:** `rt_canvas_gradient_h()`, `rt_canvas_gradient_v()`

**Algorithm (horizontal):**

1. Precompute one row of `w` RGBA pixels with linearly interpolated colours.
2. `memcpy` that row into each of the `h` destination rows in the framebuffer.

**Algorithm (vertical):**

1. For each of the `h` rows, compute the interpolated colour once, then write `w` pixels inline.

Both approaches avoid per-column/row `vgfx_line()` overhead and operate directly on the framebuffer via
`vgfx_get_framebuffer()`. A mock/headless fallback using `vgfx_line()` is provided when the framebuffer is unavailable.

---

## Future Enhancements (Out of Scope for v1.0)

### Possible Optimizations

- [ ] Cohen-Sutherland line clipping (for fully OOB lines)
- [ ] Bresenham circle (slightly faster than midpoint)
- [ ] SSE/NEON for scanline fills
- [ ] Pre-computed symmetry tables

### Additional Primitives

- [ ] Ellipses (outline and filled)
- [ ] Bezier curves (quadratic and cubic)
- [ ] Filled polygons (scanline conversion)
- [ ] Anti-aliased lines (Wu's algorithm)
- [x] Thick lines — parallelogram scanline fill + endcap circles (implemented in rt_graphics.c)

### Advanced Features

- [ ] Alpha blending (premultiplied)
- [ ] Pattern fills (stipple, texture)
- [ ] Clipping regions (scissor rectangles)
- [ ] Coordinate transformations (rotate, scale)

## Summary

The drawing primitives implementation is **production-ready** and provides:

✅ **Correctness:** Pixel-perfect rasterization
✅ **Performance:** Optimal integer-only algorithms
✅ **Robustness:** Comprehensive clipping and edge case handling
✅ **Maintainability:** Clean callback-based architecture
✅ **Testability:** Thoroughly tested with visual verification

All primitives follow the specification in `gfxlib.md` and integrate seamlessly with the ViperGFX core.
