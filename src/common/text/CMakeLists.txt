# SPDX-License-Identifier: GPL-3.0-only
# Viper shared text engine library
#
# This target packages the piece-table text implementation and its C ABI facade
# so GUI and TUI editors link one shared buffer engine.

add_library(viper_text_core STATIC
        ${CMAKE_SOURCE_DIR}/src/tui/src/text/EditHistory.cpp
        ${CMAKE_SOURCE_DIR}/src/tui/src/text/LineIndex.cpp
        ${CMAKE_SOURCE_DIR}/src/tui/src/text/PieceTable.cpp
        ${CMAKE_SOURCE_DIR}/src/tui/src/text/text_buffer.cpp
        viper_text_buffer.cpp
)

target_compile_features(viper_text_core PUBLIC cxx_std_20)

target_include_directories(viper_text_core
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/tui/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper/text>
)

target_link_libraries(viper_text_core
        PUBLIC
        viper_common_opts
)

install(FILES viper_text_buffer.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/text
)
