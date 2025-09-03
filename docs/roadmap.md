# Roadmap

Note: implementation files use `.cpp`; headers use `.hpp`.

## Milestones

| ID  | Stage           | Focus                                                                        |
| --- | --------------- | ---------------------------------------------------------------------------- |
| M0  | Bootstrap       | Build system and seed the [class catalog](class-catalog.md)                  |
| M1  | IL core         | Types, instructions, and module scaffolding                                  |
| M2  | VM              | Stack-based interpreter and runtime interface from the [IL spec](il-spec.md) |
| M3  | BASIC front end | Lexer, parser, and lowering from BASIC to IL                                 |
| M4  | Codegen prep    | Groundwork for native emission and register allocation                       |

### Status checklist

- [x] M0 Bootstrap
- [x] M1 IL core
- [ ] M2 VM
- [ ] M3 BASIC front end
- [ ] M4 Codegen prep

### Future items

- String functions
- Arrays
- GOSUB/RETURN
- SSA-based optimizations
