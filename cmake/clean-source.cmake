## Clean source-generated artifacts that are not covered by the build-system clean.

# Current known generated files in the source tree.
set(_files
  "${CMAKE_SOURCE_DIR}/break.txt"
)

foreach(_f IN LISTS _files)
  if(EXISTS "${_f}")
    file(REMOVE "${_f}")
  endif()
endforeach()

# Optionally clean common temporary directories created by tests or examples in-tree.
set(_dirs)
foreach(_d IN LISTS _dirs)
  if(EXISTS "${_d}")
    file(REMOVE_RECURSE "${_d}")
  endif()
endforeach()

message(STATUS "Cleaned source-generated artifacts (clean-source.cmake)")

