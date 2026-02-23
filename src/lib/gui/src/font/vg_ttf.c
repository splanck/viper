// vg_ttf.c - TTF font parser implementation
#include "vg_ttf_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Table Finding
//=============================================================================

static const uint8_t *ttf_find_table(vg_font_t *font, uint32_t tag, uint32_t *out_len)
{
    const uint8_t *data = font->data;

    /* Minimum header size: 12 bytes (sfVersion + numTables + searchRange + ...) */
    if (font->data_size < 12)
        return NULL;

    uint16_t num_tables = ttf_read_u16(data + 4);

    /* Validate that the entire table directory fits within the file */
    if ((uint32_t)(12 + (uint32_t)num_tables * 16) > font->data_size)
        num_tables = (uint16_t)((font->data_size - 12) / 16);

    for (int i = 0; i < num_tables; i++)
    {
        const uint8_t *entry = data + 12 + i * 16;
        uint32_t entry_tag = ttf_read_u32(entry);
        if (entry_tag == tag)
        {
            uint32_t offset = ttf_read_u32(entry + 8);
            uint32_t length = ttf_read_u32(entry + 12);
            if (out_len)
                *out_len = length;
            if (offset + length <= font->data_size)
            {
                return data + offset;
            }
            return NULL;
        }
    }
    return NULL;
}

//=============================================================================
// Parse 'head' Table
//=============================================================================

bool ttf_parse_head(vg_font_t *font, const uint8_t *data, uint32_t len)
{
    if (len < 54)
        return false;

    font->head.units_per_em = ttf_read_u16(data + 18);
    font->head.x_min = ttf_read_i16(data + 36);
    font->head.y_min = ttf_read_i16(data + 38);
    font->head.x_max = ttf_read_i16(data + 40);
    font->head.y_max = ttf_read_i16(data + 42);
    font->head.index_to_loc_format = ttf_read_i16(data + 50);

    return font->head.units_per_em > 0;
}

//=============================================================================
// Parse 'hhea' Table
//=============================================================================

bool ttf_parse_hhea(vg_font_t *font, const uint8_t *data, uint32_t len)
{
    if (len < 36)
        return false;

    font->hhea.ascent = ttf_read_i16(data + 4);
    font->hhea.descent = ttf_read_i16(data + 6);
    font->hhea.line_gap = ttf_read_i16(data + 8);
    font->hhea.num_h_metrics = ttf_read_u16(data + 34);

    return true;
}

//=============================================================================
// Parse 'maxp' Table
//=============================================================================

bool ttf_parse_maxp(vg_font_t *font, const uint8_t *data, uint32_t len)
{
    if (len < 6)
        return false;

    font->maxp.num_glyphs = ttf_read_u16(data + 4);
    return font->maxp.num_glyphs > 0;
}

//=============================================================================
// Parse 'cmap' Table
//=============================================================================

static bool ttf_parse_cmap_format4(vg_font_t *font, const uint8_t *subtable)
{
    uint16_t length = ttf_read_u16(subtable + 2);
    uint16_t seg_count_x2 = ttf_read_u16(subtable + 6);
    uint16_t seg_count = seg_count_x2 / 2;

    if (seg_count == 0)
        return false;

    font->cmap4_seg_count = seg_count;

    // Allocate arrays
    font->cmap4_end_codes = malloc(seg_count * sizeof(uint16_t));
    font->cmap4_start_codes = malloc(seg_count * sizeof(uint16_t));
    font->cmap4_id_deltas = malloc(seg_count * sizeof(int16_t));
    font->cmap4_id_range_offsets = malloc(seg_count * sizeof(uint16_t));

    if (!font->cmap4_end_codes || !font->cmap4_start_codes || !font->cmap4_id_deltas ||
        !font->cmap4_id_range_offsets)
    {
        free(font->cmap4_end_codes);
        free(font->cmap4_start_codes);
        free(font->cmap4_id_deltas);
        free(font->cmap4_id_range_offsets);
        font->cmap4_end_codes = NULL;
        font->cmap4_start_codes = NULL;
        font->cmap4_id_deltas = NULL;
        font->cmap4_id_range_offsets = NULL;
        return false;
    }

    const uint8_t *p = subtable + 14;

    // End codes
    for (int i = 0; i < seg_count; i++)
    {
        font->cmap4_end_codes[i] = ttf_read_u16(p);
        p += 2;
    }

    p += 2; // Skip reserved pad

    // Start codes
    for (int i = 0; i < seg_count; i++)
    {
        font->cmap4_start_codes[i] = ttf_read_u16(p);
        p += 2;
    }

    // ID deltas
    for (int i = 0; i < seg_count; i++)
    {
        font->cmap4_id_deltas[i] = ttf_read_i16(p);
        p += 2;
    }

    // ID range offsets
    for (int i = 0; i < seg_count; i++)
    {
        font->cmap4_id_range_offsets[i] = ttf_read_u16(p);
        p += 2;
    }

    // Glyph ID array follows (remaining bytes)
    uint32_t glyph_ids_bytes = (subtable + length) - p;
    font->cmap4_glyph_ids_count = glyph_ids_bytes / 2;
    if (font->cmap4_glyph_ids_count > 0)
    {
        font->cmap4_glyph_ids = malloc(font->cmap4_glyph_ids_count * sizeof(uint16_t));
        if (font->cmap4_glyph_ids)
        {
            for (uint32_t i = 0; i < font->cmap4_glyph_ids_count; i++)
            {
                font->cmap4_glyph_ids[i] = ttf_read_u16(p);
                p += 2;
            }
        }
    }

    return true;
}

