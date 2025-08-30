AGENTS.md — Operating Guide for Automated Contributors (Codex)
Project: IL‑based compiler stack (Frontends → IL → VM → Codegen)
Primary sources of truth:
	• /docs/il-spec.md (IL v0.1 spec — normative)
	• /docs/class-catalog.md (class list and roles)
	• /docs/cpp-overview.md is an outline of how the C++ project should be laid out.
This document defines how automated changes must be made: scope, quality bars, commit etiquette, and when to propose vs. implement.

1) Mission & Non‑Goals
Mission: Implement the project methodically, delivering small, verifiable increments that conform to the IL spec and architecture.
Non‑Goals (unless explicitly instructed):
	• No large refactors.
	• No spec changes to /docs/il-spec.md.
	• No new external dependencies.
	• No breaking changes to public APIs without an ADR (Architecture Decision Record).

2) Core Principles
	1. Spec first: Obey /docs/il-spec.md. If a task conflicts with the spec, create an ADR proposal; do not silently diverge.
	2. Small steps: Prefer many small, complete commits over one large change.
	3. Always green: Builds and tests must pass at every commit.
	4. Single responsibility: Each prompt = one coherent change set.
	5. Determinism: VM and native code must produce identical observable results for defined programs.

3) Work Protocol (Follow for every prompt)
Before you touch code:
	1. Read /docs/roadmap.md and the relevant section(s) of /docs/il-spec.md.
	2. List the intended changes and files to touch (keep it under ~10 files unless told otherwise).
	3. State the Definition of Done you will satisfy (build + tests + docs).
While implementing:
	1. If adding behavior, add or update tests first (unit/golden/e2e as appropriate).
        2. Keep public headers minimal and stable; no cross‑layer leaks (see §8).
	3. If you must stub, stub behind a TODO and ensure all tests still pass.
Before committing:
	1. Run:

cmake -S . -B build && cmake --build build
ctest --output-on-failure
	2. Format code (.clang-format) and keep warnings at zero.
	3. Update docs only if behavior or public API changed (and only with ADR approval).
        4. Write a Conventional Commit message (see §9).

4) Scope & Granularity
	• Right‑sized task examples:
		○ Implement il::io::Serializer basic printing + 1 golden test.
		○ Add scmp_* comparisons in VM + unit tests.
		○ Add LinearScanAllocator skeleton (no spills) + compile‑only tests.
	• Too big: “Implement full x86‑64 backend + linker + peephole.” Split into ISel, regalloc, emitter, assembler driver.
If a prompt is too large to complete in one pass, propose a split plan in AGENTS_NOTES.md (root) and complete the first sub‑task.

5) Definition of Done (Global)
A change is Done when:
	• ✅ Builds succeed on Linux/macOS (Debug).
	• ✅ All tests pass (unit + golden + e2e relevant to your change).
	• ✅ New code has tests and doc comments.
	• ✅ No warnings; formatted with .clang-format.
	• ✅ Commit follows Conventional Commits and touches only the stated files.
Never commit a red build or failing tests.

6) Documentation & Comment Policy
        • Every source/header file must start with a file header describing purpose, invariants, and ownership.
        • Every public class/function/member must have Doxygen-style comments.
        • Behavior changes require updates to /docs and inline comments.
        • Non-obvious algorithms must include rationale.
        • IL spec/ABI changes require an ADR.
        • See /docs/style-guide.md for file header and comment conventions.

7) Testing Policy
	• Unit tests: For leaf utilities (interner, verifier checks, VM opcode semantics).
	• Golden tests: For stable text outputs (IL serializer, BASIC→IL front end).
	• E2E tests: VM vs native equivalence for examples in /docs/examples/.
	• Rule: When adding a feature, add at least one test that would fail before your change and pass after.

