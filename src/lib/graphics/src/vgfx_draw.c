//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Drawing Primitives
//
// Implements classical raster graphics algorithms for drawing lines, circles,
// and rectangles.  All primitives use integer-only arithmetic for deterministic,
// portable behavior.  Clipping is performed at the pixel level to handle
// partially visible shapes gracefully.
//
// Algorithms Implemented:
//   - Bresenham's Line Algorithm (1965): Integer-only line rasterization
//   - Midpoint Circle Algorithm (1977): 8-way symmetric circle outline
//   - Scanline Fill: Horizontal line-based filling for rectangles and circles
//
// All drawing operations are bounds-checked.  Pixels outside the window are
// silently discarded (no error generated).  This allows drawing shapes that
// extend beyond the viewport without special-casing.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Drawing primitive implementations using classical raster algorithms.
/// @details Provides Bresenham line drawing, midpoint circle rendering, and
///          rectangle operations.  All algorithms use integer-only arithmetic
///          and perform per-pixel bounds checking.

#include "vgfx.h"
#include "vgfx_internal.h"
#include <stdlib.h> /* abs */

//===----------------------------------------------------------------------===//
// Context Structures for Algorithm Callbacks
//===----------------------------------------------------------------------===//
// These structures carry state through the generic algorithm implementations
// to the pixel-plotting callbacks.  Avoids global state and enables
// multiple concurrent drawing operations.
//===----------------------------------------------------------------------===//

/// @brief Context for single-pixel plotting callbacks.
/// @details Passed to plot_callback() by the Bresenham line and midpoint
///          circle algorithms.  Contains window handle and color.
typedef struct
{
    struct vgfx_window *win; ///< Target window for drawing
    vgfx_color_t color;      ///< Color to plot (RGB 24-bit)
} plot_context_t;

/// @brief Context for horizontal line drawing callbacks.
/// @details Passed to hline_callback() by the filled circle algorithm.
///          Contains window handle and color for scanline fills.
typedef struct
{
    struct vgfx_window *win; ///< Target window for drawing
    vgfx_color_t color;      ///< Color for scanline (RGB 24-bit)
} hline_context_t;

//===----------------------------------------------------------------------===//
// Low-Level Pixel Plotting
//===----------------------------------------------------------------------===//

/// @brief Plot a single pixel with bounds checking.
/// @details Writes a colored pixel to the framebuffer at (x, y) if the
///          coordinates are within the window bounds [0, width) × [0, height).
///          Pixels outside the window are silently discarded.  Alpha is
///          always set to 0xFF (fully opaque).
///
/// @param win   Pointer to the window structure
/// @param x     X coordinate in pixels
/// @param y     Y coordinate in pixels
/// @param color RGB color (format: 0x00RRGGBB)
///
/// @pre  win != NULL
/// @post If (x, y) in bounds: pixel is set to color with alpha=0xFF
/// @post If (x, y) out of bounds: no-op (silent discard)
static inline void plot_pixel_checked(struct vgfx_window *win,
                                      int32_t x,
                                      int32_t y,
                                      vgfx_color_t color)
{
    /* Bounds check against clip rectangle (or window bounds if no clip) */
    int32_t min_x = 0, min_y = 0;
    int32_t max_x = win->width, max_y = win->height;

    if (win->clip_enabled)
    {
        /* Intersect with clip rectangle */
        if (win->clip_x > min_x)
            min_x = win->clip_x;
        if (win->clip_y > min_y)
            min_y = win->clip_y;
        if (win->clip_x + win->clip_w < max_x)
            max_x = win->clip_x + win->clip_w;
        if (win->clip_y + win->clip_h < max_y)
            max_y = win->clip_y + win->clip_h;
    }

    if (x < min_x || x >= max_x || y < min_y || y >= max_y)
    {
        return;
    }

    /* Extract RGB components from 24-bit color */
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    uint8_t a = 0xFF; /* Opaque alpha (required by RGBA format) */

    /* Write to framebuffer in RGBA format (4 bytes per pixel) */
    uint8_t *pixel = win->pixels + (y * win->stride) + (x * 4);
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = a;
}

