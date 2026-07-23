# CLAUDE.md — AI Agent Operating Guide for Zanna

**Project:** IL-based Compiler Toolchain (Frontends → IL → VM → Codegen)
**Spec Authority:** `/docs/il/il-guide.md#reference` (IL v0.3.0), `/docs/internals/architecture.md`, `/docs/internals/codemap.md`

**Permissions:** File/network access within Zanna project scope. May download/install tools needed for Zanna development work.

-IMPORTANT: CLAUDE SHOULD HAVE ALL ALLOW PERMISSIONS. IT SHOULDNT ASK ME FOR PERMISSION TO DO ANYTHING EXCEPT COMMIT OR PUSH!

**COMMIT MESSAGE RULE:** Never mention Claude, AI, or include generated-by footers in commit messages. Plain conventional commits only.

NOTES: Never mention Claude in any commit messages or comments. Never commit changes, leave that to me.

## Workflow Preferences

- Do not launch background agents or ask clarifying questions unless explicitly asked. When given a task, proceed directly with execution.
- If interrupted with new requirements, incorporate them immediately without re-asking.
- On macOS, use POSIX-compatible shell commands. Avoid GNU-specific awk/sed syntax (no `^` anchors for indented content, no GNU awk features). Test commands mentally for BSD compatibility before running.
- For cross-platform-sensitive work, run `./scripts/lint_platform_policy.sh` and `./scripts/run_cross_platform_smoke.sh` before reporting done.

REPEAT! DO NOT USE AGENTS FOR WRITING CODE!! Agents can be used for investigation and collecting information, BUT DO NOT USE THEM TO WRITE CODE! NO EXCEPTIONS!!

## Core Principles (Priority Order)

1. **Spec First** — IL spec is normative. IL opcode, grammar, verifier-rule, cross-layer dependency, and runtime C ABI surface changes require ADR; never allow silent divergence.
2. **Always Green Locally** — Build + tests pass before proposing changes. No CI workflow modifications.
3. **Discovery Before Questions** — Search codebase for 3-5 similar implementations before asking users.
4. **Small Increments** — Each change = one coherent, verifiable unit (<50 files).
5. **Determinism** — VM and native outputs must match for all defined programs.
6. **Zero Dependencies** - Zanna is 100% a from scratch project. We dont introduce external dependencies for any reason.
7. **Cross platform** - Every feature must be completely implemented for Macos, Windows, and Linux. 100% cross platform always!
8. **Shared Platform Policy** - In normal code use `src/common/PlatformCapabilities.hpp` or `src/runtime/rt_platform.h`; raw `_WIN32` / `__APPLE__` / `__linux__` checks belong only in approved adapter layers.

---

## Development Flow (Required)

### 1. DISCOVER (Search Code First)
```
Find pattern examples → Extract structure → Identify gaps → Then ask
```
- Locate 3-5 similar implementations (naming, structure, error handling)
- Find template code demonstrating the pattern
- **Rule:** Technical patterns → search code. Business decisions → ask user.

### 2. INTERROGATE (5-Stage Progression)
**Stage 1:** What/why/success criteria
**Stage 2:** "Found pattern X at location Y—use this?"
**Stage 3:** ★ **MANDATORY** ★ Resolve ALL of:
- Feature toggle (required? default state?)
- Configuration (keys/defaults or "none")
- Scope boundaries (explicit in/out)
- Performance SLAs (e.g., "p95 < 500ms")
- All error scenarios with exact messages

**Stage 4:** Exact technical details (property names, types, API contracts, test cases)
**Stage 5:** "We're building X with Y behavior using Z pattern—what did I miss?"

### 3. SPECIFY (Before Code)
Use template from §20.4 (paste into deliverable). Must include:
- Exact names/types/values (no placeholders like "TBD")
- Feature toggle strategy or explicit "not required" + rationale
- All error scenarios with full messages
- Given/When/Then for positive, negative, edge tests