8) Architectural Guardrails
	• Layering (no exceptions unless ADR approved):
		○ Frontends (fe/*) → only depend on IL Build/IO/Verify and Support.
		○ VM (/src/vm) → only depends on IL Core/IO/Verify, Support, and runtime C ABI through a bridge.
		○ Codegen (/src/codegen/...) → only depends on IL Core/Verify/Support.
		○ Runtime (/runtime) → C library, no C++ or compiler dependencies.
	• No cross‑reach: VM must not include codegen headers; front end must not include VM or codegen.
	• Spec compliance: Instruction semantics, traps, and typing must match /docs/il-spec.md.

9) Commits & Branching
	• Commit message format (Conventional Commits):

<type>(<scope>): <short summary>

[body: what changed and why, notable decisions]
[tests: list added/updated tests]

Types: feat, fix, chore, refactor, test, docs, build, ci.
	• Examples:
		○ feat(il/io): add serializer for externs/globals/functions + golden test
		○ fix(vm): correct scmp_gt result for negative values; add unit test
	• Branching: If available, create feature/<slug> branches; otherwise commit to main with small, atomic commits.

10) ADR (Architecture Decision Record) Process
Use ADRs for any change that affects:
	• IL semantics or grammar,
	• public APIs,
	• dependencies or build strategy,
	• cross‑layer coupling.
Procedure:
	1. Create /docs/adr/NNN-short-title.md with: Context, Decision, Consequences, Alternatives.
	2. Include a “Spec Impact” section; bump spec minor if needed.
	3. Commit as docs(adr): propose NNN <title>.
	4. Do not implement the change until approved (future prompt).

11) Coding Standards (C++20)
	• Use RAII and smart pointers (std::unique_ptr); avoid raw new/delete.
	• No exceptions thrown across library boundaries; prefer Result<T> or diagnostics.
	• Keep headers self‑contained; minimize includes; use forward declarations.
	• Keep data types compact (e.g., tagged unions for Value/Slot).
	• Public headers must have brief doc comments describing invariants and ownership.
C Runtime: C99, stable ABI, no dynamic global state visible to C++ layers.

12) Quality Gates (per area)
IL Core
	• Types/opcodes stable; serializer prints deterministically.
	• Verifier checks block terminators, operand types, call signatures.
Parser/Serializer
	• Round‑trip (parse → print → parse → print) retains semantics.
VM
	• Op semantics match spec (including traps).
	• --trace prints function/block/op with values (when implemented).
Codegen
	• Conforms to SysV x86‑64 ABI (Phase 1).
	• Linear‑scan allocator documented; spills covered by tests when enabled.
Front End (BASIC)
	• Lowers to IL patterns defined in spec.
	• Golden tests: BASIC input → IL output.
Tools
	• ilc -emit-il, -run, -S behave consistently.
	• il-verify exits non‑zero on errors with clear messages.

13) Dependencies & Tooling
        • C++20 standard; Clang is the canonical toolchain; GCC optional for testing.
	• Allowed third‑party (vendored if needed): fmt, CLI11/lyra, Catch2 or gtest.
	• Do not add new dependencies without ADR.
Build & Test Commands (must run before every commit):

cmake -S . -B build
cmake --build build -j
ctest --output-on-failure

14) Compiler Preference
        • Use Clang as the canonical compiler for building, testing, and CI.
        • GCC builds may be added in CI, but Clang must pass first.
        • On macOS, Apple Clang is default; on Linux, use clang/clang++; on Windows, prefer clang-cl.

15) Handling Unknowns & Ambiguities
	• If the spec is unclear, do not improvise in code.
	• Draft an ADR proposal or add a non‑semantic TODO with a question in code and a note in AGENTS_NOTES.md.
	• Keep the build green. Prefer stubs returning explicit “unimplemented” errors over partial features.

16) Logging, Errors, and Traps
	• Diagnostics should include function, block label, and (if present) SourceLoc.
	• VM traps: raise a structured error; top‑level prints message and returns non‑zero.
	• No noisy logging by default. Add --trace/--trace-calls flags rather than ad‑hoc prints.

17) Performance Hygiene
	• Prefer contiguous containers (std::vector) for IR and VM frames.
	• Avoid per‑instruction heap allocations; use arenas.
	• Only micro‑optimize with measurements; add benchmarks later (out of scope for v1).

18) File Ownership & “Do Not Touch” List
	• Do not modify /docs/il-spec.md without an ADR.
	• Do not change CI workflows to skip tests.
	• Do not change license headers or project metadata.

19) Templates & Checklists
Pull/Commit Checklist (copy into commit body):
        - [ ] Built on macOS/Linux (Debug).
        - [ ] ctest passed.
        - [ ] Added/updated tests.
        - [ ] Code formatted; no new warnings.
        - [ ] No spec/API deviation (or ADR attached).
        - [ ] Docs updated if behavior/API changed.
        - [ ] Updated docs/comments per policy.
        - [ ] Added/updated file headers.
New Class Header Template:

// <path>/<Name>.h
#pragma once
#include <...>
/// @brief <one-sentence purpose>
/// @invariant <key invariants>
/// @ownership <ownership model>
/// @notes <links to spec/docs>
namespace il::core {
class Name {
public:
  // public API
private:
  // representation
};
} // namespace il::core
Test Naming: tests/unit/test_<area>_<thing>.cpp, tests/golden/<case>.il, tests/e2e/<scenario>.cmake.

20) Prompt Template (How to respond to tasks)
When given a task/prompt, respond in this structure:
	1. Plan: bullet list of steps and files to touch.
	2. Changes: concise explanation of the implementation approach and any assumptions.
	3. Diff Summary: list of created/modified files.
	4. Commands: the exact build/test commands you ran.
	5. Results: build output summary and test results (pass/fail).
	6. Next Steps: 2–3 suggested follow‑ups (optional).
	7. Commit Message: Conventional Commit block.

21) Recovery From Failures
	• If tests fail, fix or revert within the same prompt.
	• If a change is too big, split and commit the finished subset; leave a clear TODO and a note in AGENTS_NOTES.md.
	• Never leave the repository unbuildable.

22) Security & Safety
	• No network calls, no file I/O outside the project workspace unless required by tests.
	• No dynamic code execution or shelling out beyond the documented toolchain (compiler, linker, CMake, ctest).
	• Treat all inputs (including IL text) as untrusted — verify before execution.

End of AGENTS.md

