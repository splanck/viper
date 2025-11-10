# CLAUDE.md — Operating Guide for Automated Contributors
**Project:** IL-based Compiler Stack (Frontends → IL → VM → Codegen)  
**Primary Sources of Truth:**
- /docs/il-guide.md#reference — IL v0.1 Spec (Normative)
- /docs/codemap.md — Class Roles and Responsibilities
- /docs/architecture.md#cpp-overview — Project Structure and C++ Layering

This document defines how Claude agents must operate when contributing automated code or documentation changes. It establishes mission, scope, quality bars, and response structure.

---

## 1. Mission & Boundaries
**Mission:** Implement and evolve the compiler stack in small, verifiable increments that conform to the IL spec and architecture.

**Non-Goals:**
- No broad refactors without prior ADR.
- No modifications to /docs/il-guide.md#reference spec.
- No new third-party dependencies.
- No breaking public API changes without an approved ADR.

---

## 2. Core Principles
1. **Spec First:** Always obey /docs/il-guide.md#reference. If conflicts arise, draft an ADR — never silently diverge.
2. **Incrementalism:** Many small, correct commits beat one large risky change.
3. **Always Green:** Tests and builds must pass before commit.
4. **Single Responsibility:** Each response = one coherent change set.
5. **Determinism:** VM and native outputs must match for all defined programs.

---

## 3. Work Protocol (Claude Task Flow)
**Before modifying code:**
1. Read /docs/roadmap.md and relevant IL spec sections.
2. List the intended changes and files to touch (<10 files preferred).
3. Define a “Definition of Done” including build, tests, and docs.

**During implementation:**
- Add or update tests first.
- Keep headers minimal and avoid cross-layer dependencies.
- Stub behind TODO where needed, but ensure all tests remain green.

**Before commit:**
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure

Format code with .clang-format, ensure zero warnings, and update docs only when behavior or APIs change. Follow Conventional Commits (see §9).

---

## 4. Scope & Task Granularity
**Appropriate scope examples:**
- Implement il::io::Serializer printing + golden test.
- Add scmp_* comparisons in VM + unit tests.
- Create LinearScanAllocator skeleton + compile-only tests.

**Too large:** “Implement full x86-64 backend.”
Split large goals into smaller sub-tasks and record them in AGENTS_NOTES.md.

---

## 5. Definition of Done
A change is considered **Done** when:
- ✅ Builds succeed on Linux and macOS (Debug).
- ✅ All tests pass (unit + golden + e2e).
- ✅ New code has tests and doc comments.
- ✅ Zero warnings and formatted.
- ✅ Commit message follows Conventional Commits.

---

## 6. Documentation & Comments
- Every source/header file includes a header with purpose, invariants, ownership.
- All public classes/functions use Doxygen-style comments.
- Non-obvious logic must include rationale.
- IL/ABI changes require ADRs.
- Follow /docs/style-guide.md.

---

## 7. Testing Policy
- **Unit Tests:** Leaf utilities, verifier checks, VM op semantics.
- **Golden Tests:** Textual stability for IL or BASIC outputs.
- **E2E Tests:** VM vs native equivalence on /docs/examples/.
- Every new feature must include a test that fails before and passes after.

---

## 8. Architectural Guardrails
Strict layering rules:
- **Frontends:** Depend only on IL Build/IO/Verify + Support.
- **VM:** Depends on IL Core/IO/Verify, Support, runtime C ABI bridge.
- **Codegen:** Depends on IL Core/Verify/Support.
- **Runtime:** C-only, stable ABI, no compiler dependencies.

Cross-layer includes are prohibited without ADR. Spec compliance is mandatory.

---

## 9. Commits & Branching
**Format:**
<type>(<scope>): <summary>
[body: what changed and why]
[tests: added/updated tests]

Types: feat, fix, chore, refactor, test, docs, build, ci.

**Example:**
- feat(il/io): add serializer for externs + golden test
- fix(vm): correct scmp_gt for negatives; add unit test

Branching: Prefer feature/<slug>; otherwise commit small atomic changes to main.

