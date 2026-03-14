add_library(viper_codegen_common STATIC
        Diagnostics.cpp
        LinkerSupport.cpp
        objfile/ObjectFileWriter.cpp
        objfile/ElfWriter.cpp
        objfile/MachOWriter.cpp
        objfile/CoffWriter.cpp
        objfile/StringTable.cpp
        objfile/SymbolTable.cpp
        linker/ArchiveReader.cpp
        linker/ObjFileReader.cpp
        linker/ElfReader.cpp
        linker/MachOReader.cpp
        linker/CoffReader.cpp
        linker/SymbolResolver.cpp
        linker/DeadStripPass.cpp
        linker/StringDedup.cpp
        linker/SectionMerger.cpp
        linker/RelocApplier.cpp
        linker/ElfExeWriter.cpp
        linker/MachOCodeSign.cpp
        linker/MachOBindRebase.cpp
        linker/MachOExeWriter.cpp
        linker/PeExeWriter.cpp
        linker/DynStubGen.cpp
        linker/NativeLinker.cpp)
target_include_directories(viper_codegen_common PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>)
target_link_libraries(viper_codegen_common PUBLIC viper_common PRIVATE viper_common_opts)
set_target_properties(viper_codegen_common PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)
