//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_vgfx3d_particle_backend_contract.c
// Purpose: Cross-platform contract coverage for compact hardware particle rendering.
//
// Key invariants:
//   - The compact instance record is exactly four contiguous float4 lanes.
//   - Metal, D3D11, and OpenGL opt into and consume the same retained-quad payload.
//   - The software backend does not opt in and therefore keeps CPU-expanded billboards.
//
// Ownership/Lifetime:
//   - Each source snapshot is test-owned heap memory and is freed after its assertions.
//   - No graphics context, window, runtime object, or external dependency is created.
//
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend.h,
//   src/runtime/graphics/3d/render/rt_canvas3d_instanced.inc
//
//===----------------------------------------------------------------------===//

#ifndef ZANNA_ENABLE_GRAPHICS
#define ZANNA_ENABLE_GRAPHICS 1
#endif

#include "vgfx3d_backend.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define EXPECT_TRUE(condition, message)                                                            \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (condition)                                                                             \
            tests_passed++;                                                                        \
        else                                                                                       \
            fprintf(stderr, "FAIL: %s\n", message);                                                \
    } while (0)

/// @brief Read one repository-relative source file into a NUL-terminated test-owned buffer.
/// @details The source root is injected by CMake so the contract runs from any build directory.
///   All seek, size, allocation, and short-read failures return NULL after closing the file.
/// @param relative_path Path relative to the repository root.
/// @return Heap text buffer on success, otherwise NULL; the caller owns the returned allocation.
static char *read_source_file(const char *relative_path) {
    char path[2048];
    FILE *file;
    long length;
    char *text;
    if (!relative_path ||
        snprintf(path, sizeof(path), "%s/%s", ZANNA_SOURCE_DIR, relative_path) < 0)
        return NULL;
    file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    if ((unsigned long)length > SIZE_MAX - 1u) {
        fclose(file);
        return NULL;
    }
    text = (char *)malloc((size_t)length + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    if (fread(text, 1u, (size_t)length, file) != (size_t)length) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[length] = '\0';
    fclose(file);
    return text;
}

/// @brief Record whether @p source contains the implementation marker @p needle.
/// @details Source-level markers complement native builds: they keep inactive platform adapters
///   covered on every host and produce one focused failure message per missing contract element.
/// @param source Source snapshot returned by `read_source_file`.
/// @param needle Required literal implementation marker.
/// @param message Human-readable assertion description.
static void expect_source_contains(const char *source, const char *needle, const char *message) {
    EXPECT_TRUE(source && needle && strstr(source, needle) != NULL, message);
}

/// @brief Verify the shared compact record's byte size and lane offsets.
/// @details Native APIs bind the record without repacking, so any padding or field-order change is
///   an internal renderer ABI break and must fail on every platform before shader upload.
static void test_compact_particle_record_layout(void) {
    EXPECT_TRUE(sizeof(vgfx3d_particle_instance_t) == 64u,
                "Compact particle records remain exactly 64 bytes");
    EXPECT_TRUE(offsetof(vgfx3d_particle_instance_t, center) == 0u,
                "Particle center remains the first float4 lane");
    EXPECT_TRUE(offsetof(vgfx3d_particle_instance_t, right) == 16u,
                "Particle right half-axis remains the second float4 lane");
    EXPECT_TRUE(offsetof(vgfx3d_particle_instance_t, up) == 32u,
                "Particle up half-axis remains the third float4 lane");
    EXPECT_TRUE(offsetof(vgfx3d_particle_instance_t, color) == 48u,
                "Particle color remains the fourth float4 lane");
}

/// @brief Verify OpenGL's capability, GLSL expansion, upload, and dispatch markers.
/// @details The checks span the split backend translation unit so a shader-only or CPU-only partial
///   implementation cannot silently advertise compact-particle support.
static void test_opengl_particle_contract(void) {
    char *backend = read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c");
    char *shader =
        read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_shaders.inc");
    char *mesh = read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_mesh.inc");
    char *frame =
        read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_frame.inc");
    char *material =
        read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_material.inc");

    expect_source_contains(backend,
                           ".particle_instancing = 1",
                           "OpenGL explicitly advertises compact particle instancing");
    expect_source_contains(
        shader, "uniform int uParticleMode", "OpenGL declares the particle shader-mode uniform");
    expect_source_contains(shader,
                           "aInstanceRow0.xyz + aInstanceRow1.xyz * aPosition.x",
                           "OpenGL reconstructs unit-quad positions from compact axes");
    expect_source_contains(
        shader, "vColor = aColor * aInstanceRow3", "OpenGL forwards per-particle color modulation");
    expect_source_contains(mesh,
                           "configure_particle_instance_attributes",
                           "OpenGL owns a dedicated compact instance binding path");
    expect_source_contains(mesh,
                           "sizeof(vgfx3d_particle_instance_t)",
                           "OpenGL advances by the shared compact record stride");
    expect_source_contains(
        material, "uParticleMode", "OpenGL resolves and uploads the particle mode uniform");
    expect_source_contains(frame,
                           "cmd->particle_instances",
                           "OpenGL dispatch selects compact records from the draw command");
    expect_source_contains(
        frame, "DrawElementsInstanced", "OpenGL submits compact particles in one instanced draw");

    free(material);
    free(frame);
    free(mesh);
    free(shader);
    free(backend);
}

/// @brief Verify D3D11's capability, HLSL input, input-layout, upload, and dispatch markers.
/// @details These assertions run even on non-Windows hosts, while Windows builds additionally
///   compile the adapter and HLSL entry point through the native D3D11 context path.
static void test_d3d11_particle_contract(void) {
    char *backend = read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c");
    char *shader =
        read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_shaders.inc");
    char *context =
        read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_context.inc");
    char *draw = read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_draw.inc");

    expect_source_contains(backend,
                           ".particle_instancing = 1",
                           "D3D11 explicitly advertises compact particle instancing");
    expect_source_contains(
        shader, "VS_INPUT_PARTICLE", "D3D11 declares a compact particle vertex input");
    expect_source_contains(
        shader, "VSMainParticles", "D3D11 exposes a dedicated particle vertex entry point");
    expect_source_contains(shader,
                           "input.particleCenter.xyz + input.particleRight.xyz",
                           "D3D11 reconstructs unit-quad positions from compact axes");
    expect_source_contains(shader,
                           "input.color * input.particleColor",
                           "D3D11 forwards per-particle color modulation");
    expect_source_contains(context,
                           "d3d11_fill_particle_layout",
                           "D3D11 creates a four-lane per-instance input layout");
    expect_source_contains(context,
                           "CreateVertexShader(particles)",
                           "D3D11 creates the dedicated particle vertex shader");
    expect_source_contains(draw,
                           "instance_upload_source = cmd->particle_instances",
                           "D3D11 uploads the compact command payload directly");
    expect_source_contains(
        draw, "ctx->input_layout_particles", "D3D11 binds the compact particle input layout");
    expect_source_contains(
        draw, "DrawIndexedInstanced", "D3D11 submits compact particles in one instanced draw");

    free(draw);
    free(context);
    free(shader);
    free(backend);
}

/// @brief Verify Metal's capability, MSL payload, pipeline creation, upload, and dispatch markers.
/// @details macOS compiles the Objective-C adapter natively; this source contract also protects the
///   complete split implementation when tests are run from Windows or Linux.
static void test_metal_particle_contract(void) {
    char *backend = read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m");
    char *shader =
        read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_metal_shaders.inc");
    char *context =
        read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_metal_context.inc");
    char *draw = read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_metal_draw.inc");

    expect_source_contains(backend,
                           ".particle_instancing = 1",
                           "Metal explicitly advertises compact particle instancing");
    expect_source_contains(shader,
                           "struct ParticleInstance",
                           "Metal declares the shared four-float4 particle payload");
    expect_source_contains(
        shader, "vertex_main_particles", "Metal exposes a dedicated particle vertex function");
    expect_source_contains(shader,
                           "particle.center.xyz + particle.right.xyz",
                           "Metal reconstructs unit-quad positions from compact axes");
    expect_source_contains(
        shader, "in.color * particle.color", "Metal forwards per-particle color modulation");
    expect_source_contains(context,
                           "metal_select_particle_pipeline_state",
                           "Metal owns dedicated target/blend particle pipelines");
    expect_source_contains(context,
                           "vertex_main_particles",
                           "Metal loads the particle vertex function from its library");
    expect_source_contains(draw,
                           "instance_upload_data = cmd->particle_instances",
                           "Metal uploads the compact command payload directly");
    expect_source_contains(draw,
                           "sizeof(vgfx3d_particle_instance_t)",
                           "Metal uploads the shared compact record stride");
    expect_source_contains(draw,
                           "metal_select_particle_pipeline_state",
                           "Metal dispatch selects the dedicated particle pipeline");

    free(draw);
    free(context);
    free(shader);
    free(backend);
}

/// @brief Verify software rendering deliberately declines the compact command contract.
/// @details Canvas3D uses this absence to retain the deterministic CPU-expanded billboard path;
///   merely having a generic software instancing hook must never opt it into compact records.
static void test_software_particle_contract(void) {
    char *backend = read_source_file("src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c");
    EXPECT_TRUE(backend && strstr(backend, ".particle_instancing = 1") == NULL,
                "Software backend keeps compact particle instancing disabled");
    free(backend);
}

/// @brief Run the shared-record and per-backend compact-particle contract checks.
/// @return Process success only when every source and layout assertion passes.
int main(void) {
    test_compact_particle_record_layout();
    test_opengl_particle_contract();
    test_d3d11_particle_contract();
    test_metal_particle_contract();
    test_software_particle_contract();

    printf("vgfx3d particle backend contract: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
