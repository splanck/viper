cmake_minimum_required(VERSION 3.20)

foreach (_required ZANNA_BIN ZANNA_REPO_ROOT TEST_WORK_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to MacOSPackageSmokeTests.cmake")
    endif ()
endforeach ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

set(_zip "${TEST_WORK_DIR}/crackman-signed.zip")
set(_project "${ZANNA_REPO_ROOT}/examples/games/crackman")
set(_package_icon "${_project}/packaging/icon.png")

if (NOT EXISTS "${_package_icon}")
    message(FATAL_ERROR "Crackman package icon is missing: ${_package_icon}")
endif ()

execute_process(
        COMMAND "${ZANNA_BIN}" package "${_project}" --target macos --macos-sign-mode adhoc -o "${_zip}" --verbose
        RESULT_VARIABLE _pkg_rv
        OUTPUT_VARIABLE _pkg_out
        ERROR_VARIABLE _pkg_err
        TIMEOUT 180)
if (NOT _pkg_rv EQUAL 0)
    message(FATAL_ERROR "Crackman macOS packaging failed\nstdout:\n${_pkg_out}\nstderr:\n${_pkg_err}")
endif ()
if (NOT EXISTS "${_zip}")
    message(FATAL_ERROR "Crackman package was not created at ${_zip}")
endif ()
file(SIZE "${_zip}" _zip_size)
if (NOT _zip_size GREATER 0)
    message(FATAL_ERROR "Crackman package is empty: ${_zip}")
endif ()

set(_extract "${TEST_WORK_DIR}/extract")
file(MAKE_DIRECTORY "${_extract}")
execute_process(
        COMMAND /usr/bin/unzip -q "${_zip}" -d "${_extract}"
        RESULT_VARIABLE _unzip_rv
        OUTPUT_VARIABLE _unzip_out
        ERROR_VARIABLE _unzip_err)
if (NOT _unzip_rv EQUAL 0)
    message(FATAL_ERROR "failed to extract Crackman package\nstdout:\n${_unzip_out}\nstderr:\n${_unzip_err}")
endif ()

set(_app "${_extract}/Crackman.app")
set(_exe "${_app}/Contents/MacOS/crackman")
set(_resources "${_app}/Contents/Resources")
foreach (_path
        "${_app}/Contents/Info.plist"
        "${_app}/Contents/PkgInfo"
        "${_exe}"
        "${_app}/Contents/_CodeSignature/CodeResources"
        "${_resources}/crackman.icns"
        "${_resources}/assets/fonts/zanna_8x8.bdf")
    if (NOT EXISTS "${_path}")
        message(FATAL_ERROR "expected packaged Crackman path missing: ${_path}")
    endif ()
endforeach ()

if (EXISTS "${_resources}/assets/icon.png")
    message(FATAL_ERROR "packaging-only source icon was incorrectly bundled as a runtime asset")
endif ()
file(SIZE "${_resources}/crackman.icns" _icns_size)
if (NOT _icns_size GREATER 0)
    message(FATAL_ERROR "generated Crackman ICNS is empty")
endif ()

execute_process(
        COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2 "${_app}"
        RESULT_VARIABLE _codesign_rv
        OUTPUT_VARIABLE _codesign_out
        ERROR_VARIABLE _codesign_err)
if (NOT _codesign_rv EQUAL 0)
    message(FATAL_ERROR "extracted Crackman.app failed codesign verification\nstdout:\n${_codesign_out}\nstderr:\n${_codesign_err}")
endif ()

execute_process(
        COMMAND /usr/bin/codesign --display -vvv "${_app}"
        RESULT_VARIABLE _display_rv
        OUTPUT_VARIABLE _display_out
        ERROR_VARIABLE _display_err)
if (NOT _display_rv EQUAL 0)
    message(FATAL_ERROR "codesign display failed\nstdout:\n${_display_out}\nstderr:\n${_display_err}")
endif ()
if (NOT _display_err MATCHES "Signature=adhoc")
    message(FATAL_ERROR "Crackman.app was not ad-hoc signed\nstdout:\n${_display_out}\nstderr:\n${_display_err}")
endif ()
if (NOT _display_err MATCHES "Sealed Resources")
    message(FATAL_ERROR "Crackman.app did not report sealed resources\nstdout:\n${_display_out}\nstderr:\n${_display_err}")
endif ()
