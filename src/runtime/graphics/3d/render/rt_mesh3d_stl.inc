//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/render/rt_mesh3d_stl.inc  (#included from rt_mesh3d.c; not a standalone translation unit)
// Purpose: Mesh3D STL loader (binary + ASCII)
//===----------------------------------------------------------------------===//
//=============================================================================
// STL Loading (Binary + ASCII)
//=============================================================================

// ---------------------------------------------------------------------------
// STL parser helpers — STL is a much simpler 3D mesh format with
// two variants: binary (84-byte header + per-triangle records) and
// ASCII (text "facet normal …" blocks). Both lack texture coords
// and vertex sharing — every triangle is stored in full.
// ---------------------------------------------------------------------------

#define STL_BINARY_INITIAL_RESERVE_TRIANGLES 65536u
#define STL_MAX_FILE_BYTES (INT64_C(512) * 1024 * 1024)

#if defined(_WIN32)
#define stl_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
#define stl_ftell(fp) _ftelli64((fp))
#else
#define stl_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define stl_ftell(fp) ftello((fp))
#endif

/// @brief Read a little-endian uint32 from a binary STL byte stream.
static uint32_t stl_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Read a little-endian IEEE-754 float from a binary STL byte stream.
static float stl_read_f32_le(const uint8_t *p) {
    uint32_t bits =
        (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float val;
    memcpy(&val, &bits, sizeof(val));
    return val;
}

/// @brief Return non-zero if a facet normal is finite and directionally useful.
static int stl_normal_is_usable(const float *normal) {
    double nx;
    double ny;
    double nz;
    double len_sq;
    if (!normal || !isfinite(normal[0]) || !isfinite(normal[1]) || !isfinite(normal[2]))
        return 0;
    nx = (double)normal[0];
    ny = (double)normal[1];
    nz = (double)normal[2];
    len_sq = nx * nx + ny * ny + nz * nz;
    return len_sq > 1e-20;
}

/// @brief Cap the initial triangle reservation for a binary STL to a safe ceiling.
/// @details Avoids over-allocating from an untrusted header count; the buffer grows as facets are
///          actually read.
static uint32_t stl_initial_reserve_triangles(uint32_t tri_count) {
    return tri_count < STL_BINARY_INITIAL_RESERVE_TRIANGLES ? tri_count
                                                            : STL_BINARY_INITIAL_RESERVE_TRIANGLES;
}

/// @brief Return non-zero when STL vertex order should be flipped to match facet normal.
static int stl_triangle_should_flip(const float *verts, const float *normal) {
    double abx;
    double aby;
    double abz;
    double acx;
    double acy;
    double acz;
    double nx;
    double ny;
    double nz;
    double dot;
    if (!verts || !stl_normal_is_usable(normal))
        return 0;
    abx = (double)verts[3] - (double)verts[0];
    aby = (double)verts[4] - (double)verts[1];
    abz = (double)verts[5] - (double)verts[2];
    acx = (double)verts[6] - (double)verts[0];
    acy = (double)verts[7] - (double)verts[1];
    acz = (double)verts[8] - (double)verts[2];
    nx = aby * acz - abz * acy;
    ny = abz * acx - abx * acz;
    nz = abx * acy - aby * acx;
    dot = nx * (double)normal[0] + ny * (double)normal[1] + nz * (double)normal[2];
    return dot < 0.0;
}

/// @brief Append one STL triangle, skipping zero-area faces without failing the entire import.
static int stl_emit_triangle(void *mesh, const float *verts, const float *normal) {
    int64_t base;
    if (!mesh || !verts)
        return 0;
    if (!mesh_positions_form_triangle(verts, verts + 3, verts + 6))
        return 1;
    base = (int64_t)((rt_mesh3d *)mesh)->vertex_count;
    rt_mesh3d_add_vertex(mesh, verts[0], verts[1], verts[2], 0, 0, 0, 0, 0);
    rt_mesh3d_add_vertex(mesh, verts[3], verts[4], verts[5], 0, 0, 0, 0, 0);
    rt_mesh3d_add_vertex(mesh, verts[6], verts[7], verts[8], 0, 0, 0, 0, 0);
    if (((rt_mesh3d *)mesh)->build_failed)
        return 0;
    if (stl_triangle_should_flip(verts, normal))
        rt_mesh3d_add_triangle(mesh, base, base + 2, base + 1);
    else
        rt_mesh3d_add_triangle(mesh, base, base + 1, base + 2);
    return !((rt_mesh3d *)mesh)->build_failed;
}

/// @brief Decode a binary-STL byte buffer into a Mesh3D.
///
/// Layout: 80-byte header + `uint32` triangle count + 50 bytes
/// per triangle (12-byte normal + 3 × 12-byte vertices + 2-byte
/// attribute count). Vertices are not deduplicated — STL emits
/// each face's vertices in full.
static void *MESH3D_UNUSED_PRIVATE stl_load_binary(const uint8_t *data, size_t len) {
    if (len < 84)
        return NULL;
    uint32_t tri_count = stl_read_u32_le(data + 80);
    uint32_t reserve_triangles;
    size_t tri_count_size = (size_t)tri_count;
    if (tri_count_size > (SIZE_MAX - 84u) / 50u)
        return NULL;
    size_t expected = 84u + tri_count_size * 50u;
    if (len < expected || tri_count == 0 || tri_count > UINT32_MAX / 3u)
        return NULL;

    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;
    reserve_triangles = stl_initial_reserve_triangles(tri_count);
    if (!mesh3d_reserve_storage(
            (rt_mesh3d *)mesh, reserve_triangles * 3u, reserve_triangles * 3u, "Mesh3D.FromSTL")) {
        mesh_mark_build_failed((rt_mesh3d *)mesh);
        return mesh_return_null_if_build_failed(mesh);
    }
    rt_mesh3d_begin_geometry_batch((rt_mesh3d *)mesh);

    for (uint32_t i = 0; i < tri_count; i++) {
        const uint8_t *tri = data + 84 + (size_t)i * 50;
        float normal[3] = {
            stl_read_f32_le(tri + 0),
            stl_read_f32_le(tri + 4),
            stl_read_f32_le(tri + 8),
        };
        float verts[9] = {
            stl_read_f32_le(tri + 12),
            stl_read_f32_le(tri + 16),
            stl_read_f32_le(tri + 20),
            stl_read_f32_le(tri + 24),
            stl_read_f32_le(tri + 28),
            stl_read_f32_le(tri + 32),
            stl_read_f32_le(tri + 36),
            stl_read_f32_le(tri + 40),
            stl_read_f32_le(tri + 44),
        };
        if (!isfinite(verts[0]) || !isfinite(verts[1]) || !isfinite(verts[2]) ||
            !isfinite(verts[3]) || !isfinite(verts[4]) || !isfinite(verts[5]) ||
            !isfinite(verts[6]) || !isfinite(verts[7]) || !isfinite(verts[8])) {
            mesh_mark_build_failed((rt_mesh3d *)mesh);
            break;
        }
        if (!stl_emit_triangle(mesh, verts, normal))
            break;
    }

    if (((rt_mesh3d *)mesh)->build_failed) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        return mesh_return_null_if_build_failed(mesh);
    }
    if (((rt_mesh3d *)mesh)->index_count == 0) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    rt_mesh3d_recalc_normals(mesh);
    return mesh;
}