//===----------------------------------------------------------------------===//
// Algorithm Callbacks
//===----------------------------------------------------------------------===//
// Generic callbacks that bridge the abstract algorithms (which don't know
// about windows or colors) to the concrete framebuffer operations.
//===----------------------------------------------------------------------===//

/// @brief Callback for single-pixel plotting (used by line and circle algorithms).
/// @details Extracts window and color from the context and plots a pixel.
///          Used as the plot callback for bresenham_line() and midpoint_circle().
///
/// @param x   X coordinate in pixels
/// @param y   Y coordinate in pixels
/// @param ctx Opaque context pointer (actually plot_context_t*)
///
/// @pre  ctx points to a valid plot_context_t structure
static void plot_callback(int32_t x, int32_t y, void *ctx)
{
    plot_context_t *pctx = (plot_context_t *)ctx;
    plot_pixel_checked(pctx->win, x, y, pctx->color);
}

/// @brief Callback for horizontal line drawing (used by filled circle).
/// @details Draws a horizontal scanline from x0 to x1 at row y, with full
///          bounds checking and clipping.  Used by filled_circle() to fill
///          the interior of the circle.
///
/// @param x0  Start X coordinate (inclusive)
/// @param x1  End X coordinate (inclusive)
/// @param y   Y coordinate (row number)
/// @param ctx Opaque context pointer (actually hline_context_t*)
///
/// @pre  ctx points to a valid hline_context_t structure
/// @post Horizontal line segment drawn from max(x0, 0) to min(x1, width-1) at y
static void hline_callback(int32_t x0, int32_t x1, int32_t y, void *ctx)
{
    hline_context_t *hctx = (hline_context_t *)ctx;
    struct vgfx_window *win = hctx->win;
    vgfx_color_t color = hctx->color;

    /* Determine clip bounds */
    int32_t min_x = 0, min_y = 0;
    int32_t max_x = win->width, max_y = win->height;

    if (win->clip_enabled)
    {
        if (win->clip_x > min_x)
            min_x = win->clip_x;
        if (win->clip_y > min_y)
            min_y = win->clip_y;
        if (win->clip_x + win->clip_w < max_x)
            max_x = win->clip_x + win->clip_w;
        if (win->clip_y + win->clip_h < max_y)
            max_y = win->clip_y + win->clip_h;
    }

    /* Bounds check Y coordinate (reject entire scanline if out of clip bounds) */
    if (y < min_y || y >= max_y)
        return;

    /* Ensure x0 <= x1 (swap if needed) */
    if (x0 > x1)
    {
        int32_t tmp = x0;
        x0 = x1;
        x1 = tmp;
    }

    /* Clip X coordinates to clip bounds */
    if (x0 < min_x)
        x0 = min_x;
    if (x1 >= max_x)
        x1 = max_x - 1;
    if (x0 > x1)
        return; /* Scanline entirely clipped */

    /* Extract color components once for efficiency */
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    uint8_t a = 0xFF;

    /* Draw horizontal line segment (scanline fill) */
    uint8_t *scanline = win->pixels + (y * win->stride) + (x0 * 4);
    for (int32_t x = x0; x <= x1; x++)
    {
        scanline[0] = r;
        scanline[1] = g;
        scanline[2] = b;
        scanline[3] = a;
        scanline += 4;
    }
}

//===----------------------------------------------------------------------===//
// Bresenham Line Algorithm
//===----------------------------------------------------------------------===//
// Classic integer-only line rasterization.  Handles all octants correctly
// without floating point arithmetic, division, or multiplication in the
// inner loop.  Only uses addition, subtraction, and bit shifts.
//
// Reference: Bresenham, J. E. (1965). "Algorithm for computer control
//            of a digital plotter". IBM Systems Journal, 4(1), 25-30.
//===----------------------------------------------------------------------===//

