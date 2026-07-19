# SPDX-License-Identifier: GPL-3.0-only
# File: tests/il/CMakeLists.txt
# Purpose: IL parser, verifier, and analysis tests.

set(ZANNA_IL_CORE_IO_LIBS zanna_il_full)
set(ZANNA_IL_CORE_IO_LIBS ${ZANNA_IL_CORE_IO_LIBS} PARENT_SCOPE)
set(ZANNA_IL_ANALYSIS_LIBS il_analysis il_build)
set(ZANNA_IL_ANALYSIS_LIBS ${ZANNA_IL_ANALYSIS_LIBS} PARENT_SCOPE)

function(zanna_add_il_core_tests)
    if (TARGET test_il_serialize)
        return()
    endif ()

    set(_ZANNA_IL_UNIT_DIR ${ZANNA_TESTS_DIR}/unit)

    zanna_add_test(test_il_serialize ${_ZANNA_IL_UNIT_DIR}/test_il_serialize.cpp)
    target_link_libraries(test_il_serialize PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build)
    target_compile_definitions(test_il_serialize PRIVATE TESTS_DIR="${ZANNA_TESTS_DIR}")
    zanna_add_ctest(test_il_serialize test_il_serialize)

    zanna_add_test(
            test_il_serialize_negative_zero
            ${_ZANNA_IL_UNIT_DIR}/test_il_serialize_negative_zero.cpp)
    target_link_libraries(
            test_il_serialize_negative_zero PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build)
    zanna_add_ctest(
            test_il_serialize_negative_zero test_il_serialize_negative_zero)

    zanna_add_test(test_il_serialize_opcodes ${_ZANNA_IL_UNIT_DIR}/test_il_serialize_opcodes.cpp)
    target_link_libraries(test_il_serialize_opcodes PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    target_compile_definitions(test_il_serialize_opcodes PRIVATE TESTS_DIR="${ZANNA_TESTS_DIR}")
    zanna_add_ctest(test_il_serialize_opcodes test_il_serialize_opcodes)

    zanna_add_test(test_il_malformed_cbr ${_ZANNA_IL_UNIT_DIR}/test_il_malformed_cbr.cpp)
    target_link_libraries(test_il_malformed_cbr PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_malformed_cbr test_il_malformed_cbr)

    zanna_add_test(test_irbuilder_call_ret ${_ZANNA_IL_UNIT_DIR}/test_irbuilder_call_ret.cpp)
    target_link_libraries(test_irbuilder_call_ret PRIVATE il_build ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_irbuilder_call_ret test_irbuilder_call_ret)

    zanna_add_test(
            test_il_core_memory_effects
            ${_ZANNA_IL_UNIT_DIR}/il/core/test_MemoryEffects.cpp)
    target_link_libraries(test_il_core_memory_effects PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_core_memory_effects test_il_core_memory_effects)

    zanna_add_test(
            test_il_core_structures
            ${_ZANNA_IL_UNIT_DIR}/il/core/test_IRCoreStructures.cpp)
    target_link_libraries(
            test_il_core_structures PRIVATE il_build il_analysis ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_core_structures test_il_core_structures)

    zanna_add_test(test_irbuilder_call_unknown ${_ZANNA_IL_UNIT_DIR}/test_irbuilder_call_unknown.cpp)
    target_link_libraries(test_irbuilder_call_unknown PRIVATE il_build ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_irbuilder_call_unknown test_irbuilder_call_unknown)

    zanna_add_test(
            test_irbuilder_resume_terminators
            ${_ZANNA_IL_UNIT_DIR}/test_irbuilder_resume_terminators.cpp)
    target_link_libraries(test_irbuilder_resume_terminators PRIVATE il_build ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(
            test_irbuilder_resume_terminators test_irbuilder_resume_terminators)

    zanna_add_test(
            test_irbuilder_assertions
            ${_ZANNA_IL_UNIT_DIR}/test_irbuilder_assertions.cpp)
    target_link_libraries(test_irbuilder_assertions PRIVATE il_build ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_irbuilder_assertions test_irbuilder_assertions)

    zanna_add_test(test_il_roundtrip ${_ZANNA_IL_UNIT_DIR}/test_il_roundtrip.cpp)
    target_link_libraries(test_il_roundtrip PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_roundtrip PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples" ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/roundtrip")
    zanna_add_ctest(test_il_roundtrip test_il_roundtrip)

    zanna_add_test(test_il_linkage_roundtrip ${CMAKE_SOURCE_DIR}/src/tests/il/LinkageRoundTrip.cpp)
    target_link_libraries(test_il_linkage_roundtrip PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_linkage_roundtrip test_il_linkage_roundtrip)

    zanna_add_test(test_il_module_linker ${CMAKE_SOURCE_DIR}/src/tests/il/ModuleLinkerTests.cpp)
    target_link_libraries(test_il_module_linker PRIVATE il_link zanna_il_core)
    zanna_add_ctest(test_il_module_linker test_il_module_linker)

    zanna_add_test(test_il_interop_thunks ${CMAKE_SOURCE_DIR}/src/tests/il/InteropThunkTests.cpp)
    target_link_libraries(test_il_interop_thunks PRIVATE il_link zanna_il_core)
    zanna_add_ctest(test_il_interop_thunks test_il_interop_thunks)

    zanna_add_test(test_il_interop_integration ${CMAKE_SOURCE_DIR}/src/tests/il/InteropIntegrationTests.cpp)
    target_link_libraries(test_il_interop_integration PRIVATE il_link zanna_il_core)
    zanna_add_ctest(test_il_interop_integration test_il_interop_integration)

    zanna_add_test(test_il_io_print_parse_roundtrip ${CMAKE_SOURCE_DIR}/src/tests/il/IOPrintParseRoundTrip.cpp)
    target_link_libraries(test_il_io_print_parse_roundtrip PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    set(_ZANNA_IL_FIXTURE_DIRS
            "${CMAKE_SOURCE_DIR}/examples/il"
            "${CMAKE_SOURCE_DIR}/src/tests/il/roundtrip"
            "${CMAKE_SOURCE_DIR}/src/tests/il/e2e"
            "${CMAKE_SOURCE_DIR}/src/tests/data"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/arrays"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/basic_to_il"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/constfold"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/eh_lowering"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/eh_runtime"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/fileio"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/float"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/il"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/il_opt"
            "${CMAKE_SOURCE_DIR}/src/tests/golden/numerics_il")
    list(JOIN _ZANNA_IL_FIXTURE_DIRS "|" _ZANNA_IL_FIXTURE_DIRS_ESCAPED)
    target_compile_definitions(
            test_il_io_print_parse_roundtrip PRIVATE IL_FIXTURE_DIRS="${_ZANNA_IL_FIXTURE_DIRS_ESCAPED}")
    zanna_add_ctest(test_il_io_print_parse_roundtrip test_il_io_print_parse_roundtrip)

    zanna_add_test(test_il_parse_roundtrip ${_ZANNA_IL_UNIT_DIR}/test_il_parse_roundtrip.cpp)
    target_link_libraries(test_il_parse_roundtrip PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_roundtrip PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip" SWITCH_GOLDEN="${CMAKE_SOURCE_DIR}/src/tests/golden/il_opt/switch_basic.il")
    zanna_add_ctest(test_il_parse_roundtrip test_il_parse_roundtrip)

    zanna_add_test(test_il_parse_unresolved_branch ${_ZANNA_IL_UNIT_DIR}/test_il_parse_unresolved_branch.cpp)
    target_link_libraries(test_il_parse_unresolved_branch PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_unresolved_branch PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_unresolved_branch test_il_parse_unresolved_branch)

    zanna_add_test(test_il_parse_param_prefix ${_ZANNA_IL_UNIT_DIR}/test_il_parse_param_prefix.cpp)
    target_link_libraries(test_il_parse_param_prefix PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_param_prefix PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_param_prefix test_il_parse_param_prefix)

    zanna_add_test(test_il_parse_extra_param_commas ${_ZANNA_IL_UNIT_DIR}/test_il_parse_extra_param_commas.cpp)
    target_link_libraries(test_il_parse_extra_param_commas PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_extra_param_commas test_il_parse_extra_param_commas)

    zanna_add_test(test_il_parse_extern_extra_commas ${_ZANNA_IL_UNIT_DIR}/test_il_parse_extern_extra_commas.cpp)
    target_link_libraries(test_il_parse_extern_extra_commas PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_extern_extra_commas test_il_parse_extern_extra_commas)

    zanna_add_test(test_il_parse_missing_extern_name ${_ZANNA_IL_UNIT_DIR}/test_il_parse_missing_extern_name.cpp)
    target_link_libraries(test_il_parse_missing_extern_name PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_missing_extern_name test_il_parse_missing_extern_name)

    zanna_add_test(
            test_il_parse_block_param_extra_commas
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_block_param_extra_commas.cpp)
    target_link_libraries(test_il_parse_block_param_extra_commas PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_block_param_extra_commas
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    zanna_add_ctest(
            test_il_parse_block_param_extra_commas test_il_parse_block_param_extra_commas)

    zanna_add_test(test_il_parse_block_param_prefix ${_ZANNA_IL_UNIT_DIR}/test_il_parse_block_param_prefix.cpp)
    target_link_libraries(test_il_parse_block_param_prefix PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_block_param_prefix PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_block_param_prefix test_il_parse_block_param_prefix)

    zanna_add_test(test_il_parse_duplicate_param ${_ZANNA_IL_UNIT_DIR}/test_il_parse_duplicate_param.cpp)
    target_link_libraries(test_il_parse_duplicate_param PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_duplicate_param test_il_parse_duplicate_param)

    zanna_add_test(test_il_parse_duplicate_result ${_ZANNA_IL_UNIT_DIR}/test_il_parse_duplicate_result.cpp)
    target_link_libraries(test_il_parse_duplicate_result PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_duplicate_result test_il_parse_duplicate_result)

    zanna_add_test(test_runtime_name_map ${_ZANNA_IL_UNIT_DIR}/il/runtime/test_runtime_name_map.cpp)
    target_link_libraries(test_runtime_name_map PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_runtime)
    zanna_add_ctest(test_runtime_name_map test_runtime_name_map)

    zanna_add_test(
            test_il_parse_duplicate_block_param
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_duplicate_block_param.cpp)
    target_link_libraries(test_il_parse_duplicate_block_param PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_duplicate_block_param
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    zanna_add_ctest(test_il_parse_duplicate_block_param test_il_parse_duplicate_block_param)

    zanna_add_test(
            test_il_parse_duplicate_version
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_duplicate_version.cpp)
    target_link_libraries(test_il_parse_duplicate_version PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_duplicate_version
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    zanna_add_ctest(test_il_parse_duplicate_version test_il_parse_duplicate_version)

    zanna_add_test(
            test_il_parse_invalid_calling_conv
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_invalid_calling_conv.cpp)
    target_link_libraries(test_il_parse_invalid_calling_conv PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_invalid_calling_conv
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    zanna_add_ctest(test_il_parse_invalid_calling_conv test_il_parse_invalid_calling_conv)

    zanna_add_test(test_il_parse_missing_brace ${_ZANNA_IL_UNIT_DIR}/test_il_parse_missing_brace.cpp)
    target_link_libraries(test_il_parse_missing_brace PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_missing_brace PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_missing_brace test_il_parse_missing_brace)

    zanna_add_test(
            test_il_parse_missing_block_label
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_missing_block_label.cpp)
    target_link_libraries(test_il_parse_missing_block_label PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_missing_block_label
            PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_missing_block_label test_il_parse_missing_block_label)

    zanna_add_test(
            test_il_parse_function_name_trim
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_function_name_trim.cpp)
    target_link_libraries(test_il_parse_function_name_trim PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_function_name_trim test_il_parse_function_name_trim)

    zanna_add_test(test_il_parse_duplicate_block ${_ZANNA_IL_UNIT_DIR}/test_il_parse_duplicate_block.cpp)
    target_link_libraries(test_il_parse_duplicate_block PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_duplicate_block PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_duplicate_block test_il_parse_duplicate_block)

    zanna_add_test(test_il_parse_missing_paren ${_ZANNA_IL_UNIT_DIR}/test_il_parse_missing_paren.cpp)
    target_link_libraries(test_il_parse_missing_paren PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_missing_paren PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_missing_paren test_il_parse_missing_paren)

    zanna_add_test(test_il_parse_extern_whitespace ${_ZANNA_IL_UNIT_DIR}/test_il_parse_extern_whitespace.cpp)
    target_link_libraries(test_il_parse_extern_whitespace PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_extern_whitespace PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    zanna_add_ctest(test_il_parse_extern_whitespace test_il_parse_extern_whitespace)

    zanna_add_test(test_il_parse_extern_empty_params ${_ZANNA_IL_UNIT_DIR}/test_il_parse_extern_empty_params.cpp)
    target_link_libraries(test_il_parse_extern_empty_params PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_extern_empty_params test_il_parse_extern_empty_params)

    # call.indirect roundtrip + VM execution smoke
    zanna_add_test(il_call_indirect_roundtrip ${CMAKE_SOURCE_DIR}/src/tests/il/CallIndirectRoundTrip.cpp)
    target_link_libraries(il_call_indirect_roundtrip PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_vm)
    zanna_add_ctest(il_call_indirect_roundtrip il_call_indirect_roundtrip)

    zanna_add_test(test_il_correctness_fixes ${CMAKE_SOURCE_DIR}/src/tests/il/ILCorrectnessFixes.cpp)
    target_link_libraries(test_il_correctness_fixes PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_transform il_vm il_runtime)
    zanna_add_ctest(test_il_correctness_fixes test_il_correctness_fixes)

    zanna_add_test(test_il_parse_call_ret_type ${_ZANNA_IL_UNIT_DIR}/test_il_parse_call_ret_type.cpp)
    target_link_libraries(test_il_parse_call_ret_type PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_call_ret_type test_il_parse_call_ret_type)

    zanna_add_test(test_il_parse_negative ${_ZANNA_IL_UNIT_DIR}/test_il_parse_negative.cpp)
    target_link_libraries(test_il_parse_negative PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_negative PRIVATE BAD_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    zanna_add_ctest(test_il_parse_negative test_il_parse_negative)

    zanna_add_test(
            test_il_parse_operand_whitespace
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_operand_whitespace.cpp)
    target_link_libraries(test_il_parse_operand_whitespace PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_operand_whitespace
            PRIVATE OPERAND_WS_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    zanna_add_ctest(test_il_parse_operand_whitespace test_il_parse_operand_whitespace)

    zanna_add_test(
            test_il_parse_dollar_identifiers
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_dollar_identifiers.cpp)
    target_link_libraries(test_il_parse_dollar_identifiers PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_dollar_identifiers test_il_parse_dollar_identifiers)

    zanna_add_test(test_il_parse_comment ${_ZANNA_IL_UNIT_DIR}/test_il_parse_comment.cpp)
    target_link_libraries(test_il_parse_comment PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_comment test_il_parse_comment)

    zanna_add_test(test_il_comments ${_ZANNA_IL_UNIT_DIR}/test_il_comments.cpp)
    target_link_libraries(test_il_comments PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_comments test_il_comments)

    zanna_add_test(test_il_parse_missing_eq ${_ZANNA_IL_UNIT_DIR}/test_il_parse_missing_eq.cpp)
    target_link_libraries(test_il_parse_missing_eq PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_missing_eq test_il_parse_missing_eq)

    zanna_add_test(test_il_parse_missing_version ${_ZANNA_IL_UNIT_DIR}/test_il_parse_missing_version.cpp)
    target_link_libraries(test_il_parse_missing_version PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_missing_version test_il_parse_missing_version)

    zanna_add_test(test_il_parse_bom ${_ZANNA_IL_UNIT_DIR}/test_il_parse_bom.cpp)
    target_link_libraries(test_il_parse_bom PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_bom test_il_parse_bom)

    zanna_add_test(test_il_parse_first_error ${_ZANNA_IL_UNIT_DIR}/test_il_parse_first_error.cpp)
    target_link_libraries(test_il_parse_first_error PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_first_error test_il_parse_first_error)

    zanna_add_test(test_il_parse_global_missing_quotes ${_ZANNA_IL_UNIT_DIR}/test_il_parse_global_missing_quotes.cpp)
    target_link_libraries(test_il_parse_global_missing_quotes PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_global_missing_quotes test_il_parse_global_missing_quotes)

    zanna_add_test(
            test_il_parse_global_trailing_characters
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_global_trailing_characters.cpp)
    target_link_libraries(test_il_parse_global_trailing_characters PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(
            test_il_parse_global_trailing_characters test_il_parse_global_trailing_characters)

    zanna_add_test(test_il_parse_global_missing_at ${_ZANNA_IL_UNIT_DIR}/test_il_parse_global_missing_at.cpp)
    target_link_libraries(test_il_parse_global_missing_at PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_global_missing_at test_il_parse_global_missing_at)

    zanna_add_test(test_il_parse_global_missing_name ${_ZANNA_IL_UNIT_DIR}/test_il_parse_global_missing_name.cpp)
    target_link_libraries(test_il_parse_global_missing_name PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_global_missing_name test_il_parse_global_missing_name)

    zanna_add_test(test_il_parse_global_missing_type ${_ZANNA_IL_UNIT_DIR}/test_il_parse_global_missing_type.cpp)
    target_link_libraries(test_il_parse_global_missing_type PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_global_missing_type test_il_parse_global_missing_type)

    zanna_add_test(test_il_parse_global_unsupported_type ${_ZANNA_IL_UNIT_DIR}/test_il_parse_global_unsupported_type.cpp)
    target_link_libraries(test_il_parse_global_unsupported_type PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_global_unsupported_type test_il_parse_global_unsupported_type)

    zanna_add_test(test_il_parse_string_escapes ${_ZANNA_IL_UNIT_DIR}/test_il_parse_string_escapes.cpp)
    target_link_libraries(test_il_parse_string_escapes PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_string_escapes test_il_parse_string_escapes)

    zanna_add_test(test_il_parse_conststr_escapes ${_ZANNA_IL_UNIT_DIR}/test_il_parse_conststr_escapes.cpp)
    target_link_libraries(test_il_parse_conststr_escapes PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_conststr_escapes test_il_parse_conststr_escapes)

    zanna_add_test(test_il_parse_invalid_type ${_ZANNA_IL_UNIT_DIR}/test_il_parse_invalid_type.cpp)
    target_link_libraries(test_il_parse_invalid_type PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_invalid_type test_il_parse_invalid_type)

    zanna_add_test(test_il_parse_misc_instructions ${_ZANNA_IL_UNIT_DIR}/test_il_parse_misc_instructions.cpp)
    target_link_libraries(test_il_parse_misc_instructions PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_misc_instructions test_il_parse_misc_instructions)

    zanna_add_test(
            test_il_parse_call_string_comma
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_call_string_comma.cpp)
    target_link_libraries(test_il_parse_call_string_comma PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_call_string_comma test_il_parse_call_string_comma)

    zanna_add_test(
            test_il_parse_call_trailing_junk
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_call_trailing_junk.cpp)
    target_link_libraries(test_il_parse_call_trailing_junk PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_call_trailing_junk test_il_parse_call_trailing_junk)

    zanna_add_test(
            test_il_parse_keyword_boundary
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_keyword_boundary.cpp)
    target_link_libraries(test_il_parse_keyword_boundary PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_keyword_boundary test_il_parse_keyword_boundary)

    zanna_add_test(
            test_il_parse_global_keyword_boundary
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_global_keyword_boundary.cpp)
    target_link_libraries(test_il_parse_global_keyword_boundary PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(
            test_il_parse_global_keyword_boundary test_il_parse_global_keyword_boundary)

    zanna_add_test(
            test_il_parse_missing_operand_between_commas
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_missing_operand_between_commas.cpp)
    target_link_libraries(test_il_parse_missing_operand_between_commas PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_missing_operand_between_commas test_il_parse_missing_operand_between_commas)

    zanna_add_test(
            test_il_parse_branch_missing_label
            ${_ZANNA_IL_UNIT_DIR}/test_il_parse_branch_missing_label.cpp)
    target_link_libraries(test_il_parse_branch_missing_label PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_parse_branch_missing_label test_il_parse_branch_missing_label)

    zanna_add_test(test_il_function_parser_errors ${_ZANNA_IL_UNIT_DIR}/test_il_function_parser_errors.cpp)
    target_link_libraries(test_il_function_parser_errors PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_function_parser_errors test_il_function_parser_errors)

    zanna_add_test(test_il_parse_loc_errors ${_ZANNA_IL_UNIT_DIR}/test_il_parse_loc_errors.cpp)
    target_link_libraries(test_il_parse_loc_errors PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_parse_loc_errors test_il_parse_loc_errors)

    zanna_add_test(test_expected_api_build ${_ZANNA_IL_UNIT_DIR}/test_expected_api_build.cpp)
    target_link_libraries(test_expected_api_build PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_expected_api_build test_expected_api_build)

    zanna_add_test(test_il_verify_trap ${_ZANNA_IL_UNIT_DIR}/test_il_verify_trap.cpp)
    target_link_libraries(test_il_verify_trap PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_verify_trap test_il_verify_trap)

    zanna_add_test(test_il_verify_select ${_ZANNA_IL_UNIT_DIR}/test_il_verify_select.cpp)
    target_link_libraries(test_il_verify_select PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_verify_select test_il_verify_select)

    zanna_add_test(test_il_verify_forward_call ${_ZANNA_IL_UNIT_DIR}/test_il_verify_forward_call.cpp)
    target_link_libraries(test_il_verify_forward_call PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_verify_forward_call test_il_verify_forward_call)

    zanna_add_test(test_il_verify_release_lifetime ${_ZANNA_IL_UNIT_DIR}/test_il_verify_release_lifetime.cpp)
    target_link_libraries(test_il_verify_release_lifetime PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_verify_release_lifetime test_il_verify_release_lifetime)

    zanna_add_test(test_il_verify_table_memory_ok ${_ZANNA_IL_UNIT_DIR}/test_il_verify_table_memory_ok.cpp)
    target_link_libraries(test_il_verify_table_memory_ok PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_verify_table_memory_ok test_il_verify_table_memory_ok)

    zanna_add_test(test_il_verify_table_mismatch ${_ZANNA_IL_UNIT_DIR}/test_il_verify_table_mismatch.cpp)
    target_link_libraries(test_il_verify_table_mismatch PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_verify_table_mismatch test_il_verify_table_mismatch)

    zanna_add_test(
            test_il_verify_calls_table_interop
            ${_ZANNA_IL_UNIT_DIR}/test_il_verify_calls_table_interop.cpp)
    target_link_libraries(test_il_verify_calls_table_interop PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(
            test_il_verify_calls_table_interop test_il_verify_calls_table_interop)

    zanna_add_test(test_il_verify_eh_table ${_ZANNA_IL_UNIT_DIR}/test_il_verify_eh_table.cpp)
    target_link_libraries(test_il_verify_eh_table PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_il_verify_eh_table test_il_verify_eh_table)

    zanna_add_test(test_il_type_inference ${_ZANNA_IL_UNIT_DIR}/test_il_type_inference.cpp)
    target_link_libraries(test_il_type_inference PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_type_inference test_il_type_inference)

    zanna_add_test(test_runtime_registry ${_ZANNA_IL_UNIT_DIR}/test_runtime_registry.cpp)
    target_link_libraries(test_runtime_registry PRIVATE il_runtime)
    zanna_add_ctest(test_runtime_registry test_runtime_registry)

    zanna_add_test(
            test_runtime_signatures_self_check
            ${_ZANNA_IL_UNIT_DIR}/RuntimeSignaturesSelfCheckTests.cpp)
    target_link_libraries(test_runtime_signatures_self_check PRIVATE il_runtime)
    zanna_add_ctest(test_runtime_signatures_self_check test_runtime_signatures_self_check)

    zanna_add_test(
            test_runtime_invariant_mode
            ${_ZANNA_IL_UNIT_DIR}/test_runtime_invariant_mode.cpp)
    target_link_libraries(test_runtime_invariant_mode PRIVATE il_runtime)
    zanna_add_ctest(test_runtime_invariant_mode test_runtime_invariant_mode)

    zanna_add_test(
            test_il_runtime_signature_effects
            ${_ZANNA_IL_UNIT_DIR}/il/runtime/test_SignaturesPurity.cpp)
    target_link_libraries(test_il_runtime_signature_effects PRIVATE il_runtime)
    zanna_add_ctest(
            test_il_runtime_signature_effects test_il_runtime_signature_effects)

    zanna_add_test(test_il_instruction_checker ${_ZANNA_IL_UNIT_DIR}/test_il_instruction_checker.cpp)
    target_link_libraries(test_il_instruction_checker PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_instruction_checker test_il_instruction_checker)

    zanna_add_test(
            test_il_instruction_checker_binary_arity
            ${_ZANNA_IL_UNIT_DIR}/test_il_instruction_checker_binary_arity.cpp)
    target_link_libraries(
            test_il_instruction_checker_binary_arity PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(
            test_il_instruction_checker_binary_arity test_il_instruction_checker_binary_arity)

    zanna_add_test(test_il_error_resume_ir ${_ZANNA_IL_UNIT_DIR}/test_il_error_resume_ir.cpp)
    target_link_libraries(test_il_error_resume_ir PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_error_resume_ir test_il_error_resume_ir)

    zanna_add_test(test_il_control_flow_checker ${_ZANNA_IL_UNIT_DIR}/test_il_control_flow_checker.cpp)
    target_link_libraries(test_il_control_flow_checker PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_control_flow_checker test_il_control_flow_checker)

    zanna_add_test(test_il_exception_handler_analysis
            ${_ZANNA_IL_UNIT_DIR}/test_il_exception_handler_analysis.cpp)
    target_link_libraries(test_il_exception_handler_analysis PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_exception_handler_analysis test_il_exception_handler_analysis)

    zanna_add_test(test_il_verify_eh_checks
            ${_ZANNA_IL_UNIT_DIR}/test_il_verify_eh_checks.cpp)
    target_link_libraries(test_il_verify_eh_checks PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_verify_eh_checks test_il_verify_eh_checks)

    zanna_add_test(test_il_branch_verifier ${_ZANNA_IL_UNIT_DIR}/test_il_branch_verifier.cpp)
    target_link_libraries(test_il_branch_verifier PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_branch_verifier PRIVATE NEGATIVE_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/negatives")
    zanna_add_ctest(test_il_branch_verifier test_il_branch_verifier)
endfunction()

function(zanna_add_il_utils_test)
    if (TARGET test_il_utils)
        return()
    endif ()

    set(_ZANNA_IL_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
    zanna_add_test(test_il_utils ${_ZANNA_IL_DIR}/UtilsTests.cpp)
    target_link_libraries(test_il_utils PRIVATE il_utils)
    zanna_add_ctest(test_il_utils test_il_utils)

    zanna_add_test(
            test_il_simplifycfg_bitvector_resize
            ${_ZANNA_IL_DIR}/transform/simplifycfg_bitvector_resize.cpp)
    target_link_libraries(
            test_il_simplifycfg_bitvector_resize PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_bitvector_resize test_il_simplifycfg_bitvector_resize)

    zanna_add_test(
            test_il_simplifycfg_cbr_fold
            ${_ZANNA_IL_DIR}/transform/simplifycfg_cbr_fold.cpp)
    target_link_libraries(
            test_il_simplifycfg_cbr_fold PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_cbr_fold test_il_simplifycfg_cbr_fold)

    zanna_add_test(
            test_il_simplifycfg_bypass_params
            ${_ZANNA_IL_DIR}/transform/simplifycfg_bypass_params.cpp)
    target_link_libraries(
            test_il_simplifycfg_bypass_params PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_bypass_params test_il_simplifycfg_bypass_params)

    zanna_add_test(
            test_il_simplifycfg_merge_single_pred
            ${_ZANNA_IL_DIR}/transform/simplifycfg_merge_single_pred.cpp)
    target_link_libraries(
            test_il_simplifycfg_merge_single_pred PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_merge_single_pred test_il_simplifycfg_merge_single_pred)

    zanna_add_test(
            test_il_simplifycfg_eh_guard
            ${_ZANNA_IL_DIR}/transform/simplifycfg_eh_guard.cpp)
    target_link_libraries(
            test_il_simplifycfg_eh_guard PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_eh_guard test_il_simplifycfg_eh_guard)

    zanna_add_test(
            test_il_simplifycfg_shrink_params
            ${_ZANNA_IL_DIR}/transform/simplifycfg_shrink_params.cpp)
    target_link_libraries(
            test_il_simplifycfg_shrink_params PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_shrink_params test_il_simplifycfg_shrink_params)

    zanna_add_test(
            test_il_simplifycfg_values_equal_fp
            ${_ZANNA_IL_DIR}/transform/simplifycfg_values_equal_fp.cpp)
    target_link_libraries(
            test_il_simplifycfg_values_equal_fp PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_values_equal_fp test_il_simplifycfg_values_equal_fp)

    zanna_add_test(
            test_il_simplifycfg_pass_manager_cfg_invalidation
            ${_ZANNA_IL_DIR}/transform/simplifycfg_pass_manager_cfg_invalidation.cpp)
    target_link_libraries(
            test_il_simplifycfg_pass_manager_cfg_invalidation PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_simplifycfg_pass_manager_cfg_invalidation test_il_simplifycfg_pass_manager_cfg_invalidation)

    zanna_add_test(
            test_il_peephole_use_count
            ${_ZANNA_IL_DIR}/transform/peephole_use_count.cpp)
    target_link_libraries(
            test_il_peephole_use_count PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_peephole_use_count test_il_peephole_use_count)

    add_executable(
            test_il_alias_precision
            ${_ZANNA_IL_DIR}/transform/alias_precision.cpp)
    if (WIN32)
        target_sources(test_il_alias_precision PRIVATE
                ${ZANNA_TESTS_DIR}/common/WinDialogSuppress.c)
    endif ()
    target_link_libraries(
            test_il_alias_precision PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_alias_precision $<TARGET_FILE:test_il_alias_precision>)

    zanna_add_test(
            test_il_dce_param_compaction
            ${_ZANNA_IL_DIR}/transform/dce_param_compaction.cpp)
    target_link_libraries(
            test_il_dce_param_compaction PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_dce_param_compaction test_il_dce_param_compaction)

    zanna_add_test(
            test_il_loop_simplify_preheader
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_LoopSimplify.cpp)
    target_link_libraries(
            test_il_loop_simplify_preheader PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_loop_simplify_preheader test_il_loop_simplify_preheader)

    zanna_add_test(
            test_il_loop_simplify_ptr_stability
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_LoopSimplify_PtrStability.cpp)
    target_link_libraries(
            test_il_loop_simplify_ptr_stability PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_loop_simplify_ptr_stability test_il_loop_simplify_ptr_stability)

    zanna_add_test(
            test_il_licm_basic
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_LICM.cpp)
    target_link_libraries(
            test_il_licm_basic PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_licm_basic test_il_licm_basic)

    zanna_add_test(
            test_il_sccp_phi_branch
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_SCCP.cpp)
    target_link_libraries(
            test_il_sccp_phi_branch PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_sccp_phi_branch test_il_sccp_phi_branch)

    # Induction variable simplification tests
    zanna_add_test(
            test_il_indvars_basic
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_IndVarSimplify.cpp)
    target_link_libraries(
            test_il_indvars_basic PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform
                                          il_analysis)
    zanna_add_ctest(test_il_indvars_basic test_il_indvars_basic)

    # GVN + Redundant Load Elimination tests
    zanna_add_test(
            test_il_gvn_basic
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_GVN.cpp)
    target_link_libraries(
            test_il_gvn_basic PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_gvn_basic test_il_gvn_basic)

    zanna_add_test(
            test_il_gvn_cse_basic
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_gvn_cse_basic.cpp)
    target_link_libraries(
            test_il_gvn_cse_basic PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_gvn_cse_basic test_il_gvn_cse_basic)

    zanna_add_test(
            test_il_earlycse_domtree
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_earlycse_domtree.cpp)
    target_link_libraries(
            test_il_earlycse_domtree PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_earlycse_domtree test_il_earlycse_domtree)

    zanna_add_test(
            test_il_loopsimplify_invariants
            ${ZANNA_TESTS_DIR}/unit/il/transform/loopsimplify_invariants.cpp)
    target_link_libraries(
            test_il_loopsimplify_invariants PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_loopsimplify_invariants test_il_loopsimplify_invariants)

    zanna_add_test(
            test_il_licm_hoist ${ZANNA_TESTS_DIR}/unit/il/transform/licm_basic.cpp)
    target_link_libraries(
            test_il_licm_hoist PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_licm_hoist test_il_licm_hoist)

    zanna_add_test(
            test_il_indvars_strength ${ZANNA_TESTS_DIR}/unit/il/transform/indvars_basic.cpp)
    target_link_libraries(
            test_il_indvars_strength PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform
                                             il_analysis)
    zanna_add_ctest(test_il_indvars_strength test_il_indvars_strength)

    zanna_add_test(
            test_il_checkopt_redundancy
            ${ZANNA_TESTS_DIR}/unit/il/transform/checkopt_redundancy.cpp)
    target_link_libraries(
            test_il_checkopt_redundancy PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(
            test_il_checkopt_redundancy test_il_checkopt_redundancy)

    zanna_add_test(
            test_il_inline_basic
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_Inline.cpp)
    target_link_libraries(
            test_il_inline_basic PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_inline_basic test_il_inline_basic)
    zanna_add_test(
            test_il_inline_multiblock
            ${_ZANNA_IL_DIR}/transform/inline_multiblock.cpp)
    target_link_libraries(
            test_il_inline_multiblock PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_inline_multiblock test_il_inline_multiblock)

    zanna_add_test(
            test_il_callgraph_scc
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_callgraph_scc.cpp)
    target_link_libraries(
            test_il_callgraph_scc PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform il_analysis)
    zanna_add_ctest(test_il_callgraph_scc test_il_callgraph_scc)
    zanna_add_test(
            test_il_mem2reg_sroa
            ${_ZANNA_IL_DIR}/transform/mem2reg_sroa.cpp)
    target_link_libraries(
            test_il_mem2reg_sroa PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_mem2reg_sroa test_il_mem2reg_sroa)

    zanna_add_test(
            test_il_check_opt
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_CheckOpt.cpp)
    target_link_libraries(
            test_il_check_opt PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_check_opt test_il_check_opt)

    zanna_add_test(
            test_il_int_range_analysis
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_IntRangeAnalysis.cpp)
    target_link_libraries(
            test_il_int_range_analysis PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform
            il_api)
    zanna_add_ctest(test_il_int_range_analysis test_il_int_range_analysis)

    zanna_add_test(
            test_il_if_convert
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_IfConvert.cpp)
    target_link_libraries(
            test_il_if_convert PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform
            il_api)
    zanna_add_ctest(test_il_if_convert test_il_if_convert)

    zanna_add_test(
            test_il_late_cleanup
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_LateCleanup.cpp)
    target_link_libraries(
            test_il_late_cleanup PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_late_cleanup test_il_late_cleanup)

    zanna_add_test(
            test_il_latecleanup_fixpoint
            ${_ZANNA_IL_DIR}/transform/latecleanup_fixpoint.cpp)
    target_link_libraries(
            test_il_latecleanup_fixpoint PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_latecleanup_fixpoint test_il_latecleanup_fixpoint)

    # Loop unrolling tests
    zanna_add_test(
            test_il_loop_unroll
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_LoopUnroll.cpp)
    target_link_libraries(
            test_il_loop_unroll PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_loop_unroll test_il_loop_unroll)

    # Jump threading tests
    zanna_add_test(
            test_il_jump_threading
            ${_ZANNA_IL_DIR}/transform/jumpthreading_basic.cpp)
    target_link_libraries(
            test_il_jump_threading PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_jump_threading test_il_jump_threading)

    # Cross-block DSE tests
    zanna_add_test(
            test_il_dse_crossblock
            ${_ZANNA_IL_DIR}/transform/dse_crossblock.cpp)
    target_link_libraries(
            test_il_dse_crossblock PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_dse_crossblock test_il_dse_crossblock)

    # Enhanced constant folding tests
    zanna_add_test(
            test_il_constfold_enhanced
            ${_ZANNA_IL_DIR}/transform/constfold_enhanced.cpp)
    target_link_libraries(
            test_il_constfold_enhanced PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_constfold_enhanced test_il_constfold_enhanced)

    # Constant folding edge case tests
    zanna_add_test(
            test_il_constfold_edgecases
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_constfold_edgecases.cpp)
    target_link_libraries(
            test_il_constfold_edgecases PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_constfold_edgecases test_il_constfold_edgecases)

    # Constant folding audit tests (comprehensive coverage)
    zanna_add_test(
            test_il_constfold_audit
            ${_ZANNA_IL_DIR}/transform/ConstFoldAuditTests.cpp)
    target_link_libraries(
            test_il_constfold_audit PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_constfold_audit test_il_constfold_audit)

    # IL optimization review regression tests
    zanna_add_test(
            test_il_opt_review_basicaa
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_opt_review_basicaa.cpp)
    target_link_libraries(
            test_il_opt_review_basicaa PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_opt_review_basicaa test_il_opt_review_basicaa)

    zanna_add_test(
            test_il_opt_review_sccp
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_opt_review_sccp.cpp)
    target_link_libraries(
            test_il_opt_review_sccp PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_opt_review_sccp test_il_opt_review_sccp)

    zanna_add_test(
            test_il_opt_review_peephole
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_opt_review_peephole.cpp)
    target_link_libraries(
            test_il_opt_review_peephole PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_opt_review_peephole test_il_opt_review_peephole)

    zanna_add_test(
            test_il_opt_review_loopinfo
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_opt_review_loopinfo.cpp)
    target_link_libraries(
            test_il_opt_review_loopinfo PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_opt_review_loopinfo test_il_opt_review_loopinfo)

    zanna_add_test(
            test_il_opt_review_valuekey
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_opt_review_valuekey.cpp)
    target_link_libraries(
            test_il_opt_review_valuekey PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_opt_review_valuekey test_il_opt_review_valuekey)

    zanna_add_test(
            test_il_opt_review_dse
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_opt_review_dse.cpp)
    target_link_libraries(
            test_il_opt_review_dse PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_opt_review_dse test_il_opt_review_dse)

    zanna_add_test(
            test_il_dse_sideeffects
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_dse_sideeffects.cpp)
    target_link_libraries(
            test_il_dse_sideeffects PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_dse_sideeffects test_il_dse_sideeffects)

    zanna_add_test(
            test_il_opt_review_calleffects
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_opt_review_calleffects.cpp)
    target_link_libraries(
            test_il_opt_review_calleffects PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_opt_review_calleffects test_il_opt_review_calleffects)

    zanna_add_test(
            test_il_realworld_perf_passes
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_realworld_perf_passes.cpp)
    target_link_libraries(
            test_il_realworld_perf_passes PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_realworld_perf_passes test_il_realworld_perf_passes)

    # Canonical pipeline test: verifies runPipeline("O1"/"O2") uses full passes including SCCP
    zanna_add_test(
            test_il_canonical_pipeline
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_canonical_pipeline.cpp)
    target_link_libraries(
            test_il_canonical_pipeline PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_canonical_pipeline test_il_canonical_pipeline)

    # Mem2Reg non-entry-block promotion test
    zanna_add_test(
            test_il_mem2reg_nonentry
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_mem2reg_nonentry.cpp)
    target_link_libraries(
            test_il_mem2reg_nonentry PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_mem2reg_nonentry test_il_mem2reg_nonentry)

    # Inliner threshold test: verifies new instrThreshold=80, blockBudget=8, maxInlineDepth=3
    zanna_add_test(
            test_il_inline_threshold
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_inline_threshold.cpp)
    target_link_libraries(
            test_il_inline_threshold PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_inline_threshold test_il_inline_threshold)

    zanna_add_test(
            test_il_sibling_recursion
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_sibling_recursion.cpp)
    target_link_libraries(
            test_il_sibling_recursion PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_sibling_recursion test_il_sibling_recursion)

    # Per-pass isolation: apply each of the 17 passes individually, verify valid IL
    zanna_add_test(
            test_il_pass_isolation
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_pass_isolation.cpp)
    target_link_libraries(
            test_il_pass_isolation PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_pass_isolation test_il_pass_isolation)

    # Missed optimizations catalogue: existing + missing optimization tests
    zanna_add_test(
            test_il_missed_optimizations
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_missed_optimizations.cpp)
    target_link_libraries(
            test_il_missed_optimizations PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_missed_optimizations test_il_missed_optimizations)

    zanna_add_test(test_il_opcode_info ${_ZANNA_IL_DIR}/OpcodeInfoTests.cpp)
    target_link_libraries(test_il_opcode_info PRIVATE ${ZANNA_IL_CORE_IO_LIBS})
    zanna_add_ctest(test_il_opcode_info test_il_opcode_info)

    zanna_add_test(
            test_il_invalid_eh
            ${_ZANNA_IL_DIR}/InvalidEhTests.cpp
            TEST_NAME il_InvalidEhTests)
    target_link_libraries(test_il_invalid_eh PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_invalid_eh PRIVATE INVALID_EH_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/invalid_eh")
endfunction()

function(zanna_add_il_analysis_tests)
    if (TARGET test_analysis_cfg)
        return()
    endif ()

    set(_ZANNA_ANALYSIS_DIR ${ZANNA_TESTS_DIR}/analysis)

    zanna_add_test(test_analysis_cfg ${_ZANNA_ANALYSIS_DIR}/CFGTests.cpp)
    target_link_libraries(test_analysis_cfg PRIVATE ${ZANNA_IL_ANALYSIS_LIBS})
    zanna_add_ctest(test_analysis_cfg test_analysis_cfg)

    zanna_add_test(test_analysis_graph_order ${_ZANNA_ANALYSIS_DIR}/GraphOrderTests.cpp)
    target_link_libraries(test_analysis_graph_order PRIVATE ${ZANNA_IL_ANALYSIS_LIBS})
    zanna_add_ctest(test_analysis_graph_order test_analysis_graph_order)

    zanna_add_test(test_analysis_dominators ${_ZANNA_ANALYSIS_DIR}/DominatorsTests.cpp)
    target_link_libraries(test_analysis_dominators PRIVATE ${ZANNA_IL_ANALYSIS_LIBS})
    zanna_add_ctest(test_analysis_dominators test_analysis_dominators)

    zanna_add_test(test_analysis_post_dominators ${_ZANNA_ANALYSIS_DIR}/PostDominatorsTests.cpp)
    target_link_libraries(test_analysis_post_dominators PRIVATE ${ZANNA_IL_ANALYSIS_LIBS})
    zanna_add_ctest(test_analysis_post_dominators test_analysis_post_dominators)

    zanna_add_test(test_analysis_acyclic ${_ZANNA_ANALYSIS_DIR}/AcyclicTests.cpp)
    target_link_libraries(test_analysis_acyclic PRIVATE ${ZANNA_IL_ANALYSIS_LIBS})
    zanna_add_ctest(test_analysis_acyclic test_analysis_acyclic)

    zanna_add_test(test_analysis_basic_aa ${_ZANNA_ANALYSIS_DIR}/BasicAATests.cpp)
    target_link_libraries(test_analysis_basic_aa PRIVATE ${ZANNA_IL_ANALYSIS_LIBS})
    zanna_add_ctest(test_analysis_basic_aa test_analysis_basic_aa)

    zanna_add_test(test_analysis_memory_ssa ${_ZANNA_ANALYSIS_DIR}/MemorySSATests.cpp)
    target_link_libraries(test_analysis_memory_ssa
            PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform zanna_test_common)
    zanna_add_ctest(test_analysis_memory_ssa test_analysis_memory_ssa)
endfunction()

function(zanna_add_il_verify_examples)
    if (TEST il_verify_ex1)
        return()
    endif ()

    zanna_add_ctest(il_verify_ex1 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex1_hello_cond.il)
    zanna_add_ctest(il_verify_ex2 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex2_sum_1_to_10.il)
    zanna_add_ctest(il_verify_ex3 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex3_table_5x5.il)
    zanna_add_ctest(il_verify_ex4 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex4_factorial.il)
    zanna_add_ctest(il_verify_ex5 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex5_strings.il)
    zanna_add_ctest(il_verify_ex6 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex6_heap_array_avg.il)
endfunction()

function(zanna_add_il_invalid_tests)
    if (TEST il_verify_invalid_bad_types)
        return()
    endif ()

    set(IL_VERIFY $<TARGET_FILE:il-verify>)
    set(_ZANNA_INVALID_DIR ${CMAKE_SOURCE_DIR}/src/tests/golden/invalid_il)

    zanna_add_ctest(il_verify_invalid_bad_types
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/bad_types.il
            -DEXPECT=operand\ type\ mismatch
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_missing_term
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/missing_terminator.il
            -DEXPECT=missing\ terminator
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_wrong_call_arity
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/wrong_call_arity.il
            -DEXPECT=call\ arg\ count
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_unknown_symbol
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/unknown_symbol.il
            -DEXPECT=unknown\ callee
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_bad_load_store
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/bad_load_store.il
            -DEXPECT=pointer\ type\ mismatch
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_wrong_extern_sig
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/wrong_extern_sig.il
            -DEXPECT=extern\ @rt_print_str\ signature\ mismatch
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_wrong_extern_call
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/wrong_extern_call.il
            -DEXPECT=call\ arg\ type\ mismatch
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_wrong_block_arg_arity
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/wrong_block_arg_arity.il
            -DEXPECT=bad\ arg\ count
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_wrong_block_arg_type
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/wrong_block_arg_type.il
            -DEXPECT=arg\ type\ mismatch
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_duplicate_param
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/duplicate_param.il
            -DEXPECT=duplicate\ param
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_param_use_before_entry
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/param_use_before_entry.il
            -DEXPECT=not\ dominated\ by\ definition
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_unknown_temp
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/unknown_temp.il
            -DEXPECT=unknown\ temp
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_err_get_msg_outside_handler
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/err_get_msg_outside_handler.il
            -DEXPECT=err.get_\\*\ only\ allowed\ in\ handler\ block
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_alloca_return_escape
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/alloca_return_escape.il
            -DEXPECT=returning\ alloca-derived\ pointer
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_alloca_store_escape
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/alloca_store_escape.il
            -DEXPECT=storing\ alloca-derived\ pointer
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_alloca_load_escape
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/alloca_load_escape.il
            -DEXPECT=returning\ alloca-derived\ pointer
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    zanna_add_ctest(il_verify_invalid_shift_i32_result
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_DIR}/shift_i32_result.il
            -DEXPECT=result\ type\ mismatch
            -P ${_ZANNA_INVALID_DIR}/check_invalid.cmake)

    set(_ZANNA_NEGATIVE_DIR ${CMAKE_SOURCE_DIR}/src/tests/il/negatives)

    zanna_add_ctest(il_verify_negative_use_after_release
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_NEGATIVE_DIR}/use_after_release.il
            -DEXPECT_FILE=${_ZANNA_NEGATIVE_DIR}/use_after_release.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)

    zanna_add_ctest(il_verify_negative_unchecked_srem
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_NEGATIVE_DIR}/unchecked_srem.il
            -DEXPECT_FILE=${_ZANNA_NEGATIVE_DIR}/unchecked_srem.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)

    zanna_add_ctest(il_verify_negative_narrow_cast_nochk
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_NEGATIVE_DIR}/narrow_cast_nochk.il
            -DEXPECT_FILE=${_ZANNA_NEGATIVE_DIR}/narrow_cast_nochk.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)

    set(_ZANNA_INVALID_EH_DIR ${CMAKE_SOURCE_DIR}/src/tests/il/invalid_eh)

    zanna_add_ctest(il_verify_invalid_eh_underflow
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/pop_underflow.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/pop_underflow.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)

    zanna_add_ctest(il_verify_invalid_eh_leak
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/push_leak_branch.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/push_leak_branch.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
    zanna_add_ctest(il_verify_invalid_eh_resume_same_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/resume_same_without_token.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/resume_same_without_token.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
    zanna_add_ctest(il_verify_invalid_eh_resume_next_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/resume_next_without_token.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/resume_next_without_token.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
    zanna_add_ctest(il_verify_invalid_eh_resume_label_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/resume_label_without_token.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/resume_label_without_token.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
    zanna_add_ctest(il_verify_invalid_eh_resume_same_join_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/resume_same_join_without_token.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/resume_same_join_without_token.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
    zanna_add_ctest(il_verify_invalid_eh_resume_next_join_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/resume_next_join_without_token.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/resume_next_join_without_token.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
    zanna_add_ctest(il_verify_invalid_eh_resume_label_join_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/resume_label_join_without_token.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/resume_label_join_without_token.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
    zanna_add_ctest(il_verify_invalid_eh_resume_label_non_postdom
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_INVALID_EH_DIR}/resume_label_non_postdom.il
            -DEXPECT_FILE=${_ZANNA_INVALID_EH_DIR}/resume_label_non_postdom.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)

    # Phase III gap-fill: additional verifier negative tests
    zanna_add_ctest(il_verify_negative_call_arity_mismatch
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_NEGATIVE_DIR}/call_arity_mismatch.il
            -DEXPECT_FILE=${_ZANNA_NEGATIVE_DIR}/call_arity_mismatch.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)

    zanna_add_ctest(il_verify_negative_missing_entry_block
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_ZANNA_NEGATIVE_DIR}/missing_entry_block.il
            -DEXPECT_FILE=${_ZANNA_NEGATIVE_DIR}/missing_entry_block.expected
            -P ${_ZANNA_NEGATIVE_DIR}/check_negative.cmake)
endfunction()

function(zanna_add_il_liveness_tests)
    if (TARGET test_il_liveness)
        return()
    endif ()

    set(_ZANNA_IL_UNIT_DIR ${ZANNA_TESTS_DIR}/unit)

    zanna_add_test(test_il_liveness ${_ZANNA_IL_UNIT_DIR}/test_il_liveness.cpp)
    target_link_libraries(test_il_liveness PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_transform il_api)
    zanna_add_ctest(test_il_liveness test_il_liveness)

    zanna_add_test(test_il_pass_manager ${_ZANNA_IL_UNIT_DIR}/test_il_pass_manager.cpp)
    target_link_libraries(test_il_pass_manager PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_transform il_api)
    zanna_add_ctest(test_il_pass_manager test_il_pass_manager)

    zanna_add_test(test_il_switch_i32 ${_ZANNA_IL_UNIT_DIR}/test_il_switch_i32.cpp)
    target_link_libraries(test_il_switch_i32 PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_transform il_api)
    zanna_add_ctest(test_il_switch_i32 test_il_switch_i32)

    # EH optimization tests (Phase III gap fill)
    zanna_add_test(test_il_ehopt
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_EHOpt.cpp)
    target_link_libraries(
            test_il_ehopt PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_ehopt test_il_ehopt)

    # Reassociation tests (Phase III gap fill)
    zanna_add_test(test_il_reassociate
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_Reassociate.cpp)
    target_link_libraries(
            test_il_reassociate PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_reassociate test_il_reassociate)

    # Loop rotation tests (Phase III gap fill)
    zanna_add_test(test_il_loop_rotate
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_LoopRotate.cpp)
    target_link_libraries(
            test_il_loop_rotate PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform il_analysis)
    zanna_add_ctest(test_il_loop_rotate test_il_loop_rotate)

    # Pipeline interaction tests (Phase III gap fill)
    zanna_add_test(test_il_pipeline_interactions
            ${ZANNA_TESTS_DIR}/unit/il/transform/test_pipeline_interactions.cpp)
    target_link_libraries(
            test_il_pipeline_interactions PRIVATE ${ZANNA_IL_CORE_IO_LIBS} il_build il_transform)
    zanna_add_ctest(test_il_pipeline_interactions test_il_pipeline_interactions)
endfunction()
