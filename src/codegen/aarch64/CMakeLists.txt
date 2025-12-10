add_library(il_codegen_aarch64 STATIC
        TargetAArch64.cpp
        AsmEmitter.cpp
        RodataPool.cpp
        FrameBuilder.cpp
        MachineIR.cpp
        LowerILToMIR.cpp
        LivenessAnalysis.cpp
        TerminatorLowering.cpp
        InstrLowering.cpp
        OpcodeDispatch.cpp
        FastPaths.cpp
        fastpaths/FastPaths_Arithmetic.cpp
        fastpaths/FastPaths_Call.cpp
        fastpaths/FastPaths_Cast.cpp
        fastpaths/FastPaths_Memory.cpp
        fastpaths/FastPaths_Return.cpp
        RegAllocLinear.cpp
        Peephole.cpp)
target_include_directories(il_codegen_aarch64 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
set_target_properties(il_codegen_aarch64 PROPERTIES LINKER_LANGUAGE CXX)

# --- Generate OpcodeDispatch from JSON (no Python dependency) ---
set(VIPER_A64_SPEC "${PROJECT_SOURCE_DIR}/docs/spec/aarch64_encodings.json")
set(VIPER_A64_OUTDIR "${PROJECT_SOURCE_DIR}/src/codegen/aarch64/generated")
set(VIPER_A64_DISP "${VIPER_A64_OUTDIR}/OpcodeDispatch.inc")

add_custom_command(
        OUTPUT ${VIPER_A64_DISP}
        COMMAND ${CMAKE_COMMAND}
        -DSPEC_FILE=${VIPER_A64_SPEC}
        -DOUTPUT_FILE=${VIPER_A64_DISP}
        -P ${PROJECT_SOURCE_DIR}/cmake/GenAArch64Dispatch.cmake
        DEPENDS ${VIPER_A64_SPEC} ${PROJECT_SOURCE_DIR}/cmake/GenAArch64Dispatch.cmake
        COMMENT "Generating AArch64 OpcodeDispatch.inc from JSON"
        VERBATIM)

add_custom_target(gen_aarch64_dispatch DEPENDS ${VIPER_A64_DISP})
add_dependencies(il_codegen_aarch64 gen_aarch64_dispatch)
