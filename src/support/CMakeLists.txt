add_library(zanna_support STATIC
        string_interner.cpp
        source_manager.cpp
        source_location.cpp
        symbol.cpp
        diagnostics.cpp
        arena.cpp
        diag_expected.cpp
        diag_capture.cpp
        diag_catalog.cpp
)

target_include_directories(zanna_support PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/zanna>
)
target_link_libraries(zanna_support PRIVATE zanna_common_opts)
