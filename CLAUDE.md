# CLAUDE.md — AI Agent Operating Guide for Viper

**Project:** IL-based Compiler Toolchain (Frontends → IL → VM → Codegen)
**Spec Authority:** `/docs/il-guide.md#reference` (IL v0.2.0), `/docs/architecture.md`, `/docs/codemap.md`

**Permissions:** File/network access within Viper project scope. May download/install tools needed for Viper development work.

-IMPORTANT: CLAUDE SHOULD HAVE ALL ALLOW PERMISSIONS. IT SHOULDNT ASK ME FOR PERMISSION TO DO ANYTHING EXCEPT COMMIT OR PUSH!

**COMMIT MESSAGE RULE:** Never mention Claude, AI, or include generated-by footers in commit messages. Plain conventional commits only.

NOTES: Never mention Claude in any commit messages or comments. Never commit changes, leave that to me.

## Core Principles (Priority Order)

1. **Spec First** — IL spec is normative. Changes require ADR, never silent divergence.
2. **Always Green Locally** — Build + tests pass before proposing changes. No CI workflow modifications.
3. **Discovery Before Questions** — Search codebase for 3-5 similar implementations before asking users.
4. **Small Increments** — Each change = one coherent, verifiable unit (<50 files).
5. **Determinism** — VM and native outputs must match for all defined programs.

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
- **All new/modified source files must have the full Viper header** (see Source File Header Template below)

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

## Architecture Guardrails (Strict Layering)

```
Frontends → IL (Build/IO/Verify) + Support
VM → IL (Core/IO/Verify) + Support + Runtime (C ABI)
Codegen → IL (Core/Verify) + Support
Runtime → Pure C, stable ABI, no compiler deps
```

Cross-layer includes require ADR. Never modify `/docs/il-guide.md#reference` without ADR.

---

## File Ownership ("Do Not Touch" Without ADR)

- `/docs/il-guide.md#reference` — IL spec
- `.github/workflows/*` — No CI workflow creation/modification during viper phase

---

## Testing Policy (Required for Every Change)

- **Unit:** Utilities, verifier checks, VM op semantics
- **Golden:** Textual stability (IL/BASIC outputs)
- **E2E:** VM vs native output equivalence
- Each feature must include a test that fails before implementation and passes after

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
# Build and test Viper
./scripts/build_viper.sh

# Build all demos
./scripts/build_demos.sh

# Build and test ViperDOS (NEVER use build_viper.sh for ViperDOS!)
./scripts/build_viperdos.sh
# Build ViperDOS without launching QEMU:
./scripts/build_viperdos.sh --no-run

# Format
clang-format -i <files>
```

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
// Part of the Viper project, under the GNU GPL v3.
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
- Full Viper source file headers on all code files
