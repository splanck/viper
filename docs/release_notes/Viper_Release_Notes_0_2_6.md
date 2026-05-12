# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A focused hardening-and-correctness cycle on the v0.2.5 surface. No new public namespaces; every change closes an attack surface, eliminates a latent correctness bug, or tightens a lifetime, locking, or type-identity invariant.

- Memory and GC overhaul — validated `Viper.Memory.Retain` / `Release` wrappers, lock-free traversal, trap-safe finalizer phase, resurrection-aware refcounting.
- MessageBus end-to-end correctness — class-ID validation, managed callback objects, NUL-safe topic hashing, internal locking, GC traversal registration.
- `Viper.Threads.*` three-round correctness pass — repeatable joins, distinct class IDs, idempotent pool shutdown with sticky-then-cleared task traps, retain-during-call across every public threads entry, monitor finalize-while-waiting safety, async + Parallel.Map retained-result ownership, listener trap isolation, synchronous-channel `TrySend` rendezvous, nested-parallel self-deadlock guard, VM payload OOM handling.
- Crypto upgrade — `Viper.Text.*` → `Viper.Crypto.*` migration with backward-compat aliases, scrypt-SHA256, AES-256-GCM with AAD, P-256 ECDH, HMAC-SHA384/512, validation-ready `Viper.Crypto.Module` with approved-mode policy gates.
- TLS X.509 verifier hardening — Key Usage, Basic Constraints, and Extended Key Usage extensions enforced; DNS-name validation tightened.
- IO security hardening (two rounds) — random-nonce temp files, `openat`-based recursive removal, `O_CLOEXEC` / `O_NOFOLLOW` everywhere, SaveData absolute-path resolution.
- Archive / Compress / VPA correctness — ZIP64 rejection, local-vs-central header cross-check, exact GZIP boundary detection, 64-bit VPA seeks.
- 2D graphics correctness — saturating int64 clip math, validated polyline / polygon arrays, sprite tint alpha preservation, tilemap I/O ownership discipline.
- `Viper.Audio.*` compat surface rebuilt from `RT_ALIAS` aliases to full typed registrations.
- AArch64 register-allocator correctness — protection set covers def operands, fixing register-reuse clobbers.
- Windows x64 stabilization — out-of-order block-param lowering, RSP/RBP scheduling boundaries, lone-JCC trace fix, parallel-copy spill scratch reuse, 256-bone D3D11 cbuffer.
- x86-64 backend liveness contracts — compare/branch fold safety across edge copies, IMUL→LEA refusal when flags are read, block-DCE preservation of physical registers at exits, fixed-physical-register spill-before-clobber, void-return zero exit code.
- Native assembler & linker hardening — bounds-checked encoders, writers, and readers; alignment-UB fixed; type-safe section keys; new AArch64 LDR/STR scaled-offset relocs; COFF addends patched into instruction bytes with range checks.
- Native linker correctness pass — COMMON symbol coalescing, ELF weak-undefined handling, platform-specific symbol fallback, Mach-O text subsection splitting, executable entry validation, TLS layout emission, and relocation opcode checks.
- Native demo regression repairs — three rounds of x86-64 and AArch64 backend fixes the new linker validation surfaced, plus a MOVZX byte-source REX-prefix bug.
- 2D graphics correctness round 2 — class-id validation on every typed handle, premultiplied-alpha bilinear sampler, tagged `Color.RGBA(...,0)` distinguishable from legacy `0x00RRGGBB`, alpha-preserving Canvas `BlitAlpha` and particle source-over, Lighting2D tile-light per-frame lifetime.
- Graphics3D correctness round — Canvas3D HUD/skinned/morphed/blended draw entries, Mesh3D skeleton + morph-target retain integration, atomic Scene3D reparent, Physics3D raycast/joint hardening, terrain/skeleton input validation, glTF JOINTS round-trip in `SetBoneWeights`, new `Viper.Graphics3D.GLTF` runtime class plus `Scene3D.Load`.
- Typed `Parse.*` string ABI — `Viper.Core.Parse.Double` / `Int64` signatures flipped to `i32(str,ptr)`; legacy raw-pointer ABI preserved for INPUT# parsing via internal aliases.
- Heap immortal-refcount sentinel split — `RT_HEAP_IMMORTAL_REFCNT` (`SIZE_MAX-1`) reserved as a sticky immortal value distinct from the `SIZE_MAX` corruption marker.
- MSVC build hardening — embedded debug info via CMP0141, expanded import policy for UCRT/Win32 symbols, WSL-aware audit and smoke scripts.
- ViperIDE polish — IntelliSense linkage fix via `fe_zia` force-load, real keyword highlighting from the live `Lexer`, scrollBeyondLastLine, BMP toolbar glyphs.
- GUI runtime widget-handle correctness — every public entry routes through centralised type-checked wrappers, contextmenu submenu ownership is now bidirectional, code-editor block-comment depth derives from a buffer scan instead of render state, and progressbar/breadcrumb/slider/toast/shortcut paths receive lifetime, range, and focus-routing fixes.
- Network runtime hardening — HTTP/2 HPACK now uses separate encode/decode dynamic tables (a single table was conflating both directions and corrupting outbound headers after any dynamic-table churn), WebSocket servers reject non-minimal-length frames per RFC 6455 §5.2 and clients validate close codes per §7.4, the URL parser tightens IPv6 bracket handling and replaces silent port clamping with explicit traps, HTTP header field names are validated as full RFC 7230 tokens, UDP recv detects truncation via `MSG_TRUNC`/`WSAEMSGSIZE`, and the connection-pool health-drop path releases its slot under the pool lock instead of leaking it.
- x86-64 backend regression repairs — integer cast width selection, label-operand validation, frame-slot aliasing, overflow-pseudo lowering, switch operand normalisation, and a documentation pass across ~50 backend source files with doxygen on every helper.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 54 | +54 |
| Source files | 2,996 | 3,006 | +10 |
| Production SLOC | 552K | 571K | +19K |
| Test SLOC | 228K | 239K | +11K |
| Demo SLOC | 188K | 188K | 0 |

Counts via `scripts/count_sloc.sh` (production 571,012 / test 239,415 / demo 187,826 / source files 3,006).

---

### Memory and GC

- `Viper.Memory.Retain` / `Release` redirected through validated `rt_memory_retain` / `rt_memory_release` wrappers that authenticate live runtime handles before touching refcount state.
- New typed `RetainStr` / `ReleaseStr` variants callable from Zia without a `void *` cast.
- `Release` now releases retained element references inside `RT_HEAP_ARRAY` storage (string, owned, and box element kinds) before freeing the backing buffer — these slots previously leaked.
- `rt_obj_new_i64` traps on negative or `SIZE_MAX`-overflowing byte sizes; public heap helpers validate every payload against the live registry; `rt_obj_free` rejects non-zero refcounts.
- Resurrection uses CAS and traps if the refcount is no longer zero; `Release` reports the post-finalizer refcount.
- GC phases 2 and 3 drop the lock during traversal callbacks: outgoing edges collected into a heap-allocated `gc_edge_list`; lock re-acquired only to apply colour updates.
- Phase 3 restore rewritten as iterative worklist BFS instead of recursive in-lock callbacks.
- Phase 4 wrapped in `setjmp` / `rt_trap_set_recovery` so a trapping finalizer cleans up garbage and snapshot arrays before re-raising.
- Snapshots retained via `rt_obj_retain_maybe` before lock release — prevents UAF if another thread drops the last external reference.
- Promoted-root restoration during non-full passes preserves young objects reachable from long-lived roots; weak references cleared only after finalizers decline resurrection.
- `TotalCollected` saturates at `INT64_MAX`.

