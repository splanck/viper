# Plan 25 — Debug adapter: structured variable expansion

## 1. Objective & scope

Extend the VM debug adapter (`viper run --debug-adapter <file>`) so locals and
watch results are **structured**: composite values (objects, lists, maps, seqs,
strings-as-preview) report a child count and can be expanded lazily by
reference, instead of arriving as flat display strings. This is the protocol
half of proper variable inspection; the IDE UI half is **plan 13**.

**In scope:** adapter-side protocol additions (`variablesReference`-style),
child enumeration for the VM's runtime value kinds, protocol documentation,
adapter tests, backward compatibility with the current flat consumers.

**Out of scope:** ViperIDE UI (plan 13), evaluate-expression semantics changes,
new stepping behavior.

## 2. Current state (verified anchors)

- Adapter implementation: `src/tools/viper/DebugAdapter.cpp` (515 lines),
  launched from `src/tools/viper/cmd_run.cpp` (the only file matching
  `debug-adapter`).
- Protocol shape: newline-JSON commands on stdin, sentinel-tagged newline-JSON
  events on stderr; a background reader thread queues commands
  (`DebugAdapter.cpp:100-127`, `class DebugChannel`).
- The stop payload emits **flat locals**: an array of objects built at
  `DebugAdapter.cpp:178-194` (`locals.push_back(JsonValue::object({...}))`,
  attached as `{"locals", ...}`) — one display string per local, no children,
  no references.
- Stepping/pause logic lives in `class AdapterFrontend : public il::vm::DebugFrontend`
  (`:276+`), which implements source-line stepping over the VM's
  instruction-level hooks.
- IDE consumer: `viperide/src/build/debug_session.zia` (782 lines) parses the
  JSON events; `viperide/docs/status.md:261-268` documents the known gap:
  "object values are not expandable because the debug adapter currently returns
  flat local/watch values."
- The VM side exposes frame/locals info through `il::vm` (see the `info.locals`
  struct consumed at `:180`); child enumeration must come from the VM's runtime
  value representation (rt_obj / seq / map / list handles). Discovery task
  during implementation: locate the value-inspection helpers the adapter already
  uses to stringify locals, and extend from there rather than inventing a new
  inspection layer.

## 3. Design

### 3.1 Protocol additions (documented in `viperide/docs/runtime-integration.md`)

Stop events (and watch/evaluate responses) gain per-value fields:

```json
{ "name": "player",
  "value": "Player { hp: 100, pos: Vec2 }",   // unchanged display string
  "type": "Player",
  "varRef": 17,                                // 0 = leaf (default, back-compat)
  "childCount": 4 }                            // hint for the UI; -1 = unknown
```

New request/response pair:

```json
→ { "cmd": "variables", "varRef": 17, "start": 0, "count": 100 }
← { "event": "variables", "varRef": 17,
    "vars": [ { "name": "hp", "value": "100", "type": "Integer", "varRef": 0, "childCount": 0 }, ... ] }
```

Rules:

- `varRef` handles are **valid only while stopped**; the table is cleared on
  every resume/step/continue (same lifecycle as DAP). Monotonic ids per stop.
- Paging via `start`/`count` (UI asks for ≤100 at a time); lists/maps beyond
  that are truncated with a synthetic `"…"` row by the *UI*, not the adapter.
- Depth is naturally bounded by laziness — the adapter never recurses; it
  expands exactly one level per request.
- Watch responses reuse the same shape, so plan 13 renders locals and watches
  through one code path.
- **Back-compat:** all existing fields keep their exact names/values; a consumer
  that ignores `varRef` sees today's behavior. No version negotiation needed.

### 3.2 Adapter-side implementation

- `struct VarTable { std::vector<VarEntry> entries; }` on the adapter session;
  `VarEntry` holds whatever the VM needs to re-find the value while stopped
  (frame index + local slot, or a retained runtime handle + kind tag).
  Cleared in every resume path (continue/step/terminate/restart).
