add_library(viper_codegen_x86_64 STATIC
    TargetX64.cpp
    MachineIR.cpp
    AsmEmitter.cpp
    RegAllocLinear.cpp
    FrameLowering.cpp
    CallLowering.cpp
    ISel.cpp
    LowerDiv.cpp
    LowerILToMIR.cpp
    Peephole.cpp
    Backend.cpp
    placeholder.cpp
)

target_include_directories(viper_codegen_x86_64 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(viper_codegen_x86_64 PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
