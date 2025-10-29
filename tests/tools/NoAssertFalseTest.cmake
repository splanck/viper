cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED VIPER_SOURCE_DIR)
    message(FATAL_ERROR "VIPER_SOURCE_DIR must be provided to NoAssertFalseTest.cmake")
endif ()

set(_no_assert_false_globs
        "${VIPER_SOURCE_DIR}/src/frontends/basic/*.cpp"
        "${VIPER_SOURCE_DIR}/src/frontends/basic/*.hpp"
        "${VIPER_SOURCE_DIR}/src/vm/RuntimeBridge.cpp"
)

set(_no_assert_false_offenders)

set(_no_assert_false_allow_strings
        "string default values are not supported"
        "builtin lowering referenced missing argument without default"
        "unsupported custom builtin conversion"
)

foreach (_pattern IN LISTS _no_assert_false_globs)
    file(GLOB _no_assert_false_files LIST_DIRECTORIES FALSE ${_pattern})
    foreach (_file IN LISTS _no_assert_false_files)
        file(STRINGS "${_file}" _no_assert_false_lines REGEX "assert\\s*\\(\\s*false")
        foreach (_line IN LISTS _no_assert_false_lines)
            set(_line_allowed FALSE)
            foreach (_allowed IN LISTS _no_assert_false_allow_strings)
                string(FIND "${_line}" "${_allowed}" _found_index)
                if (NOT _found_index EQUAL -1)
                    set(_line_allowed TRUE)
                    break()
                endif ()
            endforeach ()
            if (NOT _line_allowed)
                list(APPEND _no_assert_false_offenders "${_file}")
                break()
            endif ()
        endforeach ()
    endforeach ()
endforeach ()

if (_no_assert_false_offenders)
    list(REMOVE_DUPLICATES _no_assert_false_offenders)
    list(SORT _no_assert_false_offenders)
    list(JOIN _no_assert_false_offenders "\n  " _no_assert_false_message)
    message(FATAL_ERROR "Found forbidden 'assert(false)' usage in:\n  ${_no_assert_false_message}\nReplace with appropriate error handling before committing.")
else ()
    message(STATUS "No forbidden assert(false) occurrences detected.")
endif ()
