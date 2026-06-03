# Graphics3D Runtime Refactor Report

Date: 2026-06-02

Baseline commit before this pass: `7852cb1e2` (`Stabilize 3D runtime next-level work`).

Scope: C, C++, Objective-C, header, and included C implementation files under
`src/runtime/graphics/3d/`.

## Summary

This pass focused on feasible, low-risk refactors inside the Graphics3D/Game3D runtime:

- Completed 45 helper/file-split refactors.
- Kept all new helper APIs `static` and inside the existing translation units.
- Added one source-file split: `rt_scene3d_vscn_material_parse.inc`, included by
  `rt_scene3d_vscn.c`, avoiding CMake/linkage churn.
- Reduced these top functions enough that they dropped out of the current top-50 list:
  `gltf_load_materials`, `vscn_parse_material`, and `canvas3d_build_shadow_light_vp`.
- Reduced `gltf_load_meshes` from 389 lines to 152 lines.

## Current Top 50 Largest Files

Measured with `wc -l` after the refactors.

| Rank | Lines | File |
|---:|---:|---|
| 1 | 10839 | `src/runtime/graphics/3d/assets/rt_gltf.c` |
| 2 | 7296 | `src/runtime/graphics/3d/rt_game3d.c` |
| 3 | 6742 | `src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c` |
| 4 | 5888 | `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c` |
| 5 | 5343 | `src/runtime/graphics/3d/render/rt_canvas3d.c` |
| 6 | 5120 | `src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m` |
| 7 | 4734 | `src/runtime/graphics/3d/assets/rt_fbx_loader.c` |
| 8 | 3778 | `src/runtime/graphics/3d/render/rt_mesh3d.c` |
| 9 | 3650 | `src/runtime/graphics/3d/physics/rt_physics3d_collision.c` |
| 10 | 3534 | `src/runtime/graphics/3d/nav/rt_navmesh3d.c` |
| 11 | 3491 | `src/runtime/graphics/3d/physics/rt_physics3d.c` |
| 12 | 3480 | `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c` |
| 13 | 3243 | `src/runtime/graphics/3d/scene/rt_scene3d.c` |
| 14 | 2583 | `src/runtime/graphics/3d/anim/rt_skeleton3d.c` |
| 15 | 2511 | `src/runtime/graphics/3d/anim/rt_animcontroller3d.c` |
| 16 | 2458 | `src/runtime/graphics/3d/render/rt_model3d.c` |
| 17 | 2387 | `src/runtime/graphics/3d/scene/rt_scene3d_vscn.c` |
| 18 | 1918 | `src/runtime/graphics/3d/assets/rt_textureasset3d.c` |
| 19 | 1816 | `src/runtime/graphics/3d/physics/rt_physics3d_query.c` |
| 20 | 1728 | `src/runtime/graphics/3d/world/rt_terrain3d.c` |
| 21 | 1697 | `src/runtime/graphics/3d/physics/rt_joints3d.c` |
| 22 | 1504 | `src/runtime/graphics/3d/render/rt_camera3d.c` |
| 23 | 1447 | `src/runtime/graphics/3d/render/rt_postfx3d.c` |
| 24 | 1336 | `src/runtime/graphics/3d/world/rt_particles3d.c` |
| 25 | 1335 | `src/runtime/graphics/3d/nav/rt_navagent3d.c` |
| 26 | 1269 | `src/runtime/graphics/3d/scene/rt_raycast3d.c` |
| 27 | 1147 | `src/runtime/graphics/3d/physics/rt_collider3d.c` |
| 28 | 1133 | `src/runtime/graphics/3d/rt_game3d.h` |
| 29 | 1111 | `src/runtime/graphics/3d/audio/rt_sound3d_objects.c` |
| 30 | 1092 | `src/runtime/graphics/3d/anim/rt_morphtarget3d.c` |
| 31 | 1069 | `src/runtime/graphics/3d/render/rt_material3d.c` |
| 32 | 1046 | `src/runtime/graphics/3d/rt_game3d_controllers.c` |
| 33 | 1018 | `src/runtime/graphics/3d/scene/rt_scene3d_nodeanim.c` |
| 34 | 981 | `src/runtime/graphics/3d/render/rt_canvas3d_internal.h` |
| 35 | 965 | `src/runtime/graphics/3d/backend/vgfx3d_backend_utils.c` |
| 36 | 944 | `src/runtime/graphics/3d/anim/rt_iksolver3d.c` |
| 37 | 864 | `src/runtime/graphics/3d/physics/rt_physics3d_character.c` |
| 38 | 863 | `src/runtime/graphics/3d/physics/rt_physics3d_solver.c` |
| 39 | 829 | `src/runtime/graphics/3d/scene/rt_scene3d_spatial.c` |
| 40 | 826 | `src/runtime/graphics/3d/scene/rt_scene3d_node.c` |
| 41 | 807 | `src/runtime/graphics/3d/rt_game3d_entity.c` |
| 42 | 804 | `src/runtime/graphics/3d/rt_game3d_internal.h` |
| 43 | 799 | `src/runtime/graphics/3d/world/rt_vegetation3d.c` |
| 44 | 786 | `src/runtime/graphics/3d/render/rt_canvas3d_overlay.c` |
| 45 | 773 | `src/runtime/graphics/3d/render/rt_cubemap3d.c` |
| 46 | 675 | `src/runtime/graphics/3d/world/rt_water3d.c` |
| 47 | 639 | `src/runtime/graphics/3d/audio/rt_sound3d.c` |
| 48 | 618 | `src/runtime/graphics/3d/scene/rt_transform3d.c` |
| 49 | 605 | `src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_shared.c` |
| 50 | 596 | `src/runtime/graphics/3d/render/rt_instbatch3d.c` |

