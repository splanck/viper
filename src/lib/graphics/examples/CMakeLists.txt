# ZannaGFX Examples
# Demonstration programs

message(STATUS "ZannaGFX: Configuring examples subdirectory")

# API test (works with stub backends)
add_executable(api_test api_test.c)
target_link_libraries(api_test PRIVATE zannagfx)
# zannagfx already exports its include directory as PUBLIC, so no need to add it again

# Quick visual test (auto-exits after 30 frames)
add_executable(quick_test quick_test.c)
target_link_libraries(quick_test PRIVATE zannagfx)

# Basic drawing example
add_executable(basic_draw basic_draw.c)
target_link_libraries(basic_draw PRIVATE zannagfx)

# Note: Examples with stub platform backends will not display windows
# but can be used to verify API compilation and linking