/// @brief Read @p tri_count triangles from an open binary-STL stream (positioned at the
///   first facet record) into a new Mesh3D with recomputed normals; returns NULL on a
///   zero/overflowing count or read error.
static void *stl_load_binary_stream(FILE *f, uint32_t tri_count) {
    uint32_t reserve_triangles;
    if (!f || tri_count == 0 || tri_count > UINT32_MAX / 3u)
        return NULL;
    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;
    reserve_triangles = stl_initial_reserve_triangles(tri_count);
    if (!mesh3d_reserve_storage(
            (rt_mesh3d *)mesh, reserve_triangles * 3u, reserve_triangles * 3u, "Mesh3D.FromSTL")) {
        mesh_mark_build_failed((rt_mesh3d *)mesh);
        return mesh_return_null_if_build_failed(mesh);
    }
    rt_mesh3d_begin_geometry_batch((rt_mesh3d *)mesh);

    uint8_t tri[50];
    for (uint32_t i = 0; i < tri_count; i++) {
        if (fread(tri, 1, sizeof(tri), f) != sizeof(tri)) {
            mesh_mark_build_failed((rt_mesh3d *)mesh);
            break;
        }
        float normal[3] = {
            stl_read_f32_le(tri + 0),
            stl_read_f32_le(tri + 4),
            stl_read_f32_le(tri + 8),
        };
        float verts[9] = {
            stl_read_f32_le(tri + 12),
            stl_read_f32_le(tri + 16),
            stl_read_f32_le(tri + 20),
            stl_read_f32_le(tri + 24),
            stl_read_f32_le(tri + 28),
            stl_read_f32_le(tri + 32),
            stl_read_f32_le(tri + 36),
            stl_read_f32_le(tri + 40),
            stl_read_f32_le(tri + 44),
        };
        if (!isfinite(verts[0]) || !isfinite(verts[1]) || !isfinite(verts[2]) ||
            !isfinite(verts[3]) || !isfinite(verts[4]) || !isfinite(verts[5]) ||
            !isfinite(verts[6]) || !isfinite(verts[7]) || !isfinite(verts[8])) {
            mesh_mark_build_failed((rt_mesh3d *)mesh);
            break;
        }
        if (!stl_emit_triangle(mesh, verts, normal))
            break;
    }

    if (((rt_mesh3d *)mesh)->build_failed) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        return mesh_return_null_if_build_failed(mesh);
    }
    if (((rt_mesh3d *)mesh)->index_count == 0) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    rt_mesh3d_recalc_normals(mesh);
    return mesh;
}

