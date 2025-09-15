add_subdirectory(support)

add_library(il_core STATIC
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

add_library(il_build STATIC il/build/IRBuilder.cpp)
target_link_libraries(il_build PUBLIC il_core support)
target_include_directories(il_build PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_io STATIC il/io/Serializer.cpp il/io/Parser.cpp)
target_link_libraries(il_io PUBLIC il_core)
target_include_directories(il_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_verify STATIC il/verify/Verifier.cpp)
target_link_libraries(il_verify PUBLIC il_core)
target_include_directories(il_verify PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_transform STATIC
  il/transform/PassManager.cpp
  il/transform/Peephole.cpp
  il/transform/ConstFold.cpp
  il/transform/DCE.cpp)
target_link_libraries(il_transform PUBLIC il_core il_verify)
target_include_directories(il_transform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(il_vm STATIC vm/VM.cpp vm/RuntimeBridge.cpp)
target_include_directories(il_vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(il_vm PUBLIC il_core rt support VMTrace)

add_library(il_codegen_x86_64 STATIC codegen/x86_64/placeholder.cpp)
set_target_properties(il_codegen_x86_64 PROPERTIES LINKER_LANGUAGE CXX)

add_executable(ilc tools/ilc/main.cpp tools/ilc/cmd_run_il.cpp tools/ilc/cmd_front_basic.cpp tools/ilc/cmd_il_opt.cpp)
set_target_properties(ilc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/ilc)
target_link_libraries(ilc PRIVATE il_core il_io il_vm il_verify il_transform fe_basic support Passes)

add_executable(il-verify tools/il-verify/il-verify.cpp)
set_target_properties(il-verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-verify)
target_link_libraries(il-verify PRIVATE support il_core il_build il_io il_vm il_verify)

add_executable(il-dis tools/il-dis/main.cpp)
set_target_properties(il-dis PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-dis)
target_link_libraries(il-dis PRIVATE support il_core il_build il_io)

add_library(fe_basic STATIC
  frontends/basic/Token.cpp
  frontends/basic/Lexer.cpp
  frontends/basic/AST.cpp
  frontends/basic/AstPrinter.cpp
  frontends/basic/Parser.cpp
  frontends/basic/Parser_Stmt.cpp
  frontends/basic/Parser_Expr.cpp
  frontends/basic/Parser_Token.cpp
  frontends/basic/NameMangler.cpp
  frontends/basic/LoweringContext.cpp
  frontends/basic/BuiltinRegistry.cpp
  frontends/basic/Lowerer.cpp
  frontends/basic/LowerRuntime.cpp
  frontends/basic/LowerScan.cpp
  frontends/basic/LowerEmit.cpp
  frontends/basic/SemanticAnalyzer.cpp
  frontends/basic/ConstFolder.cpp
  frontends/basic/Intrinsics.cpp
  frontends/basic/DiagnosticEmitter.cpp)
target_link_libraries(fe_basic PUBLIC support il_build il_core)
target_include_directories(fe_basic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(basic-ast-dump tools/basic-ast-dump/main.cpp)
set_target_properties(basic-ast-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/basic-ast-dump)
target_link_libraries(basic-ast-dump PRIVATE fe_basic support)
add_executable(basic-lex-dump tools/basic-lex-dump/main.cpp)
set_target_properties(basic-lex-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/basic-lex-dump)
target_link_libraries(basic-lex-dump PRIVATE fe_basic support)
