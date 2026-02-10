# Viper GUI Library CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

# GUI library sources
set(VG_GUI_FONT_SOURCES
        src/font/vg_ttf.c
        src/font/vg_raster.c
        src/font/vg_cache.c
        src/font/vg_font.c
        src/font/vg_canvas_integration.c
)

set(VG_GUI_CORE_SOURCES
        src/core/vg_widget.c
        src/core/vg_layout.c
        src/core/vg_event.c
        src/core/vg_theme.c
)

set(VG_GUI_WIDGET_SOURCES
        src/widgets/vg_label.c
        src/widgets/vg_button.c
        src/widgets/vg_textinput.c
        src/widgets/vg_checkbox.c
        src/widgets/vg_scrollview.c
        src/widgets/vg_listbox.c
        src/widgets/vg_dropdown.c
        src/widgets/vg_slider.c
        src/widgets/vg_progressbar.c
        src/widgets/vg_radiobutton.c
        src/widgets/vg_image.c
        src/widgets/vg_spinner.c
        src/widgets/vg_colorswatch.c
        src/widgets/vg_colorpalette.c
        src/widgets/vg_colorpicker.c
)

set(VG_GUI_IDE_WIDGET_SOURCES
        src/widgets/vg_treeview.c
        src/widgets/vg_tabbar.c
        src/widgets/vg_splitpane.c
        src/widgets/vg_menubar.c
        src/widgets/vg_codeeditor.c
        src/widgets/vg_statusbar.c
        src/widgets/vg_toolbar.c
        src/widgets/vg_dialog.c
        src/widgets/vg_filedialog.c
        src/widgets/vg_contextmenu.c
        src/widgets/vg_findreplacebar.c
        src/widgets/vg_tooltip.c
        src/widgets/vg_commandpalette.c
        src/widgets/vg_outputpane.c
        src/widgets/vg_breadcrumb.c
        src/widgets/vg_minimap.c
        src/widgets/vg_notification.c
)

# Platform-specific native dialogs
if (APPLE)
    set(VG_GUI_NATIVE_DIALOG_SOURCES
            src/dialogs/vg_filedialog_native.m
    )
endif ()

# Phase 1 + Phase 2 + Phase 3 + Phase 5: Font engine + Widget core + Widgets + IDE Widgets
set(VG_GUI_SOURCES ${VG_GUI_FONT_SOURCES} ${VG_GUI_CORE_SOURCES} ${VG_GUI_WIDGET_SOURCES} ${VG_GUI_IDE_WIDGET_SOURCES} ${VG_GUI_NATIVE_DIALOG_SOURCES})

# Create GUI library
add_library(vipergui STATIC ${VG_GUI_SOURCES})

# Include directories
target_include_directories(vipergui
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/font
        ${CMAKE_CURRENT_SOURCE_DIR}/../graphics/include
)

# Link against vipergfx
target_link_libraries(vipergui
        PUBLIC
        vipergfx
)

# Link math library on Unix
if (UNIX AND NOT APPLE)
    target_link_libraries(vipergui PRIVATE m)
endif ()

# Link UniformTypeIdentifiers framework on macOS (for file dialogs)
if (APPLE)
    target_link_libraries(vipergui PRIVATE "-framework UniformTypeIdentifiers")
endif ()

# C11 standard
set_target_properties(vipergui PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
)

# Compiler warnings
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vipergui PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
    )
elseif (MSVC)
    target_compile_options(vipergui PRIVATE /utf-8 /FS)
endif ()

# Tests
if (BUILD_TESTING)
    add_executable(test_vg_font tests/test_font.c)
    target_link_libraries(test_vg_font vipergui)
    target_include_directories(test_vg_font PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    add_test(NAME test_vg_font COMMAND test_vg_font)
endif ()

# Demo applications
add_executable(gui_demo examples/gui_demo.c)
target_link_libraries(gui_demo vipergui vipergfx)
target_include_directories(gui_demo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../graphics/include
)

add_executable(widget_showcase examples/widget_showcase.c)
target_link_libraries(widget_showcase vipergui vipergfx)
target_include_directories(widget_showcase PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../graphics/include
)

add_executable(color_widgets_demo examples/color_widgets_demo.c)
target_link_libraries(color_widgets_demo vipergui vipergfx)
target_include_directories(color_widgets_demo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../graphics/include
)

# ViperIDE Runtime Test (Phase 1 & 2 features)
add_executable(viperide_menu_test ../../../demos/zia/viperide/menu_test.c)
target_link_libraries(viperide_menu_test PRIVATE viper_runtime vipergui vipergfx)
target_include_directories(viperide_menu_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../graphics/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../runtime
)
