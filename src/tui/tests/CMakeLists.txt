# Tests for ViperTUI

add_executable(tui_test_smoke test_smoke.cpp)

target_link_libraries(tui_test_smoke PRIVATE tui)

add_test(NAME tui_test_smoke COMMAND tui_test_smoke)

add_executable(tui_test_term_io test_term_io.cpp)
target_link_libraries(tui_test_term_io PRIVATE tui)
add_test(NAME tui_test_term_io COMMAND tui_test_term_io)

add_executable(tui_test_clipboard test_clipboard.cpp)
target_link_libraries(tui_test_clipboard PRIVATE tui)
add_test(NAME tui_test_clipboard COMMAND tui_test_clipboard)

add_executable(tui_test_session test_session.cpp)
target_link_libraries(tui_test_session PRIVATE tui)
add_test(NAME tui_test_session COMMAND tui_test_session)

add_executable(tui_test_input_utf8 test_input_utf8.cpp)
target_link_libraries(tui_test_input_utf8 PRIVATE tui)
add_test(NAME tui_test_input_utf8 COMMAND tui_test_input_utf8)

add_executable(tui_test_input_csi test_input_csi.cpp)
target_link_libraries(tui_test_input_csi PRIVATE tui)
add_test(NAME tui_test_input_csi COMMAND tui_test_input_csi)

add_executable(tui_test_screen_diff test_screen_diff.cpp)
target_link_libraries(tui_test_screen_diff PRIVATE tui)
add_test(NAME tui_test_screen_diff COMMAND tui_test_screen_diff)

add_executable(tui_test_renderer_minimal test_renderer_minimal.cpp)
target_link_libraries(tui_test_renderer_minimal PRIVATE tui)
add_test(NAME tui_test_renderer_minimal COMMAND tui_test_renderer_minimal)

add_executable(tui_test_unicode_width test_unicode_width.cpp)
target_link_libraries(tui_test_unicode_width PRIVATE tui)
add_test(NAME tui_test_unicode_width COMMAND tui_test_unicode_width)

add_executable(tui_test_app_layout test_app_layout.cpp)
target_link_libraries(tui_test_app_layout PRIVATE tui)
add_test(NAME tui_test_app_layout COMMAND tui_test_app_layout)

add_executable(tui_test_app_resize test_app_resize.cpp)
target_link_libraries(tui_test_app_resize PRIVATE tui)
add_test(NAME tui_test_app_resize COMMAND tui_test_app_resize)

add_executable(tui_test_focus test_focus.cpp)
target_link_libraries(tui_test_focus PRIVATE tui)
add_test(NAME tui_test_focus COMMAND tui_test_focus)

add_executable(tui_test_focus_hooks test_focus_hooks.cpp)
target_link_libraries(tui_test_focus_hooks PRIVATE tui)
add_test(NAME tui_test_focus_hooks COMMAND tui_test_focus_hooks)

add_executable(tui_test_widgets_basic test_widgets_basic.cpp)
target_link_libraries(tui_test_widgets_basic PRIVATE tui)
add_test(NAME tui_test_widgets_basic COMMAND tui_test_widgets_basic)

add_executable(tui_test_modal test_modal.cpp)
target_link_libraries(tui_test_modal PRIVATE tui)
add_test(NAME tui_test_modal COMMAND tui_test_modal)

add_executable(tui_test_split_status test_split_status.cpp)
target_link_libraries(tui_test_split_status PRIVATE tui)
add_test(NAME tui_test_split_status COMMAND tui_test_split_status)

add_executable(tui_test_text_buffer test_text_buffer.cpp)
target_link_libraries(tui_test_text_buffer PRIVATE tui)
add_test(NAME tui_test_text_buffer COMMAND tui_test_text_buffer)

add_executable(tui_test_text_view test_text_view.cpp)
target_link_libraries(tui_test_text_view PRIVATE tui)
add_test(NAME tui_test_text_view COMMAND tui_test_text_view)

add_executable(tui_test_text_view_large views/test_text_view_large_buffer.cpp)
target_link_libraries(tui_test_text_view_large PRIVATE tui)
add_test(NAME tui_test_text_view_large COMMAND tui_test_text_view_large)

add_executable(tui_test_search test_search.cpp)
target_link_libraries(tui_test_search PRIVATE tui)
add_test(NAME tui_test_search COMMAND tui_test_search)

add_executable(tui_test_keymap_palette test_keymap_palette.cpp)
target_link_libraries(tui_test_keymap_palette PRIVATE tui)
add_test(NAME tui_test_keymap_palette COMMAND tui_test_keymap_palette)

add_executable(tui_test_syntax test_syntax.cpp)
target_link_libraries(tui_test_syntax PRIVATE tui)
target_compile_definitions(tui_test_syntax PRIVATE SYNTAX_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../resources/syntax/json.json")
add_test(NAME tui_test_syntax COMMAND tui_test_syntax)

add_executable(tui_test_list_tree test_list_tree.cpp)
target_link_libraries(tui_test_list_tree PRIVATE tui)
add_test(NAME tui_test_list_tree COMMAND tui_test_list_tree)

add_executable(tui_test_config test_config.cpp)
target_link_libraries(tui_test_config PRIVATE tui)
target_compile_definitions(tui_test_config
        PRIVATE CONFIG_INI="${CMAKE_CURRENT_SOURCE_DIR}/data/config.ini"
        CONFIG_BAD_TAB_INI="${CMAKE_CURRENT_SOURCE_DIR}/data/config_bad_tab.ini")
add_test(NAME tui_test_config COMMAND tui_test_config)

add_executable(tui_test_demo_headless test_demo_headless.cpp)
target_link_libraries(tui_test_demo_headless PRIVATE tui)
add_test(NAME tui_test_demo_headless COMMAND tui_test_demo_headless)

add_executable(tui_test_win_vt test_win_vt.cpp)
target_link_libraries(tui_test_win_vt PRIVATE tui)
add_test(NAME tui_test_win_vt COMMAND tui_test_win_vt)

add_executable(tui_test_splitter_keyboard test_splitter_keyboard.cpp)
target_link_libraries(tui_test_splitter_keyboard PRIVATE tui)
add_test(NAME tui_test_splitter_keyboard COMMAND tui_test_splitter_keyboard)

# Set labels for all TUI tests
get_property(_tui_tests DIRECTORY PROPERTY TESTS)
foreach(_t IN LISTS _tui_tests)
    if(_t MATCHES "^tui_")
        set_tests_properties(${_t} PROPERTIES LABELS tui)
    endif()
endforeach()