## Current Top 50 Largest Functions

Measured with a brace-aware C/ObjC function scanner after the refactors.

| Rank | Lines | Function |
|---:|---:|---|
| 1 | 389 | `rt_gltf_load_impl` in `assets/rt_gltf.c` |
| 2 | 359 | `gltf_build_node_hierarchy` in `assets/rt_gltf.c` |
| 3 | 337 | `metal_create_ctx` in `backend/vgfx3d_backend_metal.m` |
| 4 | 303 | `metal_submit_draw_instanced` in `backend/vgfx3d_backend_metal.m` |
| 5 | 299 | `metal_submit_draw` in `backend/vgfx3d_backend_metal.m` |
| 6 | 276 | `fbx_build_scene_root` in `assets/rt_fbx_loader.c` |
| 7 | 254 | `fbx_extract_skeleton` in `assets/rt_fbx_loader.c` |
| 8 | 247 | `test_collider_pair` in `physics/rt_physics3d_collision.c` |
| 9 | 237 | `rt_fbx_load` in `assets/rt_fbx_loader.c` |
| 10 | 236 | `gltf_parse_node_animations` in `assets/rt_gltf.c` |
| 11 | 208 | `build_chunk` in `world/rt_terrain3d.c` |
| 12 | 205 | `gltf_load_images_and_textures` in `assets/rt_gltf.c` |
| 13 | 198 | `gltf_parse_animations` in `assets/rt_gltf.c` |
| 14 | 194 | `vscn_parse_node` in `scene/rt_scene3d_vscn.c` |
| 15 | 192 | `navmesh3d_voxel_bake` in `nav/rt_navmesh3d.c` |
| 16 | 189 | `rt_anim_controller3d_update` in `anim/rt_animcontroller3d.c` |
| 17 | 186 | `morphtarget_draw_mesh_matrix` in `anim/rt_morphtarget3d.c` |
| 18 | 185 | `fbx_extract_model_trs` in `assets/rt_fbx_loader.c` |
| 19 | 183 | `rt_canvas3d_draw_mesh_matrix_keyed` in `render/rt_canvas3d.c` |
| 20 | 180 | `rt_particles3d_draw` in `world/rt_particles3d.c` |
| 21 | 180 | `model3d_load_from_gltf` in `render/rt_model3d.c` |
| 22 | 179 | `vscn_parse_mesh` in `scene/rt_scene3d_vscn.c` |
| 23 | 176 | `test_meshlike_sphere` in `physics/rt_physics3d_collision.c` |
| 24 | 174 | `gltf_parse_skins` in `assets/rt_gltf.c` |
| 25 | 173 | `gltf_preload_resolve_primitive_attribs` in `assets/rt_gltf.c` |
| 26 | 173 | `fbx_extract_geometry` in `assets/rt_fbx_loader.c` |
| 27 | 171 | `vscn_serialize_node` in `scene/rt_scene3d_vscn.c` |
| 28 | 169 | `d3d11_prepare_anim_resources` in `backend/vgfx3d_backend_d3d11.c` |
| 29 | 169 | `gltf_preload_stage_images` in `assets/rt_gltf.c` |
| 30 | 168 | `compute_lighting` in `backend/vgfx3d_backend_sw.c` |
| 31 | 162 | `contact3d_expand_obb_manifold` in `physics/rt_physics3d_collision.c` |
| 32 | 159 | `gltf_preload_stage_morph_targets` in `assets/rt_gltf.c` |
| 33 | 157 | `fbx_load_extract_morphs` in `assets/rt_fbx_loader.c` |
| 34 | 156 | `textureasset3d_parse_ktx2` in `assets/rt_textureasset3d.c` |
| 35 | 155 | `rt_scene3d_load` in `scene/rt_scene3d_vscn.c` |
| 36 | 155 | `rt_canvas3d_draw_sprite3d` in `render/rt_sprite3d.c` |
| 37 | 155 | `rt_animation3d_add_keyframe` in `anim/rt_skeleton3d.c` |
| 38 | 154 | `sw_shade_lighting_pbr` in `backend/vgfx3d_backend_sw.c` |
| 39 | 154 | `gltf_get_accessor_view` in `assets/rt_gltf.c` |
| 40 | 152 | `gltf_load_meshes` in `assets/rt_gltf.c` |
| 41 | 150 | `rt_canvas3d_draw_text_3d` in `render/rt_canvas3d.c` |
| 42 | 149 | `rt_water3d_update` in `world/rt_water3d.c` |
| 43 | 149 | `navmesh3d_import_v2_after_magic` in `nav/rt_navmesh3d.c` |
| 44 | 146 | `sw_apply_environment_reflection` in `backend/vgfx3d_backend_sw.c` |
| 45 | 145 | `collider3d_recompute_bounds` in `physics/rt_collider3d.c` |
| 46 | 144 | `gl_begin_frame` in `backend/vgfx3d_backend_opengl.c` |
| 47 | 140 | `rt_mesh3d_transform` in `render/rt_mesh3d.c` |
| 48 | 138 | `rt_canvas3d_draw_terrain_at` in `world/rt_terrain3d.c` |
| 49 | 137 | `rt_canvas3d_draw_instanced` in `render/rt_instbatch3d.c` |
| 50 | 136 | `rt_vegetation3d_populate` in `world/rt_vegetation3d.c` |