static bool ttf_parse_cmap_format12(vg_font_t *font, const uint8_t *subtable)
{
    uint32_t num_groups = ttf_read_u32(subtable + 12);

    if (num_groups == 0)
        return false;

    font->cmap12_num_groups = num_groups;
    font->cmap12_start_codes = malloc(num_groups * sizeof(uint32_t));
    font->cmap12_end_codes = malloc(num_groups * sizeof(uint32_t));
    font->cmap12_start_glyph_ids = malloc(num_groups * sizeof(uint32_t));

    if (!font->cmap12_start_codes || !font->cmap12_end_codes || !font->cmap12_start_glyph_ids)
    {
        free(font->cmap12_start_codes);
        free(font->cmap12_end_codes);
        free(font->cmap12_start_glyph_ids);
        font->cmap12_start_codes = NULL;
        font->cmap12_end_codes = NULL;
        font->cmap12_start_glyph_ids = NULL;
        font->cmap12_num_groups = 0;
        return false;
    }

    const uint8_t *p = subtable + 16;
    for (uint32_t i = 0; i < num_groups; i++)
    {
        font->cmap12_start_codes[i] = ttf_read_u32(p);
        font->cmap12_end_codes[i] = ttf_read_u32(p + 4);
        font->cmap12_start_glyph_ids[i] = ttf_read_u32(p + 8);
        p += 12;
    }

    return true;
}

bool ttf_parse_cmap(vg_font_t *font, const uint8_t *data, uint32_t len)
{
    if (len < 4)
        return false;

    uint16_t num_tables = ttf_read_u16(data + 2);

    // Look for format 12 (full Unicode) first, then format 4 (BMP)
    const uint8_t *format4_subtable = NULL;
    const uint8_t *format12_subtable = NULL;

    for (int i = 0; i < num_tables; i++)
    {
        const uint8_t *record = data + 4 + i * 8;
        uint16_t platform_id = ttf_read_u16(record);
        uint32_t offset = ttf_read_u32(record + 4);

        if (offset >= len)
            continue;

        const uint8_t *subtable = data + offset;
        uint16_t format = ttf_read_u16(subtable);

        // Prefer Unicode platform (0) or Windows (3)
        if (platform_id == 0 || platform_id == 3)
        {
            if (format == 4 && !format4_subtable)
            {
                format4_subtable = subtable;
            }
            else if (format == 12 && !format12_subtable)
            {
                format12_subtable = subtable;
            }
        }
    }

    // Parse format 12 if available (full Unicode support)
    if (format12_subtable)
    {
        ttf_parse_cmap_format12(font, format12_subtable);
    }

    // Always try to parse format 4 for BMP characters
    if (format4_subtable)
    {
        if (!ttf_parse_cmap_format4(font, format4_subtable))
        {
            return format12_subtable != NULL; // OK if we have format 12
        }
    }

    return font->cmap4_seg_count > 0 || font->cmap12_num_groups > 0;
}

//=============================================================================
// Parse 'kern' Table
//=============================================================================

