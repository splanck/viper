# SPDX-License-Identifier: MIT
# File: tests/smoke/CMakeLists.txt
# Purpose: CLI smoke tests for terminal BASIC programs.

add_test(NAME smoke_term_basic
         COMMAND $<TARGET_FILE:ilc> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/term_basic.bas)
