//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_fbx_loader_parse.inc  (#included from rt_fbx_loader.c;
// not a standalone translation unit) Purpose: FBX binary reader, node tree, array decompression,
// property/node parsing, queries, connections
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Binary reader helpers
 *=========================================================================*/

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t version;
    int is_64bit; /* version >= 7500 */
    int error;
} fbx_reader_t;

/// @brief Treat POSIX roots, UNC-style roots, and `C:` prefixes as absolute paths.
static int fbx_is_absolute_path(const char *path) {
    if (!path || !*path)
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

/// @brief Copy a path while normalising separators to `/`.
static void fbx_normalize_path(char *dst, size_t dst_size, const char *src) {
    size_t di = 0;
    if (!dst || dst_size == 0)
        return;
    if (!src)
        src = "";
    while (*src && di + 1 < dst_size) {
        char ch = *src++;
        dst[di++] = (ch == '\\') ? '/' : ch;
    }
    dst[di] = '\0';
}

/// @brief Normalize a path into a malloc-owned string with `/` separators.
/// @details Used by external texture resolution so long but valid FBX texture references are
///          bounded by `RT_FBX_MAX_TEXTURE_PATH_BYTES` instead of a small stack buffer.
/// @return Malloc-owned normalized path, or NULL on allocation failure or overlong input.
static char *fbx_normalize_path_alloc(const char *src) {
    size_t len;
    char *out;
    if (!src)
        src = "";
    len = strlen(src);
    if (len > RT_FBX_MAX_TEXTURE_PATH_BYTES)
        return NULL;
    out = (char *)malloc(len + 1u);
    if (!out)
        return NULL;
    fbx_normalize_path(out, len + 1u, src);
    return out;
}

/// @brief Return the last path component inside @p path.
static const char *fbx_path_basename(const char *path) {
    const char *last = path;
    if (!path)
        return "";
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            last = p + 1;
    }
    return last;
}

/// @brief Extract the directory portion of @p path into a malloc-owned string.
/// @return Directory path using `/` separators, an empty string when no directory exists, or NULL.
static char *fbx_parent_dir_alloc(const char *path) {
    char *normalized;
    char *out;
    const char *last_sep;
    size_t dir_len;
    normalized = fbx_normalize_path_alloc(path);
    if (!normalized)
        return NULL;
    last_sep = strrchr(normalized, '/');
    if (!last_sep) {
        normalized[0] = '\0';
        return normalized;
    }
    dir_len = (size_t)(last_sep - normalized);
    if (dir_len == 0 && normalized[0] == '/')
        dir_len = 1;
    out = (char *)malloc(dir_len + 1u);
    if (!out) {
        free(normalized);
        return NULL;
    }
    memcpy(out, normalized, dir_len);
    out[dir_len] = '\0';
    free(normalized);
    return out;
}

/// @brief Join a directory and leaf filename using `/` into a malloc-owned path.
/// @return Malloc-owned joined path, or NULL for empty/unsafe/overlong input.
static char *fbx_join_path_alloc(const char *dir, const char *leaf) {
    char *clean_leaf;
    char *clean_dir;
    char *out;
    size_t dir_len;
    size_t leaf_len;
    if (!leaf || !*leaf)
        return NULL;
    clean_leaf = fbx_normalize_path_alloc(leaf);
    if (!clean_leaf) {
        rt_asset_error_add_warning("FBX.Load: texture filename is too long; texture skipped");
        return NULL;
    }
    leaf_len = strlen(clean_leaf);
    if (leaf_len == 0 || leaf_len > RT_FBX_MAX_TEXTURE_PATH_BYTES) {
        free(clean_leaf);
        return NULL;
    }
    if (!dir || !*dir) {
        return clean_leaf;
    }
    clean_dir = fbx_normalize_path_alloc(dir);
    if (!clean_dir) {
        free(clean_leaf);
        return NULL;
    }
    dir_len = strlen(clean_dir);
    int needs_sep = dir_len > 0 && clean_dir[dir_len - 1u] != '/';
    if (dir_len > RT_FBX_MAX_TEXTURE_PATH_BYTES || dir_len > SIZE_MAX - leaf_len - 2u ||
        dir_len + (needs_sep ? 1u : 0u) + leaf_len > RT_FBX_MAX_TEXTURE_PATH_BYTES) {
        free(clean_dir);
        free(clean_leaf);
        rt_asset_error_add_warning("FBX.Load: joined texture path is too long; texture skipped");
        return NULL;
    }
    out = (char *)malloc(dir_len + (needs_sep ? 1u : 0u) + leaf_len + 1u);
    if (!out) {
        free(clean_dir);
        free(clean_leaf);
        return NULL;
    }
    memcpy(out, clean_dir, dir_len);
    if (needs_sep)
        out[dir_len++] = '/';
    memcpy(out + dir_len, clean_leaf, leaf_len + 1u);
    free(clean_dir);
    free(clean_leaf);
    return out;
}

