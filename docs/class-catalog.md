# Class Catalog

Classes grouped by subsystem.

## Support

| Class | Description |
| --- | --- |
| Arena | Bump allocator for temporary objects |
| DiagnosticEngine | Records and prints errors and warnings |
| Options | Global compiler flags |
| Result<T> | Minimal expected-like container |
| SourceManager | Maps file IDs to paths |
| StringInterner | Interns strings into Symbols |
| Symbol | Opaque handle for interned strings |

## IL

### Core

| Class | Description |
| --- | --- |
| Module | Top-level container for functions and globals |
| Function | Sequence of blocks forming a procedure |
| BasicBlock | Linear list of instructions with a label |
| Instr | Single IL instruction |
| Value | Operand or constant |
| Type | IL type descriptor |
| Global | Named global variable |
| Extern | External function declaration |
| Param | Function parameter |

### Build / IO / Verify

| Class | Description |
| --- | --- |
| IRBuilder | Fluent builder for IL modules |
| Parser | Parses textual IL |
| Serializer | Prints modules as text |
| Verifier | Checks structural correctness |
| PassManager | Runs transformation passes |
| ConstFold | Constant folding pass |
| Peephole | Peephole optimization pass |

## Frontend (BASIC)

| Class | Description |
| --- | --- |
| Token | Lexical token representation |
| Lexer | Converts source text to tokens |
| Parser | Builds AST from tokens |
| AST nodes | Expression and statement hierarchy |
| SemanticAnalyzer | Validates and annotates AST |
| ConstFolder | Evaluates constant expressions |
| Lowerer | Lowers AST to IL |
| LoweringContext | Shared state for lowering |
| NameMangler | Transforms identifiers for IL |
| DiagnosticEmitter | Emits front-end diagnostics |
| AstPrinter | Dumps AST for debugging |

## VM

| Class | Description |
| --- | --- |
| VM | Stack-based interpreter |
| RuntimeBridge | Calls into the C runtime |

## Codegen

| Class | Description |
| --- | --- |
| (placeholder) | Planned x86_64 backend |

## Tools

| Tool | Description |
| --- | --- |
| basic-ast-dump | Dumps BASIC ASTs |
| il-dis | Disassembles IL modules |
| il-verify | Verifies IL modules |
| ilc | Driver for compilation and execution |