bool ttf_parse_kern(vg_font_t *font, const uint8_t *data, uint32_t len)
{
    if (len < 4)
        return false;

    uint16_t num_tables = ttf_read_u16(data + 2);

    const uint8_t *p = data + 4;

    for (int t = 0; t < num_tables; t++)
    {
        if (p + 6 > data + len)
            break;

        uint16_t subtable_length = ttf_read_u16(p + 2);
        uint16_t coverage = ttf_read_u16(p + 4);

        // Only support format 0 (ordered list of kerning pairs)
        uint8_t format = coverage >> 8;
        if (format == 0 && subtable_length >= 14)
        {
            uint16_t num_pairs = ttf_read_u16(p + 6);

            if (num_pairs > 0)
            {
                font->kern_pairs = malloc(num_pairs * sizeof(ttf_kern_pair_t));
                if (font->kern_pairs)
                {
                    font->kern_pair_count = num_pairs;

                    const uint8_t *pair_data = p + 14;
                    for (uint16_t i = 0; i < num_pairs; i++)
                    {
                        font->kern_pairs[i].left = ttf_read_u16(pair_data);
                        font->kern_pairs[i].right = ttf_read_u16(pair_data + 2);
                        font->kern_pairs[i].value = ttf_read_i16(pair_data + 4);
                        pair_data += 6;
                    }
                }
            }
            break; // Only use first subtable
        }

        p += subtable_length;
    }

    return true;
}

//=============================================================================
// Parse 'name' Table
//=============================================================================

bool ttf_parse_name(vg_font_t *font, const uint8_t *data, uint32_t len)
{
    if (len < 6)
        return false;

    uint16_t count = ttf_read_u16(data + 2);
    uint16_t string_offset = ttf_read_u16(data + 4);

    const uint8_t *string_storage = data + string_offset;

    for (int i = 0; i < count; i++)
    {
        const uint8_t *record = data + 6 + i * 12;
        if (record + 12 > data + len)
            break;

        uint16_t platform_id = ttf_read_u16(record);
        uint16_t encoding_id = ttf_read_u16(record + 2);
        uint16_t name_id = ttf_read_u16(record + 6);
        uint16_t length = ttf_read_u16(record + 8);
        uint16_t offset = ttf_read_u16(record + 10);

        if (string_offset + offset + length > len)
            continue;

        const uint8_t *str = string_storage + offset;

        // Name ID 1 = Font Family, Name ID 2 = Font Subfamily
        char *dest = NULL;
        size_t dest_size = 0;

        if (name_id == 1 && font->family_name[0] == '\0')
        {
            dest = font->family_name;
            dest_size = sizeof(font->family_name);
        }
        else if (name_id == 2 && font->style_name[0] == '\0')
        {
            dest = font->style_name;
            dest_size = sizeof(font->style_name);
        }

        if (dest)
        {
            // Platform 3 (Windows) uses UTF-16BE
            if (platform_id == 3 && encoding_id == 1)
            {
                int j = 0;
                for (int k = 0; k < length && j < (int)dest_size - 1; k += 2)
                {
                    uint16_t ch = ttf_read_u16(str + k);
                    if (ch < 128)
                    {
                        dest[j++] = (char)ch;
                    }
                }
                dest[j] = '\0';
            }
            // Platform 1 (Mac) uses Mac Roman (treat as ASCII for basic chars)
            else if (platform_id == 1)
            {
                int copy_len = length < (int)dest_size - 1 ? length : (int)dest_size - 1;
                memcpy(dest, str, copy_len);
                dest[copy_len] = '\0';
            }
        }
    }

    return true;
}

//=============================================================================
// Parse All Tables
//=============================================================================

static int ttf_kern_pair_cmp(const void *a, const void *b)
{
    const ttf_kern_pair_t *pa = (const ttf_kern_pair_t *)a;
    const ttf_kern_pair_t *pb = (const ttf_kern_pair_t *)b;
    uint32_t ka = ((uint32_t)pa->left << 16) | pa->right;
    uint32_t kb = ((uint32_t)pb->left << 16) | pb->right;
    return (ka > kb) - (ka < kb);
}

