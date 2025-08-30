# Class Catalog

---
content merged from cppclasslist.md
---

here’s a comprehensive class catalog for the project, organized by area. It’s written for C++20 and follows the architecture we mapped out (Front end → IL → VM / Codegen). I mark classes as [core] (must‑have for v1) or [later](helpful once the core runs).
	Conventions
		○ Namespaces: il::core, il::build, il::io, il::verify, il::vm, il::codegen::x86_64, fe::basic, support.
		○ “class” vs “struct”: treat them both as classes here; choose struct when data is plain and public.
		○ Keep headers small and layered to preserve build times.

A. Cross‑cutting & Support
support::StringInterner [core]
	• Role: Intern and manage unique IDs for strings (symbols, identifiers, labels).
	• Key: Symbol intern(std::string_view), std::string_view lookup(Symbol).
support::Symbol [core]
	• Role: Lightweight handle (e.g., uint32_t) returned by the interner; comparable, hashable.
support::Arena [core]
	• Role: Bump‑pointer allocator for IR nodes / AST to reduce fragmentation.
	• Key: void* alloc(size_t, size_t align), reset().
support::SourceLoc [core]
	• Role: File/line/col tuple (plus file ID) carried through lexer → IL for diagnostics.
support::Diagnostic [core]
	• Role: Message bundle (severity, text, SourceLoc).
	• With: support::DiagnosticEngine to collect/format/emit.
support::Result<T> [core]
	• Role: Lightweight expected (value or Diagnostic list).
support::Options [core]
	• Role: Holds CLI/config flags (trace, verify, target triple, etc.).

B. IL Core (Intermediate Language)
il::core::Type [core]
	• Role: Holds TypeKind { Void, I1, I64, F64, Ptr, Str }.
	• Key: equality, helpers (isInteger(), byteSize()).
il::core::Value [core]
	• Role: Tagged union for temporaries and constants.
	• Kinds: Temp, ConstInt, ConstFloat, ConstStr, GlobalAddr, NullPtr.
	• Fields: Type type; ValueKind kind; payload.
il::core::Instr [core]
	• Role: An instruction (opcode, result type, optional dst, operands).
	• Fields: Opcode op; Type type; Value dst; small_vector<Value,4> ops; SourceLoc loc;.
il::core::BasicBlock [core]
	• Role: Label + linear instruction list ending in one terminator.
	• Fields: Symbol name; std::vector<Instr> instrs;.
il::core::Param [core]
	• Role: Function parameter (Symbol name; Type type;).
il::core::Function [core]
	• Role: Parameters, return type, blocks, attributes (noreturn/pure/readonly), visibility.
	• Key: BasicBlock& addBlock(Symbol), iterators, lookup by label.
il::core::Global [core]
	• Role: Global variables and string literals.
	• Fields: Symbol name; Type type; GlobalInit init; bool isConst; Visibility vis;.
il::core::ExternDecl [core]
	• Role: External/runtime function signatures (C ABI).
il::core::Module [core]
	• Role: Owns externs, globals, functions, string table, target info.
il::core::Attributes [later]
	• Role: Bitmask for function/instruction hints (advisory in v1).

C. IL Construction, I/O, and Verification
il::build::IRBuilder [core]
	• Role: Safe, ergonomic construction of functions/blocks/instructions.
	• Key: setFunction(Function*), setBlock(BasicBlock*), Value add(Value,Value), void br(Symbol), void cbr(Value, Symbol, Symbol), etc.
	• Guards: Enforces one terminator per block; asserts operand types.
il::io::Serializer [core]
	• Role: Print IL text (deterministic order and naming).
il::io::Parser [core]
	• Role: Read IL text into a Module.
	• Key: Simple tokenizer + recursive descent for the spec grammar.
il::verify::Verifier [core]
	• Role: Structural & type checks per spec; returns diagnostics.
	• Key: std::vector<Diagnostic> verify(const Module&).
il::build::NameMangler [later]
	• Role: Stable naming for temps/labels to keep golden tests deterministic.
il::pass::Pass / PassManager [later]
	• Role: Tiny pass framework for verify, peephole, etc.
il::transform::Peephole [later]
	• Role: Local IL cleanups (remove dead moves, fold trivial ops).