### Runtime objects and MessageBus

- Boxed values carry `RT_BOX_CLASS_ID`; `Object.Equals` / `GetHashCode` dispatch correctly through `Set[Box]` and `Map[Box, ...]`.
- `rt_box_hash` normalizes `-0.0` to `+0.0` so equal values hash equally.
- New `rt_box_i1_bool(int8_t)` ABI-matching variant lets the Zia lowerer skip the `i1` → `i64` zero-extend.
- `Box.ValueType` is a class-tagged managed type — boxed structs retain object/string fields, participate in GC traversal, and release fields on finalization.
- `rt_heap_realloc` holds the registry lock as one critical section across check / `realloc` / move; `rt_heap_mark_disposed` return value corrected (was inverted); `rt_heap_try_get_header` closes a TOCTOU window on the magic check.
- `rt_type_registry` refactored to a `const char*`-error-return pattern; capacity growth no longer traps under the registry write lock.
- MessageBus: `RT_MSGBUS_CLASS_ID` and `RT_MSGBUS_CALLBACK_CLASS_ID` validated through `mb_require()` on every public entry; raw function pointers and unrelated heap objects now trap.
- Topic hashing uses full byte length via `mb_hash_bytes` / `memcmp` — embedded NULs preserved across subscribe / publish / clear.
- `mb_traverse()` registers retained callback objects with the GC tracker; an internal lock serializes public operations; `Topics()` returns an owning `Seq`; `Publish` releases its retained snapshot before re-raising a trapping handler; subscription-ID overflow caught before counter wrap.
- `Publish` now retains managed runtime objects and strings for the dispatch window (`mb_retain_managed_payload`) so a concurrent free can't pull the payload away mid-call; raw foreign pointers stay borrowed and must outlive the publish.
- `Parse.DoubleOption` / `Int64Option` (with `Viper.Parse.*` aliases) — Option-returning helpers for graceful parse failure.
- `Viper.Core.Parse.Double` / `Int64` ABI flipped from `i32(ptr,ptr)` to `i32(str,ptr)` — first arg is now a typed runtime string. New `rt_parse_int64_str` / `rt_parse_double_str` natives validate the handle, reject embedded-NUL strings, then delegate to the C-string bodies. BASIC `INPUT#` parsing keeps the legacy raw-pointer ABI through internal `kParseDoubleCStr` / `kParseInt64CStr` aliases.
- `rt_parse_int_radix` accepts `+` as a leading sign (was returning `default_value` on the unary plus).
- Heap immortal-refcount sentinel split: new `RT_HEAP_IMMORTAL_REFCNT` (`SIZE_MAX-1`) and `RT_HEAP_MAX_MORTAL_REFCNT` macros reserve `SIZE_MAX` purely as a corruption marker. `rt_heap_retain` skips on already-immortal payloads; `rt_heap_release` traps with `cannot release immortal refcount`.
- `rt_heap_realloc` registry-update failure switched from `rt_trap` to `rt_abort` — at that point the registry has lost track of the live payload and trap recovery would leave the runtime in a half-corrupted state.
- 9 string-inspection helpers (`rt_str_len`, `rt_str_index_of`, `rt_str_instr3`, `rt_str_eq`, `rt_str_lt`, `rt_str_le`, `rt_str_gt`, `rt_str_ge`, `rt_str_asc`) flipped from `nothrow=true` to `nothrow=false` — they trap on invalid handles, so the optimizer must not hoist them across exception boundaries.
- `Object.RefEquals` exposed through the class method index; `rt_to_double` switched to strict `rt_parse_double` so partial parses no longer succeed silently.
- New `Box.TryToI64` / `TryToF64` / `TryToI1` / `TryToStr` option-style accessors return `i1` and never trap; `TryToStr` writes a retained string handle (modelled by the new `ownedOutArgMask` ownership-effect bit). `Box.ToI1` ABI corrected from `i64(obj)` to `i1(obj)` end-to-end.
- `Box.ValueType.AddField` now traps on offsets that aren't pointer-aligned.
- New `RT_ELEM_OBJ` heap element kind disambiguates "no managed elements" from "object pointer elements"; object arrays release-per-pointer, primitive arrays remain a true no-op.
- `Diagnostics.Trap` routes through new validating `rt_trap_string` dispatcher that escapes embedded NULs and rejects invalid string handles before reaching the C-string trap formatter; AArch64 + x86-64 backends and BASIC + Zia frontends all retargeted at the new symbol.

### Crypto

- The Password / KeyDerive / Aes / Cipher / Hash / Rand classes formerly under `Viper.Text.*` are now canonical members of `Viper.Crypto.*`; the old names continue to resolve through the runtime alias mechanism — no source changes required.
- `KeyDerive.ScryptSHA256` / `ScryptSHA256Str` — RFC 7914 with bounded `N * r * 128` memory cap; out-of-range parameters rejected, not silently clamped.
- `Password.Hash()` defaults to scrypt and emits `SCRYPT$<log2N>$<r>$<p>$<salt_b64>$<hash_b64>`; `Password.Verify()` still accepts the legacy `PBKDF2$…` format so old hashes verify without migration.
- PBKDF2 wrappers reject below-minimum iteration counts (was silently raised).
- `Aes.EncryptAuth` / `DecryptAuth` and `Cipher.EncryptAAD` / `DecryptAAD` / `EncryptWithKeyAAD` / `DecryptWithKeyAAD` accept both AES-128-GCM and AES-256-GCM keys.
- 16-byte magic header (`'V', 'A', 'K', '1'` plus version) prevents plain-AES and authenticated-AES ciphertexts from being confused; the header is composed with the application-supplied AAD so the GCM tag covers both.
- New `Viper.Crypto.Module` adds approved-mode controls, module self-tests, and fail-closed policy gates; AES-256-GCM, HMAC-SHA384, HMAC-SHA512, HKDF-SHA384, and native P-256 ECDH primitives.
- Approved mode routes `Cipher` through AES-256-GCM (`VCA1` / `VKA1`), routes `Password.Hash` through PBKDF2, and uses an in-tree HMAC-DRBG for runtime crypto randomness.
- TLS fails closed in approved mode until the P-256/P-384 TLS key-share profile is wired and validated.
- `Hash.ConstantTimeEquals` / `ConstantTimeEqualsBytes` — branch-free equality for MAC tags, session IDs, and other secret-bearing strings; `Password.Verify()` uses these internally.

### TLS

- Key Usage (OID 2.5.29.15), Basic Constraints (2.5.29.19), and Extended Key Usage (2.5.29.37) extensions parsed and enforced during chain validation.
- Intermediate certificates without the cA flag in Basic Constraints are rejected; leaf certificates without TLS Web Server Authentication EKU are rejected.
- Key Usage bits checked against the role each certificate plays in the chain.
- `tls_dns_name_bytes_valid` rejects empty names, embedded-NUL or non-ASCII bytes, names > 255 bytes, labels > 63 bytes, and improper wildcard placement.
- `rt_crypto.c` and `rt_tls.c` thread the new constant-time helpers and per-namespace class IDs through the handshake state.

