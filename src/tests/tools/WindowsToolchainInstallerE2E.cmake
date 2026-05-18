cmake_minimum_required(VERSION 3.20)

foreach (_required CMAKE_BIN VIPER_BUILD_DIR VIPER_BIN)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsToolchainInstallerE2E.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(FATAL_ERROR "WindowsToolchainInstallerE2E.cmake must run on Windows")
endif ()

if (NOT DEFINED ENV{LOCALAPPDATA} OR "$ENV{LOCALAPPDATA}" STREQUAL "")
    message(FATAL_ERROR "LOCALAPPDATA is not set; cannot locate per-user install root")
endif ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/windows-toolchain-installer-e2e")
set(_stage_dir "${_tmp_root}/stage")
set(_installer "${_tmp_root}/viper-toolchain-e2e.exe")
set(_install_root "$ENV{LOCALAPPDATA}/Viper")
set(_installed_viper "${_install_root}/bin/viper.exe")
set(_uninstaller "${_install_root}/uninstall.exe")
set(_src_dir "${_tmp_root}/consumer-src")
set(_build_dir "${_tmp_root}/consumer-build")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}" "${_src_dir}")

set(_install_cmd "${CMAKE_BIN}" --install "${VIPER_BUILD_DIR}" --prefix "${_stage_dir}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _install_cmd --config "${VIPER_CONFIG}")
endif ()
execute_process(
        COMMAND ${_install_cmd}
        RESULT_VARIABLE _stage_rv
        OUTPUT_VARIABLE _stage_out
        ERROR_VARIABLE _stage_err)
if (NOT _stage_rv EQUAL 0)
    message(FATAL_ERROR
            "cmake --install failed while staging toolchain installer e2e payload\nstdout:\n${_stage_out}\nstderr:\n${_stage_err}")
endif ()

set(_package_cmd
        "${VIPER_BIN}" install-package
        --stage-dir "${_stage_dir}"
        --target windows
        --windows-install-scope user
        --windows-file-associations off
        --windows-shortcuts off
        -o "${_installer}")

execute_process(
        COMMAND ${_package_cmd}
        RESULT_VARIABLE _pkg_rv
        OUTPUT_VARIABLE _pkg_out
        ERROR_VARIABLE _pkg_err)
if (NOT _pkg_rv EQUAL 0)
    message(FATAL_ERROR
            "toolchain installer generation failed\nstdout:\n${_pkg_out}\nstderr:\n${_pkg_err}")
endif ()
if (NOT EXISTS "${_installer}")
    message(FATAL_ERROR "toolchain installer was not produced: ${_installer}")
endif ()

if (EXISTS "${_uninstaller}")
    execute_process(COMMAND "${_uninstaller}" /quiet /norestart
                    RESULT_VARIABLE _pre_uninstall_rv
                    OUTPUT_VARIABLE _pre_uninstall_out
                    ERROR_VARIABLE _pre_uninstall_err)
    if (NOT _pre_uninstall_rv EQUAL 0)
        message(FATAL_ERROR
                "pre-existing Viper uninstall failed\nstdout:\n${_pre_uninstall_out}\nstderr:\n${_pre_uninstall_err}")
    endif ()
endif ()

execute_process(COMMAND "${_installer}" /quiet /norestart
                RESULT_VARIABLE _install_rv
                OUTPUT_VARIABLE _install_out
                ERROR_VARIABLE _install_err)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR
            "per-user Viper installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()
if (NOT EXISTS "${_installed_viper}")
    message(FATAL_ERROR "installer did not install viper.exe: ${_installed_viper}")
endif ()

execute_process(COMMAND "${_installed_viper}" --version
                RESULT_VARIABLE _version_rv
                OUTPUT_VARIABLE _version_out
                ERROR_VARIABLE _version_err)
if (NOT _version_rv EQUAL 0)
    message(FATAL_ERROR
            "installed viper --version failed\nstdout:\n${_version_out}\nstderr:\n${_version_err}")
endif ()

file(WRITE "${_src_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(viper_installer_e2e_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Viper CONFIG REQUIRED)
add_executable(viper_installer_e2e_consumer main.cpp)
target_link_libraries(viper_installer_e2e_consumer PRIVATE viper::il_core viper::il_io)
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
        COMMAND "${CMAKE_BIN}" -S "${_src_dir}" -B "${_build_dir}" "-DCMAKE_PREFIX_PATH=${_install_root}"
        RESULT_VARIABLE _cfg_rv
        OUTPUT_VARIABLE _cfg_out
        ERROR_VARIABLE _cfg_err)
if (NOT _cfg_rv EQUAL 0)
    message(FATAL_ERROR
            "external find_package(Viper) configure failed against installed toolchain\nstdout:\n${_cfg_out}\nstderr:\n${_cfg_err}")
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
    message(FATAL_ERROR
            "external consumer build failed against installed toolchain\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_uninstaller}")
    message(FATAL_ERROR "installer did not install uninstall.exe: ${_uninstaller}")
endif ()
execute_process(COMMAND "${_uninstaller}" /quiet /norestart
                RESULT_VARIABLE _uninstall_rv
                OUTPUT_VARIABLE _uninstall_out
                ERROR_VARIABLE _uninstall_err)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR
            "per-user Viper uninstall failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()
if (EXISTS "${_installed_viper}")
    message(FATAL_ERROR "uninstaller left viper.exe behind: ${_installed_viper}")
endif ()