/// @brief Parse a double from an ASCII STL stream, advancing the cursor past it.
static int stl_parse_double(const char **pp, const char *end, double *out) {
    const char *parse_end = NULL;
    while (*pp < end && (**pp == ' ' || **pp == '\t'))
        (*pp)++;
    if (*pp >= end)
        return 0;
    if (!mesh_parse_ascii_double_span(*pp, end, &parse_end, out))
        return 0;
    *pp = parse_end;
    return 1;
}

/// @brief Advance past spaces and tabs, returning the first non-blank pointer (≤ `end`).
static const char *stl_skip_horizontal_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

/// @brief Trim trailing spaces, tabs, LF, and CR from a line span, returning the new end pointer.
static const char *stl_trim_line_end(const char *start, const char *end) {
    while (end > start &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        end--;
    return end;
}

/// @brief True if the `[start,end)` span equals NUL-terminated `text` exactly.
static int stl_line_equals(const char *start, const char *end, const char *text) {
    size_t len = strlen(text);
    return (size_t)(end - start) == len && strncmp(start, text, len) == 0;
}

/// @brief True if the span begins with `keyword` followed by end-of-span or whitespace.
/// @details The trailing-boundary check stops "facetx" from matching keyword "facet".
static int stl_line_starts_keyword(const char *start, const char *end, const char *keyword) {
    size_t len = strlen(keyword);
    if ((size_t)(end - start) < len || strncmp(start, keyword, len) != 0)
        return 0;
    return (size_t)(end - start) == len || start[len] == ' ' || start[len] == '\t';
}

/// @brief Parse an ASCII-STL `facet normal nx ny nz` line into `normal[3]`.
/// @return 1 on a well-formed line with three finite, float-range components and
///         no trailing junk; 0 otherwise.
static int stl_parse_facet_normal_line(const char *start, const char *end, float *normal) {
    double nx;
    double ny;
    double nz;
    const char *p = start;
    if ((size_t)(end - p) < 6u || strncmp(p, "facet", 5) != 0 || (p[5] != ' ' && p[5] != '\t'))
        return 0;
    p = stl_skip_horizontal_ws(p + 5, end);
    if ((size_t)(end - p) < 7u || strncmp(p, "normal", 6) != 0 || (p[6] != ' ' && p[6] != '\t'))
        return 0;
    p = p + 6;
    if (!stl_parse_double(&p, end, &nx) || !stl_parse_double(&p, end, &ny) ||
        !stl_parse_double(&p, end, &nz) || !mesh_value_fits_float(nx) ||
        !mesh_value_fits_float(ny) || !mesh_value_fits_float(nz))
        return 0;
    p = stl_skip_horizontal_ws(p, end);
    if (p != end)
        return 0;
    normal[0] = (float)nx;
    normal[1] = (float)ny;
    normal[2] = (float)nz;
    return 1;
}

/// @brief Parse an ASCII-STL `vertex x y z` line into `out_xyz[3]`.
/// @return 1 on a well-formed line with three finite, float-range components and
///         no trailing junk; 0 otherwise.
static int stl_parse_vertex_line(const char *start, const char *end, float *out_xyz) {
    double x;
    double y;
    double z;
    const char *p = start;
    if ((size_t)(end - p) < 7u || strncmp(p, "vertex", 6) != 0 || (p[6] != ' ' && p[6] != '\t'))
        return 0;
    p = p + 6;
    if (!stl_parse_double(&p, end, &x) || !stl_parse_double(&p, end, &y) ||
        !stl_parse_double(&p, end, &z) || !mesh_value_fits_float(x) || !mesh_value_fits_float(y) ||
        !mesh_value_fits_float(z))
        return 0;
    p = stl_skip_horizontal_ws(p, end);
    if (p != end)
        return 0;
    out_xyz[0] = (float)x;
    out_xyz[1] = (float)y;
    out_xyz[2] = (float)z;
    return 1;
}

/// @brief Decode an ASCII-STL byte buffer into a Mesh3D.
///
/// Walks the text, recognising `facet normal …` blocks each
/// containing three `vertex …` lines. Same no-deduplication
/// rule as the binary loader.
static void *MESH3D_UNUSED_PRIVATE stl_load_ascii(const uint8_t *data, size_t len) {
    enum {
        STL_ASCII_OUTSIDE_FACET = 0,
        STL_ASCII_EXPECT_OUTER_LOOP = 1,
        STL_ASCII_IN_LOOP = 2,
        STL_ASCII_EXPECT_ENDFACET = 3
    };

    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    char *text = (char *)malloc(len + 1u);
    if (!text) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    memcpy(text, data, len);
    text[len] = '\0';

    const char *p = text;
    const char *end = text + len;
    float verts[9]; // 3 vertices × 3 components
    int vert_idx = 0;
    float normal[3] = {0.0f, 0.0f, 0.0f};
    int state = STL_ASCII_OUTSIDE_FACET;
    int parse_failed = 0;
    int emitted_triangles = 0;
    rt_mesh3d_begin_geometry_batch((rt_mesh3d *)mesh);

    while (p < end) {
        const char *line = p;
        const char *line_end;
        while (p < end && *p != '\n')
            p++;
        line_end = stl_trim_line_end(line, p);
        line = stl_skip_horizontal_ws(line, line_end);
        if (p < end)
            p++;
        if (line == line_end)
            continue;

        switch (state) {
            case STL_ASCII_OUTSIDE_FACET:
                if (stl_line_starts_keyword(line, line_end, "solid") ||
                    stl_line_starts_keyword(line, line_end, "endsolid"))
                    continue;
                if (!stl_parse_facet_normal_line(line, line_end, normal)) {
                    parse_failed = 1;
                } else {
                    vert_idx = 0;
                    state = STL_ASCII_EXPECT_OUTER_LOOP;
                }
                break;
            case STL_ASCII_EXPECT_OUTER_LOOP:
                if (stl_line_equals(line, line_end, "outer loop")) {
                    state = STL_ASCII_IN_LOOP;
                } else {
                    parse_failed = 1;
                }
                break;
            case STL_ASCII_IN_LOOP:
                if (stl_line_starts_keyword(line, line_end, "vertex")) {
                    if (vert_idx >= 3 ||
                        !stl_parse_vertex_line(line, line_end, &verts[vert_idx * 3])) {
                        parse_failed = 1;
                    } else {
                        vert_idx++;
                    }
                } else if (stl_line_equals(line, line_end, "endloop")) {
                    if (vert_idx != 3)
                        parse_failed = 1;
                    else
                        state = STL_ASCII_EXPECT_ENDFACET;
                } else {
                    parse_failed = 1;
                }
                break;
            case STL_ASCII_EXPECT_ENDFACET:
                if (!stl_line_equals(line, line_end, "endfacet")) {
                    parse_failed = 1;
                } else {
                    uint32_t before = ((rt_mesh3d *)mesh)->index_count;
                    if (!stl_emit_triangle(mesh, verts, normal) ||
                        ((rt_mesh3d *)mesh)->build_failed)
                        parse_failed = 1;
                    if (((rt_mesh3d *)mesh)->index_count > before)
                        emitted_triangles++;
                    vert_idx = 0;
                    state = STL_ASCII_OUTSIDE_FACET;
                }
                break;
        }
        if (parse_failed)
            break;
    }

    if (parse_failed || state != STL_ASCII_OUTSIDE_FACET || emitted_triangles == 0 ||
        ((rt_mesh3d *)mesh)->build_failed) {
        free(text);
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }

    free(text);
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    rt_mesh3d_recalc_normals(mesh);
    return mesh;
}

/// @brief Parse an ASCII STL stream into a new Mesh3D.
/// @details Drives a small state machine over `facet`/`outer loop`/`vertex`/`endfacet` keywords,
///          accumulating each facet's three vertices (with the stored normal) into the mesh.
/// @return New Mesh3D handle, or NULL on a malformed stream or allocation failure.
static void *stl_load_ascii_stream(FILE *f) {
    enum {
        STL_ASCII_OUTSIDE_FACET = 0,
        STL_ASCII_EXPECT_OUTER_LOOP = 1,
        STL_ASCII_IN_LOOP = 2,
        STL_ASCII_EXPECT_ENDFACET = 3
    };

    void *mesh;
    char *line_buf = NULL;
    size_t line_cap = 0;
    int line_status;
    float verts[9];
    int vert_idx = 0;
    float normal[3] = {0.0f, 0.0f, 0.0f};
    int state = STL_ASCII_OUTSIDE_FACET;
    int parse_failed = 0;
    int emitted_triangles = 0;
    if (!f)
        return NULL;
    mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;
    rt_mesh3d_begin_geometry_batch((rt_mesh3d *)mesh);
    while ((line_status = mesh3d_read_text_line(
                f, &line_buf, &line_cap, MESH3D_STL_ASCII_MAX_LINE_BYTES)) > 0) {
        const char *line = line_buf;
        const char *line_end = line_buf + strlen(line_buf);
        line_end = stl_trim_line_end(line, line_end);
        line = stl_skip_horizontal_ws(line, line_end);
        if (line == line_end)
            continue;
        switch (state) {
            case STL_ASCII_OUTSIDE_FACET:
                if (stl_line_starts_keyword(line, line_end, "solid") ||
                    stl_line_starts_keyword(line, line_end, "endsolid"))
                    continue;
                if (!stl_parse_facet_normal_line(line, line_end, normal)) {
                    parse_failed = 1;
                } else {
                    vert_idx = 0;
                    state = STL_ASCII_EXPECT_OUTER_LOOP;
                }
                break;
            case STL_ASCII_EXPECT_OUTER_LOOP:
                if (stl_line_equals(line, line_end, "outer loop"))
                    state = STL_ASCII_IN_LOOP;
                else
                    parse_failed = 1;
                break;
            case STL_ASCII_IN_LOOP:
                if (stl_line_starts_keyword(line, line_end, "vertex")) {
                    if (vert_idx >= 3 ||
                        !stl_parse_vertex_line(line, line_end, &verts[vert_idx * 3]))
                        parse_failed = 1;
                    else
                        vert_idx++;
                } else if (stl_line_equals(line, line_end, "endloop")) {
                    if (vert_idx != 3)
                        parse_failed = 1;
                    else
                        state = STL_ASCII_EXPECT_ENDFACET;
                } else {
                    parse_failed = 1;
                }
                break;
            case STL_ASCII_EXPECT_ENDFACET:
                if (!stl_line_equals(line, line_end, "endfacet")) {
                    parse_failed = 1;
                } else {
                    uint32_t before = ((rt_mesh3d *)mesh)->index_count;
                    if (!stl_emit_triangle(mesh, verts, normal) ||
                        ((rt_mesh3d *)mesh)->build_failed)
                        parse_failed = 1;
                    if (((rt_mesh3d *)mesh)->index_count > before)
                        emitted_triangles++;
                    vert_idx = 0;
                    state = STL_ASCII_OUTSIDE_FACET;
                }
                break;
        }
        if (parse_failed)
            break;
    }
    if (line_status == -2) {
        parse_failed = 1;
        rt_trap("Mesh3D.FromSTL: line exceeds 1 MiB limit");
    } else if (line_status < 0) {
        parse_failed = 1;
    }
    free(line_buf);
    if (parse_failed || state != STL_ASCII_OUTSIDE_FACET || emitted_triangles == 0 ||
        ((rt_mesh3d *)mesh)->build_failed) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    rt_mesh3d_recalc_normals(mesh);
    return mesh;
}

/// @brief Load a mesh from a binary or ASCII STL file.
/// @details STL files contain raw triangle soup (no shared vertices or UVs).
///          Each triangle has its own face normal. ASCII and binary formats are
///          auto-detected. UV coordinates are set to (0,0) for all vertices.
/// @param path File path to the .stl file (runtime string).
/// @return Mesh handle, or NULL on load failure.
void *rt_mesh3d_from_stl(rt_string path) {
    if (!path)
        return NULL;
    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    if (stl_fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    int64_t file_len = (int64_t)stl_ftell(f);
    if (file_len < 0 || file_len > STL_MAX_FILE_BYTES || stl_fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    if (file_len <= 0) {
        fclose(f);
        return NULL;
    }

    void *mesh = NULL;
    if (file_len >= 84) {
        uint8_t header[84];
        if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
            fclose(f);
            return NULL;
        }
        uint32_t tri_count = stl_read_u32_le(header + 80);
        size_t expected_binary = 0;
        if ((size_t)tri_count <= (SIZE_MAX - 84u) / 50u)
            expected_binary = 84u + (size_t)tri_count * 50u;
        if (expected_binary != 0 && (size_t)file_len == expected_binary && tri_count > 0) {
            mesh = stl_load_binary_stream(f, tri_count);
            fclose(f);
            return mesh;
        }
        if (stl_fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            return NULL;
        }
    }

    {
        char magic[5];
        if (fread(magic, 1, sizeof(magic), f) == sizeof(magic) && memcmp(magic, "solid", 5) == 0) {
            if (stl_fseek(f, 0, SEEK_SET) != 0) {
                fclose(f);
                return NULL;
            }
            mesh = stl_load_ascii_stream(f);
            if (mesh) {
                fclose(f);
                return mesh;
            }
        }
        if (stl_fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            return NULL;
        }
    }

    if (file_len >= 84 && ((file_len - 84) % 50) == 0) {
        uint64_t tri_count64 = (uint64_t)(file_len - 84) / 50u;
        if (tri_count64 > 0 && tri_count64 <= UINT32_MAX / 3u &&
            stl_fseek(f, 84, SEEK_SET) == 0) {
            mesh = stl_load_binary_stream(f, (uint32_t)tri_count64);
            fclose(f);
            return mesh;
        }
    }
    fclose(f);
    return NULL;
}
