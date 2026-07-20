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
//   optionally generates LOD chains, saves the complete SceneAsset as the
//   versioned .vscn baked form, then reloads it to report fidelity losses.
//   `validate` loads an asset and prints the AssetDiagnostics3D import report.
// Key invariants:
//   - Requires a graphics-enabled runtime build; other builds get a clear
//     diagnostic instead of unresolved behavior.
//   - Bake reports compare public SceneAsset resource counts before and after
//     serialization; the report therefore follows VSCN capability changes.
//   - `--json` writes exactly one v1 report object to stdout and keeps stderr
//     empty on success. Human mode retains the historical `baked` line and
//     writes one stable-code warning per detected resource class reduction.
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#ifdef ZANNA_ENABLE_GRAPHICS
extern "C" {
typedef struct rt_string_impl *rt_string;
rt_string rt_const_cstr(const char *text);
const char *rt_string_cstr(rt_string s);
void *rt_model3d_load_with_options_ex(rt_string path, rt_string options);
int64_t rt_model3d_generate_lods(void *model, int64_t levels, double ratio);
int64_t rt_model3d_save(void *model, rt_string path);
int64_t rt_model3d_get_mesh_count(void *model);
int64_t rt_model3d_get_material_count(void *model);
int64_t rt_model3d_get_skeleton_count(void *model);
int64_t rt_model3d_get_animation_count(void *model);
int64_t rt_model3d_get_node_animation_count(void *model);
int64_t rt_model3d_get_morph_target_count(void *model);
int64_t rt_model3d_get_morph_shape_count(void *model);
int64_t rt_model3d_get_node_count(void *model);
int64_t rt_model3d_get_scene_count(void *model);
int64_t rt_model3d_get_camera_count(void *model, int64_t scene_index);
int64_t rt_model3d_get_variant_count(void *model);
rt_string rt_assets3d_get_import_report(void);
int rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
}
#endif

namespace {

#ifdef ZANNA_ENABLE_GRAPHICS

struct AssetSnapshot {
    int64_t meshes = 0;
    int64_t materials = 0;
    int64_t skeletons = 0;
    int64_t skeletalAnimations = 0;
    int64_t nodeAnimations = 0;
    int64_t morphTargets = 0;
    int64_t morphShapes = 0;
    int64_t nodes = 0;
    int64_t scenes = 0;
    int64_t cameras = 0;
    int64_t variants = 0;
};

struct FidelityLoss {
    const char *code;
    const char *resource;
    int64_t source;
    int64_t baked;
};

std::string jsonQuote(const char *text) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.push_back('"');
    if (text) {
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
            switch (*p) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (*p < 0x20u) {
                        out += "\\u00";
                        out.push_back(hex[*p >> 4u]);
                        out.push_back(hex[*p & 0x0fu]);
                    } else {
                        out.push_back(static_cast<char>(*p));
                    }
                    break;
            }
        }
    }
    out.push_back('"');
    return out;
}

int64_t saturatingAddNonnegative(int64_t lhs, int64_t rhs) {
    if (rhs <= 0)
        return lhs;
    if (lhs > std::numeric_limits<int64_t>::max() - rhs)
        return std::numeric_limits<int64_t>::max();
    return lhs + rhs;
}

AssetSnapshot snapshotAsset(void *model) {
    AssetSnapshot snapshot;
    snapshot.meshes = rt_model3d_get_mesh_count(model);
    snapshot.materials = rt_model3d_get_material_count(model);
    snapshot.skeletons = rt_model3d_get_skeleton_count(model);
    snapshot.skeletalAnimations = rt_model3d_get_animation_count(model);
    snapshot.nodeAnimations = rt_model3d_get_node_animation_count(model);
    snapshot.morphTargets = rt_model3d_get_morph_target_count(model);
    snapshot.morphShapes = rt_model3d_get_morph_shape_count(model);
    snapshot.nodes = rt_model3d_get_node_count(model);
    snapshot.scenes = rt_model3d_get_scene_count(model);
    snapshot.variants = rt_model3d_get_variant_count(model);
    for (int64_t scene = 0; scene < snapshot.scenes; ++scene) {
        snapshot.cameras =
            saturatingAddNonnegative(snapshot.cameras, rt_model3d_get_camera_count(model, scene));
    }
    return snapshot;
}

