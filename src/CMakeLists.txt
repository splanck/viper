add_subdirectory(support)

#===----------------------------------------------------------------------===//
# rtgen: Runtime registry generator
#===----------------------------------------------------------------------===//
# Build the generator tool that creates .inc files from runtime.def
add_executable(rtgen tools/rtgen/rtgen.cpp)
target_compile_features(rtgen PRIVATE cxx_std_17)

# Generate runtime registry files
set(RTGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/il/runtime")
set(RTGEN_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/il/runtime/runtime.def")
set(RTGEN_OUTPUTS
        "${RTGEN_OUTPUT_DIR}/RuntimeNameMap.inc"
        "${RTGEN_OUTPUT_DIR}/RuntimeClasses.inc"
        "${RTGEN_OUTPUT_DIR}/RuntimeSignatures.inc"
        "${RTGEN_OUTPUT_DIR}/RuntimeNames.hpp"
        "${RTGEN_OUTPUT_DIR}/ZiaRuntimeExterns.inc"
)

add_custom_command(
        OUTPUT ${RTGEN_OUTPUTS}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RTGEN_OUTPUT_DIR}"
        COMMAND rtgen "${RTGEN_INPUT}" "${RTGEN_OUTPUT_DIR}"
        DEPENDS rtgen "${RTGEN_INPUT}"
        COMMENT "Generating runtime registry files from runtime.def"
        VERBATIM
)

add_custom_target(generate_runtime_registry DEPENDS ${RTGEN_OUTPUTS})
#===----------------------------------------------------------------------===//

add_library(viper_common STATIC
        common/RunProcess.cpp)
target_include_directories(viper_common PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
target_link_libraries(viper_common PRIVATE viper_common_opts)

add_library(viper_pass STATIC
        pass/PassManager.cpp)
target_include_directories(viper_pass PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
target_link_libraries(viper_pass PRIVATE viper_common_opts)

add_subdirectory(codegen/common)
add_subdirectory(codegen/x86_64)
add_subdirectory(codegen/aarch64)

add_library(viper_il_core STATIC
        il/core/OpcodeInfo.cpp
        il/core/OpcodeNames.cpp
        il/core/Type.cpp
        il/core/Value.cpp
        il/core/Instr.cpp
        il/core/BasicBlock.cpp
        il/core/Function.cpp
        il/core/Global.cpp
        il/core/Extern.cpp
        il/core/Module.cpp
)
target_include_directories(viper_il_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
)
target_link_libraries(viper_il_core PRIVATE viper_support viper_common_opts)

## Runtime signatures: generated from runtime.def via rtgen

add_library(il_runtime STATIC
        il/runtime/RuntimeSignatures.cpp
        il/runtime/RuntimeSignatures_Handlers.cpp
        il/runtime/RuntimeSignatureParser.cpp
        il/runtime/signatures/Registry.cpp
        il/runtime/signatures/Signatures_FileIO.cpp
        il/runtime/signatures/Signatures_Strings.cpp
        il/runtime/signatures/Signatures_Math.cpp
        il/runtime/signatures/Signatures_Arrays.cpp
        il/runtime/signatures/Signatures_OOP.cpp
        il/runtime/classes/RuntimeClasses.cpp)
add_dependencies(il_runtime generate_runtime_registry)
target_link_libraries(il_runtime PUBLIC viper_il_core viper_runtime PRIVATE viper_common_opts)
target_compile_definitions(il_runtime PUBLIC VIPER_RUNTIME_NS_DUAL=$<BOOL:${VIPER_RUNTIME_NS_DUAL}>)
target_include_directories(il_runtime PUBLIC
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)
# Increase constexpr step limit for large descriptor table in RuntimeSignatures.cpp
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    set_source_files_properties(il/runtime/RuntimeSignatures.cpp
            PROPERTIES COMPILE_FLAGS "-fconstexpr-steps=100000000")
elseif (MSVC)
    set_source_files_properties(il/runtime/RuntimeSignatures.cpp
            PROPERTIES COMPILE_FLAGS "/constexpr:steps100000000")
endif ()

add_library(il_build STATIC il/build/IRBuilder.cpp)
target_link_libraries(il_build
        PUBLIC
        viper_il_core
        PRIVATE
        viper_support
        viper_common_opts)
target_include_directories(il_build PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_subdirectory(il/io)

add_library(viper_il_verify STATIC
        il/verify/DiagFormat.cpp
        il/verify/DiagSink.cpp
        il/verify/EhChecks.cpp
        il/verify/EhModel.cpp
        il/verify/EhVerifier.cpp
        il/verify/Verifier.cpp
        il/verify/ExternVerifier.cpp
        il/verify/GlobalVerifier.cpp
        il/verify/FunctionVerifier.cpp
        il/verify/InstructionCheckUtils.cpp
        il/verify/InstructionChecker.cpp
        il/verify/generated/SpecTables.cpp
        il/verify/InstructionChecker_Arithmetic.cpp
        il/verify/InstructionChecker_Memory.cpp
        il/verify/InstructionChecker_Runtime.cpp
        il/verify/OperandCountChecker.cpp
        il/verify/OperandTypeChecker.cpp
        il/verify/ResultTypeChecker.cpp
        il/verify/InstructionStrategies.cpp
        il/verify/ExceptionHandlerAnalysis.cpp
        il/verify/BranchVerifier.cpp
        il/verify/ControlFlowChecker.cpp
        il/verify/VerifierTable.cpp
        il/verify/TypeInference.cpp)
target_link_libraries(viper_il_verify PRIVATE viper_il_core il_runtime viper_common_opts)
target_include_directories(viper_il_verify PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_library(viper_il_full INTERFACE)
target_link_libraries(viper_il_full INTERFACE
        viper_il_core
        viper_il_verify
        viper_il_io
        viper_support
)

add_library(viper::support ALIAS viper_support)
add_library(viper::il_core ALIAS viper_il_core)
add_library(viper::il_verify ALIAS viper_il_verify)
add_library(viper::il_io ALIAS viper_il_io)
add_library(viper::il_full ALIAS viper_il_full)

add_library(il_api STATIC il/api/expected_api.cpp)
target_link_libraries(il_api
        PUBLIC viper_il_full
        PRIVATE viper_common_opts)
target_include_directories(il_api PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_library(il_analysis STATIC
        il/analysis/CFG.cpp
        il/analysis/CallGraph.cpp
        il/analysis/Dominators.cpp
        il/analysis/MemorySSA.cpp)
target_link_libraries(il_analysis PUBLIC viper_il_core PRIVATE viper_common_opts)
target_include_directories(il_analysis PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_library(il_transform STATIC
        il/transform/AnalysisManager.cpp
        il/transform/PassManager.cpp
        il/transform/PassRegistry.cpp
        il/transform/PipelineExecutor.cpp
        il/transform/CheckOpt.cpp
        il/transform/LateCleanup.cpp
        il/transform/Inline.cpp
        il/transform/Peephole.cpp
        il/transform/ConstFold.cpp
        il/transform/DCE.cpp
        il/transform/Mem2Reg.cpp
        il/transform/EarlyCSE.cpp
        il/transform/DSE.cpp
        il/transform/LoopSimplify.cpp
        il/transform/LICM.cpp
        il/transform/IndVarSimplify.cpp
        il/transform/LoopUnroll.cpp
        il/transform/GVN.cpp
        il/transform/ValueKey.cpp
        il/transform/SCCP.cpp
        il/transform/SiblingRecursion.cpp
        il/transform/SimplifyCFG.cpp
        il/transform/SimplifyCFG/BlockMerging.cpp
        il/transform/SimplifyCFG/BranchFolding.cpp
        il/transform/SimplifyCFG/ForwardingElimination.cpp
        il/transform/SimplifyCFG/JumpThreading.cpp
        il/transform/SimplifyCFG/ParamCanonicalization.cpp
        il/transform/SimplifyCFG/PassContext.cpp
        il/transform/SimplifyCFG/ReachabilityCleanup.cpp
        il/transform/SimplifyCFG/Utils.cpp
        il/transform/analysis/Liveness.cpp
        il/transform/analysis/LoopInfo.cpp)
target_link_libraries(il_transform
        PUBLIC
        viper_il_core
        viper_pass
        PRIVATE
        viper_il_verify
        il_analysis
        il_utils
        viper_common_opts)
target_include_directories(il_transform PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_library(il_utils STATIC
        il/utils/Utils.cpp
        il/utils/UseDefInfo.cpp)
target_link_libraries(il_utils PUBLIC viper_il_core PRIVATE viper_common_opts)
target_include_directories(il_utils PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_library(il_link STATIC
        il/link/ModuleLinker.cpp
        il/link/InteropThunks.cpp)
target_link_libraries(il_link PUBLIC viper_il_core PRIVATE viper_common_opts)
target_include_directories(il_link PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_library(il_tools_common STATIC
        tools/common/module_loader.cpp
        tools/common/source_loader.cpp
        tools/common/project_loader.cpp
        tools/common/vm_executor.cpp
)
target_link_libraries(il_tools_common PUBLIC il_api viper_bytecode PRIVATE viper_common_opts)
target_include_directories(il_tools_common PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

add_library(il_vm STATIC
        vm/Runner.cpp
        vm/VM.cpp
        vm/DispatchStrategy.cpp
        vm/tco.cpp
        vm/FunctionExecCache.cpp
        vm/debug/VM_DebugUtils.cpp
        # VMInit.cpp is included directly in Runner.cpp to avoid Windows static lib linking issues
        vm/VMContext.cpp
        vm/ThreadsRuntime.cpp
        vm/debug/VMDebug.cpp
        vm/RuntimeBridge.cpp
        vm/Marshal.cpp
        vm/DiagFormat.cpp
        vm/OpHandlers.cpp
        vm/OpHandlerUtils.cpp
        vm/OpHelpers.cpp
        vm/Trap.cpp
        vm/err_bridge.cpp
        vm/debug/Debug.cpp
        vm/debug/DebugScript.cpp
        vm/debug/Trace.cpp
        vm/mem_ops.cpp
        vm/int_ops_arith.cpp
        vm/int_ops_cmp.cpp
        vm/int_ops_convert.cpp
        vm/fp_ops.cpp
        vm/control_flow.cpp
        vm/ops/common/Branching.cpp
        vm/ops/Op_BranchSwitch.cpp
        vm/ops/Op_CallRet.cpp
        vm/ops/Op_TrapEh.cpp)
target_include_directories(il_vm PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)
target_link_libraries(il_vm
        PUBLIC
        viper_il_core
        il_runtime
        viper_runtime
        PRIVATE
        viper_support
        viper_common_opts)

# Enable tail-call optimisation across the VM by default.
option(IL_VM_ENABLE_TAILCALL "Enable VM tail-call optimization by default" ON)
if (IL_VM_ENABLE_TAILCALL)
    target_compile_definitions(il_vm PUBLIC VIPER_VM_TAILCALL=1)
endif ()

if (VIPER_VM_THREADED)
    target_compile_definitions(il_vm PUBLIC VIPER_VM_THREADED=1)
endif ()

# Bytecode VM - fast interpreter using compact bytecode format
add_library(viper_bytecode STATIC
        bytecode/Bytecode.cpp
        bytecode/BytecodeCompiler.cpp
        bytecode/BytecodeVM.cpp)
target_include_directories(viper_bytecode PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
target_link_libraries(viper_bytecode
        PUBLIC viper_il_core
        PRIVATE viper_support il_vm viper_common_opts)

add_library(il_codegen_x86_64 STATIC
        codegen/x86_64/AsmEmitter.cpp
        codegen/x86_64/Backend.cpp
        codegen/x86_64/CallLowering.cpp
        codegen/x86_64/FrameLowering.cpp
        codegen/x86_64/ISel.cpp
        codegen/x86_64/LowerDiv.cpp
        codegen/x86_64/LowerILToMIR.cpp
        codegen/x86_64/MachineIR.cpp
        codegen/x86_64/Peephole.cpp
        codegen/x86_64/RegAllocLinear.cpp
        codegen/x86_64/ra/Allocator.cpp
        codegen/x86_64/ra/Coalescer.cpp
        codegen/x86_64/ra/LiveIntervals.cpp
        codegen/x86_64/ra/Spiller.cpp
        codegen/x86_64/TargetX64.cpp
        common/Mangle.cpp)
set_target_properties(il_codegen_x86_64 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(il_codegen_x86_64 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
target_link_libraries(il_codegen_x86_64 PRIVATE viper_common_opts)
add_dependencies(il_codegen_x86_64 generate_runtime_registry)

add_library(viper_cmd_arm64 STATIC
        tools/viper/cmd_codegen_arm64.cpp)
target_link_libraries(viper_cmd_arm64 PRIVATE viper_common_opts viper_common viper_codegen_common il_tools_common il_codegen_aarch64 il_transform viper::il_full)

add_library(viper_native_compiler STATIC
        tools/common/native_compiler.cpp)
target_link_libraries(viper_native_compiler
        PUBLIC il_tools_common
        PRIVATE viper_codegen_x86_64 viper_cmd_arm64 viper_common)
target_include_directories(viper_native_compiler PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# Build viper (CLI) sources list conditionally based on enabled frontends
set(VIPER_CLI_SOURCES
        tools/viper/main.cpp
        tools/viper/cmd_run.cpp
        tools/viper/cmd_run_il.cpp
        tools/viper/cmd_front_basic.cpp
        tools/viper/cmd_front_zia.cpp
        tools/viper/cmd_il_opt.cpp
        tools/viper/cmd_codegen_x64.cpp
        tools/viper/cmd_bench.cpp
        tools/viper/cmd_init.cpp
        tools/viper/cli.cpp
        tools/viper/break_spec.cpp)

set(VIPER_CLI_LINK_LIBS
        viper_common_opts
        viper::il_full
        il_vm
        il_transform
        il_link
        fe_basic
        fe_zia
        il_api
        il_tools_common
        viper_native_compiler
        viper_codegen_x86_64
        il_codegen_aarch64
        viper_cmd_arm64
        viper_bytecode)

add_executable(viper ${VIPER_CLI_SOURCES})
set_target_properties(viper PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/viper)
target_link_libraries(viper PRIVATE ${VIPER_CLI_LINK_LIBS})
get_target_property(VIPER_LINK_LIBS viper LINK_LIBRARIES)
message(STATUS "viper LINK_LIBRARIES: ${VIPER_LINK_LIBS}")
install(TARGETS viper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(vbasic
        tools/vbasic/main.cpp
        tools/vbasic/usage.cpp
        tools/vbasic/cli_compat.cpp
        tools/viper/cmd_front_basic.cpp
        tools/viper/cli.cpp)
set_target_properties(vbasic PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/vbasic)
target_link_libraries(vbasic
        PRIVATE
        viper_common_opts
        viper::il_full
        il_vm
        il_transform
        fe_basic
        il_api
        il_tools_common
        viper_native_compiler
        viper_codegen_x86_64
        il_codegen_aarch64
        viper_cmd_arm64
        viper_common
        viper_bytecode)
install(TARGETS vbasic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(zia
        tools/zia/main.cpp
        tools/zia/usage.cpp
        tools/zia/cli_compat.cpp
        tools/viper/cmd_front_zia.cpp
        tools/viper/cli.cpp)
set_target_properties(zia PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/zia)
target_link_libraries(zia
        PRIVATE
        viper_common_opts
        viper::il_full
        il_vm
        il_transform
        fe_zia
        il_api
        il_tools_common
        viper_native_compiler
        viper_codegen_x86_64
        il_codegen_aarch64
        viper_cmd_arm64
        viper_common
        viper_bytecode)
install(TARGETS zia RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(ilrun
        tools/ilrun/main.cpp
        tools/ilrun/cli_compat.cpp
        tools/viper/cmd_run_il.cpp
        tools/viper/cli.cpp
        tools/viper/break_spec.cpp)
set_target_properties(ilrun PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/ilrun)
target_link_libraries(ilrun
        PRIVATE
        viper_common_opts
        viper::il_full
        il_vm
        il_api
        il_tools_common
        viper_bytecode)
install(TARGETS ilrun RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(il-verify tools/il-verify/il-verify.cpp)
set_target_properties(il-verify PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-verify)
target_link_libraries(il-verify
        PRIVATE
        viper_common_opts
        viper::il_full
        il_build
        il_vm
        il_api
        il_tools_common)
install(TARGETS il-verify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(il-dis tools/il-dis/main.cpp)
set_target_properties(il-dis PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/il-dis)
target_link_libraries(il-dis
        PRIVATE
        viper_common_opts
        viper::il_full
        il_build)
install(TARGETS il-dis RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_subdirectory(frontends/common)
add_subdirectory(frontends/basic)
add_subdirectory(frontends/zia)

add_executable(basic-ast-dump
        tools/basic-ast-dump/main.cpp
        tools/basic/common.cpp)
set_target_properties(basic-ast-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/basic-ast-dump)
target_link_libraries(basic-ast-dump PRIVATE fe_basic)
target_compile_definitions(basic-ast-dump PRIVATE VIPER_BASIC_TOOL_USAGE="Usage: basic-ast-dump <file.bas>\\n")
add_executable(basic-lex-dump
        tools/basic-lex-dump/main.cpp
        tools/basic/common.cpp)
set_target_properties(basic-lex-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tools/basic-lex-dump)
target_link_libraries(basic-lex-dump PRIVATE fe_basic)
target_compile_definitions(basic-lex-dump PRIVATE VIPER_BASIC_TOOL_USAGE="Usage: basic-lex-dump <file.bas>\\n")

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/support/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/support
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/core/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/core
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.def"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/build/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/build
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/io/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/io
        FILES_MATCHING PATTERN "*.hpp"
)

install(FILES ${PROJECT_SOURCE_DIR}/include/viper/il/io/FormatUtils.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/io)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/viper/pass/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/pass
        FILES_MATCHING PATTERN "*.hpp")

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/verify/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/verify
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/analysis/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/analysis
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/transform/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/transform
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/runtime/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/runtime
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/api/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/api
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/il/utils/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/utils
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vm/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/vm
        FILES_MATCHING PATTERN "*.hpp"
        PATTERN "VM.hpp" EXCLUDE
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/viper/vm/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/vm
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/frontends/common/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/frontends/common
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/frontends/basic/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/frontends/basic
        FILES_MATCHING PATTERN "*.hpp"
)