### IO runtime

- Temp-file creation uses a 64-bit random nonce (`/dev/urandom` on POSIX, `rand_s` + `QueryPerformanceCounter` on Windows) — eliminates the ASLR side-channel from the previous pointer-encoded scheme.
- Asset decoding creates a private directory (`mkdtemp` on POSIX, random subdirectory on Windows), cleaned up on success and failure.
- `asset_name_is_safe()` rejects absolute paths, drive letters, colons, empty segments, and dot/dotdot components; asset opens use `open(O_NOFOLLOW | O_CLOEXEC) + fdopen`.
- Recursive directory removal rebuilt around `openat` / `fstatat` / `unlinkat` / `fdopendir` with `AT_SYMLINK_NOFOLLOW` — a symlink substituted mid-traversal cannot redirect deletion.
- `O_CLOEXEC` at every open with `FD_CLOEXEC` fallback via `fcntl`; Windows paths add `_O_NOINHERIT`.
- Channel validity tightened: `channel <= 0` rejected (channel 0 was never valid); `rt_file_close` saves the fd locally before clearing — closes a UAF race.
- `Glob` rejects embedded NULs; recursion capped at 4096; `MemStream.WriteI8` validates `[-128, 127]`.
- Watcher emits `RT_WATCH_EVENT_OVERFLOW` on buffer overflow rather than silently dropping; `IN_CLOEXEC` / `FD_CLOEXEC` on inotify and kqueue fds.
- New `rt_file_stdio.h` consolidates the UTF-8-aware `fopen` wrapper duplicated across four files.
- SaveData paths resolved to absolute before first use (`GetFullPathNameW` / `getcwd`-with-doubling); Windows opens use `_O_NOINHERIT | _O_BINARY`; malformed entries silently skipped so games are resilient to partial writes.
- ZIP extra-field parser rejects ZIP64 extension headers; central-directory rejects `version_needed >= 45`, encryption flags, ZIP64 sentinels.
- Local-file headers cross-checked against central-directory values for method, flags, CRC, and both size fields.
- `inflate_data_limited_ex()` exposes `consumed_bytes` for exact compressed-input accounting; multi-member GZIP uses this for precise boundary detection.
- VPA seeks use 64-bit `_fseeki64` / `fseeko`; opens perform `lstat` pre-check then `O_NOFOLLOW`.

### Collections runtime

- Eight new unsigned `BinaryBuffer` methods: `WriteU16LE/BE`, `WriteU32LE/BE`, `ReadU16LE/BE`, `ReadU32LE/BE` (with `[0, UINT16_MAX]` / `[0, UINT32_MAX]` range validation).
- Retroactive range-check guards on `WriteI16LE/BE` and `WriteI32LE/BE` — previously truncated silently.

### Threading runtime

- Thread joins are now repeatable: the first successful `Join`, `TryJoin`, or `JoinFor` reclaims the OS handle; later calls on the same handle return success instead of trapping with `Thread.Join: already joined`. The trap is gone, ADR 0002 and `viperlib/threads.md` updated to match.
- `Thread.Start` / `StartSafe` borrow their argument consistently across native, VM, and BytecodeVM dispatch; `StartOwned` / `StartSafeOwned` retain managed arguments across all three backends.
- Safe VM and BytecodeVM workers propagate trap text into the safe-thread error boundary instead of aborting the worker.
- `ConcurrentQueue`, `ConcurrentMap`, `Scheduler`, `Debouncer`, `Throttler`, and `ThreadPool` each carry a distinct runtime class ID and validate every public-API handle before downcasting.
- Retain-during-call discipline applied to every public threads entry — `rt_thread_*`, `rt_threadpool_*`, `rt_concmap_*`, `rt_concqueue_*`, `rt_debounce_*`, `rt_throttle_*`, `rt_scheduler_*`, `rt_cancellation_*`, `rt_future_*`, `rt_promise_*` — so a concurrent finalize on the host object can no longer race the deref. Safe-thread accessors copy the inner-thread pointer under retain via a dedicated `safe_thread_copy_inner_thread` helper.
- Monitor finalize-while-waiting safety: new `RT_MON_WAITER_CANCELLED` waiter state and `monitor_cancel_queue` wake every parked waiter (acquisition queue and wait queue) when `rt_monitor_forget` retires the entry, so blocked threads bail out with `Monitor.Enter: object finalized while waiting` instead of sleeping forever. All `Enter` / `TryEnter` / `TryEnterFor` paths now check the retired flag up front; recursion overflow at `SIZE_MAX` traps instead of wrapping.
- `ConcurrentMap.Keys()` / `Values()` snapshot under the map lock and retain copied values; `ConcurrentMap.Remove()` / `Clear()` / finalization and `ConcurrentQueue.Clear()` / finalization detach nodes before releasing retained values. `ConcurrentQueue` finalize broadcasts the condvar and marks the queue closed so any thread parked in `Dequeue` / `DequeueFor` wakes immediately.
- `Channel.TrySend()` on synchronous (capacity-0) channels publishes a retained handoff only when a receiver is already waiting, then returns without waiting for acknowledgement; `TryRecv()` remains strictly non-blocking. Channel deadline math falls back to `CLOCK_REALTIME` when `CLOCK_MONOTONIC` is unavailable instead of leaving the timespec uninitialised.
- Pool shutdown is idempotent — worker handles stolen under the pool monitor and joined outside the lock — so repeated `Shutdown` / `ShutdownNow` calls and concurrent finalization can no longer double-join or double-release. Pool task trap messages are sticky-then-cleared: the next `Wait` / `WaitFor` / `Shutdown` / `ShutdownNow` rethrows the captured worker error and drains it, later calls report the current pool state normally.
- `Parallel.*Pool` detects calls from a worker already running in the target pool and runs nested work inline to avoid self-deadlock; worker callback traps preserve the original trap text instead of replacing it with a generic `Parallel.*: task trapped` message. `_Static_assert` on the function-pointer-to-void* bridge confirms size equality.
- `Async.Run` / `Map` / `All` / `Any` and `Parallel.Map` switched from a transferred-result model to a retained-result model: every callback return is retained before publication, so borrowed inputs and shared runtime objects survive past the original owner's release. The `parallel_result_is_input` / `parallel_result_is_sequence_input` identity heuristics are gone; cleanup is uniform across success and trap paths. `Async.All` / `Any` state finalizers release every retained source slot.
- Future completion listeners and listener-cancel hooks isolate trapping callbacks via a new `future_invoke_listener` helper that runs the callback under `setjmp` recovery and, if it traps, runs the registered cancel hook under its own recovery — so one bad listener cannot rethrow through promise completion or cancel cleanup.
- Cancellation propagation simplified: children retain their parent and `IsCancelled` walks upward through the retained chain, so `cancellation_mark_cancelled` only flips the immediate-children flags under the lock instead of recursing through a snapshot. `IsCancelled` snapshots `parent` under retain so a finalize on `token` between the local-flag check and the parent walk can no longer free the data.
- VM payload OOM handling: every `new VmThreadStartPayload` / `BytecodeThreadPayload` / `VmAsyncRunPayload` is now `new (std::nothrow)` plus a null-check that traps with `<entry>: payload allocation failed`. The Async.Run path on null-payload resolves the promise with the failure message and returns the future to the caller so the Zia side observes a real Future error.
- `Promise` / `ConcurrentMap` / `ConcurrentQueue` / `Scheduler` `New()` paths trap with descriptive messages and free their partial allocation when `pthread_mutex_init` or `pthread_cond_init` fails, instead of leaving a half-initialised object in the GC graph.
- `Debouncer` / `Throttler` no longer use timestamp zero as an implicit state sentinel — explicit `has_signal` / `has_last_allowed` flags distinguish "never triggered" from a coincidental zero reading, and `elapsed_since_ms` clamps backwards-clock readings to zero so a system-time adjustment cannot make a previously-ready debouncer regress.
- Win32 Future timeouts compute one absolute monotonic deadline up front (`future_deadline_tick_from_now`) saturating at `ULLONG_MAX`, replacing the relative `DWORD`-clamped `future_deadline_ms_from_now`; `rt_future_get_for` / `wait_for` / `get_for_val` no longer round-trip through `MAXDWORD`.
- `SafeI64.Add` converts the post-add `uint64_t` back to `int64_t` via `memcpy` instead of a C-style cast, avoiding implementation-defined behaviour on signed overflow on both Win32 and POSIX.

