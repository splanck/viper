# IL Specification

## Introduction & design goals
Overview of the intermediate language and the small, portable design goals.

## Types
Primitive and opaque types defined by the IL (see the [class catalog](class-catalog.md)).

## Values & constants
Describes temporaries, literals, and how constants are represented.

## Instructions
Grouped by behavior:
- Arithmetic: `add`, `sub`, `mul`, `sdiv`, `udiv`, `srem`, `urem`
- Bitwise: `and`, `or`, `xor`, `shl`, `lshr`, `ashr`
- Comparison: `icmp_eq`, `icmp_ne`, `scmp_*`, `ucmp_*`, `fcmp_*`
- Memory: `alloca`, `load`, `store`, `gep`, `addr_of`
- Control: `br`, `cbr`, `ret`, `trap`
- Calls & constants: `call`, `const_str`, `const_null`
- Conversion: `sitofp`, `fptosi`, `zext1`, `trunc1`

## Control flow & functions
Outlines how basic blocks form functions and how calls and returns transfer control.

## Memory model & alignment
Notes on stack slots, pointer operations, and alignment rules enforced by the IL.

## Runtime ABI
Summarizes calling conventions and how the IL interacts with the runtime library.

## Verifier rules
Key structural and typing checks that ensure modules are well formed.

## Text grammar
High-level outline of the text format grammar used by the parser and serializer.

## Examples
See [IL examples](examples/il/) for sample modules used in tests.
