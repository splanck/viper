//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_graphics3d_abi_surface.cpp
// Purpose: Source-level guardrails for Viper.Graphics3D / Viper.Game3D public
//   ABI naming and class-id sentinels.
//
// Key invariants:
//   - RT_G3D_* class ids are append-only and contiguous.
//   - Script-facing 3D API names stay stable once published.
//
// Ownership/Lifetime:
//   - Reads repository files into transient std::string buffers.
//   - Test state lives for one process invocation only.
//
// Links: src/runtime/graphics/3d/rt_graphics3d_ids.h, src/il/runtime/runtime.def
//
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef VIPER_SOURCE_DIR
#define VIPER_SOURCE_DIR "."
#endif

namespace {

struct ExpectedId {
    const char *name;
    const char *value;
};

struct ParsedId {
    std::string name;
    std::string value;
    long long magnitude;
};

std::string read_file(const char *relative) {
    std::string path = std::string(VIPER_SOURCE_DIR) + "/" + relative;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "failed to open " << path << "\n";
        std::exit(2);
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool require(bool condition, const std::string &message) {
    if (!condition)
        std::cerr << "FAIL: " << message << "\n";
    return condition;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

std::vector<ParsedId> parse_graphics3d_ids(const std::string &ids_header) {
    std::vector<ParsedId> ids;
    std::istringstream input(ids_header);
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("#define RT_G3D_") != 0 || line.find("_CLASS_ID") == std::string::npos)
            continue;
        size_t name_begin = std::string("#define ").size();
        size_t name_end = line.find(' ', name_begin);
        size_t value_begin = line.find("INT64_C(", name_end);
        if (name_end == std::string::npos || value_begin == std::string::npos)
            continue;
        value_begin += std::string("INT64_C(").size();
        size_t value_end = line.find(')', value_begin);
        if (value_end == std::string::npos)
            continue;

        std::string value = line.substr(value_begin, value_end - value_begin);
        char *end = nullptr;
        long long parsed = std::strtoll(value.c_str(), &end, 0);
        ids.push_back({line.substr(name_begin, name_end - name_begin), value, std::llabs(parsed)});
    }
    return ids;
}

bool check_class_ids() {
    static const ExpectedId expected[] = {
        {"RT_G3D_CUBEMAP3D_CLASS_ID", "-0x603001"},
        {"RT_G3D_RENDERTARGET3D_CLASS_ID", "-0x603002"},
        {"RT_G3D_CANVAS3D_CLASS_ID", "-0x603003"},
        {"RT_G3D_MESH3D_CLASS_ID", "-0x603004"},
        {"RT_G3D_CAMERA3D_CLASS_ID", "-0x603005"},
        {"RT_G3D_MATERIAL3D_CLASS_ID", "-0x603006"},
        {"RT_G3D_LIGHT3D_CLASS_ID", "-0x603007"},
        {"RT_G3D_SCENE3D_CLASS_ID", "-0x603008"},
        {"RT_G3D_SCENENODE3D_CLASS_ID", "-0x603009"},
        {"RT_G3D_NODEANIMATION3D_CLASS_ID", "-0x60300A"},
        {"RT_G3D_NODEANIMATOR3D_CLASS_ID", "-0x60300B"},
        {"RT_G3D_SKELETON3D_CLASS_ID", "-0x60300C"},
        {"RT_G3D_ANIMATION3D_CLASS_ID", "-0x60300D"},
        {"RT_G3D_ANIMPLAYER3D_CLASS_ID", "-0x60300E"},
        {"RT_G3D_ANIMBLEND3D_CLASS_ID", "-0x60300F"},
        {"RT_G3D_ANIMCONTROLLER3D_CLASS_ID", "-0x603010"},
        {"RT_G3D_FBX_ASSET_CLASS_ID", "-0x603011"},
        {"RT_G3D_GLTF_ASSET_CLASS_ID", "-0x603012"},
        {"RT_G3D_MODEL3D_CLASS_ID", "-0x603013"},
        {"RT_G3D_MORPHTARGET3D_CLASS_ID", "-0x603014"},
        {"RT_G3D_PARTICLES3D_CLASS_ID", "-0x603015"},
        {"RT_G3D_POSTFX3D_CLASS_ID", "-0x603016"},
        {"RT_G3D_RAYHIT3D_CLASS_ID", "-0x603017"},
        {"RT_G3D_SOUNDLISTENER3D_CLASS_ID", "-0x603018"},
        {"RT_G3D_SOUNDSOURCE3D_CLASS_ID", "-0x603019"},
        {"RT_G3D_WORLD3D_CLASS_ID", "-0x60301A"},
        {"RT_G3D_PHYSICSHIT3D_CLASS_ID", "-0x60301B"},
        {"RT_G3D_PHYSICSHITLIST3D_CLASS_ID", "-0x60301C"},
        {"RT_G3D_CONTACTPOINT3D_CLASS_ID", "-0x60301D"},
        {"RT_G3D_COLLISIONEVENT3D_CLASS_ID", "-0x60301E"},
        {"RT_G3D_COLLIDER3D_CLASS_ID", "-0x60301F"},
        {"RT_G3D_BODY3D_CLASS_ID", "-0x603020"},
        {"RT_G3D_CHARACTER3D_CLASS_ID", "-0x603021"},
        {"RT_G3D_TRIGGER3D_CLASS_ID", "-0x603022"},
        {"RT_G3D_DISTANCEJOINT3D_CLASS_ID", "-0x603023"},
        {"RT_G3D_SPRINGJOINT3D_CLASS_ID", "-0x603024"},
        {"RT_G3D_TRANSFORM3D_CLASS_ID", "-0x603025"},
        {"RT_G3D_PATH3D_CLASS_ID", "-0x603026"},
        {"RT_G3D_INSTANCEBATCH3D_CLASS_ID", "-0x603027"},
        {"RT_G3D_TERRAIN3D_CLASS_ID", "-0x603028"},
        {"RT_G3D_NAVMESH3D_CLASS_ID", "-0x603029"},
        {"RT_G3D_NAVAGENT3D_CLASS_ID", "-0x60302A"},
        {"RT_G3D_DECAL3D_CLASS_ID", "-0x60302B"},
        {"RT_G3D_SPRITE3D_CLASS_ID", "-0x60302C"},
        {"RT_G3D_WATER3D_CLASS_ID", "-0x60302D"},
        {"RT_G3D_VEGETATION3D_CLASS_ID", "-0x60302E"},
        {"RT_G3D_TEXTUREATLAS3D_CLASS_ID", "-0x60302F"},
        {"RT_G3D_GAME3D_LAYERMASK_CLASS_ID", "-0x603030"},
        {"RT_G3D_GAME3D_INPUT_CLASS_ID", "-0x603031"},
        {"RT_G3D_GAME3D_ENTITY_CLASS_ID", "-0x603032"},
        {"RT_G3D_GAME3D_SOUND_CLASS_ID", "-0x603033"},
        {"RT_G3D_GAME3D_EFFECTS_CLASS_ID", "-0x603034"},
        {"RT_G3D_GAME3D_WORLD_CLASS_ID", "-0x603035"},
        {"RT_G3D_GAME3D_FIRSTPERSON_CLASS_ID", "-0x603036"},
        {"RT_G3D_GAME3D_FREEFLY_CLASS_ID", "-0x603037"},
        {"RT_G3D_GAME3D_ORBIT_CLASS_ID", "-0x603038"},
        {"RT_G3D_GAME3D_FOLLOW_CLASS_ID", "-0x603039"},
        {"RT_G3D_GAME3D_CHARACTER_CONTROLLER_CLASS_ID", "-0x60303A"},
        {"RT_G3D_GAME3D_ENV_HANDLE_CLASS_ID", "-0x60303B"},
        {"RT_G3D_GAME3D_BODYDEF_CLASS_ID", "-0x60303C"},
        {"RT_G3D_GAME3D_COLLISION_EVENT_CLASS_ID", "-0x60303D"},
        {"RT_G3D_GAME3D_MODEL_TEMPLATE_CLASS_ID", "-0x60303E"},
        {"RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID", "-0x60303F"},
        {"RT_G3D_HINGEJOINT3D_CLASS_ID", "-0x603040"},
        {"RT_G3D_ROPEJOINT3D_CLASS_ID", "-0x603041"},
        {"RT_G3D_SIXDOFJOINT3D_CLASS_ID", "-0x603042"},
        {"RT_G3D_GAME3D_ASSET_HANDLE3D_CLASS_ID", "-0x603043"},
        {"RT_G3D_GAME3D_WORLD_STREAM3D_CLASS_ID", "-0x603044"},
        {"RT_G3D_TEXTUREASSET3D_CLASS_ID", "-0x603045"},
        {"RT_G3D_BLENDTREE3D_CLASS_ID", "-0x603046"},
        {"RT_G3D_IKSOLVER3D_CLASS_ID", "-0x603047"},
        {"RT_G3D_GAME3D_DIAGNOSTICS_CLASS_ID", "-0x603048"},
    };

    std::vector<ParsedId> ids =
        parse_graphics3d_ids(read_file("src/runtime/graphics/3d/rt_graphics3d_ids.h"));
    std::map<std::string, std::string> by_name;
    std::set<std::string> seen_values;
    bool ok = true;

    ok = require(!ids.empty(), "no RT_G3D class ids parsed") && ok;
    for (const ParsedId &id : ids) {
        ok = require(by_name.emplace(id.name, id.value).second, "duplicate id name " + id.name) &&
             ok;
        ok = require(seen_values.insert(id.value).second, "duplicate id value " + id.value) && ok;
    }

    for (const ExpectedId &id : expected) {
        auto found = by_name.find(id.name);
        ok = require(found != by_name.end(), std::string("missing baseline id ") + id.name) && ok;
        if (found != by_name.end())
            ok = require(found->second == id.value,
                         std::string("renumbered baseline id ") + id.name) &&
                 ok;
    }

    if (!ids.empty()) {
        const long long first = ids.front().magnitude;
        for (size_t i = 0; i < ids.size(); ++i) {
            ok = require(ids[i].magnitude == first + static_cast<long long>(i),
                         "RT_G3D class ids must remain contiguous append-only sentinels") &&
                 ok;
        }
    }
    return ok;
}

bool check_runtime_surface_names() {
    const std::string runtime_def = read_file("src/il/runtime/runtime.def");
    const std::string canvas_header = read_file("src/runtime/graphics/3d/render/rt_canvas3d.h");
    const std::string canvas_overlay =
        read_file("src/runtime/graphics/3d/render/rt_canvas3d_overlay.c");
    bool ok = true;

    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_WorkerCount\""),
                 "World3D.WorkerCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_JobsEnabled\""),
                 "World3D.JobsEnabled getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.SetWorkerCount\""),
                 "World3D.SetWorkerCount must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_FloatingOrigin\""),
                 "World3D.FloatingOrigin getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.set_FloatingOrigin\""),
                 "World3D.FloatingOrigin setter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_WorldOrigin\""),
                 "World3D.WorldOrigin getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_Stream\""),
                 "World3D.Stream getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_EntityCount\""),
                 "World3D.EntityCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_BodyCount\""),
                 "World3D.BodyCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_DrawCount\""),
                 "World3D.DrawCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_VisibleNodeCount\""),
                 "World3D.VisibleNodeCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_OccludedDrawCount\""),
                 "World3D.OccludedDrawCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_StreamResidentBytes\""),
                 "World3D.StreamResidentBytes getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.SetOriginRebaseThreshold\""),
                 "World3D.SetOriginRebaseThreshold must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.RebaseOrigin\""),
                 "World3D.RebaseOrigin must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.BakeNavMesh\""),
                 "World3D.BakeNavMesh must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.BakeTiledNavMesh\""),
                 "World3D.BakeTiledNavMesh must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"WorkerCount\""),
                 "World3D.WorkerCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"JobsEnabled\""),
                 "World3D.JobsEnabled property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"FloatingOrigin\""),
                 "World3D.FloatingOrigin property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"WorldOrigin\""),
                 "World3D.WorldOrigin property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"Stream\""), "World3D.Stream property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"EntityCount\""),
                 "World3D.EntityCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"BodyCount\""),
                 "World3D.BodyCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"DrawCount\""),
                 "World3D.DrawCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"VisibleNodeCount\""),
                 "World3D.VisibleNodeCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"OccludedDrawCount\""),
                 "World3D.OccludedDrawCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"StreamResidentBytes\""),
                 "World3D.StreamResidentBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetWorkerCount\""),
                 "World3D.SetWorkerCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetOriginRebaseThreshold\""),
                 "World3D.SetOriginRebaseThreshold method missing") &&
         ok;
    ok = require(contains(runtime_def,
                          "RT_METHOD(\"RebaseOrigin\", \"void(f64,f64,f64)\", "
                          "Game3DWorldRebaseOrigin)"),
                 "World3D.RebaseOrigin method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"BakeNavMesh\""),
                 "World3D.BakeNavMesh method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"BakeTiledNavMesh\""),
                 "World3D.BakeTiledNavMesh method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.get_ContactCount\""),
                 "Collision3DEvent.ContactCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.ContactPoint\""),
                 "Collision3DEvent.ContactPoint must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.ContactNormal\""),
                 "Collision3DEvent.ContactNormal must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.ContactSeparation\""),
                 "Collision3DEvent.ContactSeparation must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ContactCount\""),
                 "Collision3DEvent.ContactCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"ContactPoint\""),
                 "Collision3DEvent.ContactPoint method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"ContactNormal\""),
                 "Collision3DEvent.ContactNormal method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"ContactSeparation\""),
                 "Collision3DEvent.ContactSeparation method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Entity3D.FromNode\""),
                 "Entity3D.FromNode must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"FromNode\""),
                 "Entity3D.FromNode method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadEntityAsync\""),
                 "Assets3D.LoadEntityAsync must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadEntityAssetAsync\""),
                 "Assets3D.LoadEntityAssetAsync must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadTemplateAsync\""),
                 "Assets3D.LoadTemplateAsync must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadTemplateAssetAsync\""),
                 "Assets3D.LoadTemplateAssetAsync must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Prefab.Load\""),
                 "Prefab.Load canonical loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Prefab.LoadAsset\""),
                 "Prefab.LoadAsset canonical loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Prefab.LoadAsync\""),
                 "Prefab.LoadAsync canonical loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Prefab.LoadAssetAsync\""),
                 "Prefab.LoadAssetAsync canonical loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadPrefab\""),
                 "Assets3D.LoadPrefab compatibility loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadPrefabAsset\""),
                 "Assets3D.LoadPrefabAsset compatibility loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadPrefabAsync\""),
                 "Assets3D.LoadPrefabAsync compatibility loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadPrefabAssetAsync\""),
                 "Assets3D.LoadPrefabAssetAsync compatibility loader missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.SceneTemplate.get_SceneCount\""),
                 "SceneTemplate.SceneCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.SceneTemplate.GetSceneName\""),
                 "SceneTemplate.GetSceneName must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.SceneTemplate.GetCameraCount\""),
                 "SceneTemplate.GetCameraCount must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.SceneTemplate.GetCamera\""),
                 "SceneTemplate.GetCamera must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.SceneTemplate.InstantiateSceneAt\""),
                 "SceneTemplate.InstantiateSceneAt must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"SceneCount\""),
                 "SceneTemplate.SceneCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"InstantiateSceneAt\""),
                 "SceneTemplate.InstantiateSceneAt method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.SetResidencyBudget\""),
                 "Assets3D.SetResidencyBudget must use Game3D PascalCase method naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.GetResidentBytes\""),
                 "Assets3D.GetResidentBytes must use Game3D PascalCase method naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.SetResidencyHint\""),
                 "Assets3D.SetResidencyHint must use Game3D PascalCase method naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.Evict\""),
                 "Assets3D.Evict must use Game3D PascalCase method naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.get_Ready\""),
                 "AssetHandle3D.Ready getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.get_Progress\""),
                 "AssetHandle3D.Progress getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.get_Error\""),
                 "AssetHandle3D.Error getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.Cancel\""),
                 "AssetHandle3D.Cancel must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.GetEntity\""),
                 "AssetHandle3D.GetEntity must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.GetTemplate\""),
                 "AssetHandle3D.GetTemplate must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.GetPrefab\""),
                 "AssetHandle3D.GetPrefab canonical loader result missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Game3D.AssetHandle3D\""),
                 "AssetHandle3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"Ready\""),
                 "AssetHandle3D.Ready property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"LoadEntityAsync\""),
                 "Assets3D.LoadEntityAsync method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"LoadPrefabAssetAsync\""),
                 "Assets3D.LoadPrefabAssetAsync method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetPrefab\""),
                 "AssetHandle3D.GetPrefab method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetResidencyBudget\""),
                 "Assets3D.SetResidencyBudget method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetResidentBytes\""),
                 "Assets3D.GetResidentBytes method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetResidencyHint\""),
                 "Assets3D.SetResidencyHint method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"Evict\""), "Assets3D.Evict method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetEntity\""),
                 "AssetHandle3D.GetEntity method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.New\""),
                 "WorldStream3D.New must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_ResidentCellCount\""),
                 "WorldStream3D.ResidentCellCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_ResidentTerrainTileCount\""),
             "WorldStream3D.ResidentTerrainTileCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetResidentTerrainTile\""),
                 "WorldStream3D.GetResidentTerrainTile must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellCount\""),
                 "WorldStream3D.GetCellCount must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellName\""),
                 "WorldStream3D.GetCellName must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellCenter\""),
                 "WorldStream3D.GetCellCenter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellResident\""),
                 "WorldStream3D.GetCellResident must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellBytes\""),
                 "WorldStream3D.GetCellBytes must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellMaterial\""),
                 "WorldStream3D.GetCellMaterial must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellCollisionMask\""),
                 "WorldStream3D.GetCellCollisionMask must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetCellTraversalCost\""),
                 "WorldStream3D.GetCellTraversalCost must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileCount\""),
                 "WorldStream3D.GetTerrainTileCount must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileName\""),
                 "WorldStream3D.GetTerrainTileName must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileHeightmap\""),
                 "WorldStream3D.GetTerrainTileHeightmap must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileCenter\""),
                 "WorldStream3D.GetTerrainTileCenter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileResident\""),
                 "WorldStream3D.GetTerrainTileResident must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileBytes\""),
                 "WorldStream3D.GetTerrainTileBytes must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileMaterial\""),
                 "WorldStream3D.GetTerrainTileMaterial must use Game3D PascalCase naming") &&
         ok;
    ok =
        require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileCollisionMask\""),
                "WorldStream3D.GetTerrainTileCollisionMask must use Game3D PascalCase naming") &&
        ok;
    ok =
        require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.GetTerrainTileTraversalCost\""),
                "WorldStream3D.GetTerrainTileTraversalCost must use Game3D PascalCase naming") &&
        ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_PendingRequestCount\""),
                 "WorldStream3D.PendingRequestCount getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_ResidentBytes\""),
                 "WorldStream3D.ResidentBytes getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.SetCenter\""),
                 "WorldStream3D.SetCenter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.SetRadii\""),
                 "WorldStream3D.SetRadii must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.MountTiledTerrain\""),
                 "WorldStream3D.MountTiledTerrain must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Game3D.WorldStream3D\""),
                 "WorldStream3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ResidentCellCount\""),
                 "WorldStream3D.ResidentCellCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetResidentTerrainTile\""),
                 "WorldStream3D.GetResidentTerrainTile method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCellCount\""),
                 "WorldStream3D.GetCellCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCellResident\""),
                 "WorldStream3D.GetCellResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCellMaterial\""),
                 "WorldStream3D.GetCellMaterial method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCellTraversalCost\""),
                 "WorldStream3D.GetCellTraversalCost method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetTerrainTileCount\""),
                 "WorldStream3D.GetTerrainTileCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetTerrainTileHeightmap\""),
                 "WorldStream3D.GetTerrainTileHeightmap method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetTerrainTileResident\""),
                 "WorldStream3D.GetTerrainTileResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetTerrainTileMaterial\""),
                 "WorldStream3D.GetTerrainTileMaterial method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetTerrainTileTraversalCost\""),
                 "WorldStream3D.GetTerrainTileTraversalCost method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetCenter\""),
                 "WorldStream3D.SetCenter method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.PlayLayerAdditive\""),
                 "Animator3D.PlayLayerAdditive must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"PlayLayerAdditive\""),
                 "Animator3D.PlayLayerAdditive method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.CrossfadeLayerAdditive\""),
                 "Animator3D.CrossfadeLayerAdditive must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"CrossfadeLayerAdditive\""),
                 "Animator3D.CrossfadeLayerAdditive method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.SetBlendTree\""),
                 "Animator3D.SetBlendTree must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetBlendTree\""),
                 "Animator3D.SetBlendTree method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.SetIKSolver\""),
                 "Animator3D.SetIKSolver must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetIKSolver\""),
                 "Animator3D.SetIKSolver method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.get_NodeAnimator\""),
                 "Animator3D.NodeAnimator getter must use Game3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"NodeAnimator\""),
                 "Animator3D.NodeAnimator property missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadAnimation\""),
                 "Assets3D.LoadAnimation must expose skeletal animation loading") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadNodeAnimation\""),
                 "Assets3D.LoadNodeAnimation must expose node animation loading") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"LoadNodeAnimation\""),
                 "Assets3D.LoadNodeAnimation method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Mesh3D.get_Resident\""),
                 "Mesh3D.Resident getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Mesh3D.set_Resident\""),
                 "Mesh3D.Resident setter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Mesh3D.get_ResidentBytes\""),
                 "Mesh3D.ResidentBytes getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode.SetLodResident\""),
                 "SceneNode.SetLodResident must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode.GetLodResident\""),
                 "SceneNode.GetLodResident must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode.GetLodResidentBytes\""),
                 "SceneNode.GetLodResidentBytes must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"Resident\""),
                 "Mesh3D.Resident property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ResidentBytes\""),
                 "Mesh3D.ResidentBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetLodResident\""),
                 "SceneNode.SetLodResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetLodResident\""),
                 "SceneNode.GetLodResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetLodResidentBytes\""),
                 "SceneNode.GetLodResidentBytes method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.get_SceneCount\""),
                 "SceneAsset.SceneCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.GetCameraCount\""),
                 "SceneAsset.GetCameraCount must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.GetCamera\""),
                 "SceneAsset.GetCamera must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.GetSceneName\""),
                 "SceneAsset.GetSceneName must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.InstantiateSceneAt\""),
                 "SceneAsset.InstantiateSceneAt must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.get_NodeAnimationCount\""),
                 "SceneAsset.NodeAnimationCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.GetNodeAnimation\""),
                 "SceneAsset.GetNodeAnimation must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneAsset.LoadNodeAnimation\""),
                 "SceneAsset.LoadNodeAnimation must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"SceneCount\""),
                 "SceneAsset.SceneCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCameraCount\""),
                 "SceneAsset.GetCameraCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCamera\""),
                 "SceneAsset.GetCamera method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetSceneName\""),
                 "SceneAsset.GetSceneName method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"InstantiateSceneAt\""),
                 "SceneAsset.InstantiateSceneAt method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"NodeAnimationCount\""),
                 "SceneAsset.NodeAnimationCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetNodeAnimation\""),
                 "SceneAsset.GetNodeAnimation method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"LoadNodeAnimation\""),
                 "SceneAsset.LoadNodeAnimation method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode.BindNodeAnimator\""),
                 "SceneNode.BindNodeAnimator must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode.get_NodeAnimator\""),
                 "SceneNode.NodeAnimator getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"NodeAnimator\""),
                 "SceneNode.NodeAnimator property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"BindNodeAnimator\""),
                 "SceneNode.BindNodeAnimator method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.NodeAnimation3D\""),
                 "NodeAnimation3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.NodeAnimator3D\""),
                 "NodeAnimator3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NodeAnimator3D.Play\""),
                 "NodeAnimator3D.Play must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ClipCount\""),
                 "NodeAnimator3D.ClipCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.LoadKTX2\""),
                 "TextureAsset3D.LoadKTX2 must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.LoadKTX2Asset\""),
                 "TextureAsset3D.LoadKTX2Asset must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_Width\""),
                 "TextureAsset3D.Width getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_Height\""),
                 "TextureAsset3D.Height getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_MipCount\""),
                 "TextureAsset3D.MipCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_Format\""),
                 "TextureAsset3D.Format getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_Compressed\""),
                 "TextureAsset3D.Compressed getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_ResidentMipStart\""),
                 "TextureAsset3D.ResidentMipStart getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_ResidentMipCount\""),
                 "TextureAsset3D.ResidentMipCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.get_ResidentBytes\""),
                 "TextureAsset3D.ResidentBytes getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.TextureAsset3D.SetResidentMipRange\""),
                 "TextureAsset3D.SetResidentMipRange must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.BlendTree3D.New1D\""),
                 "BlendTree3D.New1D must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.BlendTree3D.New2D\""),
                 "BlendTree3D.New2D must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.BlendTree3D.AddSample\""),
                 "BlendTree3D.AddSample must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.BlendTree3D.SetParam\""),
                 "BlendTree3D.SetParam must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.BlendTree3D.Update\""),
                 "BlendTree3D.Update must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.BlendTree3D.get_SampleCount\""),
                 "BlendTree3D.SampleCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.IKSolver3D.TwoBone\""),
                 "IKSolver3D.TwoBone must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.IKSolver3D.LookAt\""),
                 "IKSolver3D.LookAt must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.IKSolver3D.FABRIK\""),
                 "IKSolver3D.FABRIK must use Graphics3D all-caps acronym naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.IKSolver3D.SetTarget\""),
                 "IKSolver3D.SetTarget must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.IKSolver3D.SetWeight\""),
                 "IKSolver3D.SetWeight must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.IKSolver3D.Solve\""),
                 "IKSolver3D.Solve must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.AnimController3D.SetIKSolver\""),
                 "AnimController3D.SetIKSolver must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.TextureAsset3D\""),
                 "TextureAsset3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"MipCount\""),
                 "TextureAsset3D.MipCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ResidentMipStart\""),
                 "TextureAsset3D.ResidentMipStart property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ResidentMipCount\""),
                 "TextureAsset3D.ResidentMipCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ResidentBytes\""),
                 "TextureAsset3D.ResidentBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"LoadKTX2Asset\""),
                 "TextureAsset3D.LoadKTX2Asset method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetResidentMipRange\""),
                 "TextureAsset3D.SetResidentMipRange method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.BlendTree3D\""),
                 "BlendTree3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"SampleCount\""),
                 "BlendTree3D.SampleCount property missing") &&
         ok;
    ok =
        require(contains(runtime_def, "RT_METHOD(\"New2D\""), "BlendTree3D.New2D method missing") &&
        ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"AddSample\""),
                 "BlendTree3D.AddSample method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetParam\""),
                 "BlendTree3D.SetParam method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"Update\""),
                 "BlendTree3D.Update method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.IKSolver3D\""),
                 "IKSolver3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"TwoBone\""),
                 "IKSolver3D.TwoBone method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"LookAt\""),
                 "IKSolver3D.LookAt method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"FABRIK\""),
                 "IKSolver3D.FABRIK method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetTarget\""),
                 "IKSolver3D.SetTarget method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetWeight\""),
                 "IKSolver3D.SetWeight method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"Solve\""), "IKSolver3D.Solve method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetIKSolver\""),
                 "AnimController3D.SetIKSolver method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.AddOffMeshLink\""),
                 "NavMesh3D.AddOffMeshLink must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.get_OffMeshLinkCount\""),
                 "NavMesh3D.OffMeshLinkCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.SetOffMeshLinkMetadata\""),
                 "NavMesh3D.SetOffMeshLinkMetadata must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.GetOffMeshLinkKind\""),
                 "NavMesh3D.GetOffMeshLinkKind must use Graphics3D PascalCase naming") &&
         ok;
    ok =
        require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.GetOffMeshLinkTraversalCost\""),
                "NavMesh3D.GetOffMeshLinkTraversalCost must use Graphics3D PascalCase naming") &&
        ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.GetOffMeshLinkState\""),
                 "NavMesh3D.GetOffMeshLinkState must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.AddObstacle\""),
                 "NavMesh3D.AddObstacle must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.RemoveObstacle\""),
                 "NavMesh3D.RemoveObstacle must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.UpdateObstacle\""),
                 "NavMesh3D.UpdateObstacle must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.Bake\""),
                 "NavMesh3D.Bake must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.BakeTiled\""),
                 "NavMesh3D.BakeTiled must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.RebuildTile\""),
                 "NavMesh3D.RebuildTile must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.get_ObstacleCount\""),
                 "NavMesh3D.ObstacleCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.get_LastPathCost\""),
                 "NavMesh3D.LastPathCost getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.SetArea\""),
                 "NavMesh3D.SetArea must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.GetArea\""),
                 "NavMesh3D.GetArea must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.GetTraversalCost\""),
                 "NavMesh3D.GetTraversalCost must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"OffMeshLinkCount\""),
                 "NavMesh3D.OffMeshLinkCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ObstacleCount\""),
                 "NavMesh3D.ObstacleCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"LastPathCost\""),
                 "NavMesh3D.LastPathCost property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"AddOffMeshLink\""),
                 "NavMesh3D.AddOffMeshLink method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetOffMeshLinkMetadata\""),
                 "NavMesh3D.SetOffMeshLinkMetadata method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetOffMeshLinkKind\""),
                 "NavMesh3D.GetOffMeshLinkKind method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetOffMeshLinkTraversalCost\""),
                 "NavMesh3D.GetOffMeshLinkTraversalCost method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetOffMeshLinkState\""),
                 "NavMesh3D.GetOffMeshLinkState method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"AddObstacle\""),
                 "NavMesh3D.AddObstacle method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"RemoveObstacle\""),
                 "NavMesh3D.RemoveObstacle method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"UpdateObstacle\""),
                 "NavMesh3D.UpdateObstacle method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetArea\""),
                 "NavMesh3D.SetArea method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetArea\""),
                 "NavMesh3D.GetArea method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetTraversalCost\""),
                 "NavMesh3D.GetTraversalCost method missing") &&
         ok;
    ok =
        require(contains(runtime_def, "RT_METHOD(\"Bake\""), "NavMesh3D.Bake method missing") && ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"BakeTiled\""),
                 "NavMesh3D.BakeTiled method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"RebuildTile\""),
                 "NavMesh3D.RebuildTile method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavAgent3D.get_AvoidanceEnabled\""),
                 "NavAgent3D.AvoidanceEnabled getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavAgent3D.set_AvoidanceEnabled\""),
                 "NavAgent3D.AvoidanceEnabled setter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavAgent3D.get_AvoidanceRadius\""),
                 "NavAgent3D.AvoidanceRadius getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavAgent3D.set_AvoidanceRadius\""),
                 "NavAgent3D.AvoidanceRadius setter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"AvoidanceEnabled\""),
                 "NavAgent3D.AvoidanceEnabled property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"AvoidanceRadius\""),
                 "NavAgent3D.AvoidanceRadius property missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_BC1"),
                 "Canvas3D backend capability bit for bc1 missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_BC3"),
                 "Canvas3D backend capability bit for bc3 missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_BC4"),
                 "Canvas3D backend capability bit for bc4 missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_BC5"),
                 "Canvas3D backend capability bit for bc5 missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_BC7"),
                 "Canvas3D backend capability bit for bc7 missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_ASTC"),
                 "Canvas3D backend capability bit for astc missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_ETC2"),
                 "Canvas3D backend capability bit for etc2 missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_ANISOTROPY"),
                 "Canvas3D backend capability bit for anisotropy missing") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"bc1\")"),
                 "Canvas3D.BackendSupports missing bc1 capability name") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"bc3\")"),
                 "Canvas3D.BackendSupports missing bc3 capability name") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"bc4\")"),
                 "Canvas3D.BackendSupports missing bc4 capability name") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"bc5\")"),
                 "Canvas3D.BackendSupports missing bc5 capability name") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"bc7\")"),
                 "Canvas3D.BackendSupports missing bc7 capability name") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"astc\")"),
                 "Canvas3D.BackendSupports missing astc capability name") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"etc2\")"),
                 "Canvas3D.BackendSupports missing etc2 capability name") &&
         ok;
    ok = require(contains(canvas_overlay, "strcmp(name, \"anisotropy\")"),
                 "Canvas3D.BackendSupports missing anisotropy capability name") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_SolverIterations\""),
                 "Physics3DWorld.SolverIterations getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.set_SolverIterations\""),
                 "Physics3DWorld.SolverIterations setter must use get_/set_ property naming") &&
         ok;
    ok =
        require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_PositionIterations\""),
                "Physics3DWorld.PositionIterations getter must use Graphics3D PascalCase "
                "naming") &&
        ok;
    ok =
        require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.set_PositionIterations\""),
                "Physics3DWorld.PositionIterations setter must use get_/set_ property naming") &&
        ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_ContactBeta\""),
                 "Physics3DWorld.ContactBeta getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.set_ContactBeta\""),
                 "Physics3DWorld.ContactBeta setter must use get_/set_ property naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_RestitutionThreshold\""),
             "Physics3DWorld.RestitutionThreshold getter must use Graphics3D PascalCase "
             "naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.set_RestitutionThreshold\""),
             "Physics3DWorld.RestitutionThreshold setter must use get_/set_ property "
             "naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.StepFixed\""),
                 "Physics3DWorld.StepFixed must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_FixedStepAlpha\""),
                 "Physics3DWorld.FixedStepAlpha getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_DroppedFixedSteps\""),
                 "Physics3DWorld.DroppedFixedSteps getter must use Graphics3D PascalCase "
                 "naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_LastSolverIslandCount\""),
             "Physics3DWorld.LastSolverIslandCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def,
                          "\"Viper.Graphics3D.Physics3DWorld.get_LastSolverActiveBodyCount\""),
                 "Physics3DWorld.LastSolverActiveBodyCount getter must use Graphics3D PascalCase "
                 "naming") &&
         ok;
    ok =
        require(
            contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_LastSolverContactCount\""),
            "Physics3DWorld.LastSolverContactCount getter must use Graphics3D PascalCase naming") &&
        ok;
    ok = require(!contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.SetSolverIterations\""),
                 "Physics3DWorld.SetSolverIterations duplicate method must stay removed") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.RebaseOrigin\""),
                 "Physics3DWorld.RebaseOrigin must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Particles3D.RebaseOrigin\""),
                 "Particles3D.RebaseOrigin must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Sprite3D.RebaseOrigin\""),
                 "Sprite3D.RebaseOrigin must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.HingeJoint3D.New\""),
                 "HingeJoint3D.New must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.RopeJoint3D.New\""),
                 "RopeJoint3D.New must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.RopeJoint3D.get_MaxLength\""),
                 "RopeJoint3D.MaxLength getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SixDofJoint3D.New\""),
                 "SixDofJoint3D.New must use symbol-friendly Dof spelling") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SixDofJoint3D.SetLinearLimits\""),
                 "SixDofJoint3D.SetLinearLimits must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SixDofJoint3D.SetAngularLimits\""),
                 "SixDofJoint3D.SetAngularLimits must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.AnimController3D.PlayLayerAdditive\""),
                 "AnimController3D.PlayLayerAdditive must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Graphics3D.AnimController3D.CrossfadeLayerAdditive\""),
             "AnimController3D.CrossfadeLayerAdditive must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.AnimController3D.SetAnimationLOD\""),
                 "AnimController3D.SetAnimationLOD must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.AnimController3D.SetBlendTree\""),
                 "AnimController3D.SetBlendTree must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Animation3D.Retarget\""),
                 "Animation3D.Retarget must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"SolverIterations\""),
                 "Physics3DWorld.SolverIterations property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"PositionIterations\""),
                 "Physics3DWorld.PositionIterations property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ContactBeta\""),
                 "Physics3DWorld.ContactBeta property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"RestitutionThreshold\""),
                 "Physics3DWorld.RestitutionThreshold property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"FixedStepAlpha\""),
                 "Physics3DWorld.FixedStepAlpha property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"DroppedFixedSteps\""),
                 "Physics3DWorld.DroppedFixedSteps property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"LastSolverIslandCount\""),
                 "Physics3DWorld.LastSolverIslandCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"LastSolverActiveBodyCount\""),
                 "Physics3DWorld.LastSolverActiveBodyCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"LastSolverContactCount\""),
                 "Physics3DWorld.LastSolverContactCount property missing") &&
         ok;
    ok = require(!contains(runtime_def, "RT_METHOD(\"SetSolverIterations\""),
                 "Physics3DWorld.SetSolverIterations duplicate method must stay removed") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"StepFixed\", \"i64(f64,f64,i64)\""),
                 "Physics3DWorld.StepFixed method missing") &&
         ok;
    ok = require(contains(runtime_def,
                          "RT_METHOD(\"RebaseOrigin\", \"void(f64,f64,f64)\", "
                          "World3DRebaseOrigin)"),
                 "Physics3DWorld.RebaseOrigin method missing") &&
         ok;
    ok = require(contains(runtime_def,
                          "RT_METHOD(\"RebaseOrigin\", \"void(f64,f64,f64)\", "
                          "Particles3DRebaseOrigin)"),
                 "Particles3D.RebaseOrigin method missing") &&
         ok;
    ok = require(contains(runtime_def,
                          "RT_METHOD(\"RebaseOrigin\", \"void(f64,f64,f64)\", "
                          "Sprite3DRebaseOrigin)"),
                 "Sprite3D.RebaseOrigin method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.HingeJoint3D\""),
                 "HingeJoint3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.RopeJoint3D\""),
                 "RopeJoint3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"MaxLength\""),
                 "RopeJoint3D.MaxLength property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Graphics3D.SixDofJoint3D\""),
                 "SixDofJoint3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetLinearLimits\""),
                 "SixDofJoint3D.SetLinearLimits method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetAngularLimits\""),
                 "SixDofJoint3D.SetAngularLimits method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"PlayLayerAdditive\""),
                 "AnimController3D.PlayLayerAdditive method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"CrossfadeLayerAdditive\""),
                 "AnimController3D.CrossfadeLayerAdditive method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetAnimationLOD\""),
                 "AnimController3D.SetAnimationLOD method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetBlendTree\""),
                 "AnimController3D.SetBlendTree method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"Retarget\""),
                 "Animation3D.Retarget method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.RebaseOrigin\""),
                 "SceneGraph.RebaseOrigin must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.QueryAABB\""),
                 "SceneGraph.QueryAABB must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.QuerySphere\""),
                 "SceneGraph.QuerySphere must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.RaycastNodes\""),
                 "SceneGraph.RaycastNodes must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.get_VisibleNodeCount\""),
                 "SceneGraph.VisibleNodeCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"RebaseOrigin\""),
                 "SceneGraph.RebaseOrigin method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"QueryAABB\""),
                 "SceneGraph.QueryAABB method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"QuerySphere\""),
                 "SceneGraph.QuerySphere method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"RaycastNodes\""),
                 "SceneGraph.RaycastNodes method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"VisibleNodeCount\""),
                 "SceneGraph.VisibleNodeCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Light3D.get_CastsShadows\""),
                 "Light3D.CastsShadows getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Light3D.set_CastsShadows\""),
                 "Light3D.CastsShadows setter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"CastsShadows\""),
                 "Light3D.CastsShadows property missing") &&
         ok;
    ok = require(!contains(runtime_def, "RT_METHOD(\"SetCastsShadows\""),
                 "Light3D.SetCastsShadows duplicate method must stay removed") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Light3D.set_Enabled\""),
                 "Light3D.Enabled property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Light3D.set_CastsShadows\""),
                 "Light3D.CastsShadows property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasTexture\""),
                 "Material3D.HasTexture getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasNormalMap\""),
                 "Material3D.HasNormalMap getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasSpecularMap\""),
                 "Material3D.HasSpecularMap getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasEmissiveMap\""),
                 "Material3D.HasEmissiveMap getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasMetallicRoughnessMap\""),
             "Material3D.HasMetallicRoughnessMap getter must use Graphics3D PascalCase naming") &&
         ok;
    ok =
        require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasAmbientOcclusionMap\""),
                "Material3D.HasAmbientOcclusionMap getter must use Graphics3D PascalCase naming") &&
        ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasEnvMap\""),
                 "Material3D.HasEnvMap getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"HasTexture\""),
                 "Material3D.HasTexture property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"HasMetallicRoughnessMap\""),
                 "Material3D.HasMetallicRoughnessMap property missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_Metallic\""),
                 "Material3D.Metallic property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_Roughness\""),
                 "Material3D.Roughness property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_AmbientOcclusion\""),
                 "Material3D.AmbientOcclusion property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_EmissiveIntensity\""),
                 "Material3D.EmissiveIntensity property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_NormalScale\""),
                 "Material3D.NormalScale property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_Anisotropy\""),
                 "Material3D.Anisotropy getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_Anisotropy\""),
                 "Material3D.Anisotropy setter must use get_/set_ property naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_Anisotropy\""),
                 "Material3D.Anisotropy property setter missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"Anisotropy\""),
                 "Material3D.Anisotropy property missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.set_Reflectivity\""),
                 "Material3D.Reflectivity property setter missing") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.set_ClusteredLighting\""),
             "Canvas3D.ClusteredLighting property setter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_MaxActiveLights\""),
                 "Canvas3D.MaxActiveLights getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.SetShadowCascades\""),
                 "Canvas3D.SetShadowCascades must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_OccludedDrawCount\""),
                 "Canvas3D.OccludedDrawCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_OcclusionCandidateCount\""),
                 "Canvas3D.OcclusionCandidateCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_DrawCount\""),
                 "Canvas3D.DrawCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_TextureUploadBytes\""),
                 "Canvas3D.TextureUploadBytes getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_FrameGpuTimeUs\""),
                 "Canvas3D.FrameGpuTimeUs getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_DrawsSubmitted\""),
                 "Canvas3D.DrawsSubmitted getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_AabbTransforms\""),
                 "Canvas3D.AabbTransforms getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_SortPasses\""),
                 "Canvas3D.SortPasses getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_BackendStateChanges\""),
                 "Canvas3D.BackendStateChanges getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_Backend\""),
                 "Canvas3D.Backend getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_BackendFallback\""),
                 "Canvas3D.BackendFallback getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_BackendFallbackReason\""),
                 "Canvas3D.BackendFallbackReason getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def,
                          "\"Viper.Graphics3D.Canvas3D.get_InstancedFallbackDroppedCount\""),
                 "Canvas3D.InstancedFallbackDroppedCount getter must use Graphics3D PascalCase "
                 "naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_EventDropCount\""),
                 "Canvas3D.EventDropCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_MeshSnapshotDropCount\""),
                 "Canvas3D.MeshSnapshotDropCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.SetTextureUploadBudget\""),
                 "Canvas3D.SetTextureUploadBudget must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_TextureUploadPendingBytes\""),
             "Canvas3D.TextureUploadPendingBytes getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.AddVisibilityZone\""),
                 "SceneGraph.AddVisibilityZone must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.AddVisibilityPortal\""),
                 "SceneGraph.AddVisibilityPortal must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneGraph.get_PvsCulledCount\""),
                 "SceneGraph.PvsCulledCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode.SetAutoLOD\""),
                 "SceneNode.SetAutoLOD must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode.SetImpostor\""),
                 "SceneNode.SetImpostor must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ClusteredLighting\""),
                 "Canvas3D.ClusteredLighting property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetShadowCascades\""),
                 "Canvas3D.SetShadowCascades method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"MaxActiveLights\""),
                 "Canvas3D.MaxActiveLights property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"OccludedDrawCount\""),
                 "Canvas3D.OccludedDrawCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"OcclusionCandidateCount\""),
                 "Canvas3D.OcclusionCandidateCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"DrawCount\""),
                 "Canvas3D.DrawCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"TextureUploadBytes\""),
                 "Canvas3D.TextureUploadBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"FrameGpuTimeUs\""),
                 "Canvas3D.FrameGpuTimeUs property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"DrawsSubmitted\""),
                 "Canvas3D.DrawsSubmitted property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"AabbTransforms\""),
                 "Canvas3D.AabbTransforms property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"SortPasses\""),
                 "Canvas3D.SortPasses property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"BackendStateChanges\""),
                 "Canvas3D.BackendStateChanges property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetTextureUploadBudget\""),
                 "Canvas3D.SetTextureUploadBudget method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"TextureUploadPendingBytes\""),
                 "Canvas3D.TextureUploadPendingBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"AddVisibilityZone\""),
                 "SceneGraph.AddVisibilityZone method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"AddVisibilityPortal\""),
                 "SceneGraph.AddVisibilityPortal method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"PvsCulledCount\""),
                 "SceneGraph.PvsCulledCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetAutoLOD\""),
                 "SceneNode.SetAutoLOD method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetImpostor\""),
                 "SceneNode.SetImpostor method missing") &&
         ok;

    ok =
        require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.get_BroadphaseFallbackCount\""),
                "Diagnostics.BroadphaseFallbackCount getter missing") &&
        ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.get_CcdClampedFrames\""),
                 "Diagnostics.CcdClampedFrames getter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.get_CcdClampedBodies\""),
                 "Diagnostics.CcdClampedBodies getter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.get_AnimEventsDropped\""),
                 "Diagnostics.AnimEventsDropped getter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.get_AudioVoicesEvicted\""),
                 "Diagnostics.AudioVoicesEvicted getter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.get_NavGridFallbacks\""),
                 "Diagnostics.NavGridFallbacks getter missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.Reset\""),
                 "Diagnostics.Reset method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Diagnostics3D.Summary\""),
                 "Diagnostics.Summary method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Game3D.Diagnostics3D\""),
                 "Diagnostics class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"BroadphaseFallbackCount\""),
                 "Diagnostics.BroadphaseFallbackCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"Summary\", \"str()\""),
                 "Diagnostics.Summary runtime method missing") &&
         ok;

    static const char *forbidden[] = {
        "Viper.Game3D.World3D.get_workerCount",
        "Viper.Game3D.World3D.get_jobsEnabled",
        "Viper.Game3D.World3D.setWorkerCount",
        "Viper.Game3D.World3D.get_floatingOrigin",
        "Viper.Game3D.World3D.set_floatingOrigin",
        "Viper.Game3D.World3D.get_worldOrigin",
        "Viper.Game3D.World3D.get_entityCount",
        "Viper.Game3D.World3D.get_bodyCount",
        "Viper.Game3D.World3D.get_drawCount",
        "Viper.Game3D.World3D.get_visibleNodeCount",
        "Viper.Game3D.World3D.get_visible_node_count",
        "Viper.Game3D.World3D.get_occludedDrawCount",
        "Viper.Game3D.World3D.get_occluded_draw_count",
        "Viper.Game3D.World3D.get_streamResidentBytes",
        "Viper.Game3D.World3D.get_stream_resident_bytes",
        "Viper.Game3D.World3D.setOriginRebaseThreshold",
        "Viper.Game3D.World3D.rebaseOrigin",
        "Viper.Game3D.World3D.bakeNavMesh",
        "Viper.Game3D.World3D.bake_nav_mesh",
        "Viper.Game3D.World3D.bakeTiledNavMesh",
        "Viper.Game3D.World3D.bake_tiled_nav_mesh",
        "Viper.Game3D.Collision3DEvent.get_contactCount",
        "Viper.Game3D.Collision3DEvent.contactPoint",
        "Viper.Game3D.Collision3DEvent.contactNormal",
        "Viper.Game3D.Collision3DEvent.contactSeparation",
        "Viper.Game3D.Entity3D.fromNode",
        "Viper.Game3D.Entity3D.WrapNode",
        "Viper.Game3D.Animator3D.playLayerAdditive",
        "Viper.Game3D.Animator3D.play_layer_additive",
        "Viper.Game3D.Animator3D.crossfadeLayerAdditive",
        "Viper.Game3D.Animator3D.crossfadeLayeradditive",
        "Viper.Game3D.Animator3D.crossfade_layer_additive",
        "Viper.Game3D.Animator3D.setBlendTree",
        "Viper.Game3D.Animator3D.set_blend_tree",
        "Viper.Game3D.Animator3D.setIKSolver",
        "Viper.Game3D.WorldStream3D.getResidentTerrainTile",
        "Viper.Game3D.WorldStream3D.get_resident_terrain_tile",
        "Viper.Game3D.WorldStream3D.getresidentTerrainTile",
        "Viper.Game3D.WorldStream3D.getCellCount",
        "Viper.Game3D.WorldStream3D.get_cellCount",
        "Viper.Game3D.WorldStream3D.get_cell_count",
        "Viper.Game3D.WorldStream3D.getCellMaterial",
        "Viper.Game3D.WorldStream3D.get_cell_material",
        "Viper.Game3D.WorldStream3D.getTerrainTileCount",
        "Viper.Game3D.WorldStream3D.get_terrainTileCount",
        "Viper.Game3D.WorldStream3D.get_terrain_tile_count",
        "Viper.Game3D.WorldStream3D.getTerrainTileHeightmap",
        "Viper.Game3D.WorldStream3D.get_terrainTileHeightmap",
        "Viper.Game3D.WorldStream3D.get_terrain_tile_heightmap",
        "Viper.Game3D.WorldStream3D.getTerrainTileMaterial",
        "Viper.Game3D.WorldStream3D.get_terrain_tile_material",
        "Viper.Graphics3D.Physics3DWorld.get_solverIterations",
        "Viper.Graphics3D.Physics3DWorld.get_lastSolverIslandCount",
        "Viper.Graphics3D.Physics3DWorld.get_last_solver_island_count",
        "Viper.Graphics3D.Physics3DWorld.get_lastSolverActiveBodyCount",
        "Viper.Graphics3D.Physics3DWorld.get_last_solver_active_body_count",
        "Viper.Graphics3D.Physics3DWorld.get_lastSolverContactCount",
        "Viper.Graphics3D.Physics3DWorld.get_last_solver_contact_count",
        "Viper.Graphics3D.Physics3DWorld.setSolverIterations",
        "Viper.Graphics3D.Physics3DWorld.get_positionIterations",
        "Viper.Graphics3D.Physics3DWorld.setPositionIterations",
        "Viper.Graphics3D.Physics3DWorld.get_contactBeta",
        "Viper.Graphics3D.Physics3DWorld.setContactBeta",
        "Viper.Graphics3D.Physics3DWorld.get_restitutionThreshold",
        "Viper.Graphics3D.Physics3DWorld.setRestitutionThreshold",
        "Viper.Graphics3D.Physics3DWorld.stepFixed",
        "Viper.Graphics3D.Physics3DWorld.get_fixedStepAlpha",
        "Viper.Graphics3D.Physics3DWorld.get_droppedFixedSteps",
        "Viper.Graphics3D.HingeJoint3D.new",
        "Viper.Graphics3D.RopeJoint3D.get_maxLength",
        "Viper.Graphics3D.SixDofJoint3D.setLinearLimits",
        "Viper.Graphics3D.SixDofJoint3D.setAngularLimits",
        "Viper.Graphics3D.AnimController3D.playLayerAdditive",
        "Viper.Graphics3D.AnimController3D.PlaylayerAdditive",
        "Viper.Graphics3D.AnimController3D.PlayLayeradditive",
        "Viper.Graphics3D.AnimController3D.crossfadeLayerAdditive",
        "Viper.Graphics3D.AnimController3D.CrossfadelayerAdditive",
        "Viper.Graphics3D.AnimController3D.CrossfadeLayeradditive",
        "Viper.Graphics3D.AnimController3D.setAnimationLOD",
        "Viper.Graphics3D.AnimController3D.SetAnimationLod",
        "Viper.Graphics3D.AnimController3D.SetAnimLOD",
        "Viper.Graphics3D.AnimController3D.setBlendTree",
        "Viper.Graphics3D.AnimController3D.SetBlendtree",
        "Viper.Graphics3D.Animation3D.retarget",
        "Viper.Graphics3D.Animation3D.RetargetAnimation",
        "Viper.Graphics3D.SceneGraph.rebaseOrigin",
        "Viper.Graphics3D.SceneGraph.Rebaseorigin",
        "Viper.Graphics3D.SceneGraph.queryAABB",
        "Viper.Graphics3D.SceneGraph.QueryAabb",
        "Viper.Graphics3D.SceneGraph.Raycastnodes",
        "Viper.Graphics3D.SceneGraph.get_visibleNodeCount",
        "Viper.Graphics3D.Light3D.get_castsShadows",
        "Viper.Graphics3D.Light3D.set_castsShadows",
        "Viper.Graphics3D.Light3D.SetCastShadows",
        "Viper.Graphics3D.Material3D.get_hasTexture",
        "Viper.Graphics3D.Material3D.HasTexture",
        "Viper.Graphics3D.Material3D.SetTextureAsset",
        "Viper.Graphics3D.TextureAsset3D.loadKTX2",
        "Viper.Graphics3D.TextureAsset3D.loadKtx2",
        "Viper.Graphics3D.TextureAsset3D.get_mipCount",
        "Viper.Graphics3D.TextureAsset3D.get_residentMipCount",
        "Viper.Graphics3D.TextureAsset3D.setResidentMipRange",
        "Viper.Graphics3D.TextureAsset3D.SetResidentMiprange",
        "Viper.Graphics3D.TextureAsset3D.set_Texture",
        "Viper.Graphics3D.Blendtree3D",
        "Viper.Graphics3D.BlendTree3D.new1D",
        "Viper.Graphics3D.BlendTree3D.New1d",
        "Viper.Graphics3D.BlendTree3D.new2D",
        "Viper.Graphics3D.BlendTree3D.New2d",
        "Viper.Graphics3D.BlendTree3D.addSample",
        "Viper.Graphics3D.BlendTree3D.setParam",
        "Viper.Graphics3D.BlendTree3D.get_sampleCount",
        "Viper.Graphics3D.IK3D",
        "Viper.Graphics3D.IKSolver3D.twoBone",
        "Viper.Graphics3D.IKSolver3D.Twobone",
        "Viper.Graphics3D.IKSolver3D.lookAt",
        "Viper.Graphics3D.IKSolver3D.Fabrik",
        "Viper.Graphics3D.IKSolver3D.setTarget",
        "Viper.Graphics3D.IKSolver3D.setWeight",
        "Viper.Graphics3D.AnimController3D.setIKSolver",
        "Viper.Game3D.Animator3D.setIkSolver",
        "Viper.Graphics3D.NavMesh3D.addOffMeshLink",
        "Viper.Graphics3D.NavMesh3D.AddOffmeshLink",
        "Viper.Graphics3D.NavMesh3D.setOffMeshLinkMetadata",
        "Viper.Graphics3D.NavMesh3D.SetOffmeshLinkMetadata",
        "Viper.Graphics3D.NavMesh3D.getOffMeshLinkKind",
        "Viper.Graphics3D.NavMesh3D.GetOffmeshLinkKind",
        "Viper.Graphics3D.NavMesh3D.getOffMeshLinkTraversalCost",
        "Viper.Graphics3D.NavMesh3D.getOffMeshLinkState",
        "Viper.Graphics3D.NavMesh3D.get_offMeshLinkCount",
        "Viper.Graphics3D.NavMesh3D.OffmeshLinkCount",
        "Viper.Graphics3D.NavMesh3D.addObstacle",
        "Viper.Graphics3D.NavMesh3D.Addobstacle",
        "Viper.Graphics3D.NavMesh3D.removeObstacle",
        "Viper.Graphics3D.NavMesh3D.Removeobstacle",
        "Viper.Graphics3D.NavMesh3D.updateObstacle",
        "Viper.Graphics3D.NavMesh3D.Updateobstacle",
        "Viper.Graphics3D.NavMesh3D.bake",
        "Viper.Graphics3D.NavMesh3D.bakeTiled",
        "Viper.Graphics3D.NavMesh3D.Baketiled",
        "Viper.Graphics3D.NavMesh3D.rebuildTile",
        "Viper.Graphics3D.NavMesh3D.Rebuildtile",
        "Viper.Graphics3D.NavMesh3D.get_obstacleCount",
        "Viper.Graphics3D.NavMesh3D.Obstaclecount",
        "Viper.Graphics3D.NavMesh3D.get_lastPathCost",
        "Viper.Graphics3D.NavMesh3D.LastpathCost",
        "Viper.Graphics3D.NavMesh3D.setArea",
        "Viper.Graphics3D.NavMesh3D.getArea",
        "Viper.Graphics3D.NavMesh3D.getTraversalCost",
        "Viper.Graphics3D.NavAgent3D.get_avoidanceEnabled",
        "Viper.Graphics3D.NavAgent3D.set_avoidanceEnabled",
        "Viper.Graphics3D.NavAgent3D.get_avoidanceRadius",
        "Viper.Graphics3D.NavAgent3D.set_avoidanceRadius",
        "Viper.Graphics3D.NavAgent3D.setAvoidanceEnabled",
        "Viper.Graphics3D.NavAgent3D.setAvoidanceRadius",
        "Viper.Graphics3D.Canvas3D.setClusteredLighting",
        "Viper.Graphics3D.Canvas3D.get_maxActiveLights",
        "Viper.Graphics3D.Canvas3D.setShadowCascades",
        "Viper.Graphics3D.Canvas3D.get_occludedDrawCount",
        "Viper.Graphics3D.Canvas3D.get_drawCount",
        "Viper.Graphics3D.Canvas3D.setTextureUploadBudget",
        "Viper.Graphics3D.Canvas3D.get_textureUploadPendingBytes",
        "Viper.Graphics3D.SceneNode.setAutoLOD",
        "Viper.Graphics3D.SceneNode.setImpostor",
        "Viper.Graphics3D.Canvas3D.SetShadowCascade\"",
        "Viper.Game3D.ModelHandle",
        "Viper.Graphics3D.SixDOFJoint3D",
        "Viper.Graphics3D.SixDoFJoint3D",
        "SetTextureAsset",
        "SetNormalMapAsset",
        "SelectScene",
    };
    for (const char *needle : forbidden)
        ok = require(!contains(runtime_def, needle),
                     std::string("forbidden 3D API name: ") + needle) &&
             ok;
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok = check_class_ids() && ok;
    ok = check_runtime_surface_names() && ok;
    if (ok)
        std::cout << "Graphics3D ABI surface guardrails passed.\n";
    return ok ? 0 : 1;
}
