add_library(il_codegen_aarch64 STATIC
        TargetAArch64.cpp
        AsmEmitter.cpp
        MachineIR.cpp)
target_include_directories(il_codegen_aarch64 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
set_target_properties(il_codegen_aarch64 PROPERTIES LINKER_LANGUAGE CXX)
