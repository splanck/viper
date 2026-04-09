cmake_minimum_required(VERSION 3.20)

foreach (_required CMAKE_BIN VIPER_BUILD_DIR VIPER_BIN)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to InstalledViperConfigSmoke.cmake")
    endif ()
endforeach ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/installed-viper-config-smoke")
set(_stage_dir "${_tmp_root}/stage")
set(_src_dir "${_tmp_root}/src")
set(_build_dir "${_tmp_root}/build")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_src_dir}")

set(_install_cmd "${CMAKE_BIN}" --install "${VIPER_BUILD_DIR}" --prefix "${_stage_dir}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _install_cmd --config "${VIPER_CONFIG}")
endif ()
execute_process(
    COMMAND ${_install_cmd}
    RESULT_VARIABLE _install_rv
    OUTPUT_VARIABLE _install_out
    ERROR_VARIABLE _install_err
)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR
        "cmake --install for installed-config smoke failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

execute_process(
    COMMAND "${VIPER_BIN}" install-package --stage-dir "${_stage_dir}" --stage-only
    RESULT_VARIABLE _stage_rv
    OUTPUT_VARIABLE _stage_out
    ERROR_VARIABLE _stage_err
)
if (NOT _stage_rv EQUAL 0)
    message(FATAL_ERROR
        "viper install-package --stage-only failed for staged install\nstdout:\n${_stage_out}\nstderr:\n${_stage_err}")
endif ()

file(WRITE "${_src_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(viper_installed_config_smoke LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Viper CONFIG REQUIRED)
foreach(_required_target IN ITEMS
    viper::runtime
    viper::rt_base
    viper::rt_arrays
    viper::rt_oop
    viper::rt_collections
    viper::rt_game
    viper::rt_text
    viper::rt_io_fs
    viper::rt_exec
    viper::rt_threads
    viper::rt_graphics
    viper::rt_audio
    viper::rt_network)
  if (NOT TARGET ${_required_target})
    message(FATAL_ERROR "Installed Viper config is missing imported target ${_required_target}")
  endif ()
endforeach()
add_executable(viper_installed_config_smoke main.cpp)
target_link_libraries(viper_installed_config_smoke PRIVATE viper::il_core viper::il_io)
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
    COMMAND "${CMAKE_BIN}" -S "${_src_dir}" -B "${_build_dir}" "-DCMAKE_PREFIX_PATH=${_stage_dir}"
    RESULT_VARIABLE _cfg_rv
    OUTPUT_VARIABLE _cfg_out
    ERROR_VARIABLE _cfg_err
)
if (NOT _cfg_rv EQUAL 0)
    message(FATAL_ERROR
        "external find_package(Viper) configure failed\nstdout:\n${_cfg_out}\nstderr:\n${_cfg_err}")
endif ()

set(_consumer_build_cmd "${CMAKE_BIN}" --build "${_build_dir}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _consumer_build_cmd --config "${VIPER_CONFIG}")
endif ()
execute_process(
    COMMAND ${_consumer_build_cmd}
    RESULT_VARIABLE _build_rv
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err
)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
        "external find_package(Viper) build failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (WIN32)
    set(_exe_suffix ".exe")
else ()
    set(_exe_suffix "")
endif ()

set(_installed_viper_bin "${_stage_dir}/bin/viper${_exe_suffix}")
if (NOT EXISTS "${_installed_viper_bin}")
    message(FATAL_ERROR "staged install did not include viper executable: ${_installed_viper_bin}")
endif ()

set(_exe_path "${_build_dir}/viper_installed_config_smoke${_exe_suffix}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    if (EXISTS "${_build_dir}/${VIPER_CONFIG}/viper_installed_config_smoke${_exe_suffix}")
        set(_exe_path "${_build_dir}/${VIPER_CONFIG}/viper_installed_config_smoke${_exe_suffix}")
    endif ()
endif ()

if (NOT EXISTS "${_exe_path}")
    message(FATAL_ERROR "external find_package(Viper) build did not produce smoke executable: ${_exe_path}")
endif ()

execute_process(
    COMMAND "${_exe_path}"
    RESULT_VARIABLE _run_rv
    OUTPUT_VARIABLE _run_out
    ERROR_VARIABLE _run_err
)
if (NOT _run_rv EQUAL 0)
    message(FATAL_ERROR
        "external find_package(Viper) smoke executable failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()

set(_host_arch "")
if (DEFINED VIPER_HOST_ARCH AND NOT "${VIPER_HOST_ARCH}" STREQUAL "")
    set(_host_arch "${VIPER_HOST_ARCH}")
elseif (DEFINED ENV{PROCESSOR_ARCHITECTURE} AND NOT "$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "")
    set(_host_arch "$ENV{PROCESSOR_ARCHITECTURE}")
else ()
    execute_process(
        COMMAND uname -m
        RESULT_VARIABLE _uname_rv
        OUTPUT_VARIABLE _host_arch
        ERROR_VARIABLE _uname_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT _uname_rv EQUAL 0)
        message(FATAL_ERROR
            "unable to determine host architecture for installed viper smoke\nstderr:\n${_uname_err}")
    endif ()
endif ()

if (_host_arch MATCHES "^(arm64|ARM64|aarch64)$")
    set(_installed_codegen_arch arm64)
else ()
    set(_installed_codegen_arch x64)
endif ()

set(_installed_il "${_tmp_root}/installed_runtime_smoke.il")
set(_installed_exe "${_tmp_root}/installed_runtime_smoke${_exe_suffix}")

file(WRITE "${_installed_il}" [=[
il 0.2.0

extern @Viper.Terminal.PrintStr(str) -> void
global const str @.msg = "Hello, installed Viper!"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Viper.Terminal.PrintStr(%msg)
  ret 0
}
]=])

execute_process(
    COMMAND "${CMAKE_BIN}" -E env --unset=VIPER_LIB_PATH "${_installed_viper_bin}" codegen "${_installed_codegen_arch}" "${_installed_il}" -o "${_installed_exe}"
    WORKING_DIRECTORY "${_tmp_root}"
    RESULT_VARIABLE _installed_codegen_rv
    OUTPUT_VARIABLE _installed_codegen_out
    ERROR_VARIABLE _installed_codegen_err
)
if (NOT _installed_codegen_rv EQUAL 0)
    message(FATAL_ERROR
        "staged viper failed to compile a native executable outside the build tree\nstdout:\n${_installed_codegen_out}\nstderr:\n${_installed_codegen_err}")
endif ()

if (NOT EXISTS "${_installed_exe}")
    message(FATAL_ERROR "staged viper did not produce native smoke executable: ${_installed_exe}")
endif ()

execute_process(
    COMMAND "${_installed_exe}"
    WORKING_DIRECTORY "${_tmp_root}"
    RESULT_VARIABLE _installed_run_rv
    OUTPUT_VARIABLE _installed_run_out
    ERROR_VARIABLE _installed_run_err
)
if (NOT _installed_run_rv EQUAL 0)
    message(FATAL_ERROR
        "native executable built by staged viper failed\nstdout:\n${_installed_run_out}\nstderr:\n${_installed_run_err}")
endif ()

if (NOT _installed_run_out MATCHES "Hello, installed Viper!")
    message(FATAL_ERROR
        "native executable built by staged viper produced unexpected output\nstdout:\n${_installed_run_out}\nstderr:\n${_installed_run_err}")
endif ()
