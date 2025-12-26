add_library(viper_support STATIC
        string_interner.cpp
        source_manager.cpp
        source_location.cpp
        symbol.cpp
        diagnostics.cpp
        arena.cpp
        diag_expected.cpp
        diag_capture.cpp
)

target_include_directories(viper_support PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)
