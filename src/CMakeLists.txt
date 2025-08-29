add_library(support STATIC support/placeholder.cpp)
set_target_properties(support PROPERTIES LINKER_LANGUAGE CXX)

add_library(il_core STATIC il/core/placeholder.cpp)
set_target_properties(il_core PROPERTIES LINKER_LANGUAGE CXX)

add_library(il_vm STATIC vm/placeholder.cpp)
set_target_properties(il_vm PROPERTIES LINKER_LANGUAGE CXX)

add_library(il_codegen_x86_64 STATIC codegen/x86_64/placeholder.cpp)
set_target_properties(il_codegen_x86_64 PROPERTIES LINKER_LANGUAGE CXX)

add_executable(ilc tools/ilc/ilc.cpp)
set_target_properties(ilc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/ilc)
target_link_libraries(ilc PRIVATE support il_core il_vm il_codegen_x86_64)

add_executable(il-verify tools/il-verify/il-verify.cpp)
set_target_properties(il-verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-verify)
target_link_libraries(il-verify PRIVATE support il_core il_vm)
