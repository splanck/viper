# Audit Finding #2: MIR Opcode Generation from JSON

## Problem
Adding a new MIR opcode requires ~5 manual file edits (enum, name table, classify, encoder, peephole). The enum, name table, and classification predicates are not generated from the existing JSON specs.

## Current State
- `docs/spec/aarch64_encodings.json` (42 opcodes) → generates `OpcodeDispatch.inc` via `cmake/GenAArch64Dispatch.cmake`
- `docs/spec/x86_64_encodings.json` (49 opcodes) → generates `EncodingTable.inc` + `OpFmtTable.inc` via `cmake/GenX86Encodings.cmake`
- Manual: `MachineIR.hpp` enum (82 AArch64 / 62 x86), `MachineIR.cpp` name table, `OpcodeClassify.hpp`

## Implementation Plan

### Phase 1: Extend JSON Schema (1 day)
Add fields to each opcode entry in both JSON specs. AArch64 example:
```json
{
  "enum": "AddRRR",
  "mnemonic": "add",
  "format": "RRR",
  "operands": ["dst:reg", "lhs:reg", "rhs:reg"],
  "asm": "add {dst}, {lhs}, {rhs}",
  "categories": ["ThreeAddrRRR", "ALU"],
  "defs": [0],
  "uses": [1, 2],
  "sets_flags": false
}
```

Note: Opcodes not in JSON (pseudo-opcodes like `AddOvfRRR`, `PhiStoreGPR`) get a separate `"pseudo": true` section with just `enum` and `categories`.

### Phase 2: Generate Enum + Name Table (1 day)
Create `cmake/GenMIREnum.cmake` (shared by both backends):

**Input:** JSON spec
**Output:** Two `.inc` files per backend:
1. `MOpcode.inc` — enum values:
   ```cpp
   MovRR,
   MovRI,
   // ...
   ```
2. `OpcodeName.inc` — name table switch cases:
   ```cpp
   case MOpcode::MovRR: return "MovRR";
   case MOpcode::MovRI: return "MovRI";
   ```

Modify `MachineIR.hpp` to `#include "generated/MOpcode.inc"` inside the enum.
Modify `MachineIR.cpp` to `#include "generated/OpcodeName.inc"` inside the switch.

### Phase 3: Generate Classification Predicates (1 day)
Extend generator to produce `OpcodeClassify.inc` from `categories` field:
```cpp
inline bool isThreeAddrRRR(MOpcode opc) {
    switch (opc) {
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        // ... all opcodes with "ThreeAddrRRR" in categories
            return true;
        default: return false;
    }
}
```

### Phase 4: Build-Time Validation (half day)
Add CMake custom target that:
1. Counts enum values generated
2. Counts name table entries generated
3. Counts binary encoder switch cases
4. Asserts all three counts match

### Files to Modify
- `docs/spec/aarch64_encodings.json` — add categories, defs, uses fields
- `docs/spec/x86_64_encodings.json` — same
- `cmake/GenMIREnum.cmake` (new) — shared generator
- `src/codegen/aarch64/MachineIR.hpp` — include generated enum
- `src/codegen/aarch64/MachineIR.cpp` — include generated name table
- `src/codegen/aarch64/ra/OpcodeClassify.hpp` — include generated predicates
- `src/codegen/aarch64/CMakeLists.txt` — add generation commands
- Same pattern for x86_64

### Verification
1. `./scripts/build_viper.sh` — all tests pass
2. Add a test opcode to JSON only — verify enum + name + classify auto-generated
3. Remove test opcode — verify clean removal
