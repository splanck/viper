# Tests for ViperTUI

add_executable(tui_test_smoke test_smoke.cpp)

target_link_libraries(tui_test_smoke PRIVATE tui)

add_test(NAME tui_test_smoke COMMAND tui_test_smoke)

add_executable(tui_test_term_io test_term_io.cpp)
target_link_libraries(tui_test_term_io PRIVATE tui)
add_test(NAME tui_test_term_io COMMAND tui_test_term_io)

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

add_executable(tui_test_focus test_focus.cpp)
target_link_libraries(tui_test_focus PRIVATE tui)
add_test(NAME tui_test_focus COMMAND tui_test_focus)