### Audio

- `Viper.Audio.*` compatibility namespace rebuilt from `RT_ALIAS` forwarding entries into full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations — `RT_ALIAS` cannot carry typed object return values, so `obj<Viper.Audio.Sound>` was failing type resolution at every call site.
- All eight classes promoted: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, `MusicGen`.
- `rt_audio3d_register_voice` added to `RuntimeSurfacePolicy.inc`.

### 2D graphics

- New saturating int64 clip-rect arithmetic via `rtg_add_sat64` / `rtg_i64_fits_i32` helpers — extreme coordinates clip safely instead of overflowing.
- Polyline / polygon / path point arrays validated through `rt_heap_try_get_header`: NULL pointers, non-Integer arrays, and arrays shorter than `count * 2` elements are silently ignored rather than read past their bounds.
- `count` capped at `INT64_MAX / 2` so the `count * 2` multiply cannot overflow.
- Sprite tint normalization preserves the explicit-alpha tag from `Color.RGBA` — was masked to 24 bits, forcing every tinted sprite back to fully opaque.
- Tilemap I/O introduces `tilemap_io_release_ref` plus `map_set_owned` / `seq_push_owned` so reload cycles cannot double-free shared tile references.
- `Color.FromHex` rejects strings with embedded NUL bytes — was returning a partial color.
- Redundant per-subsystem class-ID constants in `rt_graphics2d.c` removed; canonical IDs come from the runtime registry.
- Class-ID validation extended to AutoTile2D, Path2D, ShapeRenderer2D, TextRenderer2D, TextLayout2D, and RenderPass2D — every public entry routes through `rt2d_has_class()` instead of a bare null check. TextRenderer2D rejects non-`BitmapFont` fonts; `RenderPass2D.New` drops a non-RenderTarget2D source/target to NULL rather than retaining a typed mismatch.
- New `bilerp_premul_rgba` replaces independent-channel `lerp_channel` in the linear-filter sampler — RGB is now interpolated in premultiplied-alpha space and divided back out, so transparent edge texels no longer darken partial-alpha results.
- New `RT_PIXELS_COLOR_EXPLICIT_ALPHA_FLAG` (bit 56) distinguishes tagged `Color.RGBA(...)` values from raw `0x00RRGGBB` legacy colors. `rt_color_get_a` returns 255 for legacy values and the actual alpha byte for tagged values; `draw_rgb` handles all three encodings (Canvas-style, raw `0xRRGGBBAA`, tagged) uniformly.
- `Particle.DrawToPixels` switched to alpha-preserving source-over via the new `particle_alpha_over_rgba` helper; legacy 0-alpha particles still draw as opaque while tagged `Color.RGBA(...,0)` stays fully transparent. A parallel `RT_PARTICLE_COLOR_EXPLICIT_ALPHA_FLAG` carries the same distinction through the particle pipeline.
- `Canvas.BlitAlpha` rewritten to preserve destination alpha — composites `out_a = sa + (da * (255-sa) + 127)/255` and divides the premultiplied RGB by `out_a`. Previously stamped `dst[3] = 255` regardless of source/destination alpha.
- `Renderer2D.BlitPixelsToCanvas` now properly clips negative destination coordinates and partial off-canvas blits, computing a `(sx, sy)` source offset for the trimmed prefix instead of reading past the source's right/bottom edge.
- New `Viper.Graphics.Palette2D.ApplyLegacy` exposes the pre-flag recolour behaviour for callers that want the older alpha-byte rules.
- `rt_canvas_set_title` strdups the new title before freeing the cached one — an OOM mid-update no longer leaves the canvas with `title=NULL`.
- `rt_pixels_load_png` releases the partially-allocated pixels object on every cleanup path so callers see a clean NULL on failure instead of a leaked half-initialised handle.
- `Lighting2D.AddTileLight` now stores screen-space lights in a separate per-frame pool that's drained after `Draw`, replacing the broken 1-frame-life dynamic-light approach that expired before the render under Update-then-Draw call orderings.
- `AnimatedSprite2D.Play()` restarts a finished non-looping clip from its first effective frame so one-shot clips can be replayed without reconstructing the clip object.

### Graphics3D

