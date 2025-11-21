# ViperGFX Examples
# Demonstration programs

message(STATUS "ViperGFX: Configuring examples subdirectory")

# API test (works with stub backends)
add_executable(api_test api_test.c)
target_link_libraries(api_test PRIVATE vipergfx)
target_include_directories(api_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Quick visual test (auto-exits after 30 frames)
add_executable(quick_test quick_test.c)
target_link_libraries(quick_test PRIVATE vipergfx)
target_include_directories(quick_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Basic drawing example
add_executable(basic_draw basic_draw.c)
target_link_libraries(basic_draw PRIVATE vipergfx)
target_include_directories(basic_draw PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Note: Examples with stub platform backends will not display windows
# but can be used to verify API compilation and linking