### 4. IMPLEMENT (After Spec Approval)
- Add tests first, then code
- Format with `.clang-format`, zero warnings
- Follow Conventional Commits: `<type>(<scope>): <summary>`
- Keep headers minimal, avoid cross-layer dependencies
- **All new/modified source files must have the full Zanna header** (see Source File Header Template below)

---

## Quality Gates

**Before proposing any change:**
- ✅ Local builds pass (Linux/macOS Debug)
- ✅ All tests pass (unit + golden + e2e)
- ✅ New code includes tests + doc comments
- ✅ Zero warnings, formatted
- ✅ Commit message follows conventions

**Per Subsystem:**
- **IL Core:** Stable types/opcodes, deterministic printing
- **VM:** Matches spec semantics, correct trap handling
- **Codegen:** SysV x86-64 ABI compliance
- **Frontend:** Lowers to spec-conformant IL

---

## Large-Scale Changes

When making large-scale changes across many files (renames, sed replacements, doc updates), always check ALL files including demos, examples, test fixtures, scripts, and config files — not just source code. Never skip files or try to shortcut the scope. After execution, re-grep for any remaining stale references before reporting done.

---

## Architecture Guardrails (Strict Layering)

```
Frontends → IL (Build/IO/Verify) + Support
VM → IL (Core/IO/Verify) + Support + Runtime (C ABI)
Codegen → IL (Core/Verify) + Support
Runtime → Pure C, stable ABI, no compiler deps
```

IL opcode, grammar, verifier-rule, cross-layer dependency, and runtime C ABI surface changes require ADR. Never modify `/docs/il/il-guide.md#reference` without ADR.

---

## File Ownership ("Do Not Touch" Without ADR)

- `/docs/il/il-guide.md#reference` — IL spec
- `.github/workflows/*` — No CI workflow creation/modification during zanna phase

---

## Testing Policy (Required for Every Change)

- **Unit:** Utilities, verifier checks, VM op semantics
- **Golden:** Textual stability (IL/BASIC outputs)
- **E2E:** VM vs native output equivalence
- Each feature must include a test that fails before implementation and passes after
- After any build/rename/refactor affecting multiple files, always rebuild and run the full test suite before reporting done. Verify zero regressions.
- For golden file regeneration, use `./scripts/update_goldens.sh` or be careful with shell escaping.
- **Assertions:** Use `EXPECT_EQ`/`ASSERT_EQ` (prints actual values on failure), `EXPECT_GT/LT/GE/LE`, `EXPECT_NEAR` (floats), `EXPECT_CONTAINS` (strings), `EXPECT_THROWS`/`EXPECT_NO_THROW`.
- **Test filtering:** Run single test from multi-test binary: `./build/test_foo --filter=Suite.Name`
- **Labels:** Use `ctest -L codegen` to run by category. See `src/tests/CMakeLists.txt` for full taxonomy.
- **Fuzz harnesses:** In `src/tests/fuzz/` (opt-in via `ZANNA_ENABLE_FUZZ=ON`). Add harnesses for new parser/input surfaces.

---

## Response Template (Use This Structure)

When responding to a task:

1. **Discovery Evidence** — Patterns found (files/lines)
2. **Knowledge Gaps** — Structured list requiring resolution
3. **Questions** — Staged interrogation (§2)
4. **Specification Draft** — Using §20.4 template; mark TODOs explicitly
5. **Implementation Plan** — Approach + files to modify (<50)
6. **Commands & Results** — Build/test output summary
7. **Validation** — Against acceptance criteria
8. **Commit Message** — Conventional Commits format

---

## Appendix: Quick Reference

### Build Commands

**IMPORTANT:** Always use the provided build scripts. Never use raw `cmake` commands directly.

