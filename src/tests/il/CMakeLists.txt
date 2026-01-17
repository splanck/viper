# SPDX-License-Identifier: GPL-3.0-only
# File: tests/il/CMakeLists.txt
# Purpose: IL parser, verifier, and analysis tests.

set(VIPER_IL_CORE_IO_LIBS viper_il_full)
set(VIPER_IL_CORE_IO_LIBS ${VIPER_IL_CORE_IO_LIBS} PARENT_SCOPE)
set(VIPER_IL_ANALYSIS_LIBS il_analysis il_build)
set(VIPER_IL_ANALYSIS_LIBS ${VIPER_IL_ANALYSIS_LIBS} PARENT_SCOPE)

function(viper_add_il_core_tests)
    if (TARGET test_il_serialize)
        return()
    endif ()

    set(_VIPER_IL_UNIT_DIR ${VIPER_TESTS_DIR}/unit)

    viper_add_test(test_il_serialize ${_VIPER_IL_UNIT_DIR}/test_il_serialize.cpp)
    target_link_libraries(test_il_serialize PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build)
    target_compile_definitions(test_il_serialize PRIVATE TESTS_DIR="${VIPER_TESTS_DIR}")
    viper_add_ctest(test_il_serialize test_il_serialize)

    viper_add_test(
            test_il_serialize_negative_zero
            ${_VIPER_IL_UNIT_DIR}/test_il_serialize_negative_zero.cpp)
    target_link_libraries(
            test_il_serialize_negative_zero PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build)
    viper_add_ctest(
            test_il_serialize_negative_zero test_il_serialize_negative_zero)

    viper_add_test(test_il_serialize_opcodes ${_VIPER_IL_UNIT_DIR}/test_il_serialize_opcodes.cpp)
    target_link_libraries(test_il_serialize_opcodes PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    target_compile_definitions(test_il_serialize_opcodes PRIVATE TESTS_DIR="${VIPER_TESTS_DIR}")
    viper_add_ctest(test_il_serialize_opcodes test_il_serialize_opcodes)

    viper_add_test(test_il_malformed_cbr ${_VIPER_IL_UNIT_DIR}/test_il_malformed_cbr.cpp)
    target_link_libraries(test_il_malformed_cbr PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_malformed_cbr test_il_malformed_cbr)

    viper_add_test(test_irbuilder_call_ret ${_VIPER_IL_UNIT_DIR}/test_irbuilder_call_ret.cpp)
    target_link_libraries(test_irbuilder_call_ret PRIVATE il_build ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_irbuilder_call_ret test_irbuilder_call_ret)

    viper_add_test(
            test_il_core_memory_effects
            ${_VIPER_IL_UNIT_DIR}/il/core/test_MemoryEffects.cpp)
    target_link_libraries(test_il_core_memory_effects PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_core_memory_effects test_il_core_memory_effects)

    viper_add_test(test_irbuilder_call_unknown ${_VIPER_IL_UNIT_DIR}/test_irbuilder_call_unknown.cpp)
    target_link_libraries(test_irbuilder_call_unknown PRIVATE il_build ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_irbuilder_call_unknown test_irbuilder_call_unknown)

    viper_add_test(
            test_irbuilder_resume_terminators
            ${_VIPER_IL_UNIT_DIR}/test_irbuilder_resume_terminators.cpp)
    target_link_libraries(test_irbuilder_resume_terminators PRIVATE il_build ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(
            test_irbuilder_resume_terminators test_irbuilder_resume_terminators)

    viper_add_test(
            test_irbuilder_assertions
            ${_VIPER_IL_UNIT_DIR}/test_irbuilder_assertions.cpp)
    target_link_libraries(test_irbuilder_assertions PRIVATE il_build ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_irbuilder_assertions test_irbuilder_assertions)

    viper_add_test(test_il_roundtrip ${_VIPER_IL_UNIT_DIR}/test_il_roundtrip.cpp)
    target_link_libraries(test_il_roundtrip PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_roundtrip PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples" ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/roundtrip")
    viper_add_ctest(test_il_roundtrip test_il_roundtrip)

    viper_add_test(test_il_io_print_parse_roundtrip ${CMAKE_SOURCE_DIR}/src/tests/il/IOPrintParseRoundTrip.cpp)
    target_link_libraries(test_il_io_print_parse_roundtrip PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    set(_VIPER_IL_FIXTURE_DIRS
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
    list(JOIN _VIPER_IL_FIXTURE_DIRS "|" _VIPER_IL_FIXTURE_DIRS_ESCAPED)
    target_compile_definitions(
            test_il_io_print_parse_roundtrip PRIVATE IL_FIXTURE_DIRS="${_VIPER_IL_FIXTURE_DIRS_ESCAPED}")
    viper_add_ctest(test_il_io_print_parse_roundtrip test_il_io_print_parse_roundtrip)

    viper_add_test(test_il_parse_roundtrip ${_VIPER_IL_UNIT_DIR}/test_il_parse_roundtrip.cpp)
    target_link_libraries(test_il_parse_roundtrip PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_roundtrip PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip" SWITCH_GOLDEN="${CMAKE_SOURCE_DIR}/src/tests/golden/il_opt/switch_basic.il")
    viper_add_ctest(test_il_parse_roundtrip test_il_parse_roundtrip)

    viper_add_test(test_il_parse_unresolved_branch ${_VIPER_IL_UNIT_DIR}/test_il_parse_unresolved_branch.cpp)
    target_link_libraries(test_il_parse_unresolved_branch PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_unresolved_branch PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_unresolved_branch test_il_parse_unresolved_branch)

    viper_add_test(test_il_parse_param_prefix ${_VIPER_IL_UNIT_DIR}/test_il_parse_param_prefix.cpp)
    target_link_libraries(test_il_parse_param_prefix PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_param_prefix PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_param_prefix test_il_parse_param_prefix)

    viper_add_test(test_il_parse_extra_param_commas ${_VIPER_IL_UNIT_DIR}/test_il_parse_extra_param_commas.cpp)
    target_link_libraries(test_il_parse_extra_param_commas PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_extra_param_commas test_il_parse_extra_param_commas)

    viper_add_test(test_il_parse_extern_extra_commas ${_VIPER_IL_UNIT_DIR}/test_il_parse_extern_extra_commas.cpp)
    target_link_libraries(test_il_parse_extern_extra_commas PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_extern_extra_commas test_il_parse_extern_extra_commas)

    viper_add_test(test_il_parse_missing_extern_name ${_VIPER_IL_UNIT_DIR}/test_il_parse_missing_extern_name.cpp)
    target_link_libraries(test_il_parse_missing_extern_name PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_missing_extern_name test_il_parse_missing_extern_name)

    viper_add_test(
            test_il_parse_block_param_extra_commas
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_block_param_extra_commas.cpp)
    target_link_libraries(test_il_parse_block_param_extra_commas PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_block_param_extra_commas
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    viper_add_ctest(
            test_il_parse_block_param_extra_commas test_il_parse_block_param_extra_commas)

    viper_add_test(test_il_parse_block_param_prefix ${_VIPER_IL_UNIT_DIR}/test_il_parse_block_param_prefix.cpp)
    target_link_libraries(test_il_parse_block_param_prefix PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_block_param_prefix PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_block_param_prefix test_il_parse_block_param_prefix)

    viper_add_test(test_il_parse_duplicate_param ${_VIPER_IL_UNIT_DIR}/test_il_parse_duplicate_param.cpp)
    target_link_libraries(test_il_parse_duplicate_param PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_duplicate_param test_il_parse_duplicate_param)

    viper_add_test(test_il_parse_duplicate_result ${_VIPER_IL_UNIT_DIR}/test_il_parse_duplicate_result.cpp)
    target_link_libraries(test_il_parse_duplicate_result PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_duplicate_result test_il_parse_duplicate_result)

    viper_add_test(test_runtime_name_map ${_VIPER_IL_UNIT_DIR}/il/runtime/test_runtime_name_map.cpp)
    target_link_libraries(test_runtime_name_map PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_runtime)
    viper_add_ctest(test_runtime_name_map test_runtime_name_map)

    viper_add_test(
            test_il_parse_duplicate_block_param
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_duplicate_block_param.cpp)
    target_link_libraries(test_il_parse_duplicate_block_param PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_duplicate_block_param
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    viper_add_ctest(test_il_parse_duplicate_block_param test_il_parse_duplicate_block_param)

    viper_add_test(
            test_il_parse_duplicate_version
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_duplicate_version.cpp)
    target_link_libraries(test_il_parse_duplicate_version PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_duplicate_version
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    viper_add_ctest(test_il_parse_duplicate_version test_il_parse_duplicate_version)

    viper_add_test(
            test_il_parse_invalid_calling_conv
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_invalid_calling_conv.cpp)
    target_link_libraries(test_il_parse_invalid_calling_conv PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_invalid_calling_conv
            PRIVATE PARSE_ERROR_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    viper_add_ctest(test_il_parse_invalid_calling_conv test_il_parse_invalid_calling_conv)

    viper_add_test(test_il_parse_missing_brace ${_VIPER_IL_UNIT_DIR}/test_il_parse_missing_brace.cpp)
    target_link_libraries(test_il_parse_missing_brace PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_missing_brace PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_missing_brace test_il_parse_missing_brace)

    viper_add_test(
            test_il_parse_missing_block_label
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_missing_block_label.cpp)
    target_link_libraries(test_il_parse_missing_block_label PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_missing_block_label
            PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_missing_block_label test_il_parse_missing_block_label)

    viper_add_test(
            test_il_parse_function_name_trim
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_function_name_trim.cpp)
    target_link_libraries(test_il_parse_function_name_trim PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_function_name_trim test_il_parse_function_name_trim)

    viper_add_test(test_il_parse_duplicate_block ${_VIPER_IL_UNIT_DIR}/test_il_parse_duplicate_block.cpp)
    target_link_libraries(test_il_parse_duplicate_block PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_duplicate_block PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_duplicate_block test_il_parse_duplicate_block)

    viper_add_test(test_il_parse_missing_paren ${_VIPER_IL_UNIT_DIR}/test_il_parse_missing_paren.cpp)
    target_link_libraries(test_il_parse_missing_paren PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_missing_paren PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_missing_paren test_il_parse_missing_paren)

    viper_add_test(test_il_parse_extern_whitespace ${_VIPER_IL_UNIT_DIR}/test_il_parse_extern_whitespace.cpp)
    target_link_libraries(test_il_parse_extern_whitespace PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_extern_whitespace PRIVATE PARSE_ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse-roundtrip")
    viper_add_ctest(test_il_parse_extern_whitespace test_il_parse_extern_whitespace)

    viper_add_test(test_il_parse_extern_empty_params ${_VIPER_IL_UNIT_DIR}/test_il_parse_extern_empty_params.cpp)
    target_link_libraries(test_il_parse_extern_empty_params PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_extern_empty_params test_il_parse_extern_empty_params)

    # call.indirect roundtrip + VM execution smoke
    viper_add_test(il_call_indirect_roundtrip ${CMAKE_SOURCE_DIR}/src/tests/il/CallIndirectRoundTrip.cpp)
    target_link_libraries(il_call_indirect_roundtrip PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_vm)
    viper_add_ctest(il_call_indirect_roundtrip il_call_indirect_roundtrip)

    viper_add_test(test_il_parse_call_ret_type ${_VIPER_IL_UNIT_DIR}/test_il_parse_call_ret_type.cpp)
    target_link_libraries(test_il_parse_call_ret_type PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_call_ret_type test_il_parse_call_ret_type)

    viper_add_test(test_il_parse_negative ${_VIPER_IL_UNIT_DIR}/test_il_parse_negative.cpp)
    target_link_libraries(test_il_parse_negative PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(test_il_parse_negative PRIVATE BAD_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    viper_add_ctest(test_il_parse_negative test_il_parse_negative)

    viper_add_test(
            test_il_parse_operand_whitespace
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_operand_whitespace.cpp)
    target_link_libraries(test_il_parse_operand_whitespace PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_parse_operand_whitespace
            PRIVATE OPERAND_WS_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/parse")
    viper_add_ctest(test_il_parse_operand_whitespace test_il_parse_operand_whitespace)

    viper_add_test(
            test_il_parse_dollar_identifiers
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_dollar_identifiers.cpp)
    target_link_libraries(test_il_parse_dollar_identifiers PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_dollar_identifiers test_il_parse_dollar_identifiers)

    viper_add_test(test_il_parse_comment ${_VIPER_IL_UNIT_DIR}/test_il_parse_comment.cpp)
    target_link_libraries(test_il_parse_comment PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_comment test_il_parse_comment)

    viper_add_test(test_il_comments ${_VIPER_IL_UNIT_DIR}/test_il_comments.cpp)
    target_link_libraries(test_il_comments PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_comments test_il_comments)

    viper_add_test(test_il_parse_missing_eq ${_VIPER_IL_UNIT_DIR}/test_il_parse_missing_eq.cpp)
    target_link_libraries(test_il_parse_missing_eq PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_missing_eq test_il_parse_missing_eq)

    viper_add_test(test_il_parse_missing_version ${_VIPER_IL_UNIT_DIR}/test_il_parse_missing_version.cpp)
    target_link_libraries(test_il_parse_missing_version PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_missing_version test_il_parse_missing_version)

    viper_add_test(test_il_parse_bom ${_VIPER_IL_UNIT_DIR}/test_il_parse_bom.cpp)
    target_link_libraries(test_il_parse_bom PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_bom test_il_parse_bom)

    viper_add_test(test_il_parse_first_error ${_VIPER_IL_UNIT_DIR}/test_il_parse_first_error.cpp)
    target_link_libraries(test_il_parse_first_error PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_first_error test_il_parse_first_error)

    viper_add_test(test_il_parse_global_missing_quotes ${_VIPER_IL_UNIT_DIR}/test_il_parse_global_missing_quotes.cpp)
    target_link_libraries(test_il_parse_global_missing_quotes PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_global_missing_quotes test_il_parse_global_missing_quotes)

    viper_add_test(
            test_il_parse_global_trailing_characters
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_global_trailing_characters.cpp)
    target_link_libraries(test_il_parse_global_trailing_characters PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(
            test_il_parse_global_trailing_characters test_il_parse_global_trailing_characters)

    viper_add_test(test_il_parse_global_missing_at ${_VIPER_IL_UNIT_DIR}/test_il_parse_global_missing_at.cpp)
    target_link_libraries(test_il_parse_global_missing_at PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_global_missing_at test_il_parse_global_missing_at)

    viper_add_test(test_il_parse_global_missing_name ${_VIPER_IL_UNIT_DIR}/test_il_parse_global_missing_name.cpp)
    target_link_libraries(test_il_parse_global_missing_name PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_global_missing_name test_il_parse_global_missing_name)

    viper_add_test(test_il_parse_global_missing_type ${_VIPER_IL_UNIT_DIR}/test_il_parse_global_missing_type.cpp)
    target_link_libraries(test_il_parse_global_missing_type PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_global_missing_type test_il_parse_global_missing_type)

    viper_add_test(test_il_parse_global_unsupported_type ${_VIPER_IL_UNIT_DIR}/test_il_parse_global_unsupported_type.cpp)
    target_link_libraries(test_il_parse_global_unsupported_type PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_global_unsupported_type test_il_parse_global_unsupported_type)

    viper_add_test(test_il_parse_string_escapes ${_VIPER_IL_UNIT_DIR}/test_il_parse_string_escapes.cpp)
    target_link_libraries(test_il_parse_string_escapes PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_string_escapes test_il_parse_string_escapes)

    viper_add_test(test_il_parse_conststr_escapes ${_VIPER_IL_UNIT_DIR}/test_il_parse_conststr_escapes.cpp)
    target_link_libraries(test_il_parse_conststr_escapes PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_conststr_escapes test_il_parse_conststr_escapes)

    viper_add_test(test_il_parse_invalid_type ${_VIPER_IL_UNIT_DIR}/test_il_parse_invalid_type.cpp)
    target_link_libraries(test_il_parse_invalid_type PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_invalid_type test_il_parse_invalid_type)

    viper_add_test(test_il_parse_misc_instructions ${_VIPER_IL_UNIT_DIR}/test_il_parse_misc_instructions.cpp)
    target_link_libraries(test_il_parse_misc_instructions PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_misc_instructions test_il_parse_misc_instructions)

    viper_add_test(
            test_il_parse_call_string_comma
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_call_string_comma.cpp)
    target_link_libraries(test_il_parse_call_string_comma PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_call_string_comma test_il_parse_call_string_comma)

    viper_add_test(
            test_il_parse_call_trailing_junk
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_call_trailing_junk.cpp)
    target_link_libraries(test_il_parse_call_trailing_junk PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_call_trailing_junk test_il_parse_call_trailing_junk)

    viper_add_test(
            test_il_parse_keyword_boundary
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_keyword_boundary.cpp)
    target_link_libraries(test_il_parse_keyword_boundary PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_keyword_boundary test_il_parse_keyword_boundary)

    viper_add_test(
            test_il_parse_global_keyword_boundary
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_global_keyword_boundary.cpp)
    target_link_libraries(test_il_parse_global_keyword_boundary PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(
            test_il_parse_global_keyword_boundary test_il_parse_global_keyword_boundary)

    viper_add_test(
            test_il_parse_missing_operand_between_commas
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_missing_operand_between_commas.cpp)
    target_link_libraries(test_il_parse_missing_operand_between_commas PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_missing_operand_between_commas test_il_parse_missing_operand_between_commas)

    viper_add_test(
            test_il_parse_branch_missing_label
            ${_VIPER_IL_UNIT_DIR}/test_il_parse_branch_missing_label.cpp)
    target_link_libraries(test_il_parse_branch_missing_label PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_parse_branch_missing_label test_il_parse_branch_missing_label)

    viper_add_test(test_il_function_parser_errors ${_VIPER_IL_UNIT_DIR}/test_il_function_parser_errors.cpp)
    target_link_libraries(test_il_function_parser_errors PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_function_parser_errors test_il_function_parser_errors)

    viper_add_test(test_il_parse_loc_errors ${_VIPER_IL_UNIT_DIR}/test_il_parse_loc_errors.cpp)
    target_link_libraries(test_il_parse_loc_errors PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_parse_loc_errors test_il_parse_loc_errors)

    viper_add_test(test_expected_api_build ${_VIPER_IL_UNIT_DIR}/test_expected_api_build.cpp)
    target_link_libraries(test_expected_api_build PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_expected_api_build test_expected_api_build)

    viper_add_test(test_il_verify_trap ${_VIPER_IL_UNIT_DIR}/test_il_verify_trap.cpp)
    target_link_libraries(test_il_verify_trap PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_verify_trap test_il_verify_trap)

    viper_add_test(test_il_verify_forward_call ${_VIPER_IL_UNIT_DIR}/test_il_verify_forward_call.cpp)
    target_link_libraries(test_il_verify_forward_call PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_verify_forward_call test_il_verify_forward_call)

    viper_add_test(test_il_verify_release_lifetime ${_VIPER_IL_UNIT_DIR}/test_il_verify_release_lifetime.cpp)
    target_link_libraries(test_il_verify_release_lifetime PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_verify_release_lifetime test_il_verify_release_lifetime)

    viper_add_test(test_il_verify_table_memory_ok ${_VIPER_IL_UNIT_DIR}/test_il_verify_table_memory_ok.cpp)
    target_link_libraries(test_il_verify_table_memory_ok PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_verify_table_memory_ok test_il_verify_table_memory_ok)

    viper_add_test(test_il_verify_table_mismatch ${_VIPER_IL_UNIT_DIR}/test_il_verify_table_mismatch.cpp)
    target_link_libraries(test_il_verify_table_mismatch PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_verify_table_mismatch test_il_verify_table_mismatch)

    viper_add_test(
            test_il_verify_calls_table_interop
            ${_VIPER_IL_UNIT_DIR}/test_il_verify_calls_table_interop.cpp)
    target_link_libraries(test_il_verify_calls_table_interop PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(
            test_il_verify_calls_table_interop test_il_verify_calls_table_interop)

    viper_add_test(test_il_verify_eh_table ${_VIPER_IL_UNIT_DIR}/test_il_verify_eh_table.cpp)
    target_link_libraries(test_il_verify_eh_table PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    viper_add_ctest(test_il_verify_eh_table test_il_verify_eh_table)

    viper_add_test(test_il_type_inference ${_VIPER_IL_UNIT_DIR}/test_il_type_inference.cpp)
    target_link_libraries(test_il_type_inference PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_type_inference test_il_type_inference)

    viper_add_test(test_runtime_registry ${_VIPER_IL_UNIT_DIR}/test_runtime_registry.cpp)
    target_link_libraries(test_runtime_registry PRIVATE il_runtime)
    viper_add_ctest(test_runtime_registry test_runtime_registry)

    viper_add_test(
            test_runtime_signatures_self_check
            ${_VIPER_IL_UNIT_DIR}/RuntimeSignaturesSelfCheckTests.cpp)
    target_link_libraries(test_runtime_signatures_self_check PRIVATE il_runtime)
    viper_add_ctest(test_runtime_signatures_self_check test_runtime_signatures_self_check)

    viper_add_test(
            test_runtime_invariant_mode
            ${_VIPER_IL_UNIT_DIR}/test_runtime_invariant_mode.cpp)
    target_link_libraries(test_runtime_invariant_mode PRIVATE il_runtime)
    viper_add_ctest(test_runtime_invariant_mode test_runtime_invariant_mode)

    viper_add_test(
            test_il_runtime_signature_effects
            ${_VIPER_IL_UNIT_DIR}/il/runtime/test_SignaturesPurity.cpp)
    target_link_libraries(test_il_runtime_signature_effects PRIVATE il_runtime)
    viper_add_ctest(
            test_il_runtime_signature_effects test_il_runtime_signature_effects)

    viper_add_test(test_il_instruction_checker ${_VIPER_IL_UNIT_DIR}/test_il_instruction_checker.cpp)
    target_link_libraries(test_il_instruction_checker PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_instruction_checker test_il_instruction_checker)

    viper_add_test(
            test_il_instruction_checker_binary_arity
            ${_VIPER_IL_UNIT_DIR}/test_il_instruction_checker_binary_arity.cpp)
    target_link_libraries(
            test_il_instruction_checker_binary_arity PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(
            test_il_instruction_checker_binary_arity test_il_instruction_checker_binary_arity)

    viper_add_test(test_il_error_resume_ir ${_VIPER_IL_UNIT_DIR}/test_il_error_resume_ir.cpp)
    target_link_libraries(test_il_error_resume_ir PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_error_resume_ir test_il_error_resume_ir)

    viper_add_test(test_il_control_flow_checker ${_VIPER_IL_UNIT_DIR}/test_il_control_flow_checker.cpp)
    target_link_libraries(test_il_control_flow_checker PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_control_flow_checker test_il_control_flow_checker)

    viper_add_test(test_il_exception_handler_analysis
            ${_VIPER_IL_UNIT_DIR}/test_il_exception_handler_analysis.cpp)
    target_link_libraries(test_il_exception_handler_analysis PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_exception_handler_analysis test_il_exception_handler_analysis)

    viper_add_test(test_il_verify_eh_checks
            ${_VIPER_IL_UNIT_DIR}/test_il_verify_eh_checks.cpp)
    target_link_libraries(test_il_verify_eh_checks PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_verify_eh_checks test_il_verify_eh_checks)

    viper_add_test(test_il_branch_verifier ${_VIPER_IL_UNIT_DIR}/test_il_branch_verifier.cpp)
    target_link_libraries(test_il_branch_verifier PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_branch_verifier PRIVATE NEGATIVE_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/negatives")
    viper_add_ctest(test_il_branch_verifier test_il_branch_verifier)
endfunction()

function(viper_add_il_utils_test)
    if (TARGET test_il_utils)
        return()
    endif ()

    set(_VIPER_IL_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
    viper_add_test(test_il_utils ${_VIPER_IL_DIR}/UtilsTests.cpp)
    target_link_libraries(test_il_utils PRIVATE il_utils)
    viper_add_ctest(test_il_utils test_il_utils)

    viper_add_test(
            test_il_simplifycfg_bitvector_resize
            ${_VIPER_IL_DIR}/transform/simplifycfg_bitvector_resize.cpp)
    target_link_libraries(
            test_il_simplifycfg_bitvector_resize PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_simplifycfg_bitvector_resize test_il_simplifycfg_bitvector_resize)

    viper_add_test(
            test_il_simplifycfg_cbr_fold
            ${_VIPER_IL_DIR}/transform/simplifycfg_cbr_fold.cpp)
    target_link_libraries(
            test_il_simplifycfg_cbr_fold PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_simplifycfg_cbr_fold test_il_simplifycfg_cbr_fold)

    viper_add_test(
            test_il_simplifycfg_merge_single_pred
            ${_VIPER_IL_DIR}/transform/simplifycfg_merge_single_pred.cpp)
    target_link_libraries(
            test_il_simplifycfg_merge_single_pred PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_simplifycfg_merge_single_pred test_il_simplifycfg_merge_single_pred)

    viper_add_test(
            test_il_simplifycfg_eh_guard
            ${_VIPER_IL_DIR}/transform/simplifycfg_eh_guard.cpp)
    target_link_libraries(
            test_il_simplifycfg_eh_guard PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_simplifycfg_eh_guard test_il_simplifycfg_eh_guard)

    viper_add_test(
            test_il_simplifycfg_shrink_params
            ${_VIPER_IL_DIR}/transform/simplifycfg_shrink_params.cpp)
    target_link_libraries(
            test_il_simplifycfg_shrink_params PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_simplifycfg_shrink_params test_il_simplifycfg_shrink_params)

    viper_add_test(
            test_il_simplifycfg_values_equal_fp
            ${_VIPER_IL_DIR}/transform/simplifycfg_values_equal_fp.cpp)
    target_link_libraries(
            test_il_simplifycfg_values_equal_fp PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_simplifycfg_values_equal_fp test_il_simplifycfg_values_equal_fp)

    viper_add_test(
            test_il_simplifycfg_pass_manager_cfg_invalidation
            ${_VIPER_IL_DIR}/transform/simplifycfg_pass_manager_cfg_invalidation.cpp)
    target_link_libraries(
            test_il_simplifycfg_pass_manager_cfg_invalidation PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_simplifycfg_pass_manager_cfg_invalidation test_il_simplifycfg_pass_manager_cfg_invalidation)

    viper_add_test(
            test_il_peephole_use_count
            ${_VIPER_IL_DIR}/transform/peephole_use_count.cpp)
    target_link_libraries(
            test_il_peephole_use_count PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_peephole_use_count test_il_peephole_use_count)

    add_executable(
            test_il_alias_precision
            ${_VIPER_IL_DIR}/transform/alias_precision.cpp)
    target_link_libraries(
            test_il_alias_precision PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_alias_precision test_il_alias_precision)

    viper_add_test(
            test_il_dce_param_compaction
            ${_VIPER_IL_DIR}/transform/dce_param_compaction.cpp)
    target_link_libraries(
            test_il_dce_param_compaction PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_dce_param_compaction test_il_dce_param_compaction)

    viper_add_test(
            test_il_loop_simplify_preheader
            ${VIPER_TESTS_DIR}/unit/il/transform/test_LoopSimplify.cpp)
    target_link_libraries(
            test_il_loop_simplify_preheader PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_loop_simplify_preheader test_il_loop_simplify_preheader)

    viper_add_test(
            test_il_loop_simplify_ptr_stability
            ${VIPER_TESTS_DIR}/unit/il/transform/test_LoopSimplify_PtrStability.cpp)
    target_link_libraries(
            test_il_loop_simplify_ptr_stability PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_loop_simplify_ptr_stability test_il_loop_simplify_ptr_stability)

    viper_add_test(
            test_il_licm_basic
            ${VIPER_TESTS_DIR}/unit/il/transform/test_LICM.cpp)
    target_link_libraries(
            test_il_licm_basic PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_licm_basic test_il_licm_basic)

    viper_add_test(
            test_il_sccp_phi_branch
            ${VIPER_TESTS_DIR}/unit/il/transform/test_SCCP.cpp)
    target_link_libraries(
            test_il_sccp_phi_branch PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_sccp_phi_branch test_il_sccp_phi_branch)

    # Induction variable simplification tests
    viper_add_test(
            test_il_indvars_basic
            ${VIPER_TESTS_DIR}/unit/il/transform/test_IndVarSimplify.cpp)
    target_link_libraries(
            test_il_indvars_basic PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_indvars_basic test_il_indvars_basic)

    # GVN + Redundant Load Elimination tests
    viper_add_test(
            test_il_gvn_basic
            ${VIPER_TESTS_DIR}/unit/il/transform/test_GVN.cpp)
    target_link_libraries(
            test_il_gvn_basic PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_gvn_basic test_il_gvn_basic)

    viper_add_test(
            test_il_gvn_cse_basic
            ${VIPER_TESTS_DIR}/unit/il/transform/test_gvn_cse_basic.cpp)
    target_link_libraries(
            test_il_gvn_cse_basic PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_gvn_cse_basic test_il_gvn_cse_basic)

    viper_add_test(
            test_il_loopsimplify_invariants
            ${VIPER_TESTS_DIR}/unit/il/transform/loopsimplify_invariants.cpp)
    target_link_libraries(
            test_il_loopsimplify_invariants PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_loopsimplify_invariants test_il_loopsimplify_invariants)

    viper_add_test(
            test_il_licm_hoist ${VIPER_TESTS_DIR}/unit/il/transform/licm_basic.cpp)
    target_link_libraries(
            test_il_licm_hoist PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_licm_hoist test_il_licm_hoist)

    viper_add_test(
            test_il_indvars_strength ${VIPER_TESTS_DIR}/unit/il/transform/indvars_basic.cpp)
    target_link_libraries(
            test_il_indvars_strength PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_indvars_strength test_il_indvars_strength)

    viper_add_test(
            test_il_checkopt_redundancy
            ${VIPER_TESTS_DIR}/unit/il/transform/checkopt_redundancy.cpp)
    target_link_libraries(
            test_il_checkopt_redundancy PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(
            test_il_checkopt_redundancy test_il_checkopt_redundancy)

    viper_add_test(
            test_il_inline_basic
            ${VIPER_TESTS_DIR}/unit/il/transform/test_Inline.cpp)
    target_link_libraries(
            test_il_inline_basic PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_inline_basic test_il_inline_basic)
    viper_add_test(
            test_il_inline_multiblock
            ${_VIPER_IL_DIR}/transform/inline_multiblock.cpp)
    target_link_libraries(
            test_il_inline_multiblock PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_inline_multiblock test_il_inline_multiblock)
    viper_add_test(
            test_il_mem2reg_sroa
            ${_VIPER_IL_DIR}/transform/mem2reg_sroa.cpp)
    target_link_libraries(
            test_il_mem2reg_sroa PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_mem2reg_sroa test_il_mem2reg_sroa)

    viper_add_test(
            test_il_check_opt
            ${VIPER_TESTS_DIR}/unit/il/transform/test_CheckOpt.cpp)
    target_link_libraries(
            test_il_check_opt PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_check_opt test_il_check_opt)

    viper_add_test(
            test_il_late_cleanup
            ${VIPER_TESTS_DIR}/unit/il/transform/test_LateCleanup.cpp)
    target_link_libraries(
            test_il_late_cleanup PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_late_cleanup test_il_late_cleanup)

    viper_add_test(
            test_il_latecleanup_fixpoint
            ${_VIPER_IL_DIR}/transform/latecleanup_fixpoint.cpp)
    target_link_libraries(
            test_il_latecleanup_fixpoint PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_latecleanup_fixpoint test_il_latecleanup_fixpoint)

    # Loop unrolling tests
    viper_add_test(
            test_il_loop_unroll
            ${VIPER_TESTS_DIR}/unit/il/transform/test_LoopUnroll.cpp)
    target_link_libraries(
            test_il_loop_unroll PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_loop_unroll test_il_loop_unroll)

    # Jump threading tests
    viper_add_test(
            test_il_jump_threading
            ${_VIPER_IL_DIR}/transform/jumpthreading_basic.cpp)
    target_link_libraries(
            test_il_jump_threading PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_jump_threading test_il_jump_threading)

    # Cross-block DSE tests
    viper_add_test(
            test_il_dse_crossblock
            ${_VIPER_IL_DIR}/transform/dse_crossblock.cpp)
    target_link_libraries(
            test_il_dse_crossblock PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_dse_crossblock test_il_dse_crossblock)

    # Enhanced constant folding tests
    viper_add_test(
            test_il_constfold_enhanced
            ${_VIPER_IL_DIR}/transform/constfold_enhanced.cpp)
    target_link_libraries(
            test_il_constfold_enhanced PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_build il_transform)
    viper_add_ctest(test_il_constfold_enhanced test_il_constfold_enhanced)

    viper_add_test(test_il_opcode_info ${_VIPER_IL_DIR}/OpcodeInfoTests.cpp)
    target_link_libraries(test_il_opcode_info PRIVATE ${VIPER_IL_CORE_IO_LIBS})
    viper_add_ctest(test_il_opcode_info test_il_opcode_info)

    viper_add_test(
            test_il_invalid_eh
            ${_VIPER_IL_DIR}/InvalidEhTests.cpp
            TEST_NAME il_InvalidEhTests)
    target_link_libraries(test_il_invalid_eh PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_api)
    target_compile_definitions(
            test_il_invalid_eh PRIVATE INVALID_EH_DIR="${CMAKE_SOURCE_DIR}/src/tests/il/invalid_eh")
endfunction()

function(viper_add_il_analysis_tests)
    if (TARGET test_analysis_cfg)
        return()
    endif ()

    set(_VIPER_ANALYSIS_DIR ${VIPER_TESTS_DIR}/analysis)

    viper_add_test(test_analysis_cfg ${_VIPER_ANALYSIS_DIR}/CFGTests.cpp)
    target_link_libraries(test_analysis_cfg PRIVATE ${VIPER_IL_ANALYSIS_LIBS})
    viper_add_ctest(test_analysis_cfg test_analysis_cfg)

    viper_add_test(test_analysis_graph_order ${_VIPER_ANALYSIS_DIR}/GraphOrderTests.cpp)
    target_link_libraries(test_analysis_graph_order PRIVATE ${VIPER_IL_ANALYSIS_LIBS})
    viper_add_ctest(test_analysis_graph_order test_analysis_graph_order)

    viper_add_test(test_analysis_dominators ${_VIPER_ANALYSIS_DIR}/DominatorsTests.cpp)
    target_link_libraries(test_analysis_dominators PRIVATE ${VIPER_IL_ANALYSIS_LIBS})
    viper_add_ctest(test_analysis_dominators test_analysis_dominators)

    viper_add_test(test_analysis_acyclic ${_VIPER_ANALYSIS_DIR}/AcyclicTests.cpp)
    target_link_libraries(test_analysis_acyclic PRIVATE ${VIPER_IL_ANALYSIS_LIBS})
    viper_add_ctest(test_analysis_acyclic test_analysis_acyclic)

    viper_add_test(test_analysis_basic_aa ${_VIPER_ANALYSIS_DIR}/BasicAATests.cpp)
    target_link_libraries(test_analysis_basic_aa PRIVATE ${VIPER_IL_ANALYSIS_LIBS})
    viper_add_ctest(test_analysis_basic_aa test_analysis_basic_aa)
endfunction()

function(viper_add_il_verify_examples)
    if (TEST il_verify_ex1)
        return()
    endif ()

    viper_add_ctest(il_verify_ex1 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex1_hello_cond.il)
    viper_add_ctest(il_verify_ex2 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex2_sum_1_to_10.il)
    viper_add_ctest(il_verify_ex3 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex3_table_5x5.il)
    viper_add_ctest(il_verify_ex4 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex4_factorial.il)
    viper_add_ctest(il_verify_ex5 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex5_strings.il)
    viper_add_ctest(il_verify_ex6 $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex6_heap_array_avg.il)
endfunction()

function(viper_add_il_invalid_tests)
    if (TEST il_verify_invalid_bad_types)
        return()
    endif ()

    set(IL_VERIFY $<TARGET_FILE:il-verify>)
    set(_VIPER_INVALID_DIR ${CMAKE_SOURCE_DIR}/src/tests/golden/invalid_il)

    viper_add_ctest(il_verify_invalid_bad_types
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/bad_types.il
            -DEXPECT=operand\ type\ mismatch
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_missing_term
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/missing_terminator.il
            -DEXPECT=missing\ terminator
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_wrong_call_arity
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/wrong_call_arity.il
            -DEXPECT=call\ arg\ count
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_unknown_symbol
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/unknown_symbol.il
            -DEXPECT=unknown\ callee
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_bad_load_store
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/bad_load_store.il
            -DEXPECT=pointer\ type\ mismatch
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_wrong_extern_sig
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/wrong_extern_sig.il
            -DEXPECT=extern\ @rt_print_str\ signature\ mismatch
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_wrong_extern_call
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/wrong_extern_call.il
            -DEXPECT=call\ arg\ type\ mismatch
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_wrong_block_arg_arity
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/wrong_block_arg_arity.il
            -DEXPECT=bad\ arg\ count
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_wrong_block_arg_type
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/wrong_block_arg_type.il
            -DEXPECT=arg\ type\ mismatch
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_duplicate_param
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/duplicate_param.il
            -DEXPECT=duplicate\ param
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_param_use_before_entry
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/param_use_before_entry.il
            -DEXPECT=use\ before\ def
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    viper_add_ctest(il_verify_invalid_unknown_temp
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_DIR}/unknown_temp.il
            -DEXPECT=unknown\ temp
            -P ${_VIPER_INVALID_DIR}/check_invalid.cmake)

    set(_VIPER_NEGATIVE_DIR ${CMAKE_SOURCE_DIR}/src/tests/il/negatives)

    viper_add_ctest(il_verify_negative_use_after_release
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_NEGATIVE_DIR}/use_after_release.il
            -DEXPECT_FILE=${_VIPER_NEGATIVE_DIR}/use_after_release.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)

    viper_add_ctest(il_verify_negative_unchecked_srem
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_NEGATIVE_DIR}/unchecked_srem.il
            -DEXPECT_FILE=${_VIPER_NEGATIVE_DIR}/unchecked_srem.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)

    viper_add_ctest(il_verify_negative_narrow_cast_nochk
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_NEGATIVE_DIR}/narrow_cast_nochk.il
            -DEXPECT_FILE=${_VIPER_NEGATIVE_DIR}/narrow_cast_nochk.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)

    set(_VIPER_INVALID_EH_DIR ${CMAKE_SOURCE_DIR}/src/tests/il/invalid_eh)

    viper_add_ctest(il_verify_invalid_eh_underflow
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/pop_underflow.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/pop_underflow.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)

    viper_add_ctest(il_verify_invalid_eh_leak
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/push_leak_branch.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/push_leak_branch.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
    viper_add_ctest(il_verify_invalid_eh_resume_same_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/resume_same_without_token.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/resume_same_without_token.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
    viper_add_ctest(il_verify_invalid_eh_resume_next_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/resume_next_without_token.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/resume_next_without_token.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
    viper_add_ctest(il_verify_invalid_eh_resume_label_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/resume_label_without_token.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/resume_label_without_token.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
    viper_add_ctest(il_verify_invalid_eh_resume_same_join_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/resume_same_join_without_token.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/resume_same_join_without_token.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
    viper_add_ctest(il_verify_invalid_eh_resume_next_join_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/resume_next_join_without_token.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/resume_next_join_without_token.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
    viper_add_ctest(il_verify_invalid_eh_resume_label_join_no_token
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/resume_label_join_without_token.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/resume_label_join_without_token.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
    viper_add_ctest(il_verify_invalid_eh_resume_label_non_postdom
            ${CMAKE_COMMAND}
            -DIL_VERIFY=${IL_VERIFY}
            -DFILE=${_VIPER_INVALID_EH_DIR}/resume_label_non_postdom.il
            -DEXPECT_FILE=${_VIPER_INVALID_EH_DIR}/resume_label_non_postdom.expected
            -P ${_VIPER_NEGATIVE_DIR}/check_negative.cmake)
endfunction()

function(viper_add_il_liveness_tests)
    if (TARGET test_il_liveness)
        return()
    endif ()

    set(_VIPER_IL_UNIT_DIR ${VIPER_TESTS_DIR}/unit)

    viper_add_test(test_il_liveness ${_VIPER_IL_UNIT_DIR}/test_il_liveness.cpp)
    target_link_libraries(test_il_liveness PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_transform il_api)
    viper_add_ctest(test_il_liveness test_il_liveness)

    viper_add_test(test_il_pass_manager ${_VIPER_IL_UNIT_DIR}/test_il_pass_manager.cpp)
    target_link_libraries(test_il_pass_manager PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_transform il_api)
    viper_add_ctest(test_il_pass_manager test_il_pass_manager)

    viper_add_test(test_il_switch_i32 ${_VIPER_IL_UNIT_DIR}/test_il_switch_i32.cpp)
    target_link_libraries(test_il_switch_i32 PRIVATE ${VIPER_IL_CORE_IO_LIBS} il_transform il_api)
    viper_add_ctest(test_il_switch_i32 test_il_switch_i32)
endfunction()