- `Canvas3D` gains `DrawMeshSkinned`, `DrawMeshMorphed`, and `DrawMeshBlended` matrix-keyed mesh draws so callers can render skeletal-animated, morph-target-driven, or combined-pose meshes through a single dispatch path. New `DrawVegetation`, `SetPostFX`, `DrawRect2D`, `DrawCrosshair`, and `DrawText2D` cover HUD-style overlay use cases.
- New `canvas3d_clamp01_to_u8`, `canvas3d_double_fits_float`, `canvas3d_mat4_d2f_checked`, `canvas3d_matrices_f32_are_finite`, and `canvas3d_mat4_checked` helpers centralise float-range and class-id validation across every matrix-keyed draw path. NaN, infinity, and out-of-FLT-range values surface as a no-op or trap instead of feeding garbage to the GPU backend; framebuffer clear and skybox CPU paths use the proper row stride and clamped u8 conversion.
- `Canvas3D` per-draw pending-splat state is now cleared on every early-return path (NULL mesh / matrix, frame inactive, malformed Mat4, mesh empty) so a failed splat-configured draw can no longer leak its splat-map and four layer pointers into the next successful draw.
- `Canvas3D.DrawMeshInstanced` validation reordered: the fallback `instance_count > CANVAS3D_MAX_FALLBACK_INSTANCES` check runs first, then matrix-finiteness validation, then tracked-object registration, then geometry snapshot. The previous order iterated every instance matrix and registered references on the canvas before trapping on the size limit.
- `Mesh3D.SetSkeleton` is no longer a stub — validates the skeleton handle, retains a slot on the mesh, releases the previous binding, and updates `bone_count` (clamped to `VGFX3D_MAX_BONES`). `SetBoneWeights` validates the mesh handle through `rt_g3d_checked_or_null`, preserves valid in-range bone *indices* even when their weight slot is zero or NaN (so glTF JOINTS attributes round-trip), and rejects only out-of-range indices. `bone_count` reflects every authored joint index, not just contributing ones.
- `Mesh3D.Clone` refuses to clone a build-failed source (traps with `Mesh3D.Clone: cannot clone a failed mesh build`), preserves `bone_count` only when the source actually has skinning data (bound skeleton or non-zero weights), retains the skeleton + morph-target references on the clone, and resets `build_failed` so retry paths work.
- `Mesh3D.FromObj` recomputes the full normal set when *any* face references vertex `0` for its normal index — partial-normal OBJ exports no longer leak zero normals into the runtime alongside their authored normals.
- `Skeleton3D` bind-pose copy validates each Mat4 entry through a finite-and-FLT-range check; malformed matrices fall back to identity instead of stamping NaN/inf into the bind pose. `rt_animation3d_new` falls back to a 1.0-second duration when given a non-finite or non-positive value (was 0.0, which made AnimPlayer3D treat the animation as instantly complete).
- `MorphTarget3D` matrix-keyed draws validate the Mat4 class id and finiteness up front so a corrupt blend-shape matrix surfaces as a no-op rather than writing through stale memory.
- `Physics3D.World3D` joint storage growth rewritten as `calloc` + `memcpy` + `free` of the old arrays; the previous double-realloc could leave the joint pointer array realloc'd live with a stale `joint_capacity` if the type-tag realloc failed. `AddJoint` deduplicates — registering the same joint twice is a no-op instead of a double-fired constraint.
- `Physics3D` raycast helpers (`raycast_sphere_raw`, `raycast_aabb_raw`, `ray_fill_hit`) give every result a coherent contact point, outward normal, fraction, and `started_penetrating` flag. The AABB path uses slab-test intersection with explicit handling of axis-parallel rays and inside-shape starts.
- `Scene3D` reparent is now atomic with respect to refcount: `AddChild` retains the child *before* detaching it from the previous parent so reparenting a node within the same scene never drops its refcount to zero. Self-reparent and re-add-to-same-parent are explicit no-ops; `count_subtree` is NULL-safe; `Scene3D.New` traps and frees on root-allocation failure.
- `Terrain3D.GeneratePerlin` validates `scale`, `octaves`, and `persistence` — NaN/inf inputs fall back to safe defaults, octaves clamp to `[1, 16]`, persistence clamps to `[0.0, 1.0]`. Per-cell heights are clamped to `[0, 1]` after the noise lookup so a malformed Perlin generator can't leak NaN values into the height map.
- `Water3D.IsPixelsHandle` drops a redundant `rt_heap_is_payload` precheck that was producing false negatives on Pixels handles whose heap-registry registration window had closed; the class-id check alone is sufficient.
- New `Viper.Graphics3D.GLTF` runtime class registered with `MeshCount`, `MaterialCount` properties and `GetMesh` / `GetMaterial` methods, mirroring the existing `FBX` import surface; `Scene3D.Load(path)` registered as a new public method.

### GUI runtime

- Widget handle validation centralised through `rt_gui_widget_handle_checked` and `_checked_type` — every runtime entry now rejects NULL, destroyed, and wrong-type handles before casting. Per-widget-type wrappers (tabbar, splitpane, dropdown, slider, progressbar, listbox, radiobutton, spinner, image, codeeditor) replace the previous unchecked casts that crashed on cross-type passes.
- Radiogroup handles carry a magic-tagged registry so dangling group pointers fail safely rather than miscasting; toolbar removal extracted into a shared `toolbar_remove_item_at` helper that consistently dismisses dropdown and overflow popups.
- Context menu submenu ownership is now bidirectional — `parent_item`/`parent_menu` are linked at attach time and cleared on either side's destruction, eliminating the stale-pointer class that produced UAF on dynamic submenu trees. Menu items gain a `checkable` flag.
- Code-editor Zia syntax highlighting derives block-comment nesting depth from a forward scan of preceding buffer lines instead of a render-order stateful counter; scrolling into the middle of a `/* … */` no longer mis-colors until the user scrolls back through the opening delimiter.
- Progressbar `set_style(circular)` rendering path; breadcrumb/file-dialog memory safety; slider step values re-snap on `set_value`; toast duration clamping; keyboard shortcuts route through the focus chain instead of bypassing it.
- File dialog (Cocoa) sheet bridge moved behind a typed header so retain/release pairs stay balanced through native callbacks.

### Network runtime

- **HTTP/2 HPACK encode/decode table separation.** A single dynamic table was previously being used for both inbound and outbound HPACK state, so `SETTINGS_HEADER_TABLE_SIZE` (a sender-tells-receiver setting) resized the wrong table and outbound request headers were encoded against the peer's decode state. After any dynamic-table churn the wire bytes the peer received could no longer be decoded. Split into independent `encode_table` and `decode_table` with RFC 7540 §6.5.2 semantics.
- **WebSocket non-minimal length frames rejected.** RFC 6455 §5.2 requires the shortest length encoding sufficient for the payload size. Both plaintext (`rt_ws_server.c`) and TLS (`rt_wss_server.c`) servers now reject frames that use the 2-byte extended length field for payloads < 126 or the 8-byte field for payloads < 65536, closing the connection with code 1002. Client close codes are validated against RFC 6455 §7.4 (1000–1014 minus the reserved 1004/1005/1006 triplet and 1015, plus the private 3000–4999 range) before being sent.
- **HTTP URL parser correctness.** IPv6 literal parsing rejects trailing junk between the closing bracket and the port colon, refuses unclosed brackets, and rejects empty `host:` forms. `set_Scheme("")` clears the field instead of allocating empty string; non-empty schemes are validated against the RFC 3986 grammar and trap on violation. `set_Port` traps on out-of-range values instead of silently clamping. Percent-decoding splits into path-mode and query-component-mode so `+` is treated as space only in query parameters.
- **WebSocket / SSE / HTTP transport URL validation.** New host validators reject control bytes, `0x7F`, `/`, `?`, and `#` in the host component; request-target validators require a leading `/` and reject control bytes and embedded `#`. SSE header values reject embedded CR/LF/`0x7F` so a malformed `Last-Event-Id` can no longer split the response. The WebSocket URL parser accepts `?` and `#` as path terminators, rejects zero-length hosts, and scans the port as a bounded unsigned with overflow guard.
- **HTTP header and scheme normalisation.** Scheme prefix matching is now case-insensitive (`http://` and `HTTP://` parse identically per RFC 3986 §3.1). Header field names are validated as full RFC 7230 token-character sequences instead of merely rejecting CRLF, closing a class of header-name smuggling.
- **HTTP server entry hardening.** Token-character classifier and request-target byte validator on the server side; malformed methods and paths return 400 before reaching application handlers.
- **Connection pool race fixes.** Tracked unhealthy connections now release the pool slot via `remove_entry_at` under the lock instead of closing the socket and leaving the slot occupied with a dead file descriptor that the next acquire would attempt to reuse. Untracked unhealthy connections release the lock before closing. The HTTP-client per-route pool similarly releases its slot before closing so a concurrent reuser cannot observe a half-released entry.
- **UDP truncation detection.** New `udp_recvfrom_checked` issues `recvmsg` on POSIX so `MSG_TRUNC` surfaces explicit truncation; on Windows `WSAEMSGSIZE` is mapped to the same flag. `udp_pending_datagram_size` queries `FIONREAD` so callers can size buffers to the next datagram instead of guessing. `setsockopt(IPV6_V6ONLY)` failure on dual-stack opens now closes the socket and returns `INVALID_SOCK` rather than silently inheriting the platform default. The `recvmsg` symbol joined `DynamicSymbolPolicy` so native-linked Linux binaries pass the allowlist.
- **DNS.** `rt_dns_local_host` zeroes the hostname buffer and pins a terminating NUL before `strlen` — Windows `gethostname` is not guaranteed to terminate when the system hostname matches the buffer length.
- **Retry policy.** `Retry.New` and `Retry.Exponential` validate `base_delay_ms` once and reuse the validated value to clamp `max_delay_ms`. Previously a negative `base_delay_ms` flowed into the cap comparison, producing an exponential backoff with a negative ceiling.
- **TLS verifier.** CN extraction split out into `tls_extract_cn_from_name` so SAN-fallback and direct subject-DER paths share one walker; constant-time helpers and per-namespace class IDs threaded through the handshake state.
- **Send/recv error semantics.** HTTP/2, HTTPS server, REST client, `network_udp`, and the `network_http` transport now treat `send()`/`recv()` returning `<= 0` as a hard error, not just the platform-specific `SOCK_ERROR` sentinel — Windows configurations that return 0 on connection drop no longer silently truncated payloads.
- **SSE parser.** Chunked-encoding parser distinguishes "no hex digit" from "malformed extension" so trailing semicolons + extensions parse per spec; `sse_transport_send_all` adopts the same `<= 0`-is-error convention; a dedicated `sse_set_recv_timeout` consolidates the recv-timeout pattern previously duplicated across three sites.
- **Timeout/length saturation helpers.** New `rt_net_timeout_ms_to_int` and `rt_net_i64_len_to_int` saturate `int64_t` arguments into the `int`-sized socket/poll APIs uniformly across every call site; negative timeouts in `set_socket_timeout` and `wait_socket` are clamped or rejected.