/// @brief Draw a line from (x0, y0) to (x1, y1) using Bresenham's algorithm.
/// @details Integer-only line rasterization that works correctly in all octants.
///          Uses an error accumulator to decide when to step in the minor axis
///          direction.  The algorithm is symmetric: swapping endpoints produces
///          the same pixels in reverse order.
///
///          Key properties:
///            - No floating point: uses only int32_t arithmetic
///            - No division or multiplication in inner loop
///            - Handles all 8 octants (dx > dy, dy > dx, positive/negative slopes)
///            - Each pixel is plotted exactly once
///
/// @param x0   Start X coordinate
/// @param y0   Start Y coordinate
/// @param x1   End X coordinate
/// @param y1   End Y coordinate
/// @param plot Callback function invoked for each pixel (x, y)
/// @param ctx  Opaque context passed to plot callback
///
/// @pre  plot != NULL
/// @post plot() called for each pixel along the line from (x0, y0) to (x1, y1)
static void bresenham_line(int32_t x0,
                           int32_t y0,
                           int32_t x1,
                           int32_t y1,
                           void (*plot)(int32_t x, int32_t y, void *ctx),
                           void *ctx)
{
    /* Calculate absolute deltas (line extents) */
    int32_t dx = abs(x1 - x0);
    int32_t dy = abs(y1 - y0);

    /* Determine step direction for each axis (-1 or +1) */
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;

    /* Initialize error term (decision parameter)
     * err represents 2 * accumulated error scaled by dx and dy
     * When err crosses zero, step in the minor axis */
    int32_t err = dx - dy;

    /* Current position (start at beginning of line) */
    int32_t x = x0;
    int32_t y = y0;

    /* Iterate until we reach the endpoint (inclusive) */
    while (1)
    {
        /* Plot current pixel */
        plot(x, y, ctx);

        /* Check if we've reached the endpoint */
        if (x == x1 && y == y1)
            break;

        /* Calculate 2 * error for comparison (avoids division) */
        int32_t e2 = 2 * err;

        /* Step in X direction if error threshold exceeded */
        if (e2 > -dy)
        {
            err -= dy;
            x += sx;
        }

        /* Step in Y direction if error threshold exceeded */
        if (e2 < dx)
        {
            err += dx;
            y += sy;
        }
    }
}

//===----------------------------------------------------------------------===//
// Midpoint Circle Algorithm (Outline)
//===----------------------------------------------------------------------===//
// Integer-only circle rasterization exploiting 8-way symmetry.  Computes
// one octant and reflects it to the other 7 octants.  Uses a decision
// parameter to determine whether the midpoint is inside or outside the
// ideal circle.
//
// Reference: Bresenham, J. E. (1977). "A linear algorithm for incremental
//            digital display of circular arcs". Communications of the ACM.
//===----------------------------------------------------------------------===//

