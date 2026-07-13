cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN VIPER_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to InstallPackageTarballSmoke.cmake")
    endif ()
endforeach ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/install-package-tarball-smoke")
set(_artifact "${_tmp_root}/viper-toolchain.tar.gz")
set(_release_artifact "${_tmp_root}/viper-toolchain-release.tar.gz")
set(_extensionless_artifact "${_tmp_root}/portable-toolchain")
set(_stage "${_tmp_root}/stage")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}")

# The installed-config smoke separately exercises the repository's full
# cmake --install tree. This test targets tarball creation, verification,
# checksums, output naming, and release collision behavior, so use a complete
# minimal manifest fixture instead of compressing the multi-gigabyte Debug
# install tree three times.
file(MAKE_DIRECTORY
        "${_stage}/bin"
        "${_stage}/lib/cmake/Viper"
        "${_stage}/lib")

if (WIN32)
    set(_tool_suffix ".exe")
    set(_archive_prefix "")
    set(_archive_suffix ".lib")
else ()
    set(_tool_suffix "")
    set(_archive_prefix "lib")
    set(_archive_suffix ".a")
endif ()

# A host-native executable is required only for staged platform/architecture
# detection. The real Viper binary still drives every command in this test.
configure_file("${CMAKE_COMMAND}" "${_stage}/bin/viper${_tool_suffix}" COPYONLY)
foreach (_tool IN ITEMS
        zia
        vbasic
        ilrun
        il-verify
        il-dis
        zia-server
        vbasic-server
        basic-ast-dump
        basic-lex-dump
        viperide)
    file(WRITE "${_stage}/bin/${_tool}${_tool_suffix}" "fixture\n")
endforeach ()
if (NOT WIN32)
    file(CHMOD "${_stage}/bin/viper${_tool_suffix}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE)
    foreach (_tool IN ITEMS
            zia
            vbasic
            ilrun
            il-verify
            il-dis
            zia-server
            vbasic-server
            basic-ast-dump
            basic-lex-dump
            viperide)
        file(CHMOD "${_stage}/bin/${_tool}${_tool_suffix}"
                PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                            WORLD_READ WORLD_EXECUTE)
    endforeach ()
endif ()

file(WRITE "${_stage}/lib/cmake/Viper/ViperConfig.cmake" "# tarball smoke fixture\n")
file(WRITE "${_stage}/lib/cmake/Viper/ViperTargets.cmake" "# tarball smoke fixture\n")
file(WRITE "${_stage}/lib/cmake/Viper/ViperConfigVersion.cmake"
        "set(PACKAGE_VERSION \"1.2.3\")\n")

set(_runtime_manifest
        "${VIPER_BUILD_DIR}/generated/viper/runtime/RuntimeComponentManifest.hpp")
if (NOT EXISTS "${_runtime_manifest}")
    message(FATAL_ERROR "generated runtime component manifest is missing: ${_runtime_manifest}")
endif ()
file(STRINGS "${_runtime_manifest}" _runtime_archive_lines
        REGEX "\"viper_rt_[A-Za-z0-9_]+\"")
foreach (_archive_line IN LISTS _runtime_archive_lines)
    string(REGEX MATCH "viper_rt_[A-Za-z0-9_]+" _archive "${_archive_line}")
    if (NOT "${_archive}" STREQUAL "")
        file(WRITE "${_stage}/lib/${_archive_prefix}${_archive}${_archive_suffix}" "fixture\n")
    endif ()
endforeach ()

set(_install_package_cmd
        "${VIPER_BIN}" install-package
        --stage-dir "${_stage}"
        --target tarball
        --output-file "${_artifact}")

execute_process(
        COMMAND ${_install_package_cmd}
        RESULT_VARIABLE _build_rv
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err
)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package tarball smoke failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_artifact}")
    message(FATAL_ERROR "expected artifact was not created: ${_artifact}")
endif ()
if (NOT EXISTS "${_artifact}.sha256" OR NOT EXISTS "${_artifact}.manifest.json")
    message(FATAL_ERROR "single-artifact output did not create checksum and JSON inventory")
