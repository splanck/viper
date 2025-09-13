# Tests for ViperTUI

add_executable(tui_test_smoke test_smoke.cpp)

target_link_libraries(tui_test_smoke PRIVATE tui)

add_test(NAME tui_test_smoke COMMAND tui_test_smoke)