/// @brief Draw a circle outline using the midpoint circle algorithm.
/// @details Integer-only circle rasterization with 8-way symmetry.  Computes
///          one octant (first 45 degrees) and reflects it to draw the entire
///          circle.  The decision parameter determines whether to step
///          horizontally or diagonally based on whether the midpoint between
///          candidate pixels is inside or outside the ideal circle.
///
///          Key properties:
///            - No floating point: uses only int32_t arithmetic
///            - Exploits 8-way symmetry: 8 pixels plotted per iteration
///            - Decision parameter uses only addition and subtraction
///            - Radius 0 draws a single point at the center
///            - Negative radius is rejected (no pixels drawn)
///
/// @param cx     Center X coordinate
/// @param cy     Center Y coordinate
/// @param radius Circle radius in pixels (must be >= 0)
/// @param plot   Callback function invoked for each pixel (x, y)
/// @param ctx    Opaque context passed to plot callback
///
/// @pre  plot != NULL
/// @pre  radius >= 0
/// @post plot() called for all pixels on the circle perimeter (if radius > 0)
/// @post plot() called once at (cx, cy) if radius == 0
static void midpoint_circle(int32_t cx,
                            int32_t cy,
                            int32_t radius,
                            void (*plot)(int32_t x, int32_t y, void *ctx),
                            void *ctx)
{
    /* Special case: radius 0 is just a single point at the center */
    if (radius == 0)
    {
        plot(cx, cy, ctx);
        return;
    }

    /* Negative radius is invalid (reject without drawing) */
    if (radius < 0)
        return;

    /* Initial position in first octant: (0, radius)
     * This corresponds to the topmost point of the circle */
    int32_t x = 0;
    int32_t y = radius;

    /* Initial decision parameter: d = 1 - radius
     * This determines whether the midpoint between (x+1, y) and (x+1, y-1)
     * is inside or outside the ideal circle */
    int32_t d = 1 - radius;

    /* Plot initial 8 symmetric points (all octants at x=0)
     * Each octant has a reflection about the X axis, Y axis, and Y=X line */
    plot(cx + x, cy + y, ctx); /* Octant 2:  90° (top) */
    plot(cx - x, cy + y, ctx); /* Octant 1:  90° (top, mirrored) */
    plot(cx + x, cy - y, ctx); /* Octant 7: 270° (bottom) */
    plot(cx - x, cy - y, ctx); /* Octant 6: 270° (bottom, mirrored) */
    plot(cx + y, cy + x, ctx); /* Octant 3:   0° (right) */
    plot(cx - y, cy + x, ctx); /* Octant 0: 180° (left) */
    plot(cx + y, cy - x, ctx); /* Octant 4:   0° (right, mirrored) */
    plot(cx - y, cy - x, ctx); /* Octant 5: 180° (left, mirrored) */

    /* Iterate through first octant (while x < y, i.e., slope > -1) */
    while (x < y)
    {
        x++; /* Always step horizontally in first octant */

        /* Update decision parameter and Y coordinate based on midpoint test */
        if (d < 0)
        {
            /* Midpoint is inside circle: step horizontally only (x+1, y) */
            d += 2 * x + 1;
        }
        else
        {
            /* Midpoint is outside circle: step diagonally (x+1, y-1) */
            y--;
            d += 2 * (x - y) + 1;
        }

        /* Plot 8 symmetric points for this (x, y) */
        plot(cx + x, cy + y, ctx);
        plot(cx - x, cy + y, ctx);
        plot(cx + x, cy - y, ctx);
        plot(cx - x, cy - y, ctx);
        plot(cx + y, cy + x, ctx);
        plot(cx - y, cy + x, ctx);
        plot(cx + y, cy - x, ctx);
        plot(cx - y, cy - x, ctx);
    }
}

//===----------------------------------------------------------------------===//
// Filled Circle (Scanline Fill)
//===----------------------------------------------------------------------===//
// Derived from the midpoint circle algorithm.  Instead of plotting 8 points
// per iteration, draws 4 horizontal scanlines to fill the circle interior.
// This produces a solid filled circle with correct rounding at the edges.
//===----------------------------------------------------------------------===//

