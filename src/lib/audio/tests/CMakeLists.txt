# ViperAUD CTests

add_executable(test_vaud_audit_fixes
        test_vaud_audit_fixes.c
        ../src/vaud_wav.c
)

target_include_directories(test_vaud_audit_fixes PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

if (UNIX AND NOT APPLE)
    target_link_libraries(test_vaud_audit_fixes PRIVATE m)
endif ()

add_test(NAME test_vaud_audit_fixes COMMAND test_vaud_audit_fixes)
set_tests_properties(test_vaud_audit_fixes PROPERTIES LABELS "unit;audio" TIMEOUT 10)
