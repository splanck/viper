 // Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: Bytecode opcode name table and encoding utilities for the Viper
//   bytecode VM. Maps BCOpcode enum values to human-readable strings.

#include "bytecode/Bytecode.hpp"

namespace viper::bytecode {

/// @brief Return a stable, human-readable mnemonic for a bytecode opcode.
/// @param op The opcode to name.
/// @return A static string literal (never NULL); "UNKNOWN" for any value
///         outside the defined BCOpcode set. Used by the disassembler and
///         trace/diagnostic output, so the names are part of the debug ABI.
const char *opcodeName(BCOpcode op) {
    switch (op) {
#define BC_OPCODE(name, value)                                                                     \
    case BCOpcode::name:                                                                           \
        return #name;
#include "bytecode/Bytecode.def"
#undef BC_OPCODE
    }
    return "UNKNOWN";
}

} // namespace viper::bytecode
