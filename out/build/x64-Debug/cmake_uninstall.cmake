if(NOT EXISTS "C:/git/viper/out/build/x64-Debug/install_manifest.txt")
  message(FATAL_ERROR "Cannot find install_manifest.txt. Run install first.")
endif()

file(READ "C:/git/viper/out/build/x64-Debug/install_manifest.txt" _files)
string(REGEX REPLACE "\n$" "" _files "${_files}")
string(REPLACE "\n" ";" _file_list "${_files}")

foreach(file ${_file_list})
  if(EXISTS "${file}")
    message(STATUS "Removing ${file}")
    file(REMOVE "${file}")
  endif()
endforeach()