/// @brief Sanitize a texture reference into a safe malloc-owned relative path.
/// @details Path-traversal guard: normalizes separators, rejects absolute paths, URI schemes, and
///          any `..` segment, then collapses `.` and redundant slashes into `/`-joined segments.
/// @return Malloc-owned safe relative path, or NULL if empty, unsafe, overlong, or unavailable.
static char *fbx_normalize_relative_texture_ref_alloc(const char *src) {
    char *normalized;
    char *out;
    const char *p;
    size_t out_len = 0;
    size_t out_cap;
    int wrote_segment = 0;
    if (!src || !*src)
        return NULL;
    normalized = fbx_normalize_path_alloc(src);
    if (!normalized)
        return NULL;
    if (!*normalized || fbx_is_absolute_path(normalized) || strchr(normalized, ':')) {
        free(normalized);
        return NULL;
    }
    for (const char *q = normalized; *q; ++q) {
        unsigned char ch = (unsigned char)*q;
        if (ch < 0x20u || ch == 0x7fu) {
            free(normalized);
            return NULL;
        }
    }
    out_cap = strlen(normalized) + 1u;
    out = (char *)malloc(out_cap);
    if (!out) {
        free(normalized);
        return NULL;
    }
    p = normalized;
    out[0] = '\0';
    while (*p) {
        const char *seg;
        size_t seg_len;
        while (*p == '/')
            p++;
        seg = p;
        while (*p && *p != '/')
            p++;
        seg_len = (size_t)(p - seg);
        if (seg_len == 0 || (seg_len == 1 && seg[0] == '.'))
            continue;
        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
            free(normalized);
            free(out);
            return NULL;
        }
        if (wrote_segment) {
            if (out_len + 1 >= out_cap) {
                free(normalized);
                free(out);
                return NULL;
            }
            out[out_len++] = '/';
        }
        if (seg_len >= out_cap || out_len > out_cap - 1 - seg_len) {
            free(normalized);
            free(out);
            return NULL;
        }
        memcpy(out + out_len, seg, seg_len);
        out_len += seg_len;
        out[out_len] = '\0';
        wrote_segment = 1;
    }
    free(normalized);
    if (!wrote_segment) {
        free(out);
        return NULL;
    }
    return out;
}

/// @brief Return non-zero for URI-style paths such as file:/, http:, or data:.
/// @details A Windows drive prefix (`C:`) is not a URI scheme and is handled as an absolute path.
static int fbx_has_uri_scheme(const char *path) {
    const char *p;
    if (!path || !*path)
        return 0;
    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            return 0;
        if (*p == ':') {
            if (p == path + 1 && isalpha((unsigned char)path[0]))
                return 0;
            return p > path;
        }
    }
    return 0;
}

/// @brief Return whether absolute FBX texture references may be loaded directly.
/// @details Absolute paths can expose arbitrary local files when loading untrusted FBX assets, so
///          they are disabled by default. Set VIPER_FBX_ALLOW_ABSOLUTE_TEXTURES=1 to opt in.
/// @return Non-zero when absolute texture loading is explicitly enabled.
static int fbx_absolute_texture_paths_allowed(void) {
    const char *value = getenv("VIPER_FBX_ALLOW_ABSOLUTE_TEXTURES");
    return value && value[0] == '1' && value[1] == '\0';
}

/// @brief Try a local texture reference, then fall back to its basename beside the FBX file.
static void *fbx_try_load_texture_path(const char *fbx_path, const char *texture_ref) {
    char *raw_ref = NULL;
    char *normalized_ref = NULL;
    char *dir = NULL;
    char *candidate = NULL;
    const char *basename;
    void *pixels = NULL;

    if (!texture_ref || !*texture_ref)
        return NULL;
    raw_ref = fbx_normalize_path_alloc(texture_ref);
    if (!raw_ref) {
        rt_asset_error_add_warning("FBX.Load: texture reference is too long; texture skipped");
        return NULL;
    }
    if (!*raw_ref || fbx_has_uri_scheme(raw_ref))
        goto done;
    dir = fbx_parent_dir_alloc(fbx_path);
    if (!dir)
        goto done;
    if (fbx_is_absolute_path(raw_ref) && fbx_absolute_texture_paths_allowed()) {
        pixels = rt_pixels_load(rt_const_cstr(raw_ref));
        if (pixels)
            goto done;
    }

    normalized_ref = fbx_normalize_relative_texture_ref_alloc(raw_ref);
    if (normalized_ref) {
        if (*dir) {
            candidate = fbx_join_path_alloc(dir, normalized_ref);
            if (candidate)
                pixels = rt_pixels_load(rt_const_cstr(candidate));
        } else {
            pixels = rt_pixels_load(rt_const_cstr(normalized_ref));
        }
        if (pixels)
            goto done;
        free(candidate);
        candidate = NULL;
    }

    basename = fbx_path_basename(raw_ref);
    if (!basename || !*basename)
        goto done;
    if (normalized_ref && strcmp(basename, normalized_ref) == 0 && !*dir)
        goto done;

    if (*dir) {
        candidate = fbx_join_path_alloc(dir, basename);
        if (!candidate)
            goto done;
    } else {
        candidate = fbx_normalize_path_alloc(basename);
        if (!candidate)
            goto done;
    }
    pixels = rt_pixels_load(rt_const_cstr(candidate));

done:
    free(candidate);
    free(dir);
    free(normalized_ref);
    free(raw_ref);
    return pixels;
}

