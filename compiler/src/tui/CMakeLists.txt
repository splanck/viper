# CMake configuration for ViperTUI

cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tui STATIC
        src/version.cpp
        src/term/term_io.cpp
        src/term/session.cpp
        src/term/input.cpp
        src/term/CsiParser.cpp
        src/term/Utf8Decoder.cpp
        src/term/clipboard.cpp
        src/input/keymap.cpp
        src/util/unicode.cpp
        src/render/screen.cpp
        src/render/renderer.cpp
        src/ui/widget.cpp
        src/ui/container.cpp
        src/ui/focus.cpp
        src/ui/modal.cpp
        src/style/theme.cpp
        src/widgets/label.cpp
        src/widgets/button.cpp
        src/widgets/splitter.cpp
        src/widgets/status_bar.cpp
        src/widgets/search_bar.cpp
        src/widgets/command_palette.cpp
        src/widgets/list_view.cpp
        src/widgets/tree_view.cpp
        src/syntax/rules.cpp
        src/text/EditHistory.cpp
        src/text/LineIndex.cpp
        src/text/PieceTable.cpp
        src/text/text_buffer.cpp
        src/text/search.cpp
        src/views/text_view_cursor.cpp
        src/views/text_view_input.cpp
        src/views/text_view_render.cpp
        src/app.cpp
        src/config/config.cpp
)

target_include_directories(tui PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

target_link_libraries(tui
        PUBLIC
        viper_common_opts
)

if (VIPER_INSTALL_TUI)
    install(TARGETS tui
            EXPORT ViperTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/tui/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/tui
            FILES_MATCHING PATTERN "*.hpp"
    )
endif ()

add_subdirectory(apps)

if (VIPER_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif ()
