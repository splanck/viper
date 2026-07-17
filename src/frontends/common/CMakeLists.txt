# SPDX-License-Identifier: GPL-3.0-only
# Zanna common frontend library
#
# This library provides shared infrastructure used by all language frontends
# (BASIC, Zia, and future languages). It includes:
#   - ExprResult: Common expression result type
#   - BlockManager: Basic block creation and tracking
#   - InstructionEmitter: IL instruction emission helpers
#   - LoopContext: Loop context management for break/continue
#   - StringTable: String literal interning
#   - StringHash: Heterogeneous string hashing utilities

add_library(fe_common STATIC
        CollectionMethodCatalog.cpp
        RuntimeMethodResolver.cpp
)

target_compile_features(fe_common PUBLIC cxx_std_20)

target_include_directories(fe_common PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/zanna>
)

# Link to core IL libraries needed by the headers
target_link_libraries(fe_common PUBLIC
        zanna_il_core
        il_build
        il_runtime
)

# Alias for consistent naming
add_library(zanna::fe_common ALIAS fe_common)
