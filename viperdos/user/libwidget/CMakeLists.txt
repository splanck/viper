# libwidget - Widget toolkit for ViperDOS GUI applications
# Provides reusable UI components: buttons, labels, textboxes, lists, menus, etc.

add_library(viperwidget STATIC
    src/widget.c
    src/button.c
    src/label.c
    src/textbox.c
    src/checkbox.c
    src/listview.c
    src/treeview.c
    src/menu.c
    src/layout.c
    src/progressbar.c
    src/scrollbar.c
    src/draw3d.c
    src/dialog.c
)

target_compile_options(viperwidget PRIVATE ${USER_COMPILE_OPTIONS})

target_include_directories(viperwidget PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(viperwidget PRIVATE
    ${CMAKE_SOURCE_DIR}/user
    ${CMAKE_SOURCE_DIR}/user/libc/include
    ${CMAKE_SOURCE_DIR}/user/libc/include/c++
    ${CMAKE_SOURCE_DIR}/user/libgui/include
    ${CMAKE_SOURCE_DIR}/include
)

# Link against libgui and libc
target_link_libraries(viperwidget PUBLIC vipergui viperlibc)
