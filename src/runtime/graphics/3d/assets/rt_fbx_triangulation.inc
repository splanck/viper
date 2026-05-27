static void fbx_project_mesh_vertex(const rt_mesh3d *mesh,
                                    int32_t vertex_index,
                                    int drop_axis,
                                    double *out_x,
                                    double *out_y) {
    const float *p = mesh->vertices[vertex_index].pos;
    if (drop_axis == 0) {
        *out_x = p[1];
        *out_y = p[2];
    } else if (drop_axis == 1) {
        *out_x = p[0];
        *out_y = p[2];
    } else {
        *out_x = p[0];
        *out_y = p[1];
    }
}

static double fbx_tri_area2(double ax, double ay, double bx, double by, double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

static int fbx_point_in_tri_2d(double px,
                               double py,
                               double ax,
                               double ay,
                               double bx,
                               double by,
                               double cx,
                               double cy) {
    double ab = fbx_tri_area2(ax, ay, bx, by, px, py);
    double bc = fbx_tri_area2(bx, by, cx, cy, px, py);
    double ca = fbx_tri_area2(cx, cy, ax, ay, px, py);
    int has_neg = (ab < -1e-12) || (bc < -1e-12) || (ca < -1e-12);
    int has_pos = (ab > 1e-12) || (bc > 1e-12) || (ca > 1e-12);
    return !(has_neg && has_pos);
}

static int fbx_emit_polygon_triangles(void *mesh_obj, const int32_t *polygon, int32_t poly_count) {
    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    int32_t indices[32];
    double normal[3] = {0.0, 0.0, 0.0};
    double area = 0.0;
    int drop_axis = 2;
    int ccw;
    int32_t remaining;
    int guard;
    if (!mesh || !polygon || poly_count < 3)
        return 1;
    if (poly_count == 3) {
        rt_mesh3d_add_triangle(mesh_obj, polygon[0], polygon[1], polygon[2]);
        return mesh->build_failed ? 0 : 1;
    }
    if (poly_count > 32)
        return 0;
    for (int32_t i = 0; i < poly_count; ++i) {
        const float *a = mesh->vertices[polygon[i]].pos;
        const float *b = mesh->vertices[polygon[(i + 1) % poly_count]].pos;
        normal[0] += ((double)a[1] - (double)b[1]) * ((double)a[2] + (double)b[2]);
        normal[1] += ((double)a[2] - (double)b[2]) * ((double)a[0] + (double)b[0]);
        normal[2] += ((double)a[0] - (double)b[0]) * ((double)a[1] + (double)b[1]);
    }
    if (fabs(normal[0]) >= fabs(normal[1]) && fabs(normal[0]) >= fabs(normal[2]))
        drop_axis = 0;
    else if (fabs(normal[1]) >= fabs(normal[2]))
        drop_axis = 1;
    for (int32_t i = 0; i < poly_count; ++i) {
        double ax, ay, bx, by;
        fbx_project_mesh_vertex(mesh, polygon[i], drop_axis, &ax, &ay);
        fbx_project_mesh_vertex(mesh, polygon[(i + 1) % poly_count], drop_axis, &bx, &by);
        area += ax * by - bx * ay;
        indices[i] = polygon[i];
    }
    if (!isfinite(area) || fabs(area) < 1e-12) {
        for (int32_t fi = 1; fi < poly_count - 1; ++fi)
            rt_mesh3d_add_triangle(mesh_obj, polygon[0], polygon[fi], polygon[fi + 1]);
        return mesh->build_failed ? 0 : 1;
    }
    ccw = area > 0.0;
    remaining = poly_count;
    guard = poly_count * poly_count;
    while (remaining > 3 && guard-- > 0) {
        int clipped = 0;
        for (int32_t i = 0; i < remaining; ++i) {
            int32_t prev_i = (i + remaining - 1) % remaining;
            int32_t next_i = (i + 1) % remaining;
            int32_t ia = indices[prev_i];
            int32_t ib = indices[i];
            int32_t ic = indices[next_i];
            double ax, ay, bx, by, cx, cy;
            double turn;
            int contains = 0;
            fbx_project_mesh_vertex(mesh, ia, drop_axis, &ax, &ay);
            fbx_project_mesh_vertex(mesh, ib, drop_axis, &bx, &by);
            fbx_project_mesh_vertex(mesh, ic, drop_axis, &cx, &cy);
            turn = fbx_tri_area2(ax, ay, bx, by, cx, cy);
            if ((ccw && turn <= 1e-12) || (!ccw && turn >= -1e-12))
                continue;
            for (int32_t j = 0; j < remaining; ++j) {
                double px, py;
                if (j == prev_i || j == i || j == next_i)
                    continue;
                fbx_project_mesh_vertex(mesh, indices[j], drop_axis, &px, &py);
                if (fbx_point_in_tri_2d(px, py, ax, ay, bx, by, cx, cy)) {
                    contains = 1;
                    break;
                }
            }
            if (contains)
                continue;
            rt_mesh3d_add_triangle(mesh_obj, ia, ib, ic);
            if (mesh->build_failed)
                return 0;
            for (int32_t j = i; j < remaining - 1; ++j)
                indices[j] = indices[j + 1];
            remaining--;
            clipped = 1;
            break;
        }
        if (!clipped)
            break;
    }
    if (remaining == 3)
        rt_mesh3d_add_triangle(mesh_obj, indices[0], indices[1], indices[2]);
    else {
        for (int32_t fi = 1; fi < poly_count - 1; ++fi)
            rt_mesh3d_add_triangle(mesh_obj, polygon[0], polygon[fi], polygon[fi + 1]);
    }
    return mesh->build_failed ? 0 : 1;
}
