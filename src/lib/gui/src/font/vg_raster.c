// vg_raster.c - Glyph rasterization with antialiasing
#include "vg_ttf_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

//=============================================================================
// Constants
//=============================================================================

#define MAX_POINTS 16384
#define CURVE_TOLERANCE 0.25f
#define OVERSAMPLE 4  // Supersampling factor for antialiasing

//=============================================================================
// Point and Edge Structures
//=============================================================================

typedef struct {
    float x, y;
} raster_point_t;

typedef struct {
    float x0, y0, x1, y1;
    float dx;  // Precomputed 1/(y1-y0) for scanline intersection
} raster_edge_t;

//=============================================================================
// Quadratic Bezier Flattening
//=============================================================================

static int flatten_quadratic(float x0, float y0, float x1, float y1, float x2, float y2,
                             float tolerance, raster_point_t* out, int max_points, int count) {
    // Calculate flatness (distance from control point to line)
    float dx = x2 - x0;
    float dy = y2 - y0;
    float len_sq = dx * dx + dy * dy;

    if (len_sq < 0.0001f) {
        // Degenerate case - just add endpoint
        if (count < max_points) {
            out[count].x = x2;
            out[count].y = y2;
            return count + 1;
        }
        return count;
    }

    float d = fabsf((x1 - x0) * dy - (y1 - y0) * dx) / sqrtf(len_sq);

    if (d <= tolerance) {
        // Flat enough, output line segment endpoint
        if (count < max_points) {
            out[count].x = x2;
            out[count].y = y2;
            return count + 1;
        }
        return count;
    }

    // Subdivide at midpoint using de Casteljau's algorithm
    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float x012 = (x01 + x12) * 0.5f;
    float y012 = (y01 + y12) * 0.5f;

    count = flatten_quadratic(x0, y0, x01, y01, x012, y012, tolerance, out, max_points, count);
    count = flatten_quadratic(x012, y012, x12, y12, x2, y2, tolerance, out, max_points, count);

    return count;
}

//=============================================================================
// Convert Glyph Outline to Polygon Points
//=============================================================================

static int outline_to_polygon(float* points_x, float* points_y, uint8_t* flags,
                              int* contour_ends, int num_contours,
                              float scale, float offset_x, float offset_y,
                              raster_point_t* out, int max_points) {
    int count = 0;

    for (int c = 0; c < num_contours; c++) {
        int contour_end = contour_ends[c];
        int contour_start = (c == 0) ? 0 : contour_ends[c - 1] + 1;
        int contour_len = contour_end - contour_start + 1;

        if (contour_len < 2) {
            continue;
        }

        // Process points in contour
        for (int i = 0; i < contour_len; i++) {
            int idx = contour_start + i;
            int next_idx = contour_start + ((i + 1) % contour_len);

            float x0 = points_x[idx] * scale + offset_x;
            float y0 = points_y[idx] * scale + offset_y;
            float x1 = points_x[next_idx] * scale + offset_x;
            float y1 = points_y[next_idx] * scale + offset_y;

            bool on0 = flags[idx];
            bool on1 = flags[next_idx];

            if (on0 && on1) {
                // Line segment
                if (count < max_points) {
                    out[count].x = x0;
                    out[count].y = y0;
                    count++;
                }
            } else if (on0 && !on1) {
                // Current on-curve, next is control point
                // Find the end point
                int end_idx = contour_start + ((i + 2) % contour_len);
                float x2, y2;

                if (flags[end_idx]) {
                    // End point is on-curve
                    x2 = points_x[end_idx] * scale + offset_x;
                    y2 = points_y[end_idx] * scale + offset_y;
                } else {
                    // End point is also off-curve, use midpoint
                    x2 = (x1 + points_x[end_idx] * scale + offset_x) * 0.5f;
                    y2 = (y1 + points_y[end_idx] * scale + offset_y) * 0.5f;
                }

                // Start with current point
                if (count < max_points) {
                    out[count].x = x0;
                    out[count].y = y0;
                    count++;
                }

                // Flatten the curve
                count = flatten_quadratic(x0, y0, x1, y1, x2, y2,
                                          CURVE_TOLERANCE, out, max_points, count);
            } else if (!on0 && !on1) {
                // Both off-curve - implicit on-curve at midpoint
                // The midpoint becomes our "on-curve" start
                // and x1,y1 is the control for next segment
                // This case is handled by the previous iteration
            } else if (!on0 && on1) {
                // Current off-curve, next on-curve
                // This is handled by previous point's curve
            }
        }
    }

    return count;
}

