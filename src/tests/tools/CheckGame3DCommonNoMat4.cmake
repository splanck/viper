if (NOT DEFINED VIPER_SOURCE_DIR)
    message(FATAL_ERROR "VIPER_SOURCE_DIR is required")
endif ()

set(_game3d_common_files
        examples/3d/game3d_hello.zia
        examples/3d/game3d_starter/main.zia
        examples/3d/game3d_starter/test.zia
        src/tests/fixtures/runtime/test_game3d_world_probe.zia
        src/tests/fixtures/runtime/test_game3d_docs_snippets.zia)

foreach (_rel IN LISTS _game3d_common_files)
    set(_path "${VIPER_SOURCE_DIR}/${_rel}")
    if (NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing Game3D common sample/probe: ${_rel}")
    endif ()
    file(READ "${_path}" _contents)
    if (_contents MATCHES "Mat4\\.")
        message(FATAL_ERROR "Common Game3D sample/probe uses Mat4 directly: ${_rel}")
    endif ()
endforeach ()

message("PASS: common Game3D samples and probes avoid direct Mat4 calls")
