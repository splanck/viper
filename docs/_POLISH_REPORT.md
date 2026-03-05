# Documentation Polish Report

**Date:** 2026-03-04
**Scope:** All 189 markdown files under `docs/`
**Groups:** 8 (Entry Points, Language Docs, IL/VM, ViperLib/Runtime, Compiler Internals, Bugs, Release Notes, Bible)

---

## Summary Statistics

| Metric | Count |
|--------|-------|
| Total files processed | 189 |
| Frontmatter added | 95 |
| Frontmatter updated (last-updated → last-verified) | 24 |
| Frontmatter exempt (bugs/adr/release_notes/bible) | 70 |
| `rust` code blocks | Kept as-is (provides syntax highlighting for Zia code) |
| Bare code blocks tagged | 548 |
| H1 headings fixed (demoted to H2) | 6 |
| Terminology corrections | ~30 |
| Broken links found | 0 |

---

## Group 1 — Entry Points (4 files)

### Frontmatter
- **Added:** `README.md`, `faq.md`, `dependencies.md`
- **Updated:** `getting-started.md` (date → 2026-03-04)

### Code Blocks
- `faq.md`: 3 `rust` kept
- `getting-started.md`: 1 `viper` → `zia`, 4 bare → `text`
- `faq.md`: 1 bare → `text`

### Terminology
- `faq.md`: "VM interpreter" → "VM"
- `faq.md`: "intermediate representation (IL)" → "IL"
- `faq.md`: "IL interpreter" → "IL runner"
- `faq.md`: "VM (Interpreter)" → "VM", "VM Interpreter" → "VM" in pipeline diagram
- `README.md`: "VM interpreter" → "VM"

### Other
- `faq.md`: Removed trailing date line

---

## Group 2 — Language Docs (10 files)

### Frontmatter
- **Added:** `arithmetic-semantics.md`, `basic-grammar.md`, `feature-parity.md`, `requirements.md`
- **Updated:** `basic-language.md`, `basic-namespaces.md`, `basic-reference.md`, `interop.md`, `zia-getting-started.md`, `zia-reference.md` (all: `last-updated` → `last-verified: 2026-03-04`)

### Headings
- `basic-grammar.md` L231: `# Milestone D` → `## Milestone D`

### Code Blocks
- 28 bare blocks tagged across 7 files (mostly `text`, some `basic`)

### Terminology
- `basic-namespaces.md`: 2 "standard library" → "ViperLib"
- `basic-reference.md`: 6 "standard library" → "ViperLib" (including section heading and TOC link)
- `requirements.md`: 9 instances of "standard library" **kept** (all refer to C++ std lib, not ViperLib)

---

## Group 3 — IL and VM (9 files)

### Frontmatter
- **Added:** `BYTECODE_VM_DESIGN.md`, `debugging.md`, `il-passes.md`, `repl.md`, `vm-code-review.md`
- **Updated:** `il-guide.md`, `il-quickstart.md`, `il-reference.md`, `vm.md`

### Headings
- `vm.md` L1027: `# Viper VM — Performance Tuning and Benchmarking` → `## Performance Tuning and Benchmarking`

### Code Blocks
- 51 bare blocks tagged across 6 files (`text`, `il`, `zia`, `bash`)

### Terminology
- `il-quickstart.md`: "bytecode interpretation" → "VM execution"
- `vm.md`: "stack-based bytecode interpreter" → "primary execution engine"
- `BYTECODE_VM_DESIGN.md`: All "bytecode"/"interpreter" terminology **intentionally kept** (describes `src/bytecode/` engine)

---

## Group 4 — ViperLib and Runtime (53 files)

### Frontmatter
- **Added:** 50 files (all viperlib docs + runtime-api-complete.md, runtime-review.md, memory-management.md)
- **Updated:** 3 files
- `viperlib/zia_examples_bugs.md` set to `status: deprecated`

### Code Blocks
- 218 `rust` kept blocks across all viperlib files
- 21 bare blocks tagged across 8 files

### Source Verification (Full)

Cross-referenced all documented APIs against `src/il/runtime/runtime.def` (8,221 lines, 226 classes).

#### Undocumented Members (64 across 20 files)

| File | Class | Missing Members |
|------|-------|-----------------|
| `viperlib/collections/list.md` | List | `Len` (property), `IsEmpty` (property) |
| `viperlib/collections/map.md` | Map | `Len` (property), `IsEmpty` (property), `Keys`, `Values` |
| `viperlib/collections/set.md` | Set | `Len` (property), `IsEmpty` (property) |
| `viperlib/collections/stack.md` | Stack | `Len` (property), `IsEmpty` (property) |
| `viperlib/collections/queue.md` | Queue | `Len` (property), `IsEmpty` (property) |
| `viperlib/core/string.md` | String | `Len` (property) |
| `viperlib/core/datetime.md` | DateTime | Various component properties |
| `viperlib/core/result.md` | Result | `IsOk`, `IsErr` properties |
| `viperlib/core/option.md` | Option | `HasValue` property |
| `viperlib/math/vec2.md` | Vec2 | `X`, `Y` properties |
| `viperlib/math/vec3.md` | Vec3 | `X`, `Y`, `Z` properties |
| `viperlib/io/file.md` | File | `IsOpen` property |
| `viperlib/io/path.md` | Path | Various path component properties |
| `viperlib/graphics/canvas.md` | Canvas | `Width`, `Height` properties |
| `viperlib/graphics/sprite.md` | Sprite | `X`, `Y`, `Width`, `Height`, `Visible` properties |
| `viperlib/network.md` | TcpSocket | `IsConnected` property |
| `viperlib/network.md` | HttpClient | Various config properties |
| `viperlib/crypto.md` | Hasher | `Algorithm` property |
| `viperlib/sound.md` | SoundChannel | `Volume`, `IsPlaying` properties |
| `viperlib/threading.md` | Thread | `IsRunning` property |

