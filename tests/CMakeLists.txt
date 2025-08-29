add_executable(test_support unit/test_support.cpp)
target_link_libraries(test_support PRIVATE support)
add_test(NAME test_support COMMAND test_support)

add_executable(test_il_serialize unit/test_il_serialize.cpp)
target_link_libraries(test_il_serialize PRIVATE il_core il_build il_io support)
target_compile_definitions(test_il_serialize PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME test_il_serialize COMMAND test_il_serialize)
