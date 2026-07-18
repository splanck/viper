---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0111: Bound Textual IL Parsing Resources

## Status

Accepted.

## Context

Textual IL is consumed by command-line tools, language servers, and fuzzing
entry points. The grammar previously imposed no implementation limits on line
length, declaration counts, block counts, instruction counts, or operand
counts. A syntactically simple input could therefore request disproportionate
memory or native stack resources before verification.

Identifier validation also admitted non-whitespace ASCII control bytes, and the
shared token reader did not itself distinguish an unterminated quoted token
from a token ending exactly at EOF. These are lexical and parser-contract
changes under `docs/il/il-guide.md#reference`, so they require an ADR.

## Decision

1. `il::io::ParserLimits` defines configurable limits for textual parsing,
   including a per-function SSA temporary limit. Explicit numeric temporary
   names are checked before resizing the value-name table, preventing integer
   wraparound and attacker-controlled oversized allocations. `Parser::parse`
   accepts a limits value while retaining source compatibility through defaults.
2. Parsing is append-transactional without copying the existing module. A
   lightweight checkpoint restores declaration-vector sizes, module metadata,
   and the string-interner suffix after any syntax, I/O, or resource failure.
3. The default limits are deliberately generous for generated compiler output:
   1 MiB per physical line, 1,000,000 lines, 100,000 functions, 1,000,000
   blocks, 10,000,000 instructions, and 65,535 operands or branch arguments on
   one instruction.
4. Crossing a limit is a normal compile error with a source line and a
   `resource limit exceeded` explanation. It is never an assertion or an
   allocation-driven process failure.
5. IL identifier fragments reject ASCII control bytes (`0x00`–`0x1f` and
   `0x7f`) in addition to existing delimiter and whitespace exclusions.
6. A quoted token must contain an unescaped closing quote. EOF before that quote
   sets parser failure state and produces the existing malformed-string
   diagnostic path.
7. Analyses and verifiers that traverse user-controlled CFGs use explicit
   worklists rather than native recursion.

The limits constrain the textual transport, not the in-memory IL model. Trusted
programmatic builders remain able to construct larger modules, and callers may
raise parser limits explicitly when processing trusted generated artifacts.

## Consequences

- Existing valid modules within the defaults are unchanged.
- Pathological or control-byte-containing modules that were previously accepted
  are rejected deterministically.
- Tools can select smaller budgets for interactive or untrusted workloads.
- New parser collections must be charged to an existing limit or introduce a
  documented limit before accepting unbounded input.

## Alternatives Considered

- Relying on process memory limits was rejected because failures would be late,
  platform-dependent, and poorly diagnosed.
- Hard-coded, non-configurable limits were rejected because trusted compiler
  pipelines may legitimately need larger generated modules.
- Limiting only total file bytes was rejected because it does not bound the
  number and shape of allocated IR objects.