endif ()
file(READ "${_artifact}.manifest.json" _inventory)
foreach (_inventory_field IN ITEMS
        "\"schema_version\": 1"
        "\"source_date_epoch\": null"
        "\"format\": \"tar-gzip\""
        "\"verified\": true"
        "\"sha256\""
        "\"size\""
        "\"trust\": \"checksum-only\"")
    if (NOT _inventory MATCHES "${_inventory_field}")
        message(FATAL_ERROR "artifact inventory is missing ${_inventory_field}\n${_inventory}")
    endif ()
endforeach ()

execute_process(
        COMMAND "${VIPER_BIN}" install-package --verify-only "${_artifact}" --require-checksum
        RESULT_VARIABLE _verify_rv
        OUTPUT_VARIABLE _verify_out
        ERROR_VARIABLE _verify_err
)
if (NOT _verify_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package verify-only failed\nstdout:\n${_verify_out}\nstderr:\n${_verify_err}")
endif ()
file(READ "${_artifact}.sha256" _valid_sidecar)
file(WRITE "${_artifact}.sha256"
        "0000000000000000000000000000000000000000000000000000000000000000  viper-toolchain.tar.gz\n")
execute_process(
        COMMAND "${VIPER_BIN}" install-package --verify-only "${_artifact}" --require-checksum
        RESULT_VARIABLE _bad_checksum_rv
        OUTPUT_VARIABLE _bad_checksum_out
        ERROR_VARIABLE _bad_checksum_err)
if (_bad_checksum_rv EQUAL 0 OR NOT _bad_checksum_err MATCHES "SHA-256 mismatch")
    message(FATAL_ERROR
            "tampered checksum sidecar was not rejected\nstdout:\n${_bad_checksum_out}\nstderr:\n${_bad_checksum_err}")
endif ()
file(WRITE "${_artifact}.sha256" "${_valid_sidecar}")

set(_extensionless_cmd
        "${VIPER_BIN}" install-package
        --stage-dir "${_stage}"
        --target tarball
        -o "${_extensionless_artifact}")
execute_process(
        COMMAND ${_extensionless_cmd}
        RESULT_VARIABLE _extensionless_rv
        OUTPUT_VARIABLE _extensionless_out
        ERROR_VARIABLE _extensionless_err)
if (NOT _extensionless_rv EQUAL 0 OR NOT EXISTS "${_extensionless_artifact}" OR IS_DIRECTORY "${_extensionless_artifact}")
    message(FATAL_ERROR
            "single-target -o extensionless output was not treated as a file\nstdout:\n${_extensionless_out}\nstderr:\n${_extensionless_err}")
endif ()

set(_release_cmd
        "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=1700000000
        "${VIPER_BIN}" install-package
        --stage-dir "${_stage}"
        --target tarball
        --output-file "${_release_artifact}"
        --release)
execute_process(
        COMMAND ${_release_cmd}
        RESULT_VARIABLE _release_rv
        OUTPUT_VARIABLE _release_out
        ERROR_VARIABLE _release_err)
if (NOT _release_rv EQUAL 0)
    message(FATAL_ERROR
            "release tarball generation failed\nstdout:\n${_release_out}\nstderr:\n${_release_err}")
endif ()
if (EXISTS "${_tmp_root}/.viper-release.lock")
    message(FATAL_ERROR "successful release generation left its output lock")
endif ()
file(READ "${_release_artifact}.manifest.json" _release_inventory)
if (NOT _release_inventory MATCHES "\"source_date_epoch\": \"1700000000\"")
    message(FATAL_ERROR "release inventory did not record SOURCE_DATE_EPOCH\n${_release_inventory}")
endif ()
execute_process(
        COMMAND ${_release_cmd}
        RESULT_VARIABLE _collision_rv
        OUTPUT_VARIABLE _collision_out
        ERROR_VARIABLE _collision_err)
if (_collision_rv EQUAL 0 OR NOT _collision_err MATCHES "refuses to overwrite")
    message(FATAL_ERROR
            "release output collision was not rejected\nstdout:\n${_collision_out}\nstderr:\n${_collision_err}")
endif ()

file(REMOVE_RECURSE "${_tmp_root}")
