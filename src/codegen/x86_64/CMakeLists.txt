add_library(viper_codegen_x86_64 STATIC
        TargetX64.cpp
        MachineIR.cpp
        AsmEmitter.cpp
        ../../common/Mangle.cpp
        asmfmt/Format.cpp
        RegAllocLinear.cpp
        ra/Allocator.cpp
        ra/Coalescer.cpp
        ra/LiveIntervals.cpp
        ra/Spiller.cpp
        FrameLowering.cpp
        CallLowering.cpp
        ISel.cpp
        LowerDiv.cpp
        LowerILToMIR.cpp
        LoweringRuleTable.cpp
        Lowering.EmitCommon.cpp
        Lowering.Arith.cpp
        Lowering.Bitwise.cpp
        Lowering.Mem.cpp
        Lowering.EH.cpp
        Lowering.CF.cpp
        LoweringRules.cpp
        Peephole.cpp
        Backend.cpp
        CodegenPipeline.cpp
        passes/PassManager.cpp
        passes/LoweringPass.cpp
        passes/LegalizePass.cpp
        passes/RegAllocPass.cpp
        passes/EmitPass.cpp
)

target_include_directories(viper_codegen_x86_64 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
)

target_link_libraries(viper_codegen_x86_64 PUBLIC viper_common)

set_target_properties(viper_codegen_x86_64 PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

# --- Generate encoding tables from JSON (no Python dependency) ---
set(VIPER_X64_SPEC "${PROJECT_SOURCE_DIR}/docs/spec/x86_64_encodings.json")
set(VIPER_X64_OUTDIR "${PROJECT_SOURCE_DIR}/src/codegen/x86_64/generated")
set(VIPER_X64_ENC "${VIPER_X64_OUTDIR}/EncodingTable.inc")
set(VIPER_X64_OPFMT "${VIPER_X64_OUTDIR}/OpFmtTable.inc")

add_custom_command(
        OUTPUT ${VIPER_X64_ENC} ${VIPER_X64_OPFMT}
        COMMAND ${CMAKE_COMMAND}
        -DSPEC_FILE=${VIPER_X64_SPEC}
        -DOUTPUT_DIR=${VIPER_X64_OUTDIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/GenX86Encodings.cmake
        DEPENDS ${VIPER_X64_SPEC} ${PROJECT_SOURCE_DIR}/cmake/GenX86Encodings.cmake
        COMMENT "Generating x86_64 encoding tables from JSON"
        VERBATIM)

add_custom_target(gen_x86_64_encodings DEPENDS ${VIPER_X64_ENC} ${VIPER_X64_OPFMT})
add_dependencies(viper_codegen_x86_64 gen_x86_64_encodings)