/// @brief Case-insensitive test (ASCII) for whether @p text ends with @p suffix.
static int fbx_ascii_ends_with_i(const char *text, const char *suffix) {
    size_t text_len;
    size_t suffix_len;
    if (!text || !suffix)
        return 0;
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len)
        return 0;
    text += text_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        if (tolower((unsigned char)text[i]) != tolower((unsigned char)suffix[i]))
            return 0;
    }
    return 1;
}

/// @brief Case-insensitive ASCII substring search.
static int fbx_ascii_contains_i(const char *text, const char *needle) {
    size_t needle_len;
    if (!text || !needle)
        return 0;
    needle_len = strlen(needle);
    if (needle_len == 0)
        return 1;
    for (const char *p = text; *p; ++p) {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == needle_len)
            return 1;
    }
    return 0;
}

/// @brief Case-insensitive ASCII string equality (NULL operands compare unequal).
static int fbx_ascii_equals_i(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb))
            return 0;
    }
    return *a == '\0' && *b == '\0';
}

/// @brief Build a Pixels object from a width×height RGBA32 buffer (copied), with
///   overflow-checked sizing. Returns NULL on bad dimensions or allocation failure.
static void *fbx_pixels_from_rgba32(uint32_t *rgba32, int64_t width, int64_t height) {
    rt_pixels_impl *pixels;
    size_t pixel_count;
    size_t pixel_bytes;
    if (!rgba32 || width <= 0 || height <= 0)
        return NULL;
    if ((uint64_t)width > SIZE_MAX || (uint64_t)height > SIZE_MAX)
        return NULL;
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return NULL;
    pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / sizeof(uint32_t))
        return NULL;
    pixel_bytes = pixel_count * sizeof(uint32_t);
    pixels = pixels_alloc(width, height);
    if (!pixels)
        return NULL;
    memcpy(pixels->data, rgba32, pixel_bytes);
    if (pixel_count > 0)
        pixels_touch(pixels);
    return pixels;
}

/// @brief Sniff a PNG payload by its 8-byte signature.
static int fbx_payload_looks_png(const uint8_t *data, size_t len) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    return data && len >= sizeof(sig) && memcmp(data, sig, sizeof(sig)) == 0;
}

/// @brief Sniff a JPEG payload by its 0xFFD8 start-of-image marker.
static int fbx_payload_looks_jpeg(const uint8_t *data, size_t len) {
    return data && len >= 2u && data[0] == 0xff && data[1] == 0xd8;
}

/// @brief Sniff a GIF payload by its "GIF87a"/"GIF89a" header.
static int fbx_payload_looks_gif(const uint8_t *data, size_t len) {
    return data && len >= 6u && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0);
}