D. Front End — BASIC
Lexing
fe::basic::Token [core]
	• Role: Token kind (identifier, number, string, keyword, symbol), lexeme, SourceLoc.
fe::basic::Lexer [core]
	• Role: Converts source text to a token stream.
	• Key: Token next(), Token peek(), handles line numbers & BASIC quirks.
AST
fe::basic::AstNode [core]
	• Role: Base class with SourceLoc.
fe::basic::Expr [core] (base)
	• Derived:
		○ LiteralExpr (int/float/string)
		○ VarExpr
		○ UnaryExpr
		○ BinaryExpr
		○ CallExpr (for built‑ins like PRINT arguments if modeled as function-like)
fe::basic::Stmt [core] (base)
	• Derived:
		○ LetStmt, PrintStmt
		○ IfStmt (then/else blocks)
		○ WhileStmt
		○ GotoStmt, GosubStmt, ReturnStmt [later]
		○ EndStmt
		○ LabelStmt (line numbers)
fe::basic::Program [core]
	• Role: Root node (ordered list of statements and labels).
Parsing & Semantics
fe::basic::Parser [core]
	• Role: Produces AST from token stream (recursive descent + Pratt for expressions).
	• Key: Recover on common errors to provide multiple diagnostics.
fe::basic::SymbolTable [core]
	• Role: Tracks variables (types: int/float/string → lowered to IL types), and labels.
	• Key: declareVar, lookupVar, declareLabel, resolveLabel.
fe::basic::TypeChecker [core]
	• Role: Minimal checking and coercions (e.g., numeric to i64/f64, string ops via runtime).
Lowering
fe::basic::LoweringContext [core]
	• Role: Holds IRBuilder, symbol maps (var → stack slot), common literals.
fe::basic::Lowerer [core]
	• Role: AST → IL emission per the IL spec (allocas in entry, load/store, cbr/br, runtime calls).
	• Key: emit(const Program&, il::core::Module&).

E. IL VM (Interpreter)
il::vm::HostFunction [core]
	• Role: Metadata & C function pointer signature for an extern (runtime) function.
il::vm::HostTable [core]
	• Role: Registry mapping Symbol → HostFunction (e.g., @rt_print_i64).
il::vm::Slot [core]
	• Role: A runtime value cell with unions for i64/f64/ptr/str (read using the IL type at use sites).
il::vm::Frame [core]
	• Role: One function activation (register vector for temps, stack buffer for alloca, IP to block/instruction).
	• Key: alloca(size), read(Value), write(Value) helpers.
il::vm::VM [core]
	• Role: The interpreter engine.
	• Key: int run(Module&, Function* entry), step(), call(Function*, args).
	• Behavior: Switch‑dispatch on Opcode, checks traps (div0, null/misaligned load/store, bad cast), calls into HostTable or IL functions.
il::vm::Tracer [later]
	• Role: Optional instruction/call tracing for debugging (--trace, --trace-calls).
il::vm::BytecodeEncoder / BytecodeVM [later]
	• Role: Optional compact encoding for faster dispatch (not needed for v1).

F. Backend — Codegen (x86‑64 SysV)
Interfaces
il::codegen::Backend [core]
	• Role: Abstract interface for backends.
	• Key: bool emitModule(const il::core::Module&, const CodegenOptions&, std::filesystem::path outAsm).
il::codegen::CodegenOptions [core]
	• Role: Target triple, assembly syntax, optimization knobs.
x86‑64 Implementation
il::codegen::x86_64::ISel [core]
	• Role: Greedy instruction selection (IL op → x86 pattern).
	• Key: select(FunctionIR&) -> X86Function.
il::codegen::x86_64::Reg [core]
	• Role: Target register descriptor (id, class GP/FP).
il::codegen::x86_64::VReg [core]
	• Role: Virtual register (maps from IL temps).
il::codegen::x86_64::LiveInterval [core]
	• Role: Live range for a VReg (needed for linear‑scan).
il::codegen::x86_64::Liveness [core]
	• Role: Computes live intervals across basic blocks.
il::codegen::x86_64::LinearScanAllocator [core]
	• Role: Assigns VRegs to physical regs/spills.
	• Key: allocate(X86Function&), tracks spill slots.
il::codegen::x86_64::StackFrame [core]
	• Role: Stack layout (spill slots, callee‑saved saves, alignment, alloca area).