/// @brief Draw a filled circle using scanline fill derived from midpoint algorithm.
/// @details Uses the same decision logic as midpoint_circle(), but instead of
///          plotting 8 symmetric points, draws 4 horizontal scanlines spanning
///          the circle's interior.  Each iteration fills the current Y levels
///          in all four quadrants, producing a solid filled circle.
///
///          Key properties:
///            - No floating point: uses only int32_t arithmetic
///            - Exploits 4-way symmetry for horizontal lines
///            - Each row is filled exactly once (no overdraw)
///            - Handles clipping in hline_callback
///
/// @param cx     Center X coordinate
/// @param cy     Center Y coordinate
/// @param radius Circle radius in pixels (must be >= 0)
/// @param hline  Callback function invoked for each horizontal scanline
/// @param ctx    Opaque context passed to hline callback
///
/// @pre  hline != NULL
/// @pre  radius >= 0
/// @post hline() called for all horizontal scanlines filling the circle interior
static void filled_circle(int32_t cx,
                          int32_t cy,
                          int32_t radius,
                          void (*hline)(int32_t x0, int32_t x1, int32_t y, void *ctx),
                          void *ctx)
{
    /* Special case: radius 0 is just a single point (degenerate scanline) */
    if (radius == 0)
    {
        hline(cx, cx, cy, ctx);
        return;
    }

    /* Negative radius is invalid (reject without drawing) */
    if (radius < 0)
        return;

    /* Initial position in first octant (same as outline algorithm) */
    int32_t x = 0;
    int32_t y = radius;
    int32_t d = 1 - radius;

    /* Fill initial horizontal lines (corresponds to initial 8-way symmetry)
     * These are the scanlines at y = cy ± radius and y = cy ± 0 */
    hline(cx - x, cx + x, cy + y, ctx); /* Top: y = cy + radius */
    hline(cx - x, cx + x, cy - y, ctx); /* Bottom: y = cy - radius */
    hline(cx - y, cx + y, cy + x, ctx); /* Middle upper: y = cy + 0 = cy */
    hline(cx - y, cx + y, cy - x, ctx); /* Middle lower: y = cy - 0 = cy */

    /* Iterate through first octant (while x < y) */
    while (x < y)
    {
        x++;

        /* Update decision parameter (same as outline algorithm) */
        if (d < 0)
        {
            d += 2 * x + 1;
        }
        else
        {
            y--;
            d += 2 * (x - y) + 1;
        }

        /* Fill 4 horizontal lines using 4-way symmetry
         * Two scanlines span the width at y = cy ± y (outer)
         * Two scanlines span the width at y = cy ± x (inner)
         * Together these fill the entire circle interior */
        hline(cx - x, cx + x, cy + y, ctx); /* Upper half, outer */
        hline(cx - x, cx + x, cy - y, ctx); /* Lower half, outer */
        hline(cx - y, cx + y, cy + x, ctx); /* Upper half, inner */
        hline(cx - y, cx + y, cy - x, ctx); /* Lower half, inner */
    }
}

//===----------------------------------------------------------------------===//
// Public Drawing Primitives
//===----------------------------------------------------------------------===//
// These are the entry points called from vgfx.c (forwarded from the public API).
// They set up the appropriate context structures and invoke the algorithm
// implementations above.
//===----------------------------------------------------------------------===//

/// @brief Draw a line from (x1, y1) to (x2, y2).
/// @details Public entry point for Bresenham line drawing.  Sets up the
///          plot context and invokes the Bresenham algorithm.  Pixels are
///          clipped to the window bounds.
///
/// @param window Window handle
/// @param x1     Start X coordinate
/// @param y1     Start Y coordinate
/// @param x2     End X coordinate
/// @param y2     End Y coordinate
/// @param color  RGB color (format: 0x00RRGGBB)
///
/// @pre  window is a valid struct vgfx_window*
/// @post Line pixels drawn from (x1, y1) to (x2, y2) in the framebuffer
void vgfx_draw_line(
    vgfx_window_t window, int32_t x1, int32_t y1, int32_t x2, int32_t y2, vgfx_color_t color)
{
    struct vgfx_window *win = (struct vgfx_window *)window;
    if (!win)
        return;

    /* Set up plot context (passed to bresenham_line via plot_callback) */
    plot_context_t ctx = {.win = win, .color = color};

    /* Draw line using Bresenham algorithm */
    bresenham_line(x1, y1, x2, y2, plot_callback, &ctx);
}