- Child enumeration by runtime kind:
  - class instances → fields (name from the runtime class metadata the VM
    already has for stringification),
  - `List`/`Seq` → indexed elements (`[0]`, `[1]`, …),
  - `Map` → key/value pairs (key stringified as the child name),
  - strings → leaf (long strings stay a display-string concern),
  - primitives → leaf.
- Reuse the adapter's existing stringification helper for each child's `value`
  field — children are just one-level-down locals.
- Threading: the `variables` command is handled at the stop wait-loop
  (`AdapterFrontend::onStop` command pump, `:271+, :371`), where the VM thread
  is already paused — the same context that safely reads locals today. Reject
  (ignore) `variables` while running, matching how other commands are ignored
  at `:484`.

## 4. Implementation steps

1. **Discovery:** read `DebugAdapter.cpp` fully; identify the locals source
   (`info.locals`) type in `il::vm`, and what handle/typing information is
   available per local. Decide `VarEntry` representation from that (do not
   guess ahead of reading).
2. Emit `varRef: 0, childCount: 0` on all existing values (pure additive,
   proves serialization without behavior change). Update
   `debug_session.zia` *not at all* — verify it tolerates unknown fields (it
   parses JSON by key; confirm with `debug_probe.zia`).
3. Implement `VarTable` + composite detection + `varRef` assignment for
   locals/watches at stop time.
4. Implement the `variables` command in the stopped command pump; one level,
   paged.
5. Clear table on every resume path; assert-log if a stale ref arrives.
6. Document the protocol in `viperide/docs/runtime-integration.md` (it already
   describes the adapter wire format).
7. **Tests:** extend the debug probe layer. `viperide/src/probes/debug_probe.zia`
   (378 lines) already drives a scripted adapter session; add a fixture program
   with a class instance, a list, and a map in scope; drive: launch → stop at
   breakpoint → read locals → expect `varRef != 0` on composites → send
   `variables` → assert child names/values → step → assert old ref now dead.
   If the probe drives the real `viper` binary, pass it via the existing
   `--viper-bin` convention (`src/tests/CMakeLists.txt:1098`).
8. Full build + test run (`ctest --test-dir build -L viperide`, plus the
   adapter's own test label if one exists — check `ctest -N | grep -i debug`).

## 5. Files to modify

- `src/tools/viper/DebugAdapter.cpp` — VarTable, structured emission,
  `variables` command.
- Possibly a small helper header next to it if the file would exceed its
  current organization (keep `DebugAdapter.cpp` cohesive; split a
  `DebugAdapterVars.{hpp,cpp}` if >~250 new lines).
- `viperide/docs/runtime-integration.md` — protocol doc.
- `viperide/src/probes/debug_probe.zia` (+ a new fixture `.zia`/`.bas` program
  under the probes dir) — coverage.
- `src/tests/CMakeLists.txt` — only if a new probe binary/registration is added.

## 6. Testing

- Probe-driven end-to-end test (step 7) is primary.
- Regression: existing `debug_probe.zia` scenarios (breakpoints, stepping,
  locals display, watches, restart) must pass unmodified after step 2 and after
  the full change — flat consumers are untouched.
- Manual: in ViperIDE (before plan 13 lands), the Variables panel must look
  exactly as today.

## 7. Acceptance criteria

- Stop event for a frame containing a class instance, list, and map carries
  non-zero `varRef` + `childCount` for each; primitives carry `varRef: 0`.
- `variables` returns correct one-level children with paging; refs die on resume.
- All pre-existing debugger tests/probes pass unchanged.
- Protocol documented; no changes to `debug_session.zia` required by this plan.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- This plan does not touch `runtime.def`; if discovery reveals it must, every
  new function needs RT_FUNC + RT_METHOD and a
  `./scripts/check_runtime_completeness.sh` run.
- Full Viper file header on all new/modified C++ files.
- 100% cross-platform; the adapter is plain C++/JSON — keep it that way.
- Zero external dependencies (the in-tree JsonValue is already used).
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
