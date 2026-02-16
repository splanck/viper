# SPDX-License-Identifier: GPL-3.0-only
# Viper common frontend library
#
# This library provides shared infrastructure used by all language frontends
# (BASIC, Zia, and future languages). It includes:
#   - ExprResult: Common expression result type
#   - BlockManager: Basic block creation and tracking
#   - InstructionEmitter: IL instruction emission helpers
#   - LoopContext: Loop context management for break/continue
#   - StringTable: String literal interning
#   - StringHash: Heterogeneous string hashing utilities

# Header-only library (no .cpp files yet)
add_library(fe_common INTERFACE)

target_include_directories(fe_common INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

# Link to core IL libraries needed by the headers
target_link_libraries(fe_common INTERFACE
        viper_il_core
        il_build
)

# Alias for consistent naming
add_library(viper::fe_common ALIAS fe_common)