void appendLoss(std::vector<FidelityLoss> &losses,
                const char *code,
                const char *resource,
                int64_t source,
                int64_t baked) {
    if (source > baked)
        losses.push_back(FidelityLoss{code, resource, source, baked});
}

std::vector<FidelityLoss> compareSnapshots(const AssetSnapshot &source,
                                           const AssetSnapshot &baked) {
    std::vector<FidelityLoss> losses;
    appendLoss(losses, "mesh-count-reduced", "meshes", source.meshes, baked.meshes);
    appendLoss(losses, "material-count-reduced", "materials", source.materials, baked.materials);
    appendLoss(losses, "skeleton-count-reduced", "skeletons", source.skeletons, baked.skeletons);
    appendLoss(losses,
               "skeletal-animation-count-reduced",
               "skeletalAnimations",
               source.skeletalAnimations,
               baked.skeletalAnimations);
    appendLoss(losses,
               "node-animation-count-reduced",
               "nodeAnimations",
               source.nodeAnimations,
               baked.nodeAnimations);
    appendLoss(losses,
               "morph-target-count-reduced",
               "morphTargets",
               source.morphTargets,
               baked.morphTargets);
    appendLoss(
        losses, "morph-shape-count-reduced", "morphShapes", source.morphShapes, baked.morphShapes);
    appendLoss(losses, "node-count-reduced", "nodes", source.nodes, baked.nodes);
    appendLoss(losses, "scene-count-reduced", "scenes", source.scenes, baked.scenes);
    appendLoss(losses, "camera-count-reduced", "cameras", source.cameras, baked.cameras);
    appendLoss(losses, "variant-count-reduced", "variants", source.variants, baked.variants);
    return losses;
}

void appendSnapshotJson(std::string &out, const AssetSnapshot &snapshot) {
    out += "{\"meshes\":" + std::to_string(snapshot.meshes);
    out += ",\"materials\":" + std::to_string(snapshot.materials);
    out += ",\"skeletons\":" + std::to_string(snapshot.skeletons);
    out += ",\"skeletalAnimations\":" + std::to_string(snapshot.skeletalAnimations);
    out += ",\"nodeAnimations\":" + std::to_string(snapshot.nodeAnimations);
    out += ",\"morphTargets\":" + std::to_string(snapshot.morphTargets);
    out += ",\"morphShapes\":" + std::to_string(snapshot.morphShapes);
    out += ",\"nodes\":" + std::to_string(snapshot.nodes);
    out += ",\"scenes\":" + std::to_string(snapshot.scenes);
    out += ",\"cameras\":" + std::to_string(snapshot.cameras);
    out += ",\"variants\":" + std::to_string(snapshot.variants) + "}";
}

std::string bakeReportJson(const char *input,
                           const char *output,
                           const AssetSnapshot &source,
                           const AssetSnapshot &baked,
                           const std::vector<FidelityLoss> &losses,
                           const std::string &importReport) {
    std::string report = "{\"schema\":\"zanna.asset-bake-report/v1\",\"status\":\"ok\"";
    report += ",\"input\":" + jsonQuote(input);
    report += ",\"output\":" + jsonQuote(output);
    report += losses.empty() ? ",\"lossy\":false,\"source\":" : ",\"lossy\":true,\"source\":";
    appendSnapshotJson(report, source);
    report += ",\"baked\":";
    appendSnapshotJson(report, baked);
    report += ",\"losses\":[";
    for (size_t i = 0; i < losses.size(); ++i) {
        const FidelityLoss &loss = losses[i];
        if (i > 0)
            report.push_back(',');
        report += "{\"code\":" + jsonQuote(loss.code);
        report += ",\"resource\":" + jsonQuote(loss.resource);
        report += ",\"source\":" + std::to_string(loss.source);
        report += ",\"baked\":" + std::to_string(loss.baked);
        report += ",\"dropped\":" + std::to_string(loss.source - loss.baked) + "}";
    }
    report += "]";
    report += ",\"importReport\":";
    report += importReport.empty() ? "{}" : importReport;
    report += "}";
    return report;
}