//=============================================================================
// Edge Comparison for Sorting
//=============================================================================

static int compare_edges(const void* a, const void* b) {
    const raster_edge_t* ea = (const raster_edge_t*)a;
    const raster_edge_t* eb = (const raster_edge_t*)b;

    float min_y_a = (ea->y0 < ea->y1) ? ea->y0 : ea->y1;
    float min_y_b = (eb->y0 < eb->y1) ? eb->y0 : eb->y1;

    if (min_y_a < min_y_b) return -1;
    if (min_y_a > min_y_b) return 1;
    return 0;
}

//=============================================================================
// Build Edge List from Polygon
//=============================================================================

static int build_edges(raster_point_t* points, int count, raster_edge_t* edges) {
    int edge_count = 0;

    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;

        // Skip horizontal edges
        if (fabsf(points[i].y - points[j].y) < 0.001f) continue;

        raster_edge_t* e = &edges[edge_count++];
        e->x0 = points[i].x;
        e->y0 = points[i].y;
        e->x1 = points[j].x;
        e->y1 = points[j].y;

        // Precompute for scanline intersection
        e->dx = (e->x1 - e->x0) / (e->y1 - e->y0);
    }

    // Sort edges by minimum y
    qsort(edges, edge_count, sizeof(raster_edge_t), compare_edges);

    return edge_count;
}

//=============================================================================
// Scanline Rasterization with Coverage-Based Antialiasing
//=============================================================================

static void rasterize_scanlines(raster_point_t* points, int point_count,
                                int width, int height, uint8_t* bitmap) {
    // Clear bitmap
    memset(bitmap, 0, width * height);

    if (point_count < 3) return;

    // Build edge list
    raster_edge_t* edges = malloc(point_count * sizeof(raster_edge_t));
    if (!edges) return;

    int edge_count = build_edges(points, point_count, edges);

    // Supersampled scanline buffer
    float* coverage = calloc(width, sizeof(float));
    if (!coverage) {
        free(edges);
        return;
    }

    // Process each row with supersampling
    for (int y = 0; y < height; y++) {
        memset(coverage, 0, width * sizeof(float));

        // Supersample vertically
        for (int sub = 0; sub < OVERSAMPLE; sub++) {
            float scan_y = y + (sub + 0.5f) / OVERSAMPLE;

            // Find intersections with active edges
            float intersections[256];
            int num_intersections = 0;

            for (int e = 0; e < edge_count; e++) {
                float y0 = edges[e].y0;
                float y1 = edges[e].y1;

                // Check if edge crosses this scanline
                bool crosses = (y0 <= scan_y && y1 > scan_y) ||
                               (y1 <= scan_y && y0 > scan_y);

                if (crosses && num_intersections < 256) {
                    // Calculate x intersection
                    float t = (scan_y - y0) / (y1 - y0);
                    float x = edges[e].x0 + t * (edges[e].x1 - edges[e].x0);
                    intersections[num_intersections++] = x;
                }
            }

            // Sort intersections
            for (int i = 0; i < num_intersections - 1; i++) {
                for (int j = i + 1; j < num_intersections; j++) {
                    if (intersections[j] < intersections[i]) {
                        float tmp = intersections[i];
                        intersections[i] = intersections[j];
                        intersections[j] = tmp;
                    }
                }
            }

            // Fill between pairs (even-odd rule)
            for (int i = 0; i + 1 < num_intersections; i += 2) {
                float x0f = intersections[i];
                float x1f = intersections[i + 1];

                int x0 = (int)floorf(x0f);
                int x1 = (int)ceilf(x1f);

                if (x0 < 0) x0 = 0;
                if (x1 > width) x1 = width;

                // Add coverage for this subsample
                for (int x = x0; x < x1; x++) {
                    float left = (x < x0f) ? x0f : x;
                    float right = (x + 1 > x1f) ? x1f : x + 1;
                    if (right > left) {
                        coverage[x] += (right - left) / OVERSAMPLE;
                    }
                }
            }
        }

        // Convert coverage to 8-bit alpha
        for (int x = 0; x < width; x++) {
            float c = coverage[x];
            if (c > 0) {
                if (c > 1.0f) c = 1.0f;
                bitmap[y * width + x] = (uint8_t)(c * 255.0f + 0.5f);
            }
        }
    }

    free(coverage);
    free(edges);
}