bool ttf_parse_tables(vg_font_t *font)
{
    const uint8_t *data = font->data;

    // Validate sfnt version
    uint32_t sfnt_version = ttf_read_u32(data);
    if (sfnt_version != 0x00010000 && sfnt_version != TTF_TAG('t', 'r', 'u', 'e'))
    {
        // Not a TrueType font
        return false;
    }

    uint32_t len;
    const uint8_t *table;

    // Required tables
    table = ttf_find_table(font, TTF_TAG_HEAD, &len);
    if (!table || !ttf_parse_head(font, table, len))
        return false;

    table = ttf_find_table(font, TTF_TAG_HHEA, &len);
    if (!table || !ttf_parse_hhea(font, table, len))
        return false;

    table = ttf_find_table(font, TTF_TAG_MAXP, &len);
    if (!table || !ttf_parse_maxp(font, table, len))
        return false;

    table = ttf_find_table(font, TTF_TAG_CMAP, &len);
    if (!table || !ttf_parse_cmap(font, table, len))
        return false;

    // Store offsets for tables we'll need later
    table = ttf_find_table(font, TTF_TAG_GLYF, &len);
    if (table)
        font->glyf_offset = (uint32_t)(table - font->data);

    table = ttf_find_table(font, TTF_TAG_LOCA, &len);
    if (table)
        font->loca_offset = (uint32_t)(table - font->data);

    table = ttf_find_table(font, TTF_TAG_HMTX, &len);
    if (table)
        font->hmtx_offset = (uint32_t)(table - font->data);

    // Optional tables
    table = ttf_find_table(font, TTF_TAG_KERN, &len);
    if (table)
    {
        font->kern_offset = (uint32_t)(table - font->data);
        ttf_parse_kern(font, table, len);
        // Sort kern pairs by (left<<16)|right so binary search works correctly
        if (font->kern_pairs && font->kern_pair_count > 1)
        {
            qsort(font->kern_pairs,
                  font->kern_pair_count,
                  sizeof(ttf_kern_pair_t),
                  ttf_kern_pair_cmp);
        }
    }

    table = ttf_find_table(font, TTF_TAG_NAME, &len);
    if (table)
    {
        font->name_offset = (uint32_t)(table - font->data);
        ttf_parse_name(font, table, len);
    }

    return font->glyf_offset > 0 && font->loca_offset > 0;
}

//=============================================================================
// Glyph Index Lookup
//=============================================================================

uint16_t ttf_get_glyph_index(vg_font_t *font, uint32_t codepoint)
{
    // Try format 12 first (full Unicode)
    if (font->cmap12_num_groups > 0)
    {
        for (uint32_t i = 0; i < font->cmap12_num_groups; i++)
        {
            if (codepoint >= font->cmap12_start_codes[i] && codepoint <= font->cmap12_end_codes[i])
            {
                return font->cmap12_start_glyph_ids[i] + (codepoint - font->cmap12_start_codes[i]);
            }
        }
    }

    // Try format 4 (BMP only)
    if (font->cmap4_seg_count > 0 && codepoint <= 0xFFFF)
    {
        for (int i = 0; i < font->cmap4_seg_count; i++)
        {
            if (codepoint <= font->cmap4_end_codes[i])
            {
                if (codepoint >= font->cmap4_start_codes[i])
                {
                    if (font->cmap4_id_range_offsets[i] == 0)
                    {
                        return (uint16_t)((codepoint + font->cmap4_id_deltas[i]) & 0xFFFF);
                    }
                    else
                    {
                        // Calculate glyph ID from range offset
                        uint32_t idx = (font->cmap4_id_range_offsets[i] / 2) +
                                       (codepoint - font->cmap4_start_codes[i]) -
                                       (font->cmap4_seg_count - i);
                        if (idx < font->cmap4_glyph_ids_count)
                        {
                            uint16_t glyph_id = font->cmap4_glyph_ids[idx];
                            if (glyph_id != 0)
                            {
                                return (uint16_t)((glyph_id + font->cmap4_id_deltas[i]) & 0xFFFF);
                            }
                        }
                    }
                }
                break;
            }
        }
    }

    return 0; // .notdef glyph
}

//=============================================================================
// Horizontal Metrics
//=============================================================================