## Top 75 Refactor Candidates

Status legend: Done = completed in this pass. Remaining = still a candidate.

| # | Status | Candidate |
|---:|---|---|
| 1 | Done | `gltf_material_slot_info`: isolate material texture-slot lookup. |
| 2 | Done | `gltf_material_bind_texture`: centralize glTF texture ref validation and sampler/UV binding. |
| 3 | Done | `gltf_material_read_pbr_base_color`: isolate PBR base-color factor parsing. |
| 4 | Done | `gltf_material_create_pbr_from_json`: isolate PBR material construction. |
| 5 | Done | `gltf_material_create_default_pbr`: centralize default material creation. |
| 6 | Done | `gltf_material_apply_pbr_textures`: isolate base-color and metallic-roughness map binding. |
| 7 | Done | `gltf_material_apply_emissive_factor`: isolate emissive factor parsing. |
| 8 | Done | `gltf_material_apply_specular_extension`: isolate `KHR_materials_specular`. |
| 9 | Done | `gltf_material_apply_clearcoat_extension`: isolate `KHR_materials_clearcoat`. |
| 10 | Done | `gltf_material_apply_transmission_extension`: isolate `KHR_materials_transmission`. |
| 11 | Done | `gltf_material_apply_extensions`: dispatch KHR material extensions. |
| 12 | Done | `gltf_material_apply_standard_textures`: isolate normal/AO/emissive texture refs. |
| 13 | Done | `gltf_material_apply_alpha_and_sided`: isolate alpha mode and double-sided flags. |
| 14 | Done | `gltf_mesh_resolve_primitive_material`: isolate primitive material/default fallback logic. |
| 15 | Done | `gltf_mesh_take_preloaded_primitive`: isolate preloaded decoded-mesh adoption. |
| 16 | Done | `gltf_mesh_validate_skin_views`: isolate JOINTS/WEIGHTS view validation. |
| 17 | Done | `gltf_mesh_read_primitive_views`: isolate primitive accessor view decoding and validation. |
| 18 | Done | `gltf_mesh_import_vertex_t`: name the transient vertex import record. |
| 19 | Done | `gltf_mesh_import_vertex_init`: isolate vertex default initialization. |
| 20 | Done | `gltf_mesh_import_vertex_is_finite`: isolate per-vertex finite validation. |
| 21 | Done | `gltf_mesh_merge_extra_joint_influences`: isolate JOINTS_1 merge and top-four selection. |
| 22 | Done | `gltf_mesh_clip_joint_influences`: isolate out-of-range joint clipping. |
| 23 | Done | `gltf_mesh_read_import_vertex`: isolate one-vertex attribute decode/sanitize path. |
| 24 | Done | `gltf_mesh_apply_vertex_skin`: isolate bone-weight assignment and bone-count tracking. |
| 25 | Done | `gltf_mesh_import_vertices`: isolate primitive vertex loop. |
| 26 | Done | `gltf_mesh_finalize_imported_primitive`: isolate index append, generated normals/tangents, morph import. |
| 27 | Done | `gltf_load_meshes`: reduce main primitive loop from monolithic decode to orchestration. |
| 28 | Done | `vscn_parse_material_color4`: isolate RGBA material array parsing. |
| 29 | Done | `vscn_parse_material_color3`: isolate RGB material array parsing. |
| 30 | Done | `vscn_parse_material_scalars`: isolate scalar PBR/alpha/shading fields. |
| 31 | Done | `vscn_parse_material_custom_params`: isolate custom param restore. |
| 32 | Done | `vscn_parse_material_texture_slots`: isolate per-slot sampler/UV transform parsing. |
| 33 | Done | `vscn_bind_material_texture_ref`: centralize texture index-ref validation. |
| 34 | Done | `vscn_bind_material_cubemap_ref`: centralize cubemap index-ref validation. |
| 35 | Done | `vscn_bind_material_refs`: isolate all material texture/cubemap binding. |
| 36 | Done | `rt_scene3d_vscn_material_parse.inc`: split VSCN material restore helpers out of `rt_scene3d_vscn.c`. |
| 37 | Done | `canvas3d_shadow_bounds_are_valid`: isolate shadow bounds validation. |
| 38 | Done | `canvas3d_shadow_bounds_center`: isolate shadow AABB center calculation. |
| 39 | Done | `canvas3d_shadow_normalize_light_dir`: isolate directional-light normalization. |
| 40 | Done | `canvas3d_shadow_place_eye`: isolate light eye placement. |
| 41 | Done | `canvas3d_shadow_build_light_view`: isolate light-view matrix construction. |
| 42 | Done | `canvas3d_shadow_build_world_corners`: isolate AABB corner generation. |
| 43 | Done | `canvas3d_shadow_accumulate_light_bounds`: isolate light-space bounds accumulation. |
| 44 | Done | `canvas3d_shadow_build_ortho_projection`: isolate shadow ortho projection. |
| 45 | Done | `canvas3d_build_shadow_light_vp`: reduce from all-in-one math routine to orchestration. |
| 46 | Remaining | `rt_gltf_load_impl`: split load phase orchestration into an included glTF pipeline slice. |
| 47 | Remaining | `gltf_build_node_hierarchy`: extract node creation, TRS, mesh/material attachment, skin attachment, scene roots. |
| 48 | Remaining | `metal_create_ctx`: move post-FX and skybox resource setup into helpers or `.inc`. |
| 49 | Remaining | `metal_submit_draw_instanced`: extract resource binding and draw encoder setup. |
| 50 | Remaining | `metal_submit_draw`: share non-instanced/instanced Metal draw setup. |
| 51 | Remaining | `fbx_build_scene_root`: split model-node creation, material lookup, skeleton attachment. |
| 52 | Remaining | `fbx_extract_skeleton`: split joint table parsing and inverse bind matrix import. |
| 53 | Remaining | `test_collider_pair`: split type-pair dispatch and contact generation. |
| 54 | Remaining | `rt_fbx_load`: split parse, asset extraction, scene build, cleanup. |
| 55 | Remaining | `gltf_parse_node_animations`: split sampler/target/channel phases. |
| 56 | Remaining | `build_chunk`: split terrain extent, vertex grid, triangles, skirts, AABB output. |
| 57 | Remaining | `gltf_load_images_and_textures`: split image decoding, sampler table, supported-format tagging. |
| 58 | Remaining | `gltf_parse_animations`: split channel validation and curve storage. |
| 59 | Remaining | `vscn_parse_node`: split transform, mesh/material refs, LODs, children. |
| 60 | Remaining | `navmesh3d_voxel_bake`: split rasterization, walkability, adjacency, compact output. |
| 61 | Remaining | `rt_anim_controller3d_update`: split state update, transitions, blends, event emission. |
| 62 | Remaining | `morphtarget_draw_mesh_matrix`: split morph resource resolution and draw submission. |
| 63 | Remaining | `fbx_extract_model_trs`: split translation/rotation/scale curve extraction. |
| 64 | Remaining | `rt_canvas3d_draw_mesh_matrix_keyed`: split validation, queueing, skinning/morph key handling. |
| 65 | Remaining | `rt_particles3d_draw`: split simulation, billboard generation, draw queueing. |
| 66 | Remaining | `model3d_load_from_gltf`: split asset load, scene import, animation setup. |
| 67 | Remaining | `vscn_parse_mesh`: split base64 decode, legacy vertex conversion, validation, mesh install. |
| 68 | Remaining | `test_meshlike_sphere`: split broadphase, triangle probe, contact reduction. |
| 69 | Remaining | `gltf_parse_skins`: split joint mapping and inverse-bind parsing. |
| 70 | Remaining | `gltf_preload_resolve_primitive_attribs`: split accessor lookup and validation. |
| 71 | Remaining | `fbx_extract_geometry`: split positions, normals, UVs, indices, material slots. |
| 72 | Remaining | `vscn_serialize_node`: split transform, refs, LODs, children serialization. |
| 73 | Remaining | `d3d11_prepare_anim_resources`: split palette/morph buffer setup. |
| 74 | Remaining | `gltf_preload_stage_images`: split budgeted image decode and KTX2 staging. |
| 75 | Remaining | `compute_lighting`: split diffuse/specular/shadow/attenuation in software backend. |

