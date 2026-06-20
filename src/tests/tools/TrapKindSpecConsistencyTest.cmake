cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED VIPER_SOURCE_DIR)
    message(FATAL_ERROR "VIPER_SOURCE_DIR must be provided to TrapKindSpecConsistencyTest.cmake")
endif ()

set(_trap_header "${VIPER_SOURCE_DIR}/src/vm/Trap.hpp")
set(_errors_spec "${VIPER_SOURCE_DIR}/docs/specs/errors.md")
set(_spec_dir "${VIPER_SOURCE_DIR}/docs/specs")

foreach (_required IN ITEMS "${_trap_header}" "${_errors_spec}")
    if (NOT EXISTS "${_required}")
        message(FATAL_ERROR "Required file is missing: ${_required}")
    endif ()
endforeach ()

file(STRINGS "${_trap_header}" _trap_lines
        REGEX "^[ \t]*[A-Za-z][A-Za-z0-9_]*[ \t]*=[ \t]*[0-9]+")

set(_trap_entries)
foreach (_line IN LISTS _trap_lines)
    string(REGEX REPLACE
            "^[ \t]*([A-Za-z][A-Za-z0-9_]*)[ \t]*=[ \t]*([0-9]+).*"
            "\\1=\\2"
            _entry
            "${_line}")
    list(APPEND _trap_entries "${_entry}")
endforeach ()
list(SORT _trap_entries)

file(STRINGS "${_errors_spec}" _spec_lines
        REGEX "^\\|[ \t]*`[A-Za-z][A-Za-z0-9_]*`[ \t]*\\|[ \t]*[0-9]+[ \t]*\\|")

set(_spec_entries)
foreach (_line IN LISTS _spec_lines)
    string(REGEX REPLACE
            "^\\|[ \t]*`([A-Za-z][A-Za-z0-9_]*)`[ \t]*\\|[ \t]*([0-9]+)[ \t]*\\|.*"
            "\\1=\\2"
            _entry
            "${_line}")
    if (_entry MATCHES "^Err_")
        continue()
    endif ()
    list(APPEND _spec_entries "${_entry}")
endforeach ()
list(SORT _spec_entries)

list(JOIN _trap_entries "\n  " _trap_pretty)
list(JOIN _spec_entries "\n  " _spec_pretty)
if (NOT "${_trap_pretty}" STREQUAL "${_spec_pretty}")
    message(FATAL_ERROR
            "docs/specs/errors.md Trap Kinds table does not match src/vm/Trap.hpp.\n"
            "Trap.hpp:\n  ${_trap_pretty}\n"
            "errors.md:\n  ${_spec_pretty}")
endif ()

file(GLOB _spec_files LIST_DIRECTORIES FALSE "${_spec_dir}/*.md")
set(_draft_specs)
foreach (_spec IN LISTS _spec_files)
    file(STRINGS "${_spec}" _draft_lines REGEX "^status:[ \t]*draft[ \t]*$")
    if (_draft_lines)
        list(APPEND _draft_specs "${_spec}")
    endif ()
endforeach ()

if (_draft_specs)
    list(JOIN _draft_specs "\n  " _draft_message)
    message(FATAL_ERROR "Normative docs/specs files must not remain draft:\n  ${_draft_message}")
endif ()

message(STATUS "TrapKind spec table matches runtime enum; docs/specs are active.")