/// @brief Decode an embedded texture payload into a Pixels object, selecting the codec by
///   magic bytes or by @p name's extension (PNG/JPEG/GIF/...). NULL if unrecognized or
///   undecodable.
static void *fbx_decode_texture_payload(const char *name, const uint8_t *data, size_t len) {
    uint32_t *rgba32 = NULL;
    int64_t width = 0;
    int64_t height = 0;
    void *pixels = NULL;
    if (!data || len == 0)
        return NULL;
    if (fbx_payload_looks_png(data, len) || fbx_ascii_ends_with_i(name, ".png")) {
        if (rt_png_decode_buffer_rgba32(data, len, &rgba32, &width, &height))
            pixels = fbx_pixels_from_rgba32(rgba32, width, height);
        free(rgba32);
        return pixels;
    }
    if (fbx_payload_looks_jpeg(data, len) || fbx_ascii_ends_with_i(name, ".jpg") ||
        fbx_ascii_ends_with_i(name, ".jpeg")) {
        if (rt_jpeg_decode_buffer_rgba32(data, len, &rgba32, &width, &height))
            pixels = fbx_pixels_from_rgba32(rgba32, width, height);
        free(rgba32);
        return pixels;
    }
    if (fbx_payload_looks_gif(data, len) || fbx_ascii_ends_with_i(name, ".gif")) {
        int gif_width = 0;
        int gif_height = 0;
        if (rt_gif_decode_memory_first_rgba32(data, len, &rgba32, &gif_width, &gif_height))
            pixels = fbx_pixels_from_rgba32(rgba32, (int64_t)gif_width, (int64_t)gif_height);
        free(rgba32);
        return pixels;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// FBX is little-endian; v >= 7500 promotes lengths/offsets to 64-bit
// (`is_64bit` flag). Short reads are marked as hard parse errors so corrupt
// assets do not surface as partially imported runtime content.
// ---------------------------------------------------------------------------

/// @brief True if the cursor has consumed the entire input.
static int fbx_eof(const fbx_reader_t *r) {
    return r->pos >= r->len;
}

/// @brief Ensure `n` bytes remain in the stream; sets the error flag and seeks to EOF if not.
static int fbx_require(fbx_reader_t *r, size_t n) {
    if (!r || r->error)
        return 0;
    if (r->pos > r->len || n > r->len - r->pos) {
        r->error = 1;
        r->pos = r->len;
        return 0;
    }
    return 1;
}

/// @brief Read one unsigned byte; marks the reader malformed at EOF.
static uint8_t fbx_u8(fbx_reader_t *r) {
    if (!fbx_require(r, 1))
        return 0;
    return r->data[r->pos++];
}

/// @brief Read a little-endian uint16; marks the reader malformed at short input.
static uint16_t fbx_u16(fbx_reader_t *r) {
    if (!fbx_require(r, 2))
        return 0;
    uint16_t v = (uint16_t)r->data[r->pos] | ((uint16_t)r->data[r->pos + 1] << 8);
    r->pos += 2;
    return v;
}

/// @brief Read a little-endian uint32; marks the reader malformed at short input.
static uint32_t fbx_u32(fbx_reader_t *r) {
    if (!fbx_require(r, 4))
        return 0;
    uint32_t v = (uint32_t)r->data[r->pos] | ((uint32_t)r->data[r->pos + 1] << 8) |
                 ((uint32_t)r->data[r->pos + 2] << 16) | ((uint32_t)r->data[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

/// @brief Read a little-endian int32; 0 at short input.
static int32_t fbx_i32(fbx_reader_t *r) {
    return (int32_t)fbx_u32(r);
}

/// @brief Read a little-endian uint64 (composed of two u32 halves).
static uint64_t fbx_u64(fbx_reader_t *r) {
    if (!fbx_require(r, 8))
        return 0;
    uint64_t lo = fbx_u32(r);
    uint64_t hi = fbx_u32(r);
    return lo | (hi << 32);
}

/// @brief Read a little-endian int64.
static int64_t fbx_i64(fbx_reader_t *r) {
    return (int64_t)fbx_u64(r);
}

/// @brief Read an IEEE 754 single-precision float (memcpy bit-pattern; safe for aliasing).
static float fbx_f32(fbx_reader_t *r) {
    uint32_t bits = fbx_u32(r);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

/// @brief Read an IEEE 754 double-precision float.
static double fbx_f64(fbx_reader_t *r) {
    uint64_t bits = fbx_u64(r);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

/// @brief Advance the cursor by `n` bytes, marking malformed if it would overrun.
static void fbx_skip(fbx_reader_t *r, size_t n) {
    if (!fbx_require(r, n))
        return;
    r->pos += n;
}

/*==========================================================================
 * FBX node tree
 *=========================================================================*/

#define FBX_MAX_CHILDREN 16384
#define FBX_MAX_NODE_DEPTH 512
#define FBX_MAX_NODE_PROPS 4096
#define FBX_MAX_PROPERTY_BYTES (64u * 1024u * 1024u)
#define FBX_MAX_COMPRESSED_ARRAY_BYTES (256u * 1024u * 1024u)
#define FBX_MAX_DECOMPRESSED_ARRAY_BYTES (256u * 1024u * 1024u)

typedef struct {
    char type; /* C/Y/I/L/F/D/S/R/b/i/l/f/d */

    union {
        int8_t bool_val;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;

        struct {
            char *str;
            uint32_t len;
        } string;

        struct {
            uint8_t *data;
            uint32_t len;
        } raw;

        struct {
            void *data;
            uint32_t count;
            char elem_type;
        } array;
    } v;
} fbx_prop_t;

typedef struct fbx_node {
    char name[128];
    fbx_prop_t *props;
    int32_t prop_count;
    struct fbx_node *children;
    int32_t child_count;
    int32_t child_capacity;
} fbx_node_t;

static fbx_node_t *fbx_find_object_by_id(fbx_node_t *objects, int64_t id);

/*==========================================================================
 * Array decompression (zlib → raw DEFLATE → rt_compress_inflate)
 *=========================================================================*/

static uint32_t fbx_adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1u;
    uint32_t b = 0u;
    for (size_t i = 0; i < len; ++i) {
        a += data ? data[i] : 0u;
        a %= 65521u;
        b += a;
        b %= 65521u;
    }
    return (b << 16) | a;
}

/// @brief Inflate a zlib-wrapped FBX array property to raw bytes.
///
/// FBX wraps array data in standard zlib (2-byte CMF/FLG header
/// + DEFLATE payload + 4-byte adler32). We strip the header,
/// hand the raw DEFLATE to `rt_compress_inflate`, and validate
/// the result equals `count * elem_size` bytes.
/// @return Newly-allocated buffer (caller `free`s) or NULL on failure.
static void *fbx_decompress_array(const uint8_t *data,
                                  uint32_t comp_len,
                                  uint32_t count,
                                  uint32_t elem_size) {
    size_t expected;
    uint8_t *inflated = NULL;
    size_t inflated_len = 0;
    if (comp_len < 6)
        return NULL;
    if (comp_len > FBX_MAX_COMPRESSED_ARRAY_BYTES)
        return NULL;
    if (!rt_untrusted_count_ok((int64_t)count, (size_t)elem_size, FBX_MAX_DECOMPRESSED_ARRAY_BYTES))
        return NULL;
    expected = (size_t)count * (size_t)elem_size;
    if ((data[0] & 0x0fu) != 8u || (data[0] >> 4) > 7u ||
        ((((uint32_t)data[0] << 8) | (uint32_t)data[1]) % 31u) != 0u || (data[1] & 0x20u) != 0u)
        return NULL;
    uint32_t deflate_len = comp_len - 6; /* strip 2-byte header + 4-byte adler32 */
    uint32_t expected_adler = ((uint32_t)data[comp_len - 4] << 24) |
                              ((uint32_t)data[comp_len - 3] << 16) |
                              ((uint32_t)data[comp_len - 2] << 8) | (uint32_t)data[comp_len - 1];

    if (!rt_compress_inflate_raw(data + 2, deflate_len, expected, &inflated, &inflated_len))
        return NULL;
    if (inflated_len != expected) {
        free(inflated);
        return NULL;
    }
    if (fbx_adler32(inflated, expected) != expected_adler) {
        free(inflated);
        return NULL;
    }
    return inflated;
}

/*==========================================================================
 * Property parsing
 *=========================================================================*/

/// @brief Parse one FBX node property into `*prop`.
///
/// FBX property types are encoded as a single ASCII type-byte:
///   - Y/I/L/F/D — scalar int16/int32/int64/float/double
///   - C/b      — bool/raw-byte
///   - S/R      — string / raw byte array
///   - lowercase i/l/f/d — array (with optional zlib compression)
/// Arrays carry a (count, encoding, compressed_len) header before
/// the data; if `encoding == 1` we hand the bytes to
/// `fbx_decompress_array`.
/// @return 1 on success, 0 on malformed property / unknown type.
static int fbx_parse_property(fbx_reader_t *r, fbx_prop_t *prop) {
    if (fbx_eof(r))
        return -1;
    prop->type = (char)fbx_u8(r);
    if (r->error)
        return -1;
    switch (prop->type) {
        case 'C':
            prop->v.bool_val = (int8_t)fbx_u8(r);
            break;
        case 'Y':
            prop->v.i16 = (int16_t)fbx_u16(r);
            break;
        case 'I':
            prop->v.i32 = fbx_i32(r);
            break;
        case 'L':
            prop->v.i64 = fbx_i64(r);
            break;
        case 'F':
            prop->v.f32 = fbx_f32(r);
            break;
        case 'D':
            prop->v.f64 = fbx_f64(r);
            break;
        case 'S':
        case 'R': {
            uint32_t len = fbx_u32(r);
            if (len > FBX_MAX_PROPERTY_BYTES)
                return -1;
            if (!fbx_require(r, len))
                return -1;
            if (prop->type == 'S') {
                prop->v.string.str = (char *)malloc(len + 1);
                if (!prop->v.string.str)
                    return -1;
                memcpy(prop->v.string.str, r->data + r->pos, len);
                prop->v.string.str[len] = '\0';
                prop->v.string.len = len;
            } else {
                prop->v.raw.data = (uint8_t *)malloc(len);
                if (len > 0 && !prop->v.raw.data)
                    return -1;
                if (len > 0)
                    memcpy(prop->v.raw.data, r->data + r->pos, len);
                prop->v.raw.len = len;
            }
            fbx_skip(r, len);
            break;
        }
        case 'b':
        case 'i':
        case 'l':
        case 'f':
        case 'd': {
            uint32_t count = fbx_u32(r);
            uint32_t encoding = fbx_u32(r);
            uint32_t comp_len = fbx_u32(r);
            if (!fbx_require(r, comp_len))
                return -1;

            uint32_t elem_size = 0;
            switch (prop->type) {
                case 'b':
                    elem_size = 1;
                    break;
                case 'i':
                    elem_size = 4;
                    break;
                case 'l':
                    elem_size = 8;
                    break;
                case 'f':
                    elem_size = 4;
                    break;
                case 'd':
                    elem_size = 8;
                    break;
            }

            prop->v.array.count = count;
            prop->v.array.elem_type = prop->type;
            if (!rt_untrusted_count_ok((int64_t)count,
                                       (size_t)elem_size,
                                       encoding == 0 ? (size_t)comp_len
                                                     : FBX_MAX_DECOMPRESSED_ARRAY_BYTES))
                return -1;
            size_t expected = (size_t)count * (size_t)elem_size;
            if (encoding == 1) {
                prop->v.array.data =
                    fbx_decompress_array(r->data + r->pos, comp_len, count, elem_size);
                if (count > 0 && !prop->v.array.data)
                    return -1;
            } else if (encoding == 0) {
                if (comp_len != expected)
                    return -1;
                prop->v.array.data = malloc(expected);
                if (expected > 0 && !prop->v.array.data)
                    return -1;
                if (expected > 0)
                    memcpy(prop->v.array.data, r->data + r->pos, expected);
            } else {
                return -1;
            }
            fbx_skip(r, comp_len);
            break;
        }
        default:
            return -1;
    }
    return 0;
}

/*==========================================================================
 * Node parsing (recursive)
 *=========================================================================*/

/// @brief Release any heap allocations owned by a property (string/raw/array buffers).
static void fbx_free_prop(fbx_prop_t *p) {
    switch (p->type) {
        case 'S':
            free(p->v.string.str);
            break;
        case 'R':
            free(p->v.raw.data);
            break;
        case 'b':
        case 'i':
        case 'l':
        case 'f':
        case 'd':
            free(p->v.array.data);
            break;
        default:
            break;
    }
}

/// @brief Recursively free an FBX node tree (props + children).
static void fbx_free_node(fbx_node_t *n) {
    for (int32_t i = 0; i < n->prop_count; i++)
        fbx_free_prop(&n->props[i]);
    free(n->props);
    for (int32_t i = 0; i < n->child_count; i++)
        fbx_free_node(&n->children[i]);
    free(n->children);
}

/// @brief Parse one FBX node (header + properties + children) at the cursor.
///
/// Each node header carries `end_offset`, `prop_count`,
/// `prop_list_len`, and a name. After consuming the properties we
/// recurse on child nodes until we reach the sentinel "null record"
/// (an all-zero header). The `is_64bit` flag widens the offset
/// fields from 4 to 8 bytes for FBX 7500+.
/// @return 1 on success, 0 if the buffer is malformed.
static int fbx_parse_node(fbx_reader_t *r, fbx_node_t *node, int depth) {
    uint8_t encoded_name_len;
    uint8_t copy_name_len;
    uint64_t props_start;
    uint64_t props_end;

    if (depth > FBX_MAX_NODE_DEPTH)
        return -1;
    memset(node, 0, sizeof(fbx_node_t));

    uint64_t end_offset = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    uint64_t num_props = r->is_64bit ? fbx_u64(r) : fbx_u32(r);
    uint64_t prop_list_len = r->is_64bit ? fbx_u64(r) : fbx_u32(r);

    encoded_name_len = fbx_u8(r);
    if (r->error)
        return -1;

    if (end_offset == 0 && num_props == 0 && encoded_name_len == 0)
        return -1; /* null record (sentinel) */

    if (end_offset > r->len || end_offset < r->pos)
        return -1;
    if (!fbx_require(r, encoded_name_len))
        return -1;
    copy_name_len = encoded_name_len > 127 ? 127 : encoded_name_len;
    memcpy(node->name, r->data + r->pos, copy_name_len);
    node->name[copy_name_len] = '\0';
    fbx_skip(r, encoded_name_len);

    props_start = (uint64_t)r->pos;
    if (props_start > end_offset || prop_list_len > end_offset - props_start)
        return -1;
    props_end = props_start + prop_list_len;
    if (props_end > r->len)
        return -1;
    if (num_props > FBX_MAX_NODE_PROPS || (size_t)num_props > SIZE_MAX / sizeof(fbx_prop_t))
        return -1;
    if (num_props > 0 && !rt_untrusted_count_ok((int64_t)num_props, 1u, (size_t)prop_list_len))
        return -1;

    /* Parse properties */
    if (num_props > 0) {
        node->props = (fbx_prop_t *)calloc((size_t)num_props, sizeof(fbx_prop_t));
        if (!node->props)
            return -1;
        for (uint64_t i = 0; i < num_props; i++) {
            if ((uint64_t)r->pos >= props_end) {
                node->prop_count = (int32_t)i;
                return -1;
            }
            if (fbx_parse_property(r, &node->props[i]) < 0) {
                node->prop_count = (int32_t)i;
                return -1;
            }
            if ((uint64_t)r->pos > props_end) {
                node->prop_count = (int32_t)(i + 1);
                return -1;
            }
        }
        node->prop_count = (int32_t)num_props;
    }
    if ((uint64_t)r->pos < props_end)
        fbx_skip(r, (size_t)(props_end - (uint64_t)r->pos));
    if (r->error || (uint64_t)r->pos != props_end)
        return -1;

    /* Parse children (until end_offset or null record) */
    while (r->pos < (size_t)end_offset && !fbx_eof(r)) {
        /* Check for null record sentinel */
        size_t sentinel_size = r->is_64bit ? 25 : 13;
        if (r->pos + sentinel_size <= r->len) {
            int is_null = 1;
            for (size_t i = 0; i < sentinel_size; i++)
                if (r->data[r->pos + i] != 0) {
                    is_null = 0;
                    break;
                }
            if (is_null) {
                fbx_skip(r, sentinel_size);
                break;
            }
        }

        if (node->child_count >= node->child_capacity) {
            if (node->child_count >= FBX_MAX_CHILDREN)
                return -1;
            if (node->child_capacity < 0 || node->child_capacity > INT32_MAX / 2)
                return -1;
            int32_t new_cap = node->child_capacity == 0 ? 8 : node->child_capacity * 2;
            if (new_cap > FBX_MAX_CHILDREN)
                new_cap = FBX_MAX_CHILDREN;
            if ((size_t)new_cap > SIZE_MAX / sizeof(fbx_node_t))
                return -1;
            if (!rt_untrusted_count_ok((int64_t)new_cap, r->is_64bit ? 25u : 13u, r->len))
                return -1;
            fbx_node_t *nc =
                (fbx_node_t *)realloc(node->children, (size_t)new_cap * sizeof(fbx_node_t));
            if (!nc)
                return -1;
            node->children = nc;
            node->child_capacity = new_cap;
        }

        fbx_node_t *child = &node->children[node->child_count];
        if (fbx_parse_node(r, child, depth + 1) < 0)
            return -1;
        node->child_count++;
    }

    /* Ensure we're at end_offset */
    if ((uint64_t)r->pos > end_offset)
        return -1;
    if (r->pos < (size_t)end_offset)
        r->pos = (size_t)end_offset;

    return 0;
}

/*==========================================================================
 * Node tree query helpers
 *=========================================================================*/

/// @brief Linear-search `parent->children` for the first child named `name`.
/// FBX node trees are small (few hundred nodes) so the O(n) cost is fine.
static fbx_node_t *fbx_find_child(fbx_node_t *parent, const char *name) {
    for (int32_t i = 0; i < parent->child_count; i++)
        if (strcmp(parent->children[i].name, name) == 0)
            return &parent->children[i];
    return NULL;
}

/// @brief Coerce property `idx` of `node` to int64 (handles Y/I/L variants).
static int64_t fbx_prop_i64(fbx_node_t *node, int idx) {
    if (!node || idx < 0 || idx >= node->prop_count)
        return 0;
    fbx_prop_t *p = &node->props[idx];
    switch (p->type) {
        case 'L':
            return p->v.i64;
        case 'I':
            return p->v.i32;
        case 'Y':
            return p->v.i16;
        case 'C':
            return p->v.bool_val;
        default:
            return 0;
    }
}

/// @brief Coerce property `idx` of `node` to double (handles F/D variants).
static double fbx_prop_f64(fbx_node_t *node, int idx) {
    if (!node || idx < 0 || idx >= node->prop_count)
        return 0.0;
    fbx_prop_t *p = &node->props[idx];
    if (p->type == 'D')
        return p->v.f64;
    if (p->type == 'F')
        return p->v.f32;
    return 0.0;
}

static const char *fbx_prop_str(fbx_node_t *node, int idx);

/// @brief Coerce property `idx` of `node` to a boolean-like value.
/// @details FBX exporters encode flags as `C`, integer, or occasionally float properties inside
///   `Properties70`. Treats non-zero numeric values and string "true"/"1" as enabled.
static int fbx_prop_truthy(fbx_node_t *node, int idx) {
    fbx_prop_t *p;
    const char *text;
    if (!node || idx < 0 || idx >= node->prop_count)
        return 0;
    p = &node->props[idx];
    switch (p->type) {
        case 'C':
            return p->v.bool_val ? 1 : 0;
        case 'Y':
        case 'I':
        case 'L':
            return fbx_prop_i64(node, idx) != 0;
        case 'F':
        case 'D':
            return fabs(fbx_prop_f64(node, idx)) > 1e-12;
        case 'S':
            text = fbx_prop_str(node, idx);
            return (strcmp(text, "true") == 0 || strcmp(text, "True") == 0 ||
                    strcmp(text, "TRUE") == 0 || strcmp(text, "1") == 0);
        default:
            return 0;
    }
}

/// @brief Borrowed C-string view of property `idx` (S/R type). NULL otherwise.
static const char *fbx_prop_str(fbx_node_t *node, int idx) {
    if (!node || idx < 0 || idx >= node->prop_count)
        return "";
    fbx_prop_t *p = &node->props[idx];
    if (p->type == 'S' && p->v.string.str)
        return p->v.string.str;
    return "";
}

/// @brief Strip the `Namespace::` prefix from an FBX object name and copy the remainder
/// into `out`. FBX stores names as `Model::Hips`, `Geometry::CubeMesh`, etc., where the
/// prefix is loader metadata that authors in Blender / Maya / Houdini never see. Walks
/// the full string to find the *last* `::` so names that legitimately contain double
/// colons in a sub-namespace still strip correctly. NULL-safe (treats NULL input as
/// empty), output is always NUL-terminated, truncates when the stripped name doesn't
/// fit in `out_size`.
static void fbx_decode_object_name(const char *raw_name, char *out, size_t out_size) {
    const char *start = raw_name ? raw_name : "";
    const char *end = start;
    const char *scope = NULL;
    size_t len;

    if (!out || out_size == 0)
        return;
    while (*end)
        end++;
    for (const char *p = start; p + 1 < end; p++) {
        if (p[0] == ':' && p[1] == ':')
            scope = p;
    }
    if (scope)
        start = scope + 2;
    len = (size_t)(end - start);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

/*==========================================================================
 * Connection table
 *=========================================================================*/

typedef struct {
    int64_t child_id;
    int64_t parent_id;
    char prop[64];
} fbx_conn_t;

typedef struct {
    fbx_conn_t *entries;
    int32_t count;
    int32_t capacity;
} fbx_conn_table_t;

static fbx_node_t *fbx_find_object_by_id(fbx_node_t *objects, int64_t id);

/// @brief Walk the `Connections` block and populate the `(child_id, parent_id)` table.
///
/// FBX models use a flat object soup where parent/child relations
/// are encoded as numeric IDs in a separate connections section.
/// Pre-collecting them lets later passes look up the parent of any
/// node in O(n) (linear scan — typical scenes have a few hundred connections).
/// @return 1 on success, 0 when the connection table cannot be grown.
static int fbx_parse_connections(fbx_node_t *root, fbx_conn_table_t *ct) {
    fbx_node_t *conns_node = fbx_find_child(root, "Connections");
    if (!conns_node)
        return 1;

    for (int32_t i = 0; i < conns_node->child_count; i++) {
        fbx_node_t *c = &conns_node->children[i];
        if (strcmp(c->name, "C") != 0 || c->prop_count < 3)
            continue;

        if (ct->count >= ct->capacity) {
            if (ct->capacity < 0 || ct->capacity > INT32_MAX / 2)
                return 0;
            int32_t new_cap = ct->capacity == 0 ? 64 : ct->capacity * 2;
            if (!rt_untrusted_count_ok(
                    (int64_t)new_cap, sizeof(fbx_conn_t), FBX_MAX_DECOMPRESSED_ARRAY_BYTES))
                return 0;
            if ((size_t)new_cap > SIZE_MAX / sizeof(fbx_conn_t))
                return 0;
            fbx_conn_t *nc =
                (fbx_conn_t *)realloc(ct->entries, (size_t)new_cap * sizeof(fbx_conn_t));
            if (!nc)
                return 0;
            ct->entries = nc;
            ct->capacity = new_cap;
        }

        fbx_conn_t *entry = &ct->entries[ct->count++];
        entry->child_id = fbx_prop_i64(c, 1);
        entry->parent_id = fbx_prop_i64(c, 2);
        entry->prop[0] = '\0';
        if (c->prop_count >= 4) {
            const char *pname = fbx_prop_str(c, 3);
            size_t plen = strlen(pname);
            if (plen > 63)
                plen = 63;
            memcpy(entry->prop, pname, plen);
            entry->prop[plen] = '\0';
        }
    }
    return 1;
}

/// @brief Return the parent ID of `child_id` from the connection table, or 0 if root/missing.
static int64_t fbx_find_parent(const fbx_conn_table_t *ct, int64_t child_id) {
    for (int32_t i = 0; i < ct->count; i++)
        if (ct->entries[i].child_id == child_id)
            return ct->entries[i].parent_id;
    return 0;
}
