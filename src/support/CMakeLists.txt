add_library(support STATIC
  string_interner.cpp
  source_manager.cpp
  diagnostics.cpp
  arena.cpp
  diag_expected.cpp
)

target_include_directories(support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
