add_executable(test_support unit/test_support.cpp)
target_link_libraries(test_support PRIVATE support)
add_test(NAME test_support COMMAND test_support)

add_executable(test_il_serialize unit/test_il_serialize.cpp)
target_link_libraries(test_il_serialize PRIVATE il_core il_build il_io support)
target_compile_definitions(test_il_serialize PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME test_il_serialize COMMAND test_il_serialize)

add_executable(test_il_roundtrip golden/test_il_roundtrip.cpp)
target_link_libraries(test_il_roundtrip PRIVATE il_core il_io il_verify support)
target_compile_definitions(test_il_roundtrip PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/docs/examples")
add_test(NAME test_il_roundtrip COMMAND test_il_roundtrip)

add_test(NAME il_verify_hello_cond COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/hello_cond.il)
add_test(NAME il_verify_sum_1_to_10 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/sum_1_to_10.il)
add_test(NAME vm_examples COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DHELLO=${CMAKE_SOURCE_DIR}/docs/examples/il/hello_cond.il
  -DSUM=${CMAKE_SOURCE_DIR}/docs/examples/il/sum_1_to_10.il
  -DOUT=${CMAKE_BINARY_DIR}/tests/e2e
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_examples.cmake)
