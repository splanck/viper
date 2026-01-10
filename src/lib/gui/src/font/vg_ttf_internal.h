// vg_ttf_internal.h - Internal TTF structures
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../include/vg_font.h"

//=============================================================================
// TTF Table Directory
//=============================================================================

typedef struct ttf_table {
    uint32_t tag;            // 4-byte table identifier
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} ttf_table_t;

//=============================================================================
// TTF 'head' Table - Font header
//=============================================================================

typedef struct ttf_head {
    uint16_t units_per_em;
    int16_t x_min, y_min, x_max, y_max;
    int16_t index_to_loc_format;  // 0=short, 1=long
} ttf_head_t;

//=============================================================================
// TTF 'hhea' Table - Horizontal header
//=============================================================================

typedef struct ttf_hhea {
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    uint16_t num_h_metrics;
} ttf_hhea_t;

//=============================================================================
// TTF 'maxp' Table - Maximum profile
//=============================================================================

typedef struct ttf_maxp {
    uint16_t num_glyphs;
} ttf_maxp_t;

//=============================================================================
// TTF 'hmtx' Entry - Horizontal metrics
//=============================================================================

typedef struct ttf_hmtx_entry {
    uint16_t advance_width;
    int16_t left_side_bearing;
} ttf_hmtx_entry_t;

//=============================================================================
// TTF 'kern' Pair - Kerning
//=============================================================================

typedef struct ttf_kern_pair {
    uint16_t left;
    uint16_t right;
    int16_t value;
} ttf_kern_pair_t;

//=============================================================================
// Glyph Cache Entry
//=============================================================================

typedef struct vg_cache_entry {
    uint64_t key;            // (size_bits << 32) | codepoint
    struct vg_glyph glyph;
    struct vg_cache_entry* next;  // For collision chaining
} vg_cache_entry_t;

//=============================================================================
// Glyph Cache
//=============================================================================

#define VG_CACHE_INITIAL_SIZE   256
#define VG_CACHE_MAX_SIZE       4096
#define VG_CACHE_MAX_MEMORY     (32 * 1024 * 1024)  // 32MB

typedef struct vg_glyph_cache {
    vg_cache_entry_t** buckets;
    size_t bucket_count;
    size_t entry_count;
    size_t memory_used;      // Track bitmap memory usage
} vg_glyph_cache_t;

//=============================================================================
// Internal Font Structure
//=============================================================================

struct vg_font {
    // Raw data
    uint8_t* data;
    size_t data_size;
    bool owns_data;          // true if we should free data

    // Parsed tables
    ttf_head_t head;
    ttf_hhea_t hhea;
    ttf_maxp_t maxp;

    // Table offsets
    uint32_t cmap_offset;
    uint32_t glyf_offset;
    uint32_t loca_offset;
    uint32_t hmtx_offset;
    uint32_t kern_offset;
    uint32_t name_offset;

    // CMAP format 4 data (BMP)
    uint16_t cmap4_seg_count;
    uint16_t* cmap4_end_codes;
    uint16_t* cmap4_start_codes;
    int16_t* cmap4_id_deltas;
    uint16_t* cmap4_id_range_offsets;
    uint16_t* cmap4_glyph_ids;
    uint32_t cmap4_glyph_ids_count;

    // CMAP format 12 data (full Unicode)
    uint32_t cmap12_num_groups;
    uint32_t* cmap12_start_codes;
    uint32_t* cmap12_end_codes;
    uint32_t* cmap12_start_glyph_ids;

    // Kerning data
    ttf_kern_pair_t* kern_pairs;
    uint32_t kern_pair_count;

    // Glyph cache
    vg_glyph_cache_t* cache;

    // Font names
    char family_name[128];
    char style_name[64];
};

//=============================================================================
// Byte Reading Utilities (Big-Endian)
//=============================================================================

static inline uint8_t ttf_read_u8(const uint8_t* p) {
    return p[0];
}

static inline int8_t ttf_read_i8(const uint8_t* p) {
    return (int8_t)p[0];
}

static inline uint16_t ttf_read_u16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline int16_t ttf_read_i16(const uint8_t* p) {
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t ttf_read_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline int32_t ttf_read_i32(const uint8_t* p) {
    return (int32_t)ttf_read_u32(p);
}

// Fixed-point 2.14 to float
static inline float ttf_read_f2dot14(const uint8_t* p) {
    int16_t val = ttf_read_i16(p);
    return (float)val / 16384.0f;
}

// Fixed-point 16.16 to float
static inline float ttf_read_fixed(const uint8_t* p) {
    int32_t val = ttf_read_i32(p);
    return (float)val / 65536.0f;
}

//=============================================================================
// Tag Macros
//=============================================================================

#define TTF_TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                             ((uint32_t)(c) << 8) | (uint32_t)(d))

#define TTF_TAG_HEAD TTF_TAG('h', 'e', 'a', 'd')
#define TTF_TAG_HHEA TTF_TAG('h', 'h', 'e', 'a')
#define TTF_TAG_MAXP TTF_TAG('m', 'a', 'x', 'p')
#define TTF_TAG_CMAP TTF_TAG('c', 'm', 'a', 'p')
#define TTF_TAG_GLYF TTF_TAG('g', 'l', 'y', 'f')
#define TTF_TAG_LOCA TTF_TAG('l', 'o', 'c', 'a')
#define TTF_TAG_HMTX TTF_TAG('h', 'm', 't', 'x')
#define TTF_TAG_KERN TTF_TAG('k', 'e', 'r', 'n')
#define TTF_TAG_NAME TTF_TAG('n', 'a', 'm', 'e')

//=============================================================================
// Internal Functions
//=============================================================================

// TTF parsing
bool ttf_parse_tables(struct vg_font* font);
bool ttf_parse_head(struct vg_font* font, const uint8_t* data, uint32_t len);
bool ttf_parse_hhea(struct vg_font* font, const uint8_t* data, uint32_t len);
bool ttf_parse_maxp(struct vg_font* font, const uint8_t* data, uint32_t len);
bool ttf_parse_cmap(struct vg_font* font, const uint8_t* data, uint32_t len);
bool ttf_parse_kern(struct vg_font* font, const uint8_t* data, uint32_t len);
bool ttf_parse_name(struct vg_font* font, const uint8_t* data, uint32_t len);

// Glyph lookup
uint16_t ttf_get_glyph_index(struct vg_font* font, uint32_t codepoint);

// Glyph outline
bool ttf_get_glyph_outline(struct vg_font* font, uint16_t glyph_id,
                           float** out_points_x, float** out_points_y,
                           uint8_t** out_flags, int** out_contour_ends,
                           int* out_num_points, int* out_num_contours);

// Horizontal metrics
void ttf_get_h_metrics(struct vg_font* font, uint16_t glyph_id,
                       int* advance_width, int* left_side_bearing);

// Cache operations
vg_glyph_cache_t* vg_cache_create(void);
void vg_cache_destroy(vg_glyph_cache_t* cache);
const struct vg_glyph* vg_cache_get(vg_glyph_cache_t* cache, float size, uint32_t codepoint);
void vg_cache_put(vg_glyph_cache_t* cache, float size, uint32_t codepoint,
                  const struct vg_glyph* glyph);
void vg_cache_clear(vg_glyph_cache_t* cache);

// Rasterization
struct vg_glyph* vg_rasterize_glyph(struct vg_font* font, uint16_t glyph_id, float size);