## What Was Missed Or Deferred

- `rt_gltf.c` remains the largest file. The material and mesh loader functions are smaller, but
  the file still needs a larger split into glTF parse/preload/material/mesh/node/animation include
  slices or real translation units.
- The backend files remain very large. The safest next split is to move Metal post-FX/skybox setup
  and shared draw encoder setup out of `metal_create_ctx`, `metal_submit_draw`, and
  `metal_submit_draw_instanced`.
- `rt_game3d.c` is still the second-largest file. This pass did not modify it because the largest
  current functions are more concentrated in asset import/backends/rendering.
- Physics collision dispatch remains a high-value target, but it needs careful contact regression
  tests before deep splitting.
- `build_chunk` is still a good terrain target. It can be split safely, but this pass prioritized
  functions that already had direct focused tests in the active build.

## Verification

- `cmake --build build --target test_rt_gltf -j8` passed.
- `ctest --test-dir build -R '^test_rt_gltf$' --output-on-failure` passed.
- `cmake --build build --target test_rt_scene3d -j8` passed.
- `ctest --test-dir build -R '^(test_rt_gltf|test_rt_scene3d)$' --output-on-failure` passed.
- `cmake --build build --target test_rt_canvas3d -j8` passed.
- `ctest --test-dir build -R '^test_rt_canvas3d$' --output-on-failure` passed.
- `cmake --build build --target test_rt_scene3d test_rt_canvas3d test_rt_gltf -j8` passed.
- `git diff --check` passed.
- `ctest --test-dir build -L graphics3d --output-on-failure` passed: 79/79 tests.
