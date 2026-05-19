function(viper_installer_smoke_host_codegen_arch out_var)
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
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (NOT _uname_rv EQUAL 0)
            message(FATAL_ERROR
                    "unable to determine host architecture for installed viper smoke\nstderr:\n${_uname_err}")
        endif ()
    endif ()

    if (_host_arch MATCHES "^(arm64|ARM64|aarch64)$")
        set(${out_var} arm64 PARENT_SCOPE)
    else ()
        set(${out_var} x64 PARENT_SCOPE)
    endif ()
endfunction()

function(viper_installer_smoke_verify_cmake_consumer cmake_bin src_dir build_dir config_name label)
    file(REMOVE_RECURSE "${src_dir}" "${build_dir}")
    file(MAKE_DIRECTORY "${src_dir}")
    file(WRITE "${src_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(viper_installed_package_consumer LANGUAGES CXX)
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
add_executable(viper_installed_package_consumer main.cpp)
target_link_libraries(viper_installed_package_consumer PRIVATE viper::il_core viper::il_io)
]=])

    file(WRITE "${src_dir}/main.cpp" [=[
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
            COMMAND "${cmake_bin}" -S "${src_dir}" -B "${build_dir}"
            RESULT_VARIABLE _cfg_rv
            OUTPUT_VARIABLE _cfg_out
            ERROR_VARIABLE _cfg_err)
    if (NOT _cfg_rv EQUAL 0)
        message(FATAL_ERROR
                "${label}: installed Viper config was not discoverable by CMake\nstdout:\n${_cfg_out}\nstderr:\n${_cfg_err}")
    endif ()

    set(_consumer_build_cmd "${cmake_bin}" --build "${build_dir}")
    if (NOT "${config_name}" STREQUAL "")
        list(APPEND _consumer_build_cmd --config "${config_name}")
    endif ()
    execute_process(
            COMMAND ${_consumer_build_cmd}
            RESULT_VARIABLE _build_rv
            OUTPUT_VARIABLE _build_out
            ERROR_VARIABLE _build_err)
    if (NOT _build_rv EQUAL 0)
        message(FATAL_ERROR
                "${label}: installed Viper consumer build failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
    endif ()

    if (WIN32)
        set(_exe_suffix ".exe")
    else ()
        set(_exe_suffix "")
    endif ()
    set(_exe_path "${build_dir}/viper_installed_package_consumer${_exe_suffix}")
    if (NOT "${config_name}" STREQUAL "" AND EXISTS "${build_dir}/${config_name}/viper_installed_package_consumer${_exe_suffix}")
        set(_exe_path "${build_dir}/${config_name}/viper_installed_package_consumer${_exe_suffix}")
    endif ()
    if (NOT EXISTS "${_exe_path}")
        message(FATAL_ERROR "${label}: consumer build did not produce ${_exe_path}")
    endif ()

    execute_process(
            COMMAND "${_exe_path}"
            RESULT_VARIABLE _run_rv
            OUTPUT_VARIABLE _run_out
            ERROR_VARIABLE _run_err)
    if (NOT _run_rv EQUAL 0)
        message(FATAL_ERROR
                "${label}: installed Viper consumer executable failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
    endif ()
endfunction()

function(viper_installer_smoke_verify_native_codegen cmake_bin viper_bin tmp_root label)
    if (DEFINED VIPER_RUN_NATIVE_CODEGEN AND NOT VIPER_RUN_NATIVE_CODEGEN)
        message(STATUS "Skipping installed native codegen smoke; native link is disabled")
        return()
    endif ()

    viper_installer_smoke_host_codegen_arch(_installed_codegen_arch)
    if (WIN32)
        set(_exe_suffix ".exe")
    else ()
        set(_exe_suffix "")
    endif ()
    set(_installed_il "${tmp_root}/installed_runtime_smoke.il")
    set(_installed_exe "${tmp_root}/installed_runtime_smoke${_exe_suffix}")

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
            COMMAND "${cmake_bin}" -E env --unset=VIPER_LIB_PATH "${viper_bin}" codegen "${_installed_codegen_arch}" "${_installed_il}" -o "${_installed_exe}"
            WORKING_DIRECTORY "${tmp_root}"
            RESULT_VARIABLE _codegen_rv
            OUTPUT_VARIABLE _codegen_out
            ERROR_VARIABLE _codegen_err)
    if (NOT _codegen_rv EQUAL 0)
        message(FATAL_ERROR
                "${label}: installed viper failed to compile native executable outside the build tree\nstdout:\n${_codegen_out}\nstderr:\n${_codegen_err}")
    endif ()
    if (NOT EXISTS "${_installed_exe}")
        message(FATAL_ERROR "${label}: installed viper did not produce native smoke executable: ${_installed_exe}")
    endif ()

    execute_process(
            COMMAND "${_installed_exe}"
            WORKING_DIRECTORY "${tmp_root}"
            RESULT_VARIABLE _run_rv
            OUTPUT_VARIABLE _run_out
            ERROR_VARIABLE _run_err)
    if (NOT _run_rv EQUAL 0)
        message(FATAL_ERROR
                "${label}: native executable built by installed viper failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
    endif ()
    if (NOT _run_out MATCHES "Hello, installed Viper!")
        message(FATAL_ERROR
                "${label}: native executable built by installed viper produced unexpected output\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
    endif ()
endfunction()
