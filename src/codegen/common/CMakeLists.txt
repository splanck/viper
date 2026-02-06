add_library(viper_codegen_common STATIC
        Diagnostics.cpp
        LinkerSupport.cpp)
target_include_directories(viper_codegen_common PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
target_link_libraries(viper_codegen_common PUBLIC viper_common)
set_target_properties(viper_codegen_common PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)