//=============================================================================
// Main Rasterization Entry Point
//=============================================================================

vg_glyph_t* vg_rasterize_glyph(vg_font_t* font, uint16_t glyph_id, float size) {
    // Get glyph outline
    float* points_x = NULL;
    float* points_y = NULL;
    uint8_t* flags = NULL;
    int* contour_ends = NULL;
    int num_points = 0;
    int num_contours = 0;

    if (!ttf_get_glyph_outline(font, glyph_id, &points_x, &points_y, &flags,
                               &contour_ends, &num_points, &num_contours)) {
        return NULL;
    }

    // Calculate scale factor
    float scale = size / (float)font->head.units_per_em;

    // Get horizontal metrics
    int advance_width, left_side_bearing;
    ttf_get_h_metrics(font, glyph_id, &advance_width, &left_side_bearing);

    // Allocate glyph
    vg_glyph_t* glyph = calloc(1, sizeof(vg_glyph_t));
    if (!glyph) {
        free(points_x);
        free(points_y);
        free(flags);
        free(contour_ends);
        return NULL;
    }

    glyph->advance = (int)(advance_width * scale + 0.5f);

    // Empty glyph (like space)
    if (num_points == 0) {
        free(points_x);
        free(points_y);
        free(flags);
        free(contour_ends);
        glyph->width = 0;
        glyph->height = 0;
        glyph->bearing_x = 0;
        glyph->bearing_y = 0;
        glyph->bitmap = NULL;
        return glyph;
    }

    // Calculate bounding box
    float min_x = points_x[0], max_x = points_x[0];
    float min_y = points_y[0], max_y = points_y[0];

    for (int i = 1; i < num_points; i++) {
        if (points_x[i] < min_x) min_x = points_x[i];
        if (points_x[i] > max_x) max_x = points_x[i];
        if (points_y[i] < min_y) min_y = points_y[i];
        if (points_y[i] > max_y) max_y = points_y[i];
    }

    // Scale bounding box
    min_x *= scale;
    max_x *= scale;
    min_y *= scale;
    max_y *= scale;

    // Calculate bitmap dimensions with padding
    int padding = 1;
    int bmp_width = (int)ceilf(max_x - min_x) + padding * 2;
    int bmp_height = (int)ceilf(max_y - min_y) + padding * 2;

    if (bmp_width <= 0) bmp_width = 1;
    if (bmp_height <= 0) bmp_height = 1;

    // Calculate offsets
    float offset_x = -min_x + padding;
    float offset_y = -min_y + padding;

    // Set glyph metrics
    // Note: TTF y increases upward, bitmap y increases downward
    glyph->width = bmp_width;
    glyph->height = bmp_height;
    glyph->bearing_x = (int)floorf(min_x);
    glyph->bearing_y = (int)ceilf(max_y);  // Top of glyph relative to baseline

    // Convert outline to polygon
    raster_point_t* polygon = malloc(MAX_POINTS * sizeof(raster_point_t));
    if (!polygon) {
        free(glyph);
        free(points_x);
        free(points_y);
        free(flags);
        free(contour_ends);
        return NULL;
    }

    int polygon_count = outline_to_polygon(points_x, points_y, flags,
                                           contour_ends, num_contours,
                                           scale, offset_x, offset_y,
                                           polygon, MAX_POINTS);

    // Flip y coordinates (TTF y-up to bitmap y-down)
    for (int i = 0; i < polygon_count; i++) {
        polygon[i].y = bmp_height - polygon[i].y;
    }

    // Allocate and rasterize bitmap
    glyph->bitmap = calloc(bmp_width * bmp_height, 1);
    if (glyph->bitmap) {
        rasterize_scanlines(polygon, polygon_count, bmp_width, bmp_height, glyph->bitmap);
    }

    // Cleanup
    free(polygon);
    free(points_x);
    free(points_y);
    free(flags);
    free(contour_ends);

    return glyph;
}
