//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/zanna/cmd_asset.cpp
// Purpose: `zanna asset` subcommands — offline 3D asset conditioning. `bake`
//   loads a glTF/GLB/FBX/OBJ/STL model through the full runtime import
//   pipeline (including the compressed-codec decoders and import options),
//   optionally generates LOD chains, instantiates the scene, and saves the
//   versioned .vscn baked form for near-instant loading. `validate` loads an
//   asset and prints the AssetDiagnostics3D import report as JSON.
// Key invariants:
//   - Requires a graphics-enabled runtime build; other builds get a clear
//     diagnostic instead of unresolved behavior.
//   - Exit codes: 0 success, 1 usage error, 2 load/bake failure.
// Ownership/Lifetime:
//   - Runtime objects are released through the runtime's release-check
//     protocol before exit.
// Links: src/runtime/graphics/3d/render/rt_model3d.h,
//   src/runtime/graphics/3d/scene/rt_scene3d.h
//
//===----------------------------------------------------------------------===//
#include "cmd_asset.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef ZANNA_ENABLE_GRAPHICS
extern "C" {
typedef struct rt_string_impl *rt_string;
rt_string rt_const_cstr(const char *text);
const char *rt_string_cstr(rt_string s);
void *rt_model3d_load_with_options_ex(rt_string path, rt_string options);
int64_t rt_model3d_generate_lods(void *model, int64_t levels, double ratio);
void *rt_model3d_instantiate_scene(void *model);
int64_t rt_scene3d_save(void *scene, rt_string path);
rt_string rt_assets3d_get_import_report(void);
int rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
}
#endif

namespace {

void printAssetUsage(std::FILE *out) {
    std::fprintf(out,
                 "usage: zanna asset <bake|validate> ...\n"
                 "  zanna asset bake <input> <output.vscn> [--force-tangents]\n"
                 "                   [--eight-influences] [--compress-anims] [--lods N]\n"
                 "      Load a model through the full import pipeline (glTF/GLB/FBX/\n"
                 "      OBJ/STL, including meshopt/Draco/BasisU decode), optionally\n"
                 "      generate LOD chains, and save the baked .vscn scene.\n"
                 "  zanna asset validate <input>\n"
                 "      Load a model and print the import diagnostics report (JSON).\n");
}

} // namespace

int cmdAsset(int argc, char **argv) {
    if (argc < 1) {
        printAssetUsage(stderr);
        return 1;
    }
#ifndef ZANNA_ENABLE_GRAPHICS
    (void)argv;
    std::fprintf(stderr, "zanna asset: requires a graphics-enabled runtime build\n");
    return 2;
#else
    const std::string sub = argv[0];
    if (sub == "validate") {
        if (argc < 2) {
            printAssetUsage(stderr);
            return 1;
        }
        void *model = rt_model3d_load_with_options_ex(rt_const_cstr(argv[1]),
                                                      rt_const_cstr(""));
        rt_string report = rt_assets3d_get_import_report();
        const char *text = report ? rt_string_cstr(report) : nullptr;
        std::printf("%s\n", text && *text ? text : "{}");
        if (!model) {
            std::fprintf(stderr, "zanna asset validate: failed to load '%s'\n", argv[1]);
            return 2;
        }
        if (rt_obj_release_check0(model))
            rt_obj_free(model);
        return 0;
    }
    if (sub == "bake") {
        if (argc < 3) {
            printAssetUsage(stderr);
            return 1;
        }
        const char *input = argv[1];
        const char *output = argv[2];
        std::string options;
        long lods = 0;
        for (int i = 3; i < argc; i++) {
            const std::string arg = argv[i];
            if (arg == "--force-tangents") {
                options += options.empty() ? "forceTangents" : ",forceTangents";
            } else if (arg == "--eight-influences") {
                options += options.empty() ? "eightInfluences" : ",eightInfluences";
            } else if (arg == "--compress-anims") {
                options += options.empty() ? "compressAnimations" : ",compressAnimations";
            } else if (arg == "--lods" && i + 1 < argc) {
                lods = std::strtol(argv[++i], nullptr, 10);
                if (lods < 0 || lods > 8) {
                    std::fprintf(stderr, "zanna asset bake: --lods expects 0..8\n");
                    return 1;
                }
            } else {
                std::fprintf(stderr, "zanna asset bake: unknown option '%s'\n", arg.c_str());
                printAssetUsage(stderr);
                return 1;
            }
        }
        void *model = rt_model3d_load_with_options_ex(rt_const_cstr(input),
                                                      rt_const_cstr(options.c_str()));
        if (!model) {
            std::fprintf(stderr, "zanna asset bake: failed to load '%s'\n", input);
            return 2;
        }
        if (lods > 0)
            (void)rt_model3d_generate_lods(model, (int64_t)lods, 0.5);
        void *scene = rt_model3d_instantiate_scene(model);
        if (!scene) {
            std::fprintf(stderr, "zanna asset bake: failed to instantiate '%s'\n", input);
            if (rt_obj_release_check0(model))
                rt_obj_free(model);
            return 2;
        }
        int64_t saved = rt_scene3d_save(scene, rt_const_cstr(output));
        if (rt_obj_release_check0(scene))
            rt_obj_free(scene);
        if (rt_obj_release_check0(model))
            rt_obj_free(model);
        if (!saved) {
            std::fprintf(stderr, "zanna asset bake: failed to save '%s'\n", output);
            return 2;
        }
        std::printf("baked %s -> %s\n", input, output);
        return 0;
    }
    printAssetUsage(stderr);
    return 1;
#endif
}

int cmdAssetHelp(std::FILE *out) {
    printAssetUsage(out);
    return 0;
}

