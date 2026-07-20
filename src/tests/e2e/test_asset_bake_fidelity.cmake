#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/e2e/test_asset_bake_fidelity.cmake
# Purpose: Verify the machine-readable and human-readable `zanna asset bake`
#   fidelity diagnostics against a full VSCN v4 SceneAsset round trip.
#
# Key invariants:
#   - JSON mode emits the stable v1 report schema on stdout.
#   - Default mode warns on stderr for every detected dropped resource class.
#
# Ownership/Lifetime: CTest owns the generated VSCN files in the build tree.
#
# Links: src/tools/zanna/cmd_asset.cpp, docs/tools/cli.md
#
#===----------------------------------------------------------------------===#

if (NOT DEFINED ZANNA_EXE OR NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "ZANNA_EXE, INPUT, and OUTPUT are required")
endif ()

execute_process(
        COMMAND "${ZANNA_EXE}" asset bake "${INPUT}" "${OUTPUT}" --json
        RESULT_VARIABLE bake_result
        OUTPUT_VARIABLE bake_stdout
        ERROR_VARIABLE bake_stderr)
if (NOT bake_result EQUAL 0)
    message(FATAL_ERROR "JSON bake failed (${bake_result}): ${bake_stderr}")
endif ()

string(JSON schema ERROR_VARIABLE json_error GET "${bake_stdout}" schema)
if (json_error OR NOT schema STREQUAL "zanna.asset-bake-report/v1")
    message(FATAL_ERROR "invalid bake report schema: ${json_error}; ${bake_stdout}")
endif ()
string(JSON status GET "${bake_stdout}" status)
string(JSON lossy GET "${bake_stdout}" lossy)
string(JSON source_scenes GET "${bake_stdout}" source scenes)
string(JSON baked_scenes GET "${bake_stdout}" baked scenes)
string(JSON source_cameras GET "${bake_stdout}" source cameras)
string(JSON source_node_animations GET "${bake_stdout}" source nodeAnimations)
string(JSON source_morph_targets GET "${bake_stdout}" source morphTargets)
string(JSON baked_morph_targets GET "${bake_stdout}" baked morphTargets)
string(JSON source_morph_shapes GET "${bake_stdout}" source morphShapes)
string(JSON baked_morph_shapes GET "${bake_stdout}" baked morphShapes)
string(JSON source_variants GET "${bake_stdout}" source variants)
string(JSON loss_count LENGTH "${bake_stdout}" losses)
if (NOT status STREQUAL "ok" OR lossy OR
    NOT source_scenes EQUAL 2 OR NOT baked_scenes EQUAL 2 OR
    NOT source_cameras EQUAL 1 OR NOT source_node_animations EQUAL 1 OR
    source_morph_targets LESS_EQUAL 0 OR
    NOT source_morph_targets EQUAL baked_morph_targets OR
    source_morph_shapes LESS_EQUAL 0 OR
    NOT source_morph_shapes EQUAL baked_morph_shapes OR
    NOT source_variants EQUAL 2 OR NOT loss_count EQUAL 0)
    message(FATAL_ERROR "unexpected bake fidelity report: ${bake_stdout}")
endif ()
if (bake_stderr)
    message(FATAL_ERROR "JSON bake wrote unexpected stderr: ${bake_stderr}")
endif ()

execute_process(
        COMMAND "${ZANNA_EXE}" asset bake "${INPUT}" "${OUTPUT}.human.vscn"
        RESULT_VARIABLE human_result
        OUTPUT_VARIABLE human_stdout
        ERROR_VARIABLE human_stderr)
if (NOT human_result EQUAL 0 OR NOT human_stdout MATCHES "baked ")
    message(FATAL_ERROR "human bake failed (${human_result}): ${human_stdout}; ${human_stderr}")
endif ()
if (human_stderr)
    message(FATAL_ERROR "lossless human bake wrote unexpected warnings: ${human_stderr}")
endif ()