### ViperIDE

- The `zia` binary's CMake target now force-loads `fe_zia` (`LINKER:-force_load` on macOS, `--whole-archive` on Linux) — without this the static linker satisfied every `rt_zia_*` reference from the weak stubs in `viper_runtime/core/rt_zia_completion_stub.c`, so completion / hover / diagnostics / symbols silently returned `"Zia completion unavailable"`.
- New C-callable bridge `rt_zia_highlight.cpp` in `fe_zia` exposes `rt_zia_is_keyword(name, len)`.
- The GUI tokenizer in `rt_gui_codeeditor.c` now consults `Lexer::lookupKeyword` (52 keywords) instead of the local 28-entry table that had been silently drifting.
- Inline `/* ... */` block-comment tracking via a per-editor `zia_block_comment_depth` field; uppercase-first heuristic colors `Foo` / `MyClass` / `Math` as types; identifiers immediately followed by `(` colored as function calls (`#DCDCAA`).
- `codeeditor_max_scroll_y` uses half-viewport scrollBeyondLastLine padding (matches VS Code / Sublime) — last line floats to mid-viewport rather than being pinned to the bottom edge.
- Mouse-wheel scroll dropped from 1.0 to 0.3 lines per delta — more comfortable on macOS trackpads.
- Toolbar uses BMP-only Geometric Shapes / Arrows glyphs as button labels — the previous emoji attempt fell back to the missing-glyph "hamburger" on UI fonts that lack color emoji.
- New `tbBuild` / `tbRun` / `tbFind` items wired into the command dispatch.
- Untitled (Ctrl+N) buffers and the welcome buffer begin with `module UntitledN;` / `module Welcome;` so the completion engine can parse them on the first keystroke; without a module declaration `CompleteForFile` was returning an empty string and the popup appeared blank.

### Compiler, IL & codegen

