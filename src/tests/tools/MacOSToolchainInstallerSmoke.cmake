cmake_minimum_required(VERSION 3.20)

foreach (_required CMAKE_BIN VIPER_BIN VIPER_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to MacOSToolchainInstallerSmoke.cmake")
    endif ()
endforeach ()

if (NOT "$ENV{VIPER_RUN_MACOS_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping macOS installer smoke; set VIPER_RUN_MACOS_INSTALLER_SMOKE=1 to install into /usr/local")
    return()
endif ()

execute_process(
        COMMAND id -u
        RESULT_VARIABLE _id_rv
        OUTPUT_VARIABLE _uid
        ERROR_VARIABLE _id_err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT _id_rv EQUAL 0)
    message(FATAL_ERROR "cannot determine uid for macOS installer smoke\nstderr:\n${_id_err}")
endif ()
if (NOT _uid STREQUAL "0")
    message(STATUS "Skipping macOS installer smoke; run CTest as root to install into /usr/local")
    return()
endif ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/macos-toolchain-installer-smoke")
set(_pkg "${_tmp_root}/viper-toolchain.pkg")
set(_src_dir "${_tmp_root}/consumer-src")
set(_build_dir "${_tmp_root}/consumer-build")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}" "${_src_dir}")

set(_pkg_cmd
        "${VIPER_BIN}" install-package
        --build-dir "${VIPER_BUILD_DIR}"
        --target macos
        -o "${_pkg}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _pkg_cmd --config "${VIPER_CONFIG}")
endif ()

execute_process(
        COMMAND ${_pkg_cmd}
        RESULT_VARIABLE _pkg_rv
        OUTPUT_VARIABLE _pkg_out
        ERROR_VARIABLE _pkg_err)
if (NOT _pkg_rv EQUAL 0)
    message(FATAL_ERROR "macOS install-package smoke failed\nstdout:\n${_pkg_out}\nstderr:\n${_pkg_err}")
endif ()

execute_process(
        COMMAND installer -pkg "${_pkg}" -target /
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR "macOS installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

execute_process(
        COMMAND /usr/local/bin/viper --version
        RESULT_VARIABLE _version_rv
        OUTPUT_VARIABLE _version_out
        ERROR_VARIABLE _version_err)
if (NOT _version_rv EQUAL 0)
    message(FATAL_ERROR "installed viper --version failed\nstdout:\n${_version_out}\nstderr:\n${_version_err}")
endif ()

file(WRITE "${_src_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(viper_macos_installed_config_smoke LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Viper CONFIG REQUIRED)
add_executable(viper_macos_installed_config_smoke main.cpp)
target_link_libraries(viper_macos_installed_config_smoke PRIVATE viper::il_core viper::il_io)
]=])

file(WRITE "${_src_dir}/main.cpp" [=[
#include <sstream>
#include <viper/il/core/Module.hpp>
#include <viper/il/io/Serializer.hpp>

int main() {
    il::core::Module module;
    std::ostringstream out;
    il::io::Serializer::write(module, out);
    return out.str().empty() ? 1 : 0;
}
]=])

execute_process(
        COMMAND "${CMAKE_BIN}" -S "${_src_dir}" -B "${_build_dir}"
        RESULT_VARIABLE _cfg_rv
        OUTPUT_VARIABLE _cfg_out
        ERROR_VARIABLE _cfg_err)
if (NOT _cfg_rv EQUAL 0)
    message(FATAL_ERROR "installed /usr/local Viper config was not discoverable by CMake\nstdout:\n${_cfg_out}\nstderr:\n${_cfg_err}")
endif ()

set(_consumer_build_cmd "${CMAKE_BIN}" --build "${_build_dir}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _consumer_build_cmd --config "${VIPER_CONFIG}")
endif ()
execute_process(
        COMMAND ${_consumer_build_cmd}
        RESULT_VARIABLE _build_rv
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR "installed Viper consumer build failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()