/// @brief Draw a rectangle outline.
/// @details Draws the four edges of a rectangle using vgfx_draw_line().
///          The rectangle has top-left corner at (x, y) and dimensions w × h.
///          Zero or negative dimensions are silently rejected (no drawing).
///
/// @param window Window handle
/// @param x      Top-left X coordinate
/// @param y      Top-left Y coordinate
/// @param w      Width in pixels (must be > 0)
/// @param h      Height in pixels (must be > 0)
/// @param color  RGB color (format: 0x00RRGGBB)
///
/// @pre  window is a valid struct vgfx_window*
/// @post Rectangle outline drawn at (x, y) with dimensions w × h
void vgfx_draw_rect(
    vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h, vgfx_color_t color)
{
    /* Trivial reject: zero or negative dimensions (invalid rectangle) */
    if (w <= 0 || h <= 0)
        return;

    /* Draw four edges of rectangle using line primitive
     * Top:    (x, y) to (x+w-1, y)
     * Bottom: (x, y+h-1) to (x+w-1, y+h-1)
     * Left:   (x, y) to (x, y+h-1)
     * Right:  (x+w-1, y) to (x+w-1, y+h-1)
     */
    vgfx_draw_line(window, x, y, x + w - 1, y, color);                 /* Top */
    vgfx_draw_line(window, x, y + h - 1, x + w - 1, y + h - 1, color); /* Bottom */
    vgfx_draw_line(window, x, y, x, y + h - 1, color);                 /* Left */
    vgfx_draw_line(window, x + w - 1, y, x + w - 1, y + h - 1, color); /* Right */
}

/// @brief Draw a filled rectangle.
/// @details Fills a rectangle with top-left corner at (x, y) and dimensions
///          w × h.  Uses optimized scanline filling (no overdraw).  The
///          rectangle is clipped to the window bounds before rendering.
///
/// @param window Window handle
/// @param x      Top-left X coordinate
/// @param y      Top-left Y coordinate
/// @param w      Width in pixels (must be > 0)
/// @param h      Height in pixels (must be > 0)
/// @param color  RGB color (format: 0x00RRGGBB)
///
/// @pre  window is a valid struct vgfx_window*
/// @post Rectangle filled at (x, y) with dimensions w × h (clipped to window)
void vgfx_draw_fill_rect(
    vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h, vgfx_color_t color)
{
    struct vgfx_window *win = (struct vgfx_window *)window;
    if (!win)
        return;

    /* Trivial reject: zero or negative dimensions (invalid rectangle) */
    if (w <= 0 || h <= 0)
        return;

    /* Determine clip bounds (use clip rect if enabled, otherwise window bounds) */
    int32_t clip_min_x = 0, clip_min_y = 0;
    int32_t clip_max_x = win->width, clip_max_y = win->height;

    if (win->clip_enabled)
    {
        if (win->clip_x > clip_min_x)
            clip_min_x = win->clip_x;
        if (win->clip_y > clip_min_y)
            clip_min_y = win->clip_y;
        if (win->clip_x + win->clip_w < clip_max_x)
            clip_max_x = win->clip_x + win->clip_w;
        if (win->clip_y + win->clip_h < clip_max_y)
            clip_max_y = win->clip_y + win->clip_h;
    }

    /* Clip rectangle to clip bounds */
    int32_t x1 = (x < clip_min_x) ? clip_min_x : x;
    int32_t y1 = (y < clip_min_y) ? clip_min_y : y;
    int32_t x2 = (x + w > clip_max_x) ? clip_max_x : x + w;
    int32_t y2 = (y + h > clip_max_y) ? clip_max_y : y + h;

    /* Check if rectangle is completely out of bounds (no pixels to draw) */
    if (x1 >= x2 || y1 >= y2)
        return;

    /* Extract color components once for efficiency (avoid repeated bit shifts) */
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    uint8_t a = 0xFF;

    /* Fill each scanline (row-by-row rendering) */
    for (int32_t row = y1; row < y2; row++)
    {
        /* Compute scanline base address (start of row) */
        uint8_t *scanline = win->pixels + (row * win->stride) + (x1 * 4);

        /* Fill scanline with color (column-by-column) */
        for (int32_t col = x1; col < x2; col++)
        {
            scanline[0] = r;
            scanline[1] = g;
            scanline[2] = b;
            scanline[3] = a;
            scanline += 4; /* Advance to next pixel (4 bytes = RGBA) */
        }
    }
}

