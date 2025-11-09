# CODEMAP: IL Utilities

- **src/il/utils/Utils.cpp**

  Collects small IL convenience helpers used across analyses to query blocks and instructions without materializing extra structures. `belongsToBlock` performs linear membership tests over a block's instruction vector, while `terminator` and `isTerminator` centralize opcode-based control-flow classification. These utilities back verifier and optimizer code that need quick checks when rewriting IR without duplicating opcode tables. Dependencies include `il/utils/Utils.hpp` together with `il/core/BasicBlock.hpp`, `il/core/Instr.hpp`, and `il/core/Opcode.hpp` for the IR data structures.

- **src/il/utils/Utils.hpp**

  Declares shared IL utility functions for quick IR queries: membership checks, terminator classification, and small helpers used by verifiers and transforms. The header keeps declarations lightweight and pulls in only the minimal IL core forward declarations required for callers.