void ttf_get_h_metrics(vg_font_t *font,
                       uint16_t glyph_id,
                       int *advance_width,
                       int *left_side_bearing)
{
    if (font->hmtx_offset == 0)
    {
        *advance_width = font->head.units_per_em;
        *left_side_bearing = 0;
        return;
    }

    const uint8_t *hmtx = font->data + font->hmtx_offset;

    if (glyph_id < font->hhea.num_h_metrics)
    {
        *advance_width = ttf_read_u16(hmtx + glyph_id * 4);
        *left_side_bearing = ttf_read_i16(hmtx + glyph_id * 4 + 2);
    }
    else
    {
        // Use last advance width for glyphs beyond num_h_metrics
        *advance_width = ttf_read_u16(hmtx + (font->hhea.num_h_metrics - 1) * 4);
        // Left side bearing from array after long metrics
        uint32_t lsb_offset =
            font->hhea.num_h_metrics * 4 + (glyph_id - font->hhea.num_h_metrics) * 2;
        *left_side_bearing = ttf_read_i16(hmtx + lsb_offset);
    }
}

//=============================================================================
// Glyph Outline
//=============================================================================

static uint32_t ttf_get_glyph_offset(vg_font_t *font, uint16_t glyph_id)
{
    const uint8_t *loca = font->data + font->loca_offset;

    if (font->head.index_to_loc_format == 0)
    {
        // Short format (16-bit offsets, multiply by 2)
        return ttf_read_u16(loca + glyph_id * 2) * 2;
    }
    else
    {
        // Long format (32-bit offsets)
        return ttf_read_u32(loca + glyph_id * 4);
    }
}

// Forward declaration for recursive composite glyph handling
static bool ttf_get_simple_glyph_outline(vg_font_t *font,
                                         const uint8_t *glyph_data,
                                         int16_t num_contours,
                                         float **out_points_x,
                                         float **out_points_y,
                                         uint8_t **out_flags,
                                         int **out_contour_ends,
                                         int *out_num_points,
                                         int *out_num_contours);

// Composite glyph flags
#define COMP_ARG_1_AND_2_ARE_WORDS 0x0001
#define COMP_ARGS_ARE_XY_VALUES 0x0002
#define COMP_WE_HAVE_A_SCALE 0x0008
#define COMP_MORE_COMPONENTS 0x0020
#define COMP_WE_HAVE_AN_X_AND_Y_SCALE 0x0040
#define COMP_WE_HAVE_A_TWO_BY_TWO 0x0080

