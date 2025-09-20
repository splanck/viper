add_subdirectory(support)

add_library(il_core STATIC
  il/core/OpcodeInfo.cpp
  il/core/Type.cpp
  il/core/Value.cpp
  il/core/Instr.cpp
  il/core/BasicBlock.cpp
  il/core/Function.cpp
  il/core/Global.cpp
  il/core/Extern.cpp
  il/core/Module.cpp
)
target_include_directories(il_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_runtime STATIC il/runtime/RuntimeSignatures.cpp)
target_link_libraries(il_runtime PUBLIC il_core)
target_include_directories(il_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_build STATIC il/build/IRBuilder.cpp)
target_link_libraries(il_build PUBLIC il_core support)
target_include_directories(il_build PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(il/io)

add_library(il_verify STATIC
  il/verify/Verifier.cpp
  il/verify/InstructionChecker.cpp
  il/verify/ControlFlowChecker.cpp
  il/verify/TypeInference.cpp)
target_link_libraries(il_verify PUBLIC il_core il_runtime)
target_include_directories(il_verify PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_api STATIC il/api/expected_api.cpp)
target_link_libraries(il_api
  PUBLIC support il_io il_verify)
target_include_directories(il_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_analysis STATIC
  il/analysis/CFG.cpp
  il/analysis/Dominators.cpp)
target_link_libraries(il_analysis PUBLIC il_core)
target_include_directories(il_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_transform STATIC
  il/transform/PassManager.cpp
  il/transform/Peephole.cpp
  il/transform/ConstFold.cpp
  il/transform/DCE.cpp
  il/transform/Mem2Reg.cpp)
target_link_libraries(il_transform PUBLIC il_core il_verify il_analysis)
target_include_directories(il_transform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_utils STATIC il/utils/Utils.cpp)
target_link_libraries(il_utils PUBLIC il_core)
target_include_directories(il_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_vm STATIC
  vm/VM.cpp
  vm/VMInit.cpp
  vm/VMDebug.cpp
  vm/RuntimeBridge.cpp
  vm/OpHandlers.cpp
  vm/OpHandlerUtils.cpp
  vm/Debug.cpp
  vm/DebugScript.cpp
  vm/Trace.cpp
  vm/mem_ops.cpp
  vm/int_ops.cpp
  vm/fp_ops.cpp
  vm/control_flow.cpp)
target_include_directories(il_vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(il_vm PUBLIC il_core rt support)

add_library(il_codegen_x86_64 STATIC codegen/x86_64/placeholder.cpp)
set_target_properties(il_codegen_x86_64 PROPERTIES LINKER_LANGUAGE CXX)

add_executable(ilc
  tools/ilc/main.cpp
  tools/ilc/cmd_run_il.cpp
  tools/ilc/cmd_front_basic.cpp
  tools/ilc/cmd_il_opt.cpp
  tools/ilc/cli.cpp
  tools/ilc/break_spec.cpp)
set_target_properties(ilc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/ilc)
target_link_libraries(ilc PRIVATE il_core il_io il_vm il_verify il_transform fe_basic il_api support)

add_executable(il-verify tools/il-verify/il-verify.cpp)
set_target_properties(il-verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-verify)
target_link_libraries(il-verify PRIVATE support il_core il_build il_io il_vm il_verify il_api)

add_executable(il-dis tools/il-dis/main.cpp)
set_target_properties(il-dis PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-dis)
target_link_libraries(il-dis PRIVATE support il_core il_build il_io)

add_subdirectory(frontends/basic)

add_executable(basic-ast-dump tools/basic-ast-dump/main.cpp)
set_target_properties(basic-ast-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/basic-ast-dump)
target_link_libraries(basic-ast-dump PRIVATE fe_basic support)
add_executable(basic-lex-dump tools/basic-lex-dump/main.cpp)
set_target_properties(basic-lex-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/basic-lex-dump)
target_link_libraries(basic-lex-dump PRIVATE fe_basic support)
