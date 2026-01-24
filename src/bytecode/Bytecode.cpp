// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.

#include "bytecode/Bytecode.hpp"

namespace viper::bytecode
{

const char *opcodeName(BCOpcode op)
{
    switch (op)
    {
        // Stack Operations
        case BCOpcode::NOP:
            return "NOP";
        case BCOpcode::DUP:
            return "DUP";
        case BCOpcode::DUP2:
            return "DUP2";
        case BCOpcode::POP:
            return "POP";
        case BCOpcode::POP2:
            return "POP2";
        case BCOpcode::SWAP:
            return "SWAP";
        case BCOpcode::ROT3:
            return "ROT3";

        // Local Variable Operations
        case BCOpcode::LOAD_LOCAL:
            return "LOAD_LOCAL";
        case BCOpcode::STORE_LOCAL:
            return "STORE_LOCAL";
        case BCOpcode::LOAD_LOCAL_W:
            return "LOAD_LOCAL_W";
        case BCOpcode::STORE_LOCAL_W:
            return "STORE_LOCAL_W";
        case BCOpcode::INC_LOCAL:
            return "INC_LOCAL";
        case BCOpcode::DEC_LOCAL:
            return "DEC_LOCAL";

        // Constant Loading
        case BCOpcode::LOAD_I8:
            return "LOAD_I8";
        case BCOpcode::LOAD_I16:
            return "LOAD_I16";
        case BCOpcode::LOAD_I32:
            return "LOAD_I32";
        case BCOpcode::LOAD_I64:
            return "LOAD_I64";
        case BCOpcode::LOAD_F64:
            return "LOAD_F64";
        case BCOpcode::LOAD_STR:
            return "LOAD_STR";
        case BCOpcode::LOAD_NULL:
            return "LOAD_NULL";
        case BCOpcode::LOAD_ZERO:
            return "LOAD_ZERO";
        case BCOpcode::LOAD_ONE:
            return "LOAD_ONE";
        case BCOpcode::LOAD_GLOBAL:
            return "LOAD_GLOBAL";
        case BCOpcode::STORE_GLOBAL:
            return "STORE_GLOBAL";

        // Integer Arithmetic
        case BCOpcode::ADD_I64:
            return "ADD_I64";
        case BCOpcode::SUB_I64:
            return "SUB_I64";
        case BCOpcode::MUL_I64:
            return "MUL_I64";
        case BCOpcode::SDIV_I64:
            return "SDIV_I64";
        case BCOpcode::UDIV_I64:
            return "UDIV_I64";
        case BCOpcode::SREM_I64:
            return "SREM_I64";
        case BCOpcode::UREM_I64:
            return "UREM_I64";
        case BCOpcode::NEG_I64:
            return "NEG_I64";
        case BCOpcode::ADD_I64_OVF:
            return "ADD_I64_OVF";
        case BCOpcode::SUB_I64_OVF:
            return "SUB_I64_OVF";
        case BCOpcode::MUL_I64_OVF:
            return "MUL_I64_OVF";
        case BCOpcode::SDIV_I64_CHK:
            return "SDIV_I64_CHK";
        case BCOpcode::UDIV_I64_CHK:
            return "UDIV_I64_CHK";
        case BCOpcode::SREM_I64_CHK:
            return "SREM_I64_CHK";
        case BCOpcode::UREM_I64_CHK:
            return "UREM_I64_CHK";
        case BCOpcode::IDX_CHK:
            return "IDX_CHK";

        // Float Arithmetic
        case BCOpcode::ADD_F64:
            return "ADD_F64";
        case BCOpcode::SUB_F64:
            return "SUB_F64";
        case BCOpcode::MUL_F64:
            return "MUL_F64";
        case BCOpcode::DIV_F64:
            return "DIV_F64";
        case BCOpcode::NEG_F64:
            return "NEG_F64";

        // Bitwise Operations
        case BCOpcode::AND_I64:
            return "AND_I64";
        case BCOpcode::OR_I64:
            return "OR_I64";
        case BCOpcode::XOR_I64:
            return "XOR_I64";
        case BCOpcode::NOT_I64:
            return "NOT_I64";
        case BCOpcode::SHL_I64:
            return "SHL_I64";
        case BCOpcode::LSHR_I64:
            return "LSHR_I64";
        case BCOpcode::ASHR_I64:
            return "ASHR_I64";

        // Integer Comparisons
        case BCOpcode::CMP_EQ_I64:
            return "CMP_EQ_I64";
        case BCOpcode::CMP_NE_I64:
            return "CMP_NE_I64";
        case BCOpcode::CMP_SLT_I64:
            return "CMP_SLT_I64";
        case BCOpcode::CMP_SLE_I64:
            return "CMP_SLE_I64";
        case BCOpcode::CMP_SGT_I64:
            return "CMP_SGT_I64";
        case BCOpcode::CMP_SGE_I64:
            return "CMP_SGE_I64";
        case BCOpcode::CMP_ULT_I64:
            return "CMP_ULT_I64";
        case BCOpcode::CMP_ULE_I64:
            return "CMP_ULE_I64";
        case BCOpcode::CMP_UGT_I64:
            return "CMP_UGT_I64";
        case BCOpcode::CMP_UGE_I64:
            return "CMP_UGE_I64";

        // Float Comparisons
        case BCOpcode::CMP_EQ_F64:
            return "CMP_EQ_F64";
        case BCOpcode::CMP_NE_F64:
            return "CMP_NE_F64";
        case BCOpcode::CMP_LT_F64:
            return "CMP_LT_F64";
        case BCOpcode::CMP_LE_F64:
            return "CMP_LE_F64";
        case BCOpcode::CMP_GT_F64:
            return "CMP_GT_F64";
        case BCOpcode::CMP_GE_F64:
            return "CMP_GE_F64";

        // Type Conversions
        case BCOpcode::I64_TO_F64:
            return "I64_TO_F64";
        case BCOpcode::U64_TO_F64:
            return "U64_TO_F64";
        case BCOpcode::F64_TO_I64:
            return "F64_TO_I64";
        case BCOpcode::F64_TO_I64_CHK:
            return "F64_TO_I64_CHK";
        case BCOpcode::F64_TO_U64_CHK:
            return "F64_TO_U64_CHK";
        case BCOpcode::I64_NARROW_CHK:
            return "I64_NARROW_CHK";
        case BCOpcode::U64_NARROW_CHK:
            return "U64_NARROW_CHK";
        case BCOpcode::BOOL_TO_I64:
            return "BOOL_TO_I64";
        case BCOpcode::I64_TO_BOOL:
            return "I64_TO_BOOL";

        // Memory Operations
        case BCOpcode::ALLOCA:
            return "ALLOCA";
        case BCOpcode::GEP:
            return "GEP";
        case BCOpcode::LOAD_I8_MEM:
            return "LOAD_I8_MEM";
        case BCOpcode::LOAD_I16_MEM:
            return "LOAD_I16_MEM";
        case BCOpcode::LOAD_I32_MEM:
            return "LOAD_I32_MEM";
        case BCOpcode::LOAD_I64_MEM:
            return "LOAD_I64_MEM";
        case BCOpcode::LOAD_F64_MEM:
            return "LOAD_F64_MEM";
        case BCOpcode::LOAD_PTR_MEM:
            return "LOAD_PTR_MEM";
        case BCOpcode::LOAD_STR_MEM:
            return "LOAD_STR_MEM";
        case BCOpcode::STORE_I8_MEM:
            return "STORE_I8_MEM";
        case BCOpcode::STORE_I16_MEM:
            return "STORE_I16_MEM";
        case BCOpcode::STORE_I32_MEM:
            return "STORE_I32_MEM";
        case BCOpcode::STORE_I64_MEM:
            return "STORE_I64_MEM";
        case BCOpcode::STORE_F64_MEM:
            return "STORE_F64_MEM";
        case BCOpcode::STORE_PTR_MEM:
            return "STORE_PTR_MEM";
        case BCOpcode::STORE_STR_MEM:
            return "STORE_STR_MEM";

        // Control Flow
        case BCOpcode::JUMP:
            return "JUMP";
        case BCOpcode::JUMP_IF_TRUE:
            return "JUMP_IF_TRUE";
        case BCOpcode::JUMP_IF_FALSE:
            return "JUMP_IF_FALSE";
        case BCOpcode::JUMP_LONG:
            return "JUMP_LONG";
        case BCOpcode::SWITCH:
            return "SWITCH";
        case BCOpcode::CALL:
            return "CALL";
        case BCOpcode::CALL_NATIVE:
            return "CALL_NATIVE";
        case BCOpcode::CALL_INDIRECT:
            return "CALL_INDIRECT";
        case BCOpcode::RETURN:
            return "RETURN";
        case BCOpcode::RETURN_VOID:
            return "RETURN_VOID";
        case BCOpcode::TAIL_CALL:
            return "TAIL_CALL";

        // Exception Handling
        case BCOpcode::EH_PUSH:
            return "EH_PUSH";
        case BCOpcode::EH_POP:
            return "EH_POP";
        case BCOpcode::EH_ENTRY:
            return "EH_ENTRY";
        case BCOpcode::TRAP:
            return "TRAP";
        case BCOpcode::TRAP_FROM_ERR:
            return "TRAP_FROM_ERR";
        case BCOpcode::MAKE_ERROR:
            return "MAKE_ERROR";
        case BCOpcode::ERR_GET_KIND:
            return "ERR_GET_KIND";
        case BCOpcode::ERR_GET_CODE:
            return "ERR_GET_CODE";
        case BCOpcode::ERR_GET_IP:
            return "ERR_GET_IP";
        case BCOpcode::ERR_GET_LINE:
            return "ERR_GET_LINE";
        case BCOpcode::RESUME_SAME:
            return "RESUME_SAME";
        case BCOpcode::RESUME_NEXT:
            return "RESUME_NEXT";
        case BCOpcode::RESUME_LABEL:
            return "RESUME_LABEL";

        // Debug Operations
        case BCOpcode::LINE:
            return "LINE";
        case BCOpcode::BREAKPOINT:
            return "BREAKPOINT";
        case BCOpcode::WATCH_VAR:
            return "WATCH_VAR";

        // String Operations
        case BCOpcode::STR_RETAIN:
            return "STR_RETAIN";
        case BCOpcode::STR_RELEASE:
            return "STR_RELEASE";

        case BCOpcode::OPCODE_COUNT:
            return "OPCODE_COUNT";
    }
    return "UNKNOWN";
}

} // namespace viper::bytecode
