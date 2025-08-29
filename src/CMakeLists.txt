add_subdirectory(support)

add_library(il_core STATIC
  il/core/Type.cc
  il/core/Value.cc
  il/core/Instr.cc
  il/core/BasicBlock.cc
  il/core/Function.cc
  il/core/Global.cc
  il/core/Extern.cc
  il/core/Module.cc
)
target_include_directories(il_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_build STATIC il/build/IRBuilder.cc)
target_link_libraries(il_build PUBLIC il_core support)
target_include_directories(il_build PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_io STATIC il/io/Serializer.cc il/io/Parser.cc)
target_link_libraries(il_io PUBLIC il_core)
target_include_directories(il_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_verify STATIC il/verify/Verifier.cc)
target_link_libraries(il_verify PUBLIC il_core)
target_include_directories(il_verify PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_vm STATIC vm/placeholder.cpp)
set_target_properties(il_vm PROPERTIES LINKER_LANGUAGE CXX)

add_library(il_codegen_x86_64 STATIC codegen/x86_64/placeholder.cpp)
set_target_properties(il_codegen_x86_64 PROPERTIES LINKER_LANGUAGE CXX)

add_executable(ilc tools/ilc/ilc.cpp)
set_target_properties(ilc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/ilc)
target_link_libraries(ilc PRIVATE support il_core il_build il_io il_vm il_codegen_x86_64)

add_executable(il-verify tools/il-verify/il-verify.cpp)
set_target_properties(il-verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-verify)
target_link_libraries(il-verify PRIVATE support il_core il_build il_io il_vm il_verify)

add_executable(il-dis tools/il-dis/main.cpp)
set_target_properties(il-dis PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-dis)
target_link_libraries(il-dis PRIVATE support il_core il_build il_io)
