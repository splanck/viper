# libwidget - Widget toolkit for ViperDOS GUI applications
# Provides reusable UI components: buttons, labels, textboxes, lists, menus, etc.

add_library(viperwidget STATIC
        src/widget.cpp
        src/button.cpp
        src/label.cpp
        src/textbox.cpp
        src/checkbox.cpp
        src/listview.cpp
        src/treeview.cpp
        src/menu.cpp
        src/layout.cpp
        src/progressbar.cpp
        src/scrollbar.cpp
        src/draw3d.cpp
        src/dialog.cpp
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