il::codegen::x86_64::ABI [core]
	• Role: Call lowering per SysV: arg/ret registers, shadow space, align rules.
	• Key: lowerCall(CallNode&, X86Emitter&), prologue/epilogue.
il::codegen::x86_64::X86Emitter [core]
	• Role: Emits textual assembly (.s), labels, directives, comments.
	• Key: emitMov, emitAdd, emitCmp, emitJcc, emitCall, emitRet, etc.
il::codegen::x86_64::Peephole [later]
	• Role: Local x86 optimizations (remove redundant moves, fuse cmp+jcc patterns).
il::codegen::AssemblerDriver [core]
	• Role: Shells out to clang/cc to assemble .s → .o and link with librt.a.

G. Tools / CLI
tools::IlcDriver [core]
	• Role: Command dispatcher for ilc.
	• Commands:
		○ front basic FILE -emit-il
		○ front basic FILE -run
		○ front basic FILE -S
		○ front basic FILE -o a.out
	• Collaborators: Parser/Lowerer, Verifier, VM, Backend, AssemblerDriver.
tools::VerifyTool [core]
	• Role: Loads .il, runs Verifier, prints diagnostics; exit code = pass/fail.
tools::Disassembler [later]
	• Role: Pretty‑prints IL from a binary form if you add .ilbc.

H. Runtime (C, not C++ classes)
	Implemented in C for a stable ABI; invoked from VM and native code.
	• Files: rt_print.c, rt_string.c, rt_input.c, rt_mem.c.
	• Header: rt.h (declares rt_print_*, rt_input_line, rt_len, rt_concat, rt_substr, rt_to_int, rt_to_float, rt_str_eq, rt_alloc, rt_free).
For C++ integration, you can add a tiny shim:
il::vm::RuntimeBridge [core]
	• Role: Populates HostTable with pointers to the C functions.

I. Testing & Infrastructure (light classes)
tests::GoldenFile [core]
	• Role: Compare actual IL/text output to checked‑in golden files (with normalization hooks).
tests::ConformanceHarness [core]
	• Role: Run a suite (BASIC → IL → VM and BASIC → IL → Codegen → Native), capture stdout/stderr/exit codes, and diff.

Relationships at a glance
	• Front end (fe::basic) builds AST → lowered by Lowerer using IRBuilder into an il::core::Module.
	• Serializer/Parser convert between text .il and in‑memory Module.
	• Verifier checks Module well‑formedness.
	• VM runs the Module (using HostTable/RuntimeBridge).
	• Backend lowers Module to .s via ISel, Liveness, LinearScanAllocator, X86Emitter; AssemblerDriver links with librt.a.
	• Support provides diagnostics, symbols, arenas used everywhere.

Minimal v1 set (checklist)
Support: StringInterner, Symbol, Arena, SourceLoc, Diagnostic, DiagnosticEngine, Result<T>, Options
IL Core: Type, Value, Instr, BasicBlock, Function, Global, ExternDecl, Module
IL Build/IO/Verify: IRBuilder, Serializer, Parser, Verifier
Front end (BASIC): Token, Lexer, AstNode (+ common Expr/Stmt nodes), Program, Parser, SymbolTable, LoweringContext, Lowerer
VM: HostFunction, HostTable, Slot, Frame, VM, RuntimeBridge
Codegen: Backend, CodegenOptions, ISel, Reg, VReg, LiveInterval, Liveness, LinearScanAllocator, StackFrame, ABI, X86Emitter, AssemblerDriver
Tools: IlcDriver, VerifyTool
Tests: GoldenFile, ConformanceHarness

“Later” extensions (when you’re ready)
	• il::vm::Tracer, BytecodeEncoder / BytecodeVM
	• il::pass::PassManager, transform::Peephole (IL and x86)
	• fe::basic::ControlFlowGraph (if you want pre‑lowering CFG)
	• il::codegen::x86_64::Peephole, DebugInfoEmitter (DWARF)
	• Alternative backends: il::codegen::arm64::Backend
	• .ilbc binary format with tools::Disassembler

This catalog keeps the codebase modular and predictable. Track which [core] classes are implemented per milestone. If you want, I can turn this into a CMake target map and a header skeleton for each class (empty methods + TODOs) so your first compile already succeeds.
