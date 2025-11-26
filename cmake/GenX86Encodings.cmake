#
# Generate x86_64 encoding tables from JSON spec using CMake's JSON support.
# Inputs (passed via -D):
#   SPEC_FILE   - path to tools/spec/x86_64_encodings.json
#   OUTPUT_DIR  - directory to write generated .inc files
#

if(NOT DEFINED SPEC_FILE)
  message(FATAL_ERROR "GenX86Encodings.cmake: SPEC_FILE is not set")
endif()
if(NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "GenX86Encodings.cmake: OUTPUT_DIR is not set")
endif()

file(READ "${SPEC_FILE}" _JSON_TEXT)

# Helper to append a line with newline to a variable
function(_append var text)
  set(${var} "${${var}}${text}\n" PARENT_SCOPE)
endfunction()

# ----------------- EncodingTable.inc -----------------
string(JSON _ENC_LEN LENGTH "${_JSON_TEXT}" encodings)

set(_enc_content "")
_append(_enc_content "// Generated from tools/spec/x86_64_encodings.json")
_append(_enc_content "// DO NOT EDIT - regenerate with CMake (cmake/GenX86Encodings.cmake)")
_append(_enc_content "")
_append(_enc_content "static constexpr std::array<EncodingRow, ${_ENC_LEN}> kEncodingTable = {{")

math(EXPR _ENC_LAST "${_ENC_LEN} - 1")
foreach(_i RANGE 0 ${_ENC_LAST})
  string(JSON _ITEM GET "${_JSON_TEXT}" encodings ${_i})
  string(JSON _OPCODE   GET "${_ITEM}" opcode)
  string(JSON _MNEMONIC GET "${_ITEM}" mnemonic)
  string(JSON _FORM     GET "${_ITEM}" form)
  string(JSON _ORDER    GET "${_ITEM}" order)

  # pattern -> makePattern(OperandKind::X, Y, Z) padded to 3
  string(JSON _PAT_LEN LENGTH "${_ITEM}" pattern)
  set(_PAT_ARGS)
  foreach(_j RANGE 0 2)
    if(_j LESS _PAT_LEN)
      string(JSON _PK GET "${_ITEM}" pattern ${_j})
      list(APPEND _PAT_ARGS "OperandKind::${_PK}")
    else()
      list(APPEND _PAT_ARGS "OperandKind::None")
    endif()
  endforeach()
  list(JOIN _PAT_ARGS ", " _PAT_JOINED)

  # flags -> join or EncodingFlag::None
  set(_FLAGS_STR "")
  string(JSON _FL_LEN LENGTH "${_ITEM}" flags)
  if(_FL_LEN GREATER 0)
    set(_FLAG_PARTS "")
    math(EXPR _FL_LAST "${_FL_LEN} - 1")
    foreach(_k RANGE 0 ${_FL_LAST})
      string(JSON _F GET "${_ITEM}" flags ${_k})
      list(APPEND _FLAG_PARTS "EncodingFlag::${_F}")
    endforeach()
    list(JOIN _FLAG_PARTS " | " _FLAGS_STR)
  else()
    set(_FLAGS_STR "EncodingFlag::None")
  endif()

  _append(_enc_content "    {MOpcode::${_OPCODE},")
  _append(_enc_content "     \"${_MNEMONIC}\",")
  _append(_enc_content "     EncodingForm::${_FORM},")
  _append(_enc_content "     OperandOrder::${_ORDER},")
  _append(_enc_content "     makePattern(${_PAT_JOINED}),")
  _append(_enc_content "     ${_FLAGS_STR}},")
endforeach()
_append(_enc_content "}};")
_append(_enc_content "")

# ----------------- OpFmtTable.inc -----------------
set(_opfmt_content "")
_append(_opfmt_content "// Generated from tools/spec/x86_64_encodings.json")
_append(_opfmt_content "// DO NOT EDIT - regenerate with CMake (cmake/GenX86Encodings.cmake)")
_append(_opfmt_content "")
_append(_opfmt_content "static constexpr std::array<OpFmt, ${_ENC_LEN}> kOpFmt = {{")

foreach(_i RANGE 0 ${_ENC_LAST})
  string(JSON _ITEM GET "${_JSON_TEXT}" encodings ${_i})
  string(JSON _OPCODE   GET "${_ITEM}" opcode)
  string(JSON _MNEMONIC GET "${_ITEM}" mnemonic)
  string(JSON _FORM     GET "${_ITEM}" form)
  string(JSON _ORDER    GET "${_ITEM}" order)
  string(JSON _PAT_LEN LENGTH "${_ITEM}" pattern)

  # Determine opfmt flags
  set(_FMT "")
  if(_ORDER STREQUAL "LEA")
    set(_FMT "kFmtLea")
  elseif(_ORDER STREQUAL "SHIFT")
    set(_FMT "kFmtShift")
  elseif(_ORDER STREQUAL "MOVZX_RR8")
    set(_FMT "kFmtMovzx8")
  elseif(_ORDER STREQUAL "CALL")
    set(_FMT "kFmtCall")
  elseif(_ORDER STREQUAL "JUMP")
    set(_FMT "kFmtJump")
  elseif(_ORDER STREQUAL "JCC")
    set(_FMT "kFmtJump | kFmtCond")
  elseif(_ORDER STREQUAL "SETCC")
    set(_FMT "kFmtCond | kFmtSetcc")
  elseif(_ORDER STREQUAL "DIRECT")
    set(_FMT "kFmtDirect")
  elseif(_FORM STREQUAL "Call")
    set(_FMT "kFmtCall")
  elseif(_FORM STREQUAL "Jump")
    set(_FMT "kFmtJump")
  elseif(_FORM STREQUAL "Condition" AND _OPCODE STREQUAL "JCC")
    set(_FMT "kFmtJump | kFmtCond")
  elseif(_FORM STREQUAL "Setcc")
    set(_FMT "kFmtCond | kFmtSetcc")
  endif()

  set(_FMT_VAL "0U")
  if(_FMT)
    string(FIND "${_FMT}" " | " _has_or)
    if(_has_or GREATER -1)
      set(_FMT_VAL "static_cast<std::uint8_t>(${_FMT})")
    else()
      set(_FMT_VAL ${_FMT})
    endif()
  endif()

  _append(_opfmt_content "    {MOpcode::${_OPCODE}, \"${_MNEMONIC}\", ${_PAT_LEN}U, ${_FMT_VAL}},")
endforeach()
_append(_opfmt_content "}};")
_append(_opfmt_content "")

# Write outputs
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_DIR}/EncodingTable.inc" "${_enc_content}")
file(WRITE "${OUTPUT_DIR}/OpFmtTable.inc"     "${_opfmt_content}")