static bool ttf_get_composite_glyph_outline(vg_font_t *font,
                                            const uint8_t *glyph_data,
                                            float **out_points_x,
                                            float **out_points_y,
                                            uint8_t **out_flags,
                                            int **out_contour_ends,
                                            int *out_num_points,
                                            int *out_num_contours)
{
    // Start after the 10-byte glyph header
    const uint8_t *p = glyph_data + 10;

    // Collect all component points
    float *all_points_x = NULL;
    float *all_points_y = NULL;
    uint8_t *all_flags = NULL;
    int *all_contour_ends = NULL;
    int total_points = 0;
    int total_contours = 0;

    uint16_t flags;
    do
    {
        flags = ttf_read_u16(p);
        p += 2;
        uint16_t component_glyph_id = ttf_read_u16(p);
        p += 2;

        // Read translation offsets
        float dx = 0, dy = 0;
        if (flags & COMP_ARGS_ARE_XY_VALUES)
        {
            if (flags & COMP_ARG_1_AND_2_ARE_WORDS)
            {
                dx = (float)ttf_read_i16(p);
                p += 2;
                dy = (float)ttf_read_i16(p);
                p += 2;
            }
            else
            {
                dx = (float)(int8_t)(*p++);
                dy = (float)(int8_t)(*p++);
            }
        }
        else
        {
            // Point indices - skip for now
            if (flags & COMP_ARG_1_AND_2_ARE_WORDS)
            {
                p += 4;
            }
            else
            {
                p += 2;
            }
        }

        // Read scale/matrix (skip for now, just advance pointer)
        float scale_x = 1.0f, scale_y = 1.0f;
        if (flags & COMP_WE_HAVE_A_SCALE)
        {
            int16_t scale = ttf_read_i16(p);
            p += 2;
            scale_x = scale_y = (float)scale / 16384.0f;
        }
        else if (flags & COMP_WE_HAVE_AN_X_AND_Y_SCALE)
        {
            scale_x = (float)ttf_read_i16(p) / 16384.0f;
            p += 2;
            scale_y = (float)ttf_read_i16(p) / 16384.0f;
            p += 2;
        }
        else if (flags & COMP_WE_HAVE_A_TWO_BY_TWO)
        {
            p += 8; // Skip 2x2 matrix for now
        }

        // Get component glyph outline recursively
        float *comp_x = NULL;
        float *comp_y = NULL;
        uint8_t *comp_flags = NULL;
        int *comp_contours = NULL;
        int comp_num_points = 0;
        int comp_num_contours = 0;

        if (component_glyph_id < font->maxp.num_glyphs)
        {
            uint32_t comp_offset = ttf_get_glyph_offset(font, component_glyph_id);
            uint32_t comp_next = ttf_get_glyph_offset(font, component_glyph_id + 1);

            if (comp_offset != comp_next)
            {
                const uint8_t *comp_data = font->data + font->glyf_offset + comp_offset;
                int16_t comp_contour_count = ttf_read_i16(comp_data);

                if (comp_contour_count >= 0)
                {
                    ttf_get_simple_glyph_outline(font,
                                                 comp_data,
                                                 comp_contour_count,
                                                 &comp_x,
                                                 &comp_y,
                                                 &comp_flags,
                                                 &comp_contours,
                                                 &comp_num_points,
                                                 &comp_num_contours);
                }
                // Note: nested composites not supported to avoid infinite recursion
            }
        }

        if (comp_num_points > 0)
        {
            // Apply transformation and merge
            for (int i = 0; i < comp_num_points; i++)
            {
                comp_x[i] = comp_x[i] * scale_x + dx;
                comp_y[i] = comp_y[i] * scale_y + dy;
            }

            // Adjust contour end indices
            for (int i = 0; i < comp_num_contours; i++)
            {
                comp_contours[i] += total_points;
            }

            // Expand arrays — use temporaries so a partial failure doesn't orphan
            // allocations. realloc success frees the old pointer, so checking all four
            // results after the fact can miss already-freed buffers.
            {
                size_t np = (size_t)(total_points + comp_num_points);
                size_t nc = (size_t)(total_contours + comp_num_contours);
                float   *tmp_x = (float   *)realloc(all_points_x,    np * sizeof(float));
                float   *tmp_y = (float   *)realloc(all_points_y,    np * sizeof(float));
                uint8_t *tmp_f = (uint8_t *)realloc(all_flags,       np * sizeof(uint8_t));
                int     *tmp_c = (int     *)realloc(all_contour_ends, nc * sizeof(int));

                if (!tmp_x || !tmp_y || !tmp_f || !tmp_c)
                {
                    // Free whichever allocations succeeded (on success the old pointer is freed
                    // and tmp_* holds the only valid reference; on failure the original is still valid).
                    free(tmp_x ? tmp_x : all_points_x);
                    free(tmp_y ? tmp_y : all_points_y);
                    free(tmp_f ? tmp_f : all_flags);
                    free(tmp_c ? tmp_c : all_contour_ends);
                    all_points_x = all_points_y = NULL;
                    all_flags = NULL;
                    all_contour_ends = NULL;
                    total_points = total_contours = 0;
                    free(comp_x);
                    free(comp_y);
                    free(comp_flags);
                    free(comp_contours);
                    break;
                }

                all_points_x     = tmp_x;
                all_points_y     = tmp_y;
                all_flags        = tmp_f;
                all_contour_ends = tmp_c;

                memcpy(all_points_x + total_points, comp_x, comp_num_points * sizeof(float));
                memcpy(all_points_y + total_points, comp_y, comp_num_points * sizeof(float));
                memcpy(all_flags + total_points, comp_flags, comp_num_points * sizeof(uint8_t));
                memcpy(all_contour_ends + total_contours,
                       comp_contours,
                       comp_num_contours * sizeof(int));

                total_points  += comp_num_points;
                total_contours += comp_num_contours;
            }

            free(comp_x);
            free(comp_y);
            free(comp_flags);
            free(comp_contours);
        }
    } while (flags & COMP_MORE_COMPONENTS);

    *out_points_x = all_points_x;
    *out_points_y = all_points_y;
    *out_flags = all_flags;
    *out_contour_ends = all_contour_ends;
    *out_num_points = total_points;
    *out_num_contours = total_contours;

    return true;
}