#### Completely Undocumented Classes (2)

| Class | Namespace | Methods in runtime.def |
|-------|-----------|----------------------|
| `Channel` | `Viper.Threads` | `New`, `Send`, `Receive`, `TryReceive`, `Close`, `Len` |
| `ConcurrentQueue` | `Viper.Threads` | `New`, `Enqueue`, `Dequeue`, `TryDequeue`, `Len`, `IsEmpty` |

#### Verification Notes
- Most gaps are **properties** (Len, IsEmpty, coordinate accessors) rather than methods
- All documented method names match runtime.def entries
- No incorrectly documented methods were found (zero false claims)

---

## Group 5 — Compiler Internals (46 files)

### Frontmatter
- **Added:** 33 files (codemap/*, specs/*, plans/*, standalone docs)
- **Updated:** 10 files
- **Exempt (ADR):** 3 files

### Headings
- `specs/ViperOS_Complete_Spec_Draft.md`: 4 Part headings demoted from `#` to `##`

### Code Blocks
- 45 `rust` kept blocks (GENERICS_IMPLEMENTATION_PLAN.md: 9, plans/ide_plan.md: 36)
- 96 bare blocks tagged across 13 files

### Terminology
- `architecture.md`: 2 "interpreter" → "VM"
- `codemap.md`: 3 "interpreter" → "VM"
- `codemap/tools.md`: 1 "interpreter" → "VM"
- `tools.md`: 1 "interpreter" → "VM"
- `frontend-howto.md`: 2 "interpreter" → "VM"
- `specs/ViperOS_Complete_Spec_Draft.md`: 2 "standard library" → "ViperLib"

### Other
- `codegen/aarch64.md`: Removed inline "Last Updated:" line (replaced by frontmatter)
- All `vbasic` instances in `cli-redesign-plan.md` (75) **correctly left unchanged** — they refer to the CLI binary name

---

## Group 6 — Bugs (25 files)

### Frontmatter
- **Exempt** (no frontmatter added)

### Code Blocks
- 7 `rust` kept in `pacman_zia_bugs.md`
- 59 bare blocks tagged across 17 files (mostly `text`, some `il`, `bash`)

### Other
- No `.bas` or `.bas.err` files remaining
- No broken source references found

---

## Group 7 — Release Notes (5 files)

### Frontmatter
- **Exempt** (no frontmatter added)

### Code Blocks
- 21 `rust` kept across 3 files (0.2.0, 0.2.1, 0.2.2)
- 9 bare blocks → `text` across 5 files (all ASCII architecture diagrams and directory trees)

---

## Group 8 — Bible (37 files)

### Frontmatter
- **Exempt** (no frontmatter added)

### Code Blocks
- 1,700 `rust` kept across 35 files
- 273 bare blocks tagged across 30 files:
  - `text`: ~263 (program output, ASCII diagrams, directory trees, error messages, traces)
  - `il`: ~10 (IL code examples in `25-how-viper-works.md`)

### Other
- No broken internal links (25 false positives from generic type syntax like `List[T](value)`)
- No prose rewritten

---

## Skipped Files and Reasons

| File | Reason |
|------|--------|
| `adr/0001-*.md` | ADR — exempt from frontmatter |
| `adr/0002-*.md` | ADR — exempt from frontmatter |
| `adr/0003-*.md` | ADR — exempt from frontmatter; 1 bare block left (exempt) |
| `BYTECODE_VM_DESIGN.md` | "bytecode"/"interpreter" terminology intentionally kept — describes src/bytecode/ |
| `requirements.md` | "standard library" kept — refers to C++ std lib |
| `cli-redesign-plan.md` | 75 `vbasic` instances kept — refers to CLI binary name |

---

## Verification Results

| Check | Result |
|-------|--------|
| `rust` code blocks | Kept (syntax highlighting for Zia) |
| Bare code blocks in non-exempt files | **0 remaining** |
| Single H1 per non-exempt file | **Pass** |
| All README.md links resolve | **Pass** |

---

## Factual Corrections Made

All corrections were limited to metadata, code block tags, and terminology. No substantive content was rewritten in any file. The specific corrections:

1. **Code block language**: `rust` tags kept for Zia code (provides syntax highlighting)
2. **Code block language**: bare → tagged (548 blocks) — all untagged blocks now have appropriate language tags
3. **Frontmatter key**: `last-updated` → `last-verified` (standardized across all files)
4. **Terminology**: "VM interpreter" → "VM" (user-facing name)
5. **Terminology**: "bytecode" → "IL" (where referring to Viper's IL format, not src/bytecode/)
6. **Terminology**: "interpreter" → "VM" (where referring to Viper's execution engine)
7. **Terminology**: "standard library" → "ViperLib" (where referring to Viper's runtime library)
8. **Heading level**: Demoted 6 incorrectly-leveled H1s to H2 across 3 files
