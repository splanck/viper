//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_graphics3d_abi_surface.cpp
// Purpose: Source-level guardrails for Viper.Graphics3D / Viper.Game3D public
//   ABI naming and class-id sentinels.
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
    const std::string canvas_overlay = read_file("src/runtime/graphics/3d/render/rt_canvas3d_overlay.c");
    bool ok = true;

    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_workerCount\""),
                 "World3D.workerCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_jobsEnabled\""),
                 "World3D.jobsEnabled getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.setWorkerCount\""),
                 "World3D.setWorkerCount must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_floatingOrigin\""),
                 "World3D.floatingOrigin getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.set_floatingOrigin\""),
                 "World3D.floatingOrigin setter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_worldOrigin\""),
                 "World3D.worldOrigin getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_stream\""),
                 "World3D.stream getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_entityCount\""),
                 "World3D.entityCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_bodyCount\""),
                 "World3D.bodyCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_drawCount\""),
                 "World3D.drawCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_visibleNodeCount\""),
                 "World3D.visibleNodeCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_occludedDrawCount\""),
                 "World3D.occludedDrawCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.get_streamResidentBytes\""),
                 "World3D.streamResidentBytes getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.setOriginRebaseThreshold\""),
                 "World3D.setOriginRebaseThreshold must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.rebaseOrigin\""),
                 "World3D.rebaseOrigin must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.bakeNavMesh\""),
                 "World3D.bakeNavMesh must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.World3D.bakeTiledNavMesh\""),
                 "World3D.bakeTiledNavMesh must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"workerCount\""),
                 "World3D.workerCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"jobsEnabled\""),
                 "World3D.jobsEnabled property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"floatingOrigin\""),
                 "World3D.floatingOrigin property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"worldOrigin\""),
                 "World3D.worldOrigin property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"stream\""),
                 "World3D.stream property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"entityCount\""),
                 "World3D.entityCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"bodyCount\""),
                 "World3D.bodyCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"drawCount\""),
                 "World3D.drawCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"visibleNodeCount\""),
                 "World3D.visibleNodeCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"occludedDrawCount\""),
                 "World3D.occludedDrawCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"streamResidentBytes\""),
                 "World3D.streamResidentBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"setWorkerCount\""),
                 "World3D.setWorkerCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"setOriginRebaseThreshold\""),
                 "World3D.setOriginRebaseThreshold method missing") &&
         ok;
    ok = require(contains(runtime_def,
                          "RT_METHOD(\"rebaseOrigin\", \"void(f64,f64,f64)\", "
                          "Game3DWorldRebaseOrigin)"),
                 "World3D.rebaseOrigin method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"bakeNavMesh\""),
                 "World3D.bakeNavMesh method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"bakeTiledNavMesh\""),
                 "World3D.bakeTiledNavMesh method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.get_contactCount\""),
                 "Collision3DEvent.contactCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.contactPoint\""),
                 "Collision3DEvent.contactPoint must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.contactNormal\""),
                 "Collision3DEvent.contactNormal must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Collision3DEvent.contactSeparation\""),
                 "Collision3DEvent.contactSeparation must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"contactCount\""),
                 "Collision3DEvent.contactCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"contactPoint\""),
                 "Collision3DEvent.contactPoint method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"contactNormal\""),
                 "Collision3DEvent.contactNormal method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"contactSeparation\""),
                 "Collision3DEvent.contactSeparation method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Entity3D.FromNode\""),
                 "Entity3D.FromNode must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"FromNode\""),
                 "Entity3D.FromNode method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadModelAsync\""),
                 "Assets3D.LoadModelAsync must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadModelAssetAsync\""),
                 "Assets3D.LoadModelAssetAsync must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadModelTemplateAsync\""),
                 "Assets3D.LoadModelTemplateAsync must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Assets3D.LoadModelTemplateAssetAsync\""),
                 "Assets3D.LoadModelTemplateAssetAsync must use Game3D PascalCase factory naming") &&
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
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.get_ready\""),
                 "AssetHandle3D.ready getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.get_progress\""),
                 "AssetHandle3D.progress getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.get_error\""),
                 "AssetHandle3D.error getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.cancel\""),
                 "AssetHandle3D.cancel must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.getEntity\""),
                 "AssetHandle3D.getEntity must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.AssetHandle3D.getTemplate\""),
                 "AssetHandle3D.getTemplate must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Game3D.AssetHandle3D\""),
                 "AssetHandle3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ready\""),
                 "AssetHandle3D.ready property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"LoadModelAsync\""),
                 "Assets3D.LoadModelAsync method missing") &&
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
    ok = require(contains(runtime_def, "RT_METHOD(\"Evict\""),
                 "Assets3D.Evict method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getEntity\""),
                 "AssetHandle3D.getEntity method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.New\""),
                 "WorldStream3D.New must use Game3D PascalCase factory naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_residentCellCount\""),
                 "WorldStream3D.residentCellCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(
             contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_residentTerrainTileCount\""),
             "WorldStream3D.residentTerrainTileCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getResidentTerrainTile\""),
                 "WorldStream3D.getResidentTerrainTile must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellCount\""),
                 "WorldStream3D.getCellCount must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellName\""),
                 "WorldStream3D.getCellName must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellCenter\""),
                 "WorldStream3D.getCellCenter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellResident\""),
                 "WorldStream3D.getCellResident must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellBytes\""),
                 "WorldStream3D.getCellBytes must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellMaterial\""),
                 "WorldStream3D.getCellMaterial must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellCollisionMask\""),
                 "WorldStream3D.getCellCollisionMask must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getCellTraversalCost\""),
                 "WorldStream3D.getCellTraversalCost must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getTerrainTileCount\""),
                 "WorldStream3D.getTerrainTileCount must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getTerrainTileName\""),
                 "WorldStream3D.getTerrainTileName must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getTerrainTileHeightmap\""),
                 "WorldStream3D.getTerrainTileHeightmap must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getTerrainTileCenter\""),
                 "WorldStream3D.getTerrainTileCenter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getTerrainTileResident\""),
                 "WorldStream3D.getTerrainTileResident must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getTerrainTileBytes\""),
                 "WorldStream3D.getTerrainTileBytes must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.getTerrainTileMaterial\""),
                 "WorldStream3D.getTerrainTileMaterial must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def,
                         "\"Viper.Game3D.WorldStream3D.getTerrainTileCollisionMask\""),
                 "WorldStream3D.getTerrainTileCollisionMask must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def,
                         "\"Viper.Game3D.WorldStream3D.getTerrainTileTraversalCost\""),
                 "WorldStream3D.getTerrainTileTraversalCost must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_pendingRequestCount\""),
                 "WorldStream3D.pendingRequestCount getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.get_residentBytes\""),
                 "WorldStream3D.residentBytes getter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.setCenter\""),
                 "WorldStream3D.setCenter must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.setRadii\""),
                 "WorldStream3D.setRadii must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.WorldStream3D.mountTiledTerrain\""),
                 "WorldStream3D.mountTiledTerrain must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_CLASS_BEGIN(\"Viper.Game3D.WorldStream3D\""),
                 "WorldStream3D class missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"residentCellCount\""),
                 "WorldStream3D.residentCellCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getResidentTerrainTile\""),
                 "WorldStream3D.getResidentTerrainTile method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getCellCount\""),
                 "WorldStream3D.getCellCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getCellResident\""),
                 "WorldStream3D.getCellResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getCellMaterial\""),
                 "WorldStream3D.getCellMaterial method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getCellTraversalCost\""),
                 "WorldStream3D.getCellTraversalCost method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getTerrainTileCount\""),
                 "WorldStream3D.getTerrainTileCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getTerrainTileHeightmap\""),
                 "WorldStream3D.getTerrainTileHeightmap method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getTerrainTileResident\""),
                 "WorldStream3D.getTerrainTileResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getTerrainTileMaterial\""),
                 "WorldStream3D.getTerrainTileMaterial method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"getTerrainTileTraversalCost\""),
                 "WorldStream3D.getTerrainTileTraversalCost method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"setCenter\""),
                 "WorldStream3D.setCenter method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.playLayerAdditive\""),
                 "Animator3D.playLayerAdditive must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"playLayerAdditive\""),
                 "Animator3D.playLayerAdditive method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.crossfadeLayerAdditive\""),
                 "Animator3D.crossfadeLayerAdditive must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"crossfadeLayerAdditive\""),
                 "Animator3D.crossfadeLayerAdditive method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.setBlendTree\""),
                 "Animator3D.setBlendTree must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"setBlendTree\""),
                 "Animator3D.setBlendTree method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Game3D.Animator3D.setIKSolver\""),
                 "Animator3D.setIKSolver must use Game3D lower/camel naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"setIKSolver\""),
                 "Animator3D.setIKSolver method missing") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode3D.SetLodResident\""),
                 "SceneNode3D.SetLodResident must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode3D.GetLodResident\""),
                 "SceneNode3D.GetLodResident must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode3D.GetLodResidentBytes\""),
                 "SceneNode3D.GetLodResidentBytes must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"Resident\""),
                 "Mesh3D.Resident property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"ResidentBytes\""),
                 "Mesh3D.ResidentBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetLodResident\""),
                 "SceneNode3D.SetLodResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetLodResident\""),
                 "SceneNode3D.GetLodResident method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetLodResidentBytes\""),
                 "SceneNode3D.GetLodResidentBytes method missing") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Model3D.get_SceneCount\""),
                 "Model3D.SceneCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Model3D.GetCameraCount\""),
                 "Model3D.GetCameraCount must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Model3D.GetCamera\""),
                 "Model3D.GetCamera must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Model3D.GetSceneName\""),
                 "Model3D.GetSceneName must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Model3D.InstantiateSceneAt\""),
                 "Model3D.InstantiateSceneAt must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"SceneCount\""),
                 "Model3D.SceneCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCameraCount\""),
                 "Model3D.GetCameraCount method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetCamera\""),
                 "Model3D.GetCamera method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"GetSceneName\""),
                 "Model3D.GetSceneName method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"InstantiateSceneAt\""),
                 "Model3D.InstantiateSceneAt method missing") &&
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
    ok = require(contains(runtime_def, "RT_METHOD(\"New2D\""),
                 "BlendTree3D.New2D method missing") &&
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
    ok = require(contains(runtime_def, "RT_METHOD(\"Solve\""),
                 "IKSolver3D.Solve method missing") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.NavMesh3D.GetOffMeshLinkTraversalCost\""),
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
    ok = require(contains(runtime_def, "RT_METHOD(\"Bake\""),
                 "NavMesh3D.Bake method missing") &&
         ok;
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
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_BC7"),
                 "Canvas3D backend capability bit for bc7 missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_ASTC"),
                 "Canvas3D backend capability bit for astc missing") &&
         ok;
    ok = require(contains(canvas_header, "RT_CANVAS3D_BACKEND_CAP_ETC2"),
                 "Canvas3D backend capability bit for etc2 missing") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_SolverIterations\""),
                 "Physics3DWorld.SolverIterations getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_LastSolverIslandCount\""),
                 "Physics3DWorld.LastSolverIslandCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_LastSolverActiveBodyCount\""),
                 "Physics3DWorld.LastSolverActiveBodyCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.get_LastSolverContactCount\""),
                 "Physics3DWorld.LastSolverContactCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Physics3DWorld.SetSolverIterations\""),
                 "Physics3DWorld.SetSolverIterations must use Graphics3D PascalCase naming") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.AnimController3D.CrossfadeLayerAdditive\""),
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
    ok = require(contains(runtime_def, "RT_PROP(\"LastSolverIslandCount\""),
                 "Physics3DWorld.LastSolverIslandCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"LastSolverActiveBodyCount\""),
                 "Physics3DWorld.LastSolverActiveBodyCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"LastSolverContactCount\""),
                 "Physics3DWorld.LastSolverContactCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetSolverIterations\""),
                 "Physics3DWorld.SetSolverIterations method missing") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.RebaseOrigin\""),
                 "Scene3D.RebaseOrigin must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.QueryAABB\""),
                 "Scene3D.QueryAABB must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.QuerySphere\""),
                 "Scene3D.QuerySphere must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.RaycastNodes\""),
                 "Scene3D.RaycastNodes must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.get_VisibleNodeCount\""),
                 "Scene3D.VisibleNodeCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"RebaseOrigin\""),
                 "Scene3D.RebaseOrigin method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"QueryAABB\""),
                 "Scene3D.QueryAABB method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"QuerySphere\""),
                 "Scene3D.QuerySphere method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"RaycastNodes\""),
                 "Scene3D.RaycastNodes method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"VisibleNodeCount\""),
                 "Scene3D.VisibleNodeCount property missing") &&
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
    ok = require(contains(runtime_def, "RT_METHOD(\"SetCastsShadows\""),
                 "Light3D.SetCastsShadows method missing") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasMetallicRoughnessMap\""),
                 "Material3D.HasMetallicRoughnessMap getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Material3D.get_HasAOMap\""),
                 "Material3D.HasAOMap getter must use Graphics3D PascalCase naming") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.SetClusteredLighting\""),
                 "Canvas3D.SetClusteredLighting must use Graphics3D PascalCase naming") &&
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
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.SetTextureUploadBudget\""),
                 "Canvas3D.SetTextureUploadBudget must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Canvas3D.get_TextureUploadPendingBytes\""),
                 "Canvas3D.TextureUploadPendingBytes getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.AddVisibilityZone\""),
                 "Scene3D.AddVisibilityZone must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.AddVisibilityPortal\""),
                 "Scene3D.AddVisibilityPortal must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.Scene3D.get_PvsCulledCount\""),
                 "Scene3D.PvsCulledCount getter must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode3D.SetAutoLOD\""),
                 "SceneNode3D.SetAutoLOD must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "\"Viper.Graphics3D.SceneNode3D.SetImpostor\""),
                 "SceneNode3D.SetImpostor must use Graphics3D PascalCase naming") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetClusteredLighting\""),
                 "Canvas3D.SetClusteredLighting method missing") &&
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
    ok = require(contains(runtime_def, "RT_METHOD(\"SetTextureUploadBudget\""),
                 "Canvas3D.SetTextureUploadBudget method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"TextureUploadPendingBytes\""),
                 "Canvas3D.TextureUploadPendingBytes property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"AddVisibilityZone\""),
                 "Scene3D.AddVisibilityZone method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"AddVisibilityPortal\""),
                 "Scene3D.AddVisibilityPortal method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_PROP(\"PvsCulledCount\""),
                 "Scene3D.PvsCulledCount property missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetAutoLOD\""),
                 "SceneNode3D.SetAutoLOD method missing") &&
         ok;
    ok = require(contains(runtime_def, "RT_METHOD(\"SetImpostor\""),
                 "SceneNode3D.SetImpostor method missing") &&
         ok;

    static const char *forbidden[] = {
        "Viper.Game3D.World3D.get_WorkerCount",
        "Viper.Game3D.World3D.get_JobsEnabled",
        "Viper.Game3D.World3D.SetWorkerCount",
        "Viper.Game3D.World3D.get_FloatingOrigin",
        "Viper.Game3D.World3D.set_FloatingOrigin",
        "Viper.Game3D.World3D.get_WorldOrigin",
        "Viper.Game3D.World3D.get_EntityCount",
        "Viper.Game3D.World3D.get_BodyCount",
        "Viper.Game3D.World3D.get_DrawCount",
        "Viper.Game3D.World3D.get_VisibleNodeCount",
        "Viper.Game3D.World3D.get_visible_node_count",
        "Viper.Game3D.World3D.get_OccludedDrawCount",
        "Viper.Game3D.World3D.get_occluded_draw_count",
        "Viper.Game3D.World3D.get_StreamResidentBytes",
        "Viper.Game3D.World3D.get_stream_resident_bytes",
        "Viper.Game3D.World3D.SetOriginRebaseThreshold",
        "Viper.Game3D.World3D.BakeNavMesh",
        "Viper.Game3D.World3D.bake_nav_mesh",
        "Viper.Game3D.World3D.BakeTiledNavMesh",
        "Viper.Game3D.World3D.bake_tiled_nav_mesh",
        "Viper.Game3D.Collision3DEvent.get_ContactCount",
        "Viper.Game3D.Collision3DEvent.ContactPoint",
        "Viper.Game3D.Collision3DEvent.ContactNormal",
        "Viper.Game3D.Collision3DEvent.ContactSeparation",
        "Viper.Game3D.Entity3D.fromNode",
        "Viper.Game3D.Entity3D.WrapNode",
        "Viper.Game3D.Animator3D.PlayLayerAdditive",
        "Viper.Game3D.Animator3D.play_layer_additive",
        "Viper.Game3D.Animator3D.CrossfadeLayerAdditive",
        "Viper.Game3D.Animator3D.crossfadeLayeradditive",
        "Viper.Game3D.Animator3D.crossfade_layer_additive",
        "Viper.Game3D.Animator3D.SetBlendTree",
        "Viper.Game3D.Animator3D.set_blend_tree",
        "Viper.Game3D.WorldStream3D.GetResidentTerrainTile",
        "Viper.Game3D.WorldStream3D.get_resident_terrain_tile",
        "Viper.Game3D.WorldStream3D.getresidentTerrainTile",
        "Viper.Game3D.WorldStream3D.GetCellCount",
        "Viper.Game3D.WorldStream3D.get_cellCount",
        "Viper.Game3D.WorldStream3D.get_cell_count",
        "Viper.Game3D.WorldStream3D.GetCellMaterial",
        "Viper.Game3D.WorldStream3D.get_cell_material",
        "Viper.Game3D.WorldStream3D.GetTerrainTileCount",
        "Viper.Game3D.WorldStream3D.get_terrainTileCount",
        "Viper.Game3D.WorldStream3D.get_terrain_tile_count",
        "Viper.Game3D.WorldStream3D.GetTerrainTileHeightmap",
        "Viper.Game3D.WorldStream3D.get_terrainTileHeightmap",
        "Viper.Game3D.WorldStream3D.get_terrain_tile_heightmap",
        "Viper.Game3D.WorldStream3D.GetTerrainTileMaterial",
        "Viper.Game3D.WorldStream3D.get_terrain_tile_material",
        "Viper.Graphics3D.Physics3DWorld.get_solverIterations",
        "Viper.Graphics3D.Physics3DWorld.get_lastSolverIslandCount",
        "Viper.Graphics3D.Physics3DWorld.get_last_solver_island_count",
        "Viper.Graphics3D.Physics3DWorld.get_lastSolverActiveBodyCount",
        "Viper.Graphics3D.Physics3DWorld.get_last_solver_active_body_count",
        "Viper.Graphics3D.Physics3DWorld.get_lastSolverContactCount",
        "Viper.Graphics3D.Physics3DWorld.get_last_solver_contact_count",
        "Viper.Graphics3D.Physics3DWorld.setSolverIterations",
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
        "Viper.Graphics3D.Scene3D.rebaseOrigin",
        "Viper.Graphics3D.Scene3D.Rebaseorigin",
        "Viper.Graphics3D.Scene3D.queryAABB",
        "Viper.Graphics3D.Scene3D.QueryAabb",
        "Viper.Graphics3D.Scene3D.Raycastnodes",
        "Viper.Graphics3D.Scene3D.get_visibleNodeCount",
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
        "Viper.Game3D.Animator3D.SetIKSolver",
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
        "Viper.Graphics3D.SceneNode3D.setAutoLOD",
        "Viper.Graphics3D.SceneNode3D.setImpostor",
        "Viper.Graphics3D.Canvas3D.SetShadowCascade\"",
        "Viper.Game3D.ModelHandle",
        "Viper.Graphics3D.SixDOFJoint3D",
        "Viper.Graphics3D.SixDoFJoint3D",
        "SetTextureAsset",
        "SetNormalMapAsset",
        "SelectScene",
    };
    for (const char *needle : forbidden)
        ok = require(!contains(runtime_def, needle), std::string("forbidden 3D API name: ") + needle) &&
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