bool ttf_get_glyph_outline(vg_font_t *font,
                           uint16_t glyph_id,
                           float **out_points_x,
                           float **out_points_y,
                           uint8_t **out_flags,
                           int **out_contour_ends,
                           int *out_num_points,
                           int *out_num_contours)
{
    if (glyph_id >= font->maxp.num_glyphs)
        return false;

    uint32_t offset = ttf_get_glyph_offset(font, glyph_id);
    uint32_t next_offset = ttf_get_glyph_offset(font, glyph_id + 1);

    // Empty glyph (like space)
    if (offset == next_offset)
    {
        *out_num_points = 0;
        *out_num_contours = 0;
        *out_points_x = NULL;
        *out_points_y = NULL;
        *out_flags = NULL;
        *out_contour_ends = NULL;
        return true;
    }

    const uint8_t *glyph = font->data + font->glyf_offset + offset;
    int16_t num_contours = ttf_read_i16(glyph);

    // Composite glyph (num_contours < 0)
    if (num_contours < 0)
    {
        return ttf_get_composite_glyph_outline(font,
                                               glyph,
                                               out_points_x,
                                               out_points_y,
                                               out_flags,
                                               out_contour_ends,
                                               out_num_points,
                                               out_num_contours);
    }

    // Simple glyph - delegate to helper function
    return ttf_get_simple_glyph_outline(font,
                                        glyph,
                                        num_contours,
                                        out_points_x,
                                        out_points_y,
                                        out_flags,
                                        out_contour_ends,
                                        out_num_points,
                                        out_num_contours);
}

static bool ttf_get_simple_glyph_outline(vg_font_t *font,
                                         const uint8_t *glyph_data,
                                         int16_t num_contours,
                                         float **out_points_x,
                                         float **out_points_y,
                                         uint8_t **out_flags,
                                         int **out_contour_ends,
                                         int *out_num_points,
                                         int *out_num_contours)
{
    (void)font;                         // Unused in simple glyph case
    const uint8_t *p = glyph_data + 10; // Skip header

    // Read contour end points
    int *contour_ends = malloc(num_contours * sizeof(int));
    if (!contour_ends)
        return false;

    int total_points = 0;
    for (int i = 0; i < num_contours; i++)
    {
        contour_ends[i] = ttf_read_u16(p);
        p += 2;
        if (contour_ends[i] >= total_points)
        {
            total_points = contour_ends[i] + 1;
        }
    }

    // Skip instructions
    uint16_t instruction_length = ttf_read_u16(p);
    p += 2 + instruction_length;

    // Allocate output arrays
    float *points_x = malloc(total_points * sizeof(float));
    float *points_y = malloc(total_points * sizeof(float));
    uint8_t *flags = malloc(total_points * sizeof(uint8_t));

    if (!points_x || !points_y || !flags)
    {
        free(contour_ends);
        free(points_x);
        free(points_y);
        free(flags);
        return false;
    }

    // Read flags (with repeat handling)
    int flags_read = 0;
    while (flags_read < total_points)
    {
        uint8_t flag = *p++;
        flags[flags_read++] = flag;

        if (flag & 0x08)
        { // Repeat flag
            uint8_t repeat_count = *p++;
            for (int r = 0; r < repeat_count && flags_read < total_points; r++)
            {
                flags[flags_read++] = flag;
            }
        }
    }

    // Read x coordinates
    int16_t x = 0;
    for (int i = 0; i < total_points; i++)
    {
        uint8_t flag = flags[i];
        if (flag & 0x02)
        { // x is 1 byte
            int16_t dx = *p++;
            if (!(flag & 0x10))
                dx = -dx; // Sign
            x += dx;
        }
        else if (!(flag & 0x10))
        { // x is 2 bytes
            x += ttf_read_i16(p);
            p += 2;
        }
        // else: x is same as previous (delta = 0)
        points_x[i] = (float)x;
    }

    // Read y coordinates
    int16_t y = 0;
    for (int i = 0; i < total_points; i++)
    {
        uint8_t flag = flags[i];
        if (flag & 0x04)
        { // y is 1 byte
            int16_t dy = *p++;
            if (!(flag & 0x20))
                dy = -dy; // Sign
            y += dy;
        }
        else if (!(flag & 0x20))
        { // y is 2 bytes
            y += ttf_read_i16(p);
            p += 2;
        }
        // else: y is same as previous (delta = 0)
        points_y[i] = (float)y;
    }

    // Convert flags to on-curve indicator (bit 0)
    for (int i = 0; i < total_points; i++)
    {
        flags[i] = flags[i] & 0x01; // Keep only on-curve bit
    }

    *out_points_x = points_x;
    *out_points_y = points_y;
    *out_flags = flags;
    *out_contour_ends = contour_ends;
    *out_num_points = total_points;
    *out_num_contours = num_contours;

    return true;
}
