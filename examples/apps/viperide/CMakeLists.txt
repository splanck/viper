# ViperIDE Test Programs

# Menu test - tests Phase 1 & 2 runtime features
add_executable(menu_test menu_test.c)
target_link_libraries(menu_test PRIVATE viper_runtime vipergui vipergfx)