- AArch64 protection set in `Allocator.cpp` now covers both *use* and *def* operands — prevents eviction of live def vregs mid-instruction; fields renamed `protectedUse{GPR,FPR}_` → `protectedOperand{GPR,FPR}_` to reflect the broader scope. Fixes a class of register-reuse clobber bugs.
- x86-64 lowerer pre-registers block parameters and instruction results before lowering textually out-of-order blocks, so optimized IL can reference dominated values without tripping unknown-SSA diagnostics.
- Legalize creates typed virtual registers for edge-copy arguments whose defining block has not been lowered yet — fixes out-of-order block-parameter copies in optimized BASIC demos.
- Post-RA scheduler treats physical RSP / RBP definitions as scheduling boundaries, preserving Win64 frame setup and unwind-sensitive prologue ordering before callee-save spills.
- Branch layout no longer follows a lone JCC as a trace edge after fallthrough-jump removal — a JCC-only terminator still has an implicit physical fallthrough block, and moving its taken target into that slot was changing optimized native control flow and causing immediate startup traps.
- Register coalescer lowers spilled parallel-copy sources from memory one at a time instead of demanding one scratch register per source — eliminates scratch exhaustion in 3dbowling and xenoscape.
- `DynamicSymbolPolicy` extended with the syscalls introduced by IO hardening: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`, `fdopendir`, `dirfd`, `chmod`, `fchmod`, `mkdtemp`. Native-linked Linux binaries previously failed dynamic-symbol validation without these entries.
- `RuntimeSurfacePolicy.inc` registers the new `rt_threadpool_current_worker_pool` accessor used by the nested-parallel guard.
- x86-64 compare/branch fold safety: compare/setcc/test/jcc folding now refuses to delete a materialised boolean that is still live in a successor block via branch-argument edge copies. Fold-safety checks route through shared operand-role metadata so register uses inside both explicit and memory operands are considered.
- IMUL → LEA strength reduction is rejected when flags are read before a later clobber, preserving condition-code consumers that depend on the multiply.
- Block-DCE now preserves physical registers at exits for blocks that may transfer control, so fallthrough-carried values aren't deleted as dead locals. JCC-only fallthrough pairs stay fixed during cold-block movement so conditional branches keep reaching their intended hot successor.
- x86-64 frame lowering now treats callee-saved registers embedded in memory-address base/index operands as real uses, and MIR-generated local labels are function-stemmed so multi-function assembly cannot duplicate select/split labels.
- x86-64 ISel now materializes large ALU/CMP immediates before binary emission, branch-chain cleanup retargets non-final `JCC` instructions, cold-block movement preserves implicit fallthrough, and frame-memory peepholes treat GPR/XMM accesses to the same frame displacement as aliases.
- x86-64 switch lowering now rewrites block-argument edge helpers with IL's default-first successor order instead of the emitted case-first operand order, so class-ID dispatch and other switch users route each case to the right target.
- Known-constant tracking now invalidates physical defs from `POP`, read-modify-write forms, `CQO`, `DIV`/`IDIV`, and CALL caller-saved clobbers, while recognising `XOR reg,reg` as a known zero.
- Register allocator collects explicit and implicit physical clobbers through operand roles (including `RAX`/`RDX` division setup and `CQO`/`DIV`/`IDIV` results) and spills active live values before instructions that overwrite fixed physical registers — fixes a class of Windows-only corruption in demos that kept values live across those clobbers.
- Empty native returns now emit a zero integer return value before `RET` so void main-style native executables don't leak stale register contents as their process exit status; regression coverage added for the void-return exit-code path.
- Optimizer ownership-effect model gains a new `ownedOutArgMask` bitmask describing pointer arguments that receive an owned reference (e.g. `Box.TryToStr`); plumbed through `RuntimeOwnership.hpp`, `CallEffects.hpp`, signature plumbing, and the runtime metadata for `Memory.Retain` / `Release` / `RetainStr` / `ReleaseStr`, the Box family, `Object.ToString` / `TypeName`, `Parse.DoubleOption` / `Int64Option`, `Convert.ToString_*`, and the MessageBus surface.
- Binary encoders and ELF/Mach-O/COFF object-file writers now bounds-check every emitted instruction and relocation. Malformed input surfaces as an error instead of UB.
- Object-file readers switched from `reinterpret_cast` on possibly-misaligned bytes to `std::optional<T>` + `memcpy`. Fixes strict-alignment UB on Mach-O load commands and ELF section headers.
- Per-file caps (`kMaxObjSectionBytes` = 2 GiB, `kMaxObjMaterializedBytes` = 4 GiB) reject crafted inputs that would otherwise exhaust host memory.
- `ArchiveReader::parseSize` rejects empty fields, digits-after-padding, and trailing garbage. `AlignUtil::alignUp` throws on bad alignment instead of relying on a release-disabled `assert`.
- Archive extraction now treats corrupt or empty indexed members as hard errors, preserves duplicate candidates across retries, and canonicalizes GNU long-name entries by stripping their `/\n` terminators.
- COFF writer patches relocation addends directly into instruction bytes per kind, with explicit alignment and range checks for x86-64 rel32, AArch64 branch26/branch19, ADRP page21, page-offset, and Abs64.
- COFF relocation-overflow records now store the total relocation-table entry count including the overflow marker, and the reader validates malformed overflow counts before iterating.
- Three new relocation kinds: `A64LdSt32Off12`, `A64LdSt128Off12`, and the corrected `IMAGE_REL_ARM64_BRANCH19` constant (was 8, should be 15).
- New `InputSectionKey{objIndex, secIndex}` replaces ad-hoc `(obj<<32)|sec` packing across six passes. The old form silently truncated above 2³²; the new key is `size_t` end-to-end.
- New `defaultImageBaseForPlatform` constexpr replaces hard-coded `0x140000000` / `0x100000000` / `0x400000` literals scattered across five files.
- `CodeSection` carries a monotonic identity; ELF/COFF writers prefer identity-based section-offset relocations and reject ambiguous offset-only references. ELF writer now preserves `Symbol::size` (was always 0).
- `SymbolResolver` tracks per-name candidate lists so duplicate archive definitions resolve in archive order; the Windows CRT-shim override exception is narrowed to Viper's own runtime archives.
- AArch64 `BranchTrampoline` address arithmetic is overflow-checked end-to-end and trampoline target addresses are recomputed after island insertion shifts later chunks. Far-conditional encoding fixed. Mach-O `X86_64_RELOC_SIGNED_4` trailing-byte bias now applied.
- MOVZX byte-source emits a REX prefix when the source is SPL/BPL/SIL/DIL — without REX those encodings name AH/CH/DH/BH and silently moved the wrong byte. AArch64 `SmulhRRR` activated for `IMulOvf` overflow checks.
- COMMON symbols enter the global table at read time with size and alignment preserved. A post-resolution pass materialises them as zero-filled storage, picking the largest size and strictest alignment across all referencing objects.
- Mach-O underscore alias lookup is now gated on platform. ELF and COFF treat leading underscores as literal name bytes, so the previous unconditional alias could pull the wrong definition on Linux and Windows.
- `RelocApplier` and `DeadStripPass` prefer global resolutions over same-object local definitions when the reference is a Global or Weak binding. A strong def in another object now wins over a local def in the referencing object.
- ICF redirects folded-section symbols to Undefined so subsequent relocs route through the canonical copy via global lookup instead of pointing into stripped bytes.
- Dead-strip preserves EH/unwind roots and preserves non-alloc debug sections (`.debug*`, `__DWARF`) only for debug-section-preserving links; ICF now tracks address-taken local/section-symbol references by object/section/offset.
- `preserveDebugSections` plumbed end-to-end through `CodegenPipeline::link` → `linkToExe` → linker options, so `.debug_line` data survives the link step when `emit_debug_lines` is set.
- Branch trampolines now key dedup on `symName + addend` and resolve target addresses after all trampolines are placed, so the address shifts that insertion induces no longer desync the dedup map.
- ELF executables emit a proper `PT_TLS` program header when the layout contains TLS sections, with monotonic ordering and overflow-checked span/memSize math. Dynamic metadata, startup-stub placement, section names, and section-header offsets now reject overflow before narrowing or allocation.
- Mach-O `MH_SUBSECTIONS_VIA_SYMBOLS` inputs split `__TEXT,__text` per global/weak atom on read, so dead-strip and ICF operate at function granularity instead of treating `__text` as one chunk.
- PE writer narrows 64-bit virtual addresses, sizes, and offsets to 32-bit fields through a uniform set of overflow-checked helpers (`checkedU32` / `checkedSizeU32` / `checkedAddU32` / `checkedRva` / `checkedAlignUpU32`) instead of silent truncation; reused external IAT slots must map to writable output sections before being seeded.
- Mach-O executable output now checks load-command and link-edit field narrowing and rejects overlapping section file offsets instead of appending bytes at the wrong position.
- `RelocApplier` validates AArch64 instruction class: Branch26 relocs must land on B/BL, ADRP-page21 on ADRP, page-offset relocs on ADD/SUB-immediate or load/store, and conditional 19-bit on B.cond/CBZ/CBNZ. Wrong-class targets surface as a named error instead of garbage patching.
- Mach-O writer rejects section and segment names longer than the 16-byte field instead of silently truncating; AArch64 addend pairs remain adjacent after sorting, large section-offset relocations use synthetic anchors, and `MH_SUBSECTIONS_VIA_SYMBOLS` moved to a shared header.
- ELF and Mach-O readers reject invalid section/symbol string-table offsets and undersized Mach-O command payloads instead of returning truncated or empty names.
- x86-64 lowerer validates malformed inputs (empty blocks, dangling branches, mismatched edge-copy arity, unresolved phi predecessors) and surfaces them through the diagnostic engine instead of asserting or producing broken MIR.
- Multiple rounds of x86-64 and AArch64 backend regressions repaired: switch edge-copy ordering, R10/R11 scratch reservation, Win64 shadow space, i1 immediate canonicalization, caller-saved live-out across calls, AArch64 branch-patch math, integer cast width selection, label-operand validation, frame-slot aliasing, overflow-pseudo lowering, switch operand normalisation, and malformed-IL diagnostics at the lowerer boundary.
- Doc-comment audit across the codegen tree: assembler and linker subtree (~75 files) plus the full x86-64 backend (~50 files in `src/codegen/x86_64/` covering ISel, lowering rules, peephole sub-passes, register allocator, scheduler, binary encoder, asm emitter, and pass-manager wrappers). Canonical Viper file headers, per-helper doxygen, refreshed `docs/codegen/native-assembler.md` and `native-linker.md`.

### Windows / MSVC toolchain

- Top-level CMake opts into CMP0141 (when available) and requests embedded MSVC debug information for `Debug` / `RelWithDebInfo` builds — keeps generated projects aligned with modern CMake/MSVC behavior and removes PDB-discovery friction for local Windows debug builds.
- New `size_t` compare-exchange wrapper in the MSVC atomic compatibility layer wires through `rt_atomic_compare_exchange`; size_t-refcount code now shares the same portable atomic API as the `int` and pointer paths on Windows.
- Windows import policy expanded for newly observed UCRT and Win32 symbols: `GetFullPathNameA`, `DragQueryFileW`, `CoTaskMemFree`, `__CxxFrameHandler4`, `_open_osfhandle`, `rand_s`, `fmaxl` / `fminl`, `_wcsnicmp`, `_wchmod`. Routed to the correct DLL buckets so dynamic-link audits no longer reject valid Windows surface area.
- `audit_runtime_surface.sh` and `run_cross_platform_smoke.sh` are WSL-aware: honor `VIPER_BUILD_TYPE`, pass `--config` for multi-config generators, use `cmake.exe` / `ctest.exe` plus `wslpath` when run from WSL against a Windows build tree; capability-macro reads strip CRLF; `rtgen` lookup also handles `build/src/<Config>/rtgen.exe` from Visual Studio generators.
- `rt_crypto` clamps BCrypt random-fill chunks with `UINT32_MAX` instead of the unavailable `DWORD_MAX` macro — MSVC builds the network runtime without ad-hoc workarounds.
- D3D11 backend derives the bone-palette buffer allocation from the shared 256-bone palette constants used by the shader and upload-packing path. The old 128-bone hard-coded cbuffer let the first 16 KB palette upload overrun an 8 KB mapped buffer, crashing 3dbowling under the Windows debug UCRT.
- Windows full-build TLS and timeout test stabilization — flaky network-runtime tests reworked so the MSVC full build runs the network suite cleanly without timing-sensitive false failures.

### Tests

- Memory / GC: traversal callbacks, promoted roots, weak-ref resurrection, finalizer trap recovery.
- MessageBus: NUL-embedded topics, callback class-ID validation, raw-callback rejection, owning topic snapshots, publish trap cleanup, subscription-ID overflow, payload retention through dispatch.
- Object: `rt_obj_equals` / `GetHashCode` Box dispatch, `±0.0` hash equality, type identity, `RefEquals`, `Object.TypeName` for built-in MessageBus / Box / ValueType / Option / Callback class IDs.
- New `RTMemorySurfaceTests`: invalid-handle traps, resurrected release counts, array-element release, `RetainStr` / `ReleaseStr` round-trip, immortal-release trap, `RT_ELEM_OBJ` array regression.
- New `RTParseTests`: `DoubleOption` / `Int64Option` success and `None`, class IDs, typed return metadata, leading-`+` sign, typed-string vs C-string ABI parity.
- New `RTTrapContractTests`: NUL-embedded diagnostic message escaping; `Diagnostics.Trap` routes through `rt_trap_string`.
- New `RTSeqBoxTests::call_value_type_misaligned_field`: pointer-aligned offset validation for `Box.ValueType.AddField`.
- New `RTCoreOwnershipTests::test_runtime_metadata_matches_core_contracts`: end-to-end ownership classifications for `Memory.*`, Box family, `Object.ToString` / `TypeName`, `Parse.*Option`, `Convert.ToString_*`, and MessageBus.
- IO: ZIP64 / extra-field rejection, asset temp-dir isolation, savedata absolute paths, VPA large-file seek, watcher overflow, inflate consumed-bytes, unsigned `BinaryBuffer`, OGG CRC.
- Threads: repeatable joins, future listener trap isolation, sticky-then-cleared pool errors through Wait / Shutdown / ShutdownNow, idempotent pool shutdown, nested-parallel same-pool execution, retained borrowed-callback results across `Async.Run` / `Async.Map` / `Parallel.Map`, synchronous-channel `TrySend`, ConcurrentMap remove/clear release-after-unlock, VM/BytecodeVM owned thread and async dispatch.
- Crypto: scrypt round-trips, AEAD encrypt / decrypt with AAD, `ConstantTimeEquals`, TLS extension parsers.
- 2D graphics: new `RTBitmapFontContractTests`; expanded `RTCanvasContractTests`; `RTColorUtilsTests` for alpha-tag preservation through `Brighten` / `Darken` / `Invert` / `Grayscale` / `Saturate` / `Desaturate` / `Complement` / `Lerp`; `RTSpriteContractTests` for tint-alpha survival; `RTGraphics2DTests`, `RTPixelsTests`, `RTCameraTests`, `TestTilemapAnim`. Round-2 additions cover `rt_color_get_a` legacy-vs-tagged behaviour, premultiplied-alpha bilerp on a transparent-edge texture, hex-vs-`Color.RGBA` stroke parity, TextRenderer2D rejecting non-BitmapFont fonts, `AnimatedSprite2D.Play()` replay after one-shot completion, and SpriteRenderer2D material/tint state isolation against subsequent direct `Renderer2D` draws.
- Graphics3D: `RTGraphics3DRobustnessTests` extended with introspection structs for the Water3D and Vegetation3D internal layouts and regression coverage for the new Canvas3D draw entries, terrain Perlin clamping, mesh skeleton binding, morph Mat4 validation, scene reparent atomicity, physics joint deduplication, glTF JOINTS preservation in `SetBoneWeights`, and the bone-count derivation rule on `Mesh3D.Clone`.
- x86-64 codegen: out-of-order block-param lowering, instruction-result pre-registration, scheduler prologue-boundary preservation, large-spill `PX_COPY` regression, lone-JCC fallthrough peephole. Backend-liveness round adds compare/branch fold-safety across edge copies, IMUL→LEA refusal under live flags, block-DCE physical-register preservation at exits, JCC-only fallthrough preservation under cold-block movement, fixed-physical-register spill-before-clobber for `RAX`/`RDX`/`CQO`/`DIV`/`IDIV`, and a void-return zero-exit-code regression.
- x86-64 regression additions also cover switch edge-copy ordering, internal-label/control-transfer splitting, `R10`/`R11` scratch reservation, Win64 runtime-call shadow-space detection, I1 immediate canonicalization, and caller-saved live-out preservation across calls.
- D3D11: shared-backend coverage tying the bone-palette byte count to the 256 supported shader entries.
- Native assembler & linker: new tests for `parseSize` rejection, archive symbol-candidate ordering and GNU long names, `CodeSection` identity, ELF symbol-size preservation, COFF ambiguous-target rejection and relocation-overflow records, AArch64 addend range/alignment/instruction-class validation, the new LDR/STR scaled-offset reloc kinds, Mach-O `SIGNED_4` bias and large section-offset anchors, `InputSectionKey` lookups, branch-trampoline overflow/retargeting guards, executable-writer overflow/overlap checks, and MOVZX REX emission.
- Linker correctness round: COMMON coalescing across mixed alignments, per-platform underscore fallback (Mach-O accepts, ELF/COFF reject), strong-global precedence over same-object local definitions, ICF folded-symbol redirect, ELF `PT_TLS` emission, Mach-O 16-byte name validation, AArch64 reloc instruction-class validators, PE 32-bit field-overflow guards, and Mach-O subsections-via-symbols splitting.
- The seven `Viper.GUI` widget tests in `src/lib/gui/tests/` were relabeled `tui` → `gui` (a new dedicated ctest label); the wheel-scroll regression contract was rewritten with floating-point tolerance to match the new mouse-wheel constant.

Demos and docs were updated to track the runtime work above; the stale Windows debug/O0 pins for chess, xenoscape, and Baseball were removed after optimized x86-64 builds and smoke probes were restored.

---

### Commits

See `git log v0.2.5-dev..HEAD -- .` for the full 54-commit history since v0.2.5.

<!-- END DRAFT -->
