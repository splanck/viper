cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN VIPER_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to LinuxToolchainAppImageSmoke.cmake")
    endif ()
endforeach ()

if (NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Skipping Linux AppImage smoke; host is not Linux")
    return()
endif ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/linux-toolchain-appimage-smoke")
set(_artifact "${_tmp_root}/Viper-toolchain.AppImage")
set(_stage_dir "${_tmp_root}/stage")
file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY
        "${_tmp_root}"
        "${_stage_dir}/bin"
        "${_stage_dir}/include/viper"
        "${_stage_dir}/lib"
        "${_stage_dir}/lib/cmake/Viper"
        "${_stage_dir}/share/man/man1"
        "${_stage_dir}/share/doc/viper")
file(COPY_FILE "${VIPER_BIN}" "${_stage_dir}/bin/viper")
file(CHMOD "${_stage_dir}/bin/viper"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(WRITE "${_stage_dir}/include/viper/version.hpp" "#define VIPER_VERSION_STR \"0.0.0\"\n")
file(WRITE "${_stage_dir}/lib/cmake/Viper/ViperConfig.cmake" "# AppImage smoke config\n")
file(WRITE "${_stage_dir}/lib/cmake/Viper/ViperTargets.cmake" "# AppImage smoke targets\n")
file(WRITE "${_stage_dir}/lib/cmake/Viper/ViperConfigVersion.cmake" "set(PACKAGE_VERSION \"0.0.0\")\n")
file(GLOB _runtime_archives
        "${VIPER_BUILD_DIR}/src/runtime/*.a"
        "${VIPER_BUILD_DIR}/src/*.a"
        "${VIPER_BUILD_DIR}/src/lib/*.a")
foreach (_archive IN LISTS _runtime_archives)
    get_filename_component(_archive_name "${_archive}" NAME)
    file(COPY_FILE "${_archive}" "${_stage_dir}/lib/${_archive_name}")
endforeach ()
file(WRITE "${_stage_dir}/share/man/man1/viper.1" ".TH viper 1\n")
file(WRITE "${_stage_dir}/share/doc/viper/README.md" "Viper AppImage smoke stage\n")

set(_install_package_cmd
        "${VIPER_BIN}" install-package
        --stage-dir "${_stage_dir}"
        --target appimage
        --no-verify
        -o "${_artifact}")

execute_process(
        COMMAND ${_install_package_cmd}
        RESULT_VARIABLE _build_rv
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err
)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package --target appimage failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_artifact}")
    message(FATAL_ERROR "expected AppImage artifact was not created: ${_artifact}")
endif ()

execute_process(
        COMMAND "${VIPER_BIN}" install-package --verify-only "${_artifact}"
        RESULT_VARIABLE _verify_rv
        OUTPUT_VARIABLE _verify_out
        ERROR_VARIABLE _verify_err
)
if (NOT _verify_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package --verify-only failed for AppImage\nstdout:\n${_verify_out}\nstderr:\n${_verify_err}")
endif ()

execute_process(
        COMMAND chmod +x "${_artifact}"
        RESULT_VARIABLE _chmod_rv
        OUTPUT_VARIABLE _chmod_out
        ERROR_VARIABLE _chmod_err
)
if (NOT _chmod_rv EQUAL 0)
    message(FATAL_ERROR "chmod +x failed for AppImage\nstdout:\n${_chmod_out}\nstderr:\n${_chmod_err}")
endif ()

execute_process(
        COMMAND "${_artifact}" --version
        RESULT_VARIABLE _run_rv
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err
)
if (NOT _run_rv EQUAL 0)
    message(FATAL_ERROR "AppImage --version failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()