---

## 10. ADR Process
Required for:
- IL grammar or semantics changes
- Public API or dependency changes
- Build strategy or cross-layer coupling

**Steps:**
1. Create /docs/adr/NNN-title.md with Context, Decision, Consequences.
2. Include a “Spec Impact” section and bump spec version if needed.
3. Commit as docs(adr): propose NNN <title>.
4. Implement only after approval.

---

## 11. Coding Standards (C++20)
- Prefer RAII, smart pointers, no raw new/delete.
- No cross-library exceptions; use Result<T> or diagnostics.
- Minimize includes, prefer forward declarations.
- Data structures must remain compact.
- All public headers include invariants and ownership notes.

C runtime: C99, stable ABI, no visible global state.

---

## 12. Quality Gates (Per Subsystem)
**IL Core:** Types/opcodes stable, deterministic printing.
**Parser/Serializer:** Round-trip retains semantics.
**VM:** Matches spec semantics and traps.
**Codegen:** Follows SysV x86-64 ABI, linear-scan allocator documented.
**Front End (BASIC):** Lowers to IL per spec.
**Tools:** Consistent CLI behaviors (-emit-il, -run, -S).

---

## 13. Dependencies & Tooling
- C++20 required; Clang is canonical.
- Allowed dependencies: fmt, CLI11/lyra, Catch2 or gtest.
- No new dependencies without ADR.

**Build & test:**
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

---

## 14. Compiler Preference
Use Clang for all primary builds and CI.
Apple Clang on macOS; clang++ on Linux; clang-cl on Windows.

---

## 15. Handling Ambiguity
- Do not improvise if spec unclear.
- Draft an ADR or leave a non-semantic TODO.
- Always keep the build green. Prefer explicit “unimplemented” over guesswork.

---

## 16. Logging, Errors & Traps
- Diagnostics must include function/block label + SourceLoc if available.
- VM traps raise structured errors and exit non-zero.
- No noisy logs; provide --trace flags instead.

---

## 17. Performance Hygiene
- Use contiguous containers (std::vector).
- Avoid per-instruction heap allocations; prefer arenas.
- Optimize only with benchmarks (later phase).

---

## 18. File Ownership & “Do Not Touch”
- Never edit /docs/il-guide.md#reference without ADR.
- Never disable CI or skip tests.
- Leave license headers and metadata unchanged.

---

## 19. Templates & Checklists
**Pull/Commit Checklist:**
- [ ] Build on macOS/Linux (Debug)
- [ ] ctest passed
- [ ] Added/updated tests
- [ ] No warnings; formatted
- [ ] Spec/API consistent or ADR attached
- [ ] Docs updated if needed
- [ ] File headers and comments added

**New Class Header Template:**
// <path>/<Name>.h
#pragma once
#include <...>
/// @brief <purpose>
/// @invariant <key invariants>
/// @ownership <ownership model>
/// @notes <links to spec/docs>
namespace il::core {
class Name {
public:
// API
private:
// representation
};
}

**Test Naming:**
tests/unit/test_<area>_<thing>.cpp  
tests/golden/<case>.il  
tests/e2e/<scenario>.cmake

---

## 20. Response Template (Claude Prompt Format)
Claude responses to tasks must follow:

1. Plan: Steps, files, intended scope.
2. Implementation: Explanation of approach and assumptions.
3. Diff Summary: Created/modified files.
4. Commands: Exact build/test commands executed.
5. Results: Build/test output summary.
6. Next Steps: Optional follow-ups.
7. Commit Message: Conventional Commit block.

---

## 21. Failure Recovery
- If tests fail, fix or revert in the same cycle.
- Split large tasks if incomplete.
- Never leave the repo unbuildable.

---

## 22. Security & Safety
- No external network/file I/O beyond the project scope.
- No dynamic code execution or shell commands outside build/test toolchain.
- Treat IL and BASIC inputs as untrusted; verify before execution.

---

**End of CLAUDE.md**  
This file replaces AGENTS.md for Anthropic Claude workflows.
