add_library(support STATIC
  string_interner.cpp
  source_manager.cpp
  source_location.cpp
  symbol.cpp
  diagnostics.cpp
  arena.cpp
  diag_expected.cpp
  diag_capture.cpp
)

target_include_directories(support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