/// @brief Draw a circle outline.
/// @details Public entry point for midpoint circle drawing.  Sets up the
///          plot context and invokes the midpoint circle algorithm.  Pixels
///          are clipped to the window bounds.
///
/// @param window Window handle
/// @param cx     Center X coordinate
/// @param cy     Center Y coordinate
/// @param radius Radius in pixels (must be >= 0)
/// @param color  RGB color (format: 0x00RRGGBB)
///
/// @pre  window is a valid struct vgfx_window*
/// @post Circle outline drawn centered at (cx, cy) with given radius
void vgfx_draw_circle(
    vgfx_window_t window, int32_t cx, int32_t cy, int32_t radius, vgfx_color_t color)
{
    struct vgfx_window *win = (struct vgfx_window *)window;
    if (!win)
        return;

    /* Trivial reject: negative radius (invalid circle) */
    if (radius < 0)
        return;

    /* Set up plot context (passed to midpoint_circle via plot_callback) */
    plot_context_t ctx = {.win = win, .color = color};

    /* Draw circle outline using midpoint algorithm */
    midpoint_circle(cx, cy, radius, plot_callback, &ctx);
}

/// @brief Draw a filled circle.
/// @details Public entry point for filled circle drawing.  Sets up the
///          hline context and invokes the scanline fill algorithm.  Scanlines
///          are clipped to the window bounds.
///
/// @param window Window handle
/// @param cx     Center X coordinate
/// @param cy     Center Y coordinate
/// @param radius Radius in pixels (must be >= 0)
/// @param color  RGB color (format: 0x00RRGGBB)
///
/// @pre  window is a valid struct vgfx_window*
/// @post Filled circle drawn centered at (cx, cy) with given radius
void vgfx_draw_fill_circle(
    vgfx_window_t window, int32_t cx, int32_t cy, int32_t radius, vgfx_color_t color)
{
    struct vgfx_window *win = (struct vgfx_window *)window;
    if (!win)
        return;

    /* Trivial reject: negative radius (invalid circle) */
    if (radius < 0)
        return;

    /* Set up horizontal line context (passed to filled_circle via hline_callback) */
    hline_context_t ctx = {.win = win, .color = color};

    /* Fill circle using scanline algorithm */
    filled_circle(cx, cy, radius, hline_callback, &ctx);
}

//===----------------------------------------------------------------------===//
// Clipping Functions
//===----------------------------------------------------------------------===//

/// @brief Set the clipping rectangle for all drawing operations.
/// @details All subsequent drawing operations will be clipped to the specified
///          rectangle. The clip rectangle is intersected with the window bounds.
///
/// @param window Window handle
/// @param x      Left edge X coordinate of clip rect
/// @param y      Top edge Y coordinate of clip rect
/// @param w      Width of clip rect
/// @param h      Height of clip rect
///
/// @pre  window is a valid struct vgfx_window*
/// @post All drawing operations will be clipped to (x, y, w, h)
void vgfx_set_clip(vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct vgfx_window *win = (struct vgfx_window *)window;
    if (!win)
        return;

    /* Store clip rectangle */
    win->clip_x = x;
    win->clip_y = y;
    win->clip_w = w;
    win->clip_h = h;
    win->clip_enabled = 1;
}

/// @brief Clear the clipping rectangle, restoring full-window drawing.
/// @details After calling this function, drawing operations can affect any
///          pixel within the window bounds.
///
/// @param window Window handle
///
/// @pre  window is a valid struct vgfx_window*
/// @post Clipping is disabled; drawing affects entire window
void vgfx_clear_clip(vgfx_window_t window)
{
    struct vgfx_window *win = (struct vgfx_window *)window;
    if (!win)
        return;

    win->clip_enabled = 0;
}

//===----------------------------------------------------------------------===//
// End of Drawing Primitives
//===----------------------------------------------------------------------===//