```sh
# Build and test Zanna (canonical POSIX script; build_zanna_linux.sh /
# build_zanna_mac.sh are thin wrappers around it)
./scripts/build_zanna_unix.sh

# Build all demos
./scripts/build_demos.sh

# Build and test Zanna on Windows (PowerShell)
.\scripts\build_zanna_win.ps1

# Build all demos on Windows (PowerShell is canonical; cmd shim is supported)
.\scripts\build_demos_win.ps1
.\scripts\build_demos_win.cmd

# Format
clang-format -i <files>
```

### Fast Iteration Loop (Agent-Optimized)

The build scripts accept environment variables to shorten the edit-build-test
cycle. ccache is auto-detected (opt out with `ZANNA_NO_CCACHE=1`).

```sh
# Incremental build only (no clean, no tests) — seconds, not minutes
ZANNA_SKIP_CLEAN=1 ZANNA_SKIP_TESTS=1 ZANNA_SKIP_LINT=1 ZANNA_SKIP_AUDIT=1 \
ZANNA_SKIP_SMOKE=1 ZANNA_SKIP_INSTALL=1 ./scripts/build_zanna_unix.sh

# Incremental build + one test label
ZANNA_SKIP_CLEAN=1 ZANNA_TEST_LABEL=tools ZANNA_SKIP_LINT=1 ZANNA_SKIP_AUDIT=1 \
ZANNA_SKIP_SMOKE=1 ZANNA_SKIP_INSTALL=1 ./scripts/build_zanna_unix.sh

# Then run targeted tests directly
ctest --test-dir build -R test_zia_lexer --output-on-failure
ctest --test-dir build -L golden --output-on-failure
```

Always finish with a full build + test run (no skip flags) before reporting done.

### Agent-Facing CLI

- `zanna check <target> --diagnostic-format=json` — fast type-check/verify gate
  (exit 0 clean / 1 usage / 2 compile errors; JSON carries code, stage, range,
  notes, and applicable fixits)
- `zanna eval 'expr' [--json --type --il]` — one-shot snippet evaluation
  (exit 3 = runtime trap)
- `zanna explain <CODE> [--json]` / `zanna --print-error-codes --json` —
  diagnostic-code catalog
- `zanna --dump-runtime-api` / `zanna --dump-opcodes` — machine-readable
  registry inventories (never drift; generated from the live binary)

### Conventional Commits
```
<type>(<scope>): <summary>
[body: what and why]
[tests: coverage added]
```
Types: `feat`, `fix`, `chore`, `refactor`, `test`, `docs`, `build`

### Source File Header Template

**All source files (.h, .c, .hpp, .cpp) must use this header:**

```cpp
//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: <path>
// Purpose: <brief description of file purpose>
// Key invariants:
//   - <invariant 1>
//   - <invariant 2>
// Ownership/Lifetime:
//   - <ownership model>
//   - <lifetime guarantee>
// Links: <related files or docs>
//
//===----------------------------------------------------------------------===//
```

### New Class Header Template (after file header)
```cpp
#pragma once
/// @brief <purpose>
/// @invariant <key invariants>
/// @ownership <ownership model>
namespace il::core {
class Name {
  // ...
};
}
```

### Specification Template (§20.4)
1. Summary & Objective
2. Scope (in/out)
3. **Feature Toggle** (strategy/default or "not required" + reason)
4. **Configuration** (keys/defaults or "none")
5. Technical Requirements (exact names/types)
6. **Error Handling** (all scenarios + exact messages)
7. **Tests** (Given/When/Then; pos/neg/edge)
8. Code References (files/lines + exemplars)

---

**Compiler:** Clang is canonical (Apple Clang on macOS, clang++ on Linux)

---

**Key Differences from Generic AI Guidance:**
- Spec-first development with ADR process
- Always-green local builds (no CI modifications)
- Discovery-driven interrogation before specification
- Strict architectural layering enforcement
- VM/native determinism requirement
- Mandatory build scripts (never raw cmake)
- Full Zanna source file headers on all code files