void printBakeJsonError(const char *stage,
                        const char *input,
                        const char *output,
                        const std::string &importReport) {
    std::string report = "{\"schema\":\"zanna.asset-bake-report/v1\",\"status\":\"error\"";
    report += ",\"stage\":" + jsonQuote(stage);
    report += ",\"input\":" + jsonQuote(input);
    report += ",\"output\":" + jsonQuote(output);
    report += ",\"importReport\":";
    report += importReport.empty() ? "{}" : importReport;
    report += "}";
    std::printf("%s\n", report.c_str());
}

std::string currentImportReport() {
    rt_string report = rt_assets3d_get_import_report();
    const char *text = report ? rt_string_cstr(report) : nullptr;
    return text && *text ? text : "{}";
}

#endif

void printAssetUsage(std::FILE *out) {
    std::fprintf(out,
                 "usage: zanna asset <bake|validate> ...\n"
                 "  zanna asset bake <input> <output.vscn> [--force-tangents]\n"
                 "                   [--eight-influences] [--compress-anims] [--lods N]\n"
                 "                   [--json]\n"
                 "      Load a model through the full import pipeline (glTF/GLB/FBX/\n"
                 "      OBJ/STL, including meshopt/Draco/BasisU decode), optionally\n"
                 "      generate LOD chains, save the baked .vscn scene, and report\n"
                 "      source-versus-baked fidelity. --json emits the v1 report.\n"
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
        void *model = rt_model3d_load_with_options_ex(rt_const_cstr(argv[1]), rt_const_cstr(""));
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
        bool json = false;
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
            } else if (arg == "--json") {
                json = true;
            } else {
                std::fprintf(stderr, "zanna asset bake: unknown option '%s'\n", arg.c_str());
                printAssetUsage(stderr);
                return 1;
            }
        }
        void *model =
            rt_model3d_load_with_options_ex(rt_const_cstr(input), rt_const_cstr(options.c_str()));
        const std::string importReport = currentImportReport();
        if (!model) {
            if (json)
                printBakeJsonError("load", input, output, importReport);
            else
                std::fprintf(stderr, "zanna asset bake: failed to load '%s'\n", input);
            return 2;
        }
        if (lods > 0)
            (void)rt_model3d_generate_lods(model, (int64_t)lods, 0.5);
        const AssetSnapshot sourceSnapshot = snapshotAsset(model);
        int64_t saved = rt_model3d_save(model, rt_const_cstr(output));
        if (rt_obj_release_check0(model))
            rt_obj_free(model);
        if (!saved) {
            if (json)
                printBakeJsonError("save", input, output, importReport);
            else
                std::fprintf(stderr, "zanna asset bake: failed to save '%s'\n", output);
            return 2;
        }
        void *bakedModel =
            rt_model3d_load_with_options_ex(rt_const_cstr(output), rt_const_cstr(""));
        if (!bakedModel) {
            if (json)
                printBakeJsonError("verify", input, output, importReport);
            else
                std::fprintf(
                    stderr, "zanna asset bake: saved output failed verification '%s'\n", output);
            return 2;
        }
        const AssetSnapshot bakedSnapshot = snapshotAsset(bakedModel);
        if (rt_obj_release_check0(bakedModel))
            rt_obj_free(bakedModel);
        const std::vector<FidelityLoss> losses = compareSnapshots(sourceSnapshot, bakedSnapshot);
        if (json) {
            const std::string report =
                bakeReportJson(input, output, sourceSnapshot, bakedSnapshot, losses, importReport);
            std::printf("%s\n", report.c_str());
        } else {
            for (const FidelityLoss &loss : losses) {
                std::fprintf(stderr,
                             "zanna asset bake: warning: dropped %lld %s "
                             "(source=%lld, baked=%lld) [%s]\n",
                             static_cast<long long>(loss.source - loss.baked),
                             loss.resource,
                             static_cast<long long>(loss.source),
                             static_cast<long long>(loss.baked),
                             loss.code);
            }
            std::printf("baked %s -> %s\n", input, output);
        }
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
