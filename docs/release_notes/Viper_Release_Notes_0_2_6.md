# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

An alpha-quality hardening cycle, not a feature release. The Zia frontend reached alpha quality, raw pointers were removed from both source languages, the native linker became real enough to consume optimized C++ object input and ship ViperIDE's IntelliSense end-to-end, and a broad runtime correctness/ownership pass landed across memory, threads, crypto, IO, graphics, the bytecode VM, packaging, and toolchain installers. The one additive surface is a targeted set of game-engine helpers (plan 24).

- **Zia frontend → alpha quality.** `defer`; structured `try`/`catch`/`finally`, multi-catch, bare rethrow; `Result[T]` with `?` propagation; weak fields, function references, constrained generics, default interface methods; declaration-order independence; module-scoped name-collision disambiguation.
- **Pointer-safety gate (the biggest user-visible change).** Zia and BASIC reject raw `Ptr` types and pointer-signature runtime APIs; the `--unsafe-pointers` escape hatch was added then removed — the typed surface is now the only surface.
- **Memory, GC & threads ownership.** Validated `Retain`/`Release` wrappers; weak-ref CAS retain inside the GC lock; trap-safe finalizers; class-ID validation on every public threads / MessageBus entry; saturated wait deadlines on Win32 and POSIX.
- **Crypto, TLS & IO security.** `Viper.Crypto.*` canonicalized (scrypt-SHA256, AES-GCM+AAD, approved-mode module, fixed-schedule ECDSA P-256); TLS enforces Key Usage / Basic Constraints / EKU and DNS-name limits; hardened temp-file, archive, and ZIP64 paths.
- **Network protocol correctness.** Independent HPACK encode/decode tables, strict RFC 7230 `Transfer-Encoding` parsing (closes a request-smuggling avenue), WebSocket frame and close-code validation.
- **Native toolchain becomes real.** All four object-file readers and three writers received multiple bounds-checking, alignment-UB, and reloc-correctness rounds, capped by a shared-helper consolidation that re-homes `readLE*`/`writeLE*`/`putLE*`/checked-arithmetic helpers and `physicalSymbolValue` into a single `ObjFileWriterUtil` header so future hardening rounds stay in sync across formats; COFF, ELF, and Mach-O agree on the addend convention end-to-end; the `fe_zia` frontend is now native-linked into `zia`, with a broadened `rt_zia_*` completion bridge and keyboard/text-input plumbing carrying ViperIDE's IntelliSense / hover / diagnostics / symbols against the real semantic engine.
- **Toolchain installer completion.** Native-emitted Windows `.msi`/`.exe`, macOS `.pkg`, and Linux `.deb`/`.rpm`/tarball toolchain packages reach feature parity: PE32+ payload validation, ad-hoc-by-default macOS signing with optional Developer ID + notarization + stapling, Linux runtime/developer dependency advertisement, file-association registration on all three platforms, and deep post-build verification of every staged path.
- **Standard-library namespace de-clutter (breaking).** Seven root modules re-home under their documented taxonomy: `Lazy`/`LazySeq` → `Viper.Functional`, `Machine`/`Environment`/`Exec` → `Viper.System`, `Log` → `Viper.Diagnostics`, `Fmt` → `Viper.Text`. No back-compat aliases; `Math`, `String`, `Terminal`, and the intrinsic `Option`/`Result`/`Error` stay at root.
- **Backends, bytecode VM & Windows HiDPI.** x86-64 cross-block fold liveness + AT&T operand-class validation; AArch64 sub-word transfers, terminator/CFG, def-operand fixes; bytecode-VM two's-complement wrapping arithmetic and checked float→int traps; Windows physical-pixel sizing via `AdjustWindowRectExForDpi` and waitable-timer frame pacing.
- **GUI correctness audit.** Multi-round audit closing handle-validation, dialog-lifetime, focus-routing, and menubar/context-menu/toolbar/statusbar gaps; every public `Viper.GUI.*` entry routes through `rt_gui_widget_handle_checked`.
- **Game-engine surface (plan 24, the one additive piece).** New `Viper.Game.UI` widgets, `AnimTimeline` + multi-event `AnimStateMachine`, `Projectile2D`, rotated-texture `Renderer2D` draws, named audio mixer groups, and a `Viper.System.Clipboard` text surface.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 148 | +148 |
| Source files | 2,996 | 3,034 | +38 |
| Production SLOC | 552K | 599K | +47K |
| Test SLOC | 228K | 255K | +27K |
| Demo SLOC | 188K | 189K | +1K |

Counts via `scripts/count_sloc.sh` (production 599,454 / test 255,235 / demo 189,273 / source files 3,034).

---

### Memory, GC, and object identity

- Public `Memory.*` routes through validated wrappers that authenticate live runtime handles; array-element releases cover `RT_ELEM_STR` / `RT_ELEM_OBJ` / `RT_ELEM_BOX` slots; `rt_obj_new_i64` traps on `SIZE_MAX`-overflowing sizes and `rt_obj_free` rejects non-zero refcounts.
- GC phases 2/3 drop the lock during traversal; phase 3 restore is iterative worklist BFS; phase 4 wraps finalizers in `setjmp` recovery; `rt_gc_run_all_finalizers` snapshots `{obj, retained}` so cleanup only releases what it actually retained. `TotalCollected` saturates.
- `rt_weakref_get` retains its target through a CAS loop inside the GC lock (closes a TOCTOU window); `WeakRef.Get`/`Alive`/`Reset` reject invalid or freed non-null handles. `RT_HEAP_IMMORTAL_REFCNT` (`SIZE_MAX-1`) is a sticky immortal value distinct from the `SIZE_MAX` corruption marker.
- `rt_obj_get_hash_code` mixes the pointer through a splitmix64 finalizer so Map/Set bucket masks no longer cluster on adjacent allocations. `Box` rejects unboxes whose tag isn't `I64`/`F64`/`I1`/`STR`; `Box.ValueType.AddField` rolls back cleanly under `setjmp` if `rt_gc_track` traps.

### Runtime objects and MessageBus

- Boxed values carry `RT_BOX_CLASS_ID`; `Object.Equals`/`GetHashCode` dispatch correctly through `Set[Box]` and `Map[Box, ...]`. `Box.TryToI64/TryToF64/TryToI1/TryToStr` are option-style accessors; `Box.ToI1` ABI is `i1(obj)` end-to-end.
- MessageBus validates `MSGBUS` / `MSGBUS_CALLBACK` class IDs on every public entry, hashes topics by full byte length (preserving embedded NULs), retains the bus through every call, and orders unsubscribe so unref can't re-enter through a half-freed node. Per-callback retains use lock-held atomic CAS with overflow rollback.
- `Diagnostics.Trap` routes through a validating `rt_trap_string` that always escapes control bytes, quotes, backslashes, and NULs.
- `Viper.Core.Parse.Double`/`Int64` ABI is `i32(str,ptr)`; `INPUT#` keeps the raw-pointer ABI through internal aliases; `Parse.DoubleOption`/`Int64Option` plus the legacy aliases give graceful failure.

### Crypto, TLS, and IO security

- Password / KeyDerive / Aes / Cipher / Hash / Rand are canonical members of `Viper.Crypto.*`. New: `KeyDerive.ScryptSHA256` (RFC 7914, bounded memory cap), `Password.Hash` default `SCRYPT$…` with PBKDF2 legacy-verify, `Cipher.EncryptAAD`/`DecryptAAD` for AES-128/256-GCM, a 16-byte `VAK1` AEAD magic, `Viper.Crypto.Module` approved-mode + self-tests + HMAC-DRBG, and `Hash.ConstantTimeEquals`.
- RSA modulus floor raised to 1024 bits with parity/size validation and secure-zero key buffers; ECDSA P-256 signing uses a fixed-schedule scalar multiply and validates every public point at ingress.
- TLS chain validation enforces Key Usage, Basic Constraints (cA), and EKU; intermediates without cA and leaves without Server-Auth EKU are rejected; SAN hostname checks scan every DNS name instead of the extraction cap; chains with malformed tails or more than 16 intermediates fail closed; non-minimal DER long-form lengths are rejected.
- Temp-file creation uses a 64-bit `/dev/urandom`/`rand_s`+`QueryPerformanceCounter` nonce; assets decode into a private `mkdtemp` directory; `asset_name_is_safe()` rejects absolute paths, drive letters, dots, and embedded NULs; recursive directory removal uses `openat`/`fstatat`/`unlinkat` with `AT_SYMLINK_NOFOLLOW`; ZIP64 sentinels, encryption flags, and `version_needed >= 45` are rejected at the central directory.

### Threads

- Joins are repeatable: first successful `Join`/`TryJoin`/`JoinFor` reclaims the OS handle; later calls return success. Every `Viper.Threads` built-in carries a distinct runtime class ID that surfaces through `Object.TypeName`/`Object.ToString`. Retain-during-call discipline applied to every public entry.
- Monitor finalize-while-waiting wakes parked waiters with `Monitor.Enter: object finalized while waiting` instead of sleeping forever. `Channel.TrySend()` on capacity-0 channels publishes a retained handoff only when a receiver is waiting. `Parallel.*Pool` detects calls from a worker inside the target pool and runs nested work inline.
- `Async.Run`/`Map`/`All`/`Any` and `Parallel.Map` switched from transferred-result to retained-result. Future listeners run under `setjmp` with a per-listener cancel hook so a trapping listener can't rethrow through completion. Win32 remaining-time math and POSIX absolute-deadline construction saturate, closing a class of long-wait overflows including the ~49-day Win32 hang.

### Core runtime and numeric round-trip

- `rt_format_f64_roundtrip` writes the shortest `%.*g` (precision 1–17) whose `strtod` under the C numeric locale recovers the original IEEE-754 bits, with a fixed-vs-exponent tie-break. `Convert.ToString_Double` and friends route through it; the 15-digit BASIC `PRINT`/`WRITE#` display form is preserved as a separate entry point so all goldens are unchanged.
- Time, text, threads, and `SafeI64.Add` adopt overflow-checked / `memcpy`-based arithmetic so signed overflow never relies on implementation-defined behaviour. Runtime Perlin 2D/3D guards inputs against NaN / infinity / out-of-range integer casts and clamps octave counts to 1–16.

### Collections runtime

- All 26 collection types carry stable class IDs, register typed GC traversal so cycles through containers are reachable, and follow a uniform retain-on-return contract; `RuntimeOwnership.hpp` declares `returnsOwned` so the optimizer stops emitting defensive retains.
- `Queue` and `Stack` gain `Seq`'s opt-in `owns_elements`: borrowed by default, retain-on-push / release-on-pop / clone-propagated when enabled. `Map.Values`/`IntMap.Values`/`FrozenMap.Values`/`MultiMap.Get` return owning `Seq`s.
- `Bytes.ReadI16/I32` LE/BE sign-extend correctly (were zero-extending); new `BinaryBuffer` `WriteU16`/`U32` + `ReadU16`/`U32` with range validation; `IntMap`/`DefaultMap`/`Bag`/`BiMap`/`BitSet`/`Ring` constructors trap on size overflow instead of returning partial objects.

### Audio, 2D graphics, Graphics3D

- `Viper.Audio.*` rebuilt from `RT_ALIAS` forwarders into full typed `RT_CLASS_BEGIN`/`RT_FUNC`/`RT_METHOD`/`RT_PROP` registrations across Audio/Sound/Voice/Music/Playlist/SoundBank/Synth/MusicGen — `RT_ALIAS` couldn't carry typed-object returns.
- 2D graphics correctness: saturating int64 clip math; class-ID validation on AutoTile2D / Path2D / ShapeRenderer2D / TextRenderer2D / RenderPass2D; premultiplied-alpha bilerp on transparent edges; alpha-preserving `Canvas.BlitAlpha` and particle source-over; Lighting2D tile lights in a per-frame pool.
- Pixels raw-vs-Color boundary: `Pixels.Get`/`Set`/`Fill` keep their raw `0xRRGGBBAA` contract but now route through `rt_pixels_rgba_or_tagged_color_to_rgba` so a tagged `Color.RGBA(...)` argument is unpacked instead of bit-reinterpreted (fixes the cyan-bevel artifact in Xenoscape's tile overlay); new `GetColor`/`SetColor`/`FillColor` are canonical accessors. `RT_PIXELS_COLOR_EXPLICIT_ALPHA_FLAG` (bit 56) distinguishes tagged `Color.RGBA(...,0)` from legacy `0x00RRGGBB`.
- Image IO hardening: PNG chunk validation; BMP pixel-offset/size checks (no partial save on failure); JPEG table/orientation handling; GIF normalized per-frame delays; Canvas titles round-trip embedded NULs via independent byte length.
- Graphics3D: `Canvas3D` gains `DrawMeshSkinned`/`DrawMeshMorphed`/`DrawMeshBlended` plus HUD/overlay draws; `Mesh3D.SetSkeleton` retain/release integration; `Physics3D` deduplicates joints and surfaces `started_penetrating`; `Scene3D` reparent is atomic; `Terrain3D.GeneratePerlin` clamps NaN/inf inputs; new `Viper.Graphics3D.GLTF` runtime class plus `Scene3D.Load`.

### Game runtime

- **Plan-24 additions (the one additive surface).** New `Viper.Game.UI` widgets — TextInput, Table, Modal, Slider, Dropdown, Tooltip — with GC-managed handles; `AnimTimeline` plus multi-event `AnimStateMachine`; `Projectile2D` ballistic helpers; `Renderer2D.DrawTextureRotated`/`DrawTextureRotatedAt`; string-keyed named audio mixer groups (legacy Music/SFX IDs preserved); a `Viper.System.Clipboard` UTF-8 text surface.
- Behavior gravity/patrol/chase/animation, Entity gravity and collision sweeps, Lighting2D glow/fade, Game-UI hit-testing, and ScreenFX shake all saturate at int64 limits instead of wrapping; tilemap raycast DDA checks both side-touched tiles so corner-crossing rays cannot skip solids.
- `Config`/`LevelData` release input text and parsed JSON roots across every success/failure path; Quadtree, Pathfinder, ButtonGroup, AchievementTracker, and SpriteAnimation receive dedicated class IDs and destroy-time release. Timer one-shot expiry uses an explicit latch; Typewriter reveals by UTF-8 codepoint and completes immediately on empty/NULL text.

### Network and GUI

- HTTP/2: independent encode/decode HPACK tables, `pthread_once`-guarded Huffman init, length-aware decode preserves embedded NULs. HTTP/1.1: strict RFC 7230 §3.3.1 `Transfer-Encoding` token-list parsing closes the smuggling avenue; the URL parser validates IPv6 brackets, empty hosts, and out-of-range ports.
- WebSocket servers reject non-minimal-length frames per §5.2; clients validate close codes per §7.4. SSE host/target validators reject control bytes and non-leading-`/` targets; UDP detects truncation via `MSG_TRUNC`/`WSAEMSGSIZE`; TLS `ServerHello` `key_share` exact-length validated.
- GUI handles route through `rt_gui_widget_handle_checked` so every public entry rejects NULL / destroyed / wrong-type before casting. Across five audit rounds, menubar, context-menu, statusbar, toolbar, FindBar, CommandPalette, Breadcrumb, Minimap, Toast, VideoWidget, ProgressBar, ColorPicker, FileDialog, and the dialog/modal stack all participate; context-menu `Clear()` retires stale handles after dismissing active submenus.
- GUI event/layout correctness: a non-shifting `VG_EVENT_NONE` sentinel filters unknown platform events; focus and modifier state survive the VGFX→GUI translation on every backend; a handled mouse-up suppresses synthetic click/double-click; non-wrapped flex-grow sizes from each child's measured basis plus margins before distributing the remainder; shortcut routing now flows through focus.

### Compiler, IL, codegen

- AArch64 protection set in `Allocator.cpp` covers both *use* and *def* operands (fixing a class of register-reuse clobbers); `i1`/`i16`/`i32` loads/stores use byte/halfword/word transfer opcodes in both assembly and native-object emission; `cbz`/`cbnz` are treated as real terminators after their register operand is allocated; X29 is no longer allocator-managed callee-saved state.
- x86-64: cross-block fold-liveness guards on SIB and IMUL→LEA prevent address-strength reductions from erasing virtual registers still consumed in another block; IMUL→LEA refusal under live flags; block-DCE preserves physical registers at exits; AT&T `AsmEmitter` rejects invalid `CALL`/`JMP`/`JCC`/`LEA`/`SETcc`/`MOVZX` operand classes and non-`RCX` shift counts before printing.
- Bytecode VM: arithmetic uses explicit two's-complement wrapping helpers (add/sub/mul, shifts, local inc/dec) instead of host signed-overflow behaviour; checked float→int conversions raise consistent `InvalidCast`/`Overflow` traps; local indexes, memory pointers, and alloca sizes are validated before host state is touched.
- Optimizer ownership-effect model gains `ownedOutArgMask` for pointer args that receive owned references, plumbed through `RuntimeOwnership.hpp`; the retain-on-return contract is captured via `returnsOwned` on every collection accessor so optimizer-side defensive retains are eliminated.

### Native toolchain (linker, readers, writers)

- All four object-file readers (ELF / COFF / Mach-O / Archive) and all three writers received bounds-checking and alignment-UB fixes; per-file caps reject crafted inputs; ELF `.rel` implicit addends, symbol section offsets, and zero-fill logical sizes are validated; new typed `InputSectionKey{objIndex, secIndex}` replaces ad-hoc bit-packing.
- COFF reloc addend convention now agrees across reader, applier, and writer: AMD64 `REL32[_n]` keeps the raw 32-bit displacement field as the internal addend and `RelocApplier` owns the COFF end-of-field bias when patching, mirroring the ELF and Mach-O paths. ARM64 ADRP/page-offset/branch decoders are unchanged.
- COFF weak externals carry the `Characteristics` field through the reader; `SymbolResolver` honors `IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY` (kept as weak alias without forcing archive extraction) while library-search fallbacks still drive the iterative resolver. Identity hashing (hashU64/hashString/hashRelocTarget) keeps duplicate-symbol and reloc-target detection deterministic across re-runs.
- Three new reloc kinds (`A64LdSt32Off12`, `A64LdSt128Off12`, corrected `IMAGE_REL_ARM64_BRANCH19`); `RelocApplier` validates AArch64 instruction class per reloc kind and forbids duplicate input-section chunks; branch trampolines key dedup on `symName + addend` and resolve targets after placement.
- Section identity is preserved across reader-to-writer copies via shared `CodeSection` aliases so multi-section COFF and ELF writers resolve section-offset relocations even when sections are passed by value; AArch64 reloc decoding is shared between the Mach-O reader and the binary encoder so load/store page-offset relocations agree across paths.
- P1 hardening: Mach-O TLV descriptor sections carry an explicit `OutputSection::tlvDescriptors` flag (the `_tlv_bootstrap` bind loop no longer disambiguates by section name), a non-24-byte-multiple TLV descriptor section is a hard link error instead of silently dropping the trailing record, ADDR32NB RVAs come from `LinkLayout::imageBase` so applier and writer share one source of truth, Mach-O `n_sect` past the parsed section table now errors out instead of resolving the symbol as undefined, `BranchTrampoline` errors on an out-of-bounds patch site rather than emitting an island and leaving the original branch unredirected, `DynStubGen` GOT slots live in `.got.viper_stubs` instead of colliding with user `.data` by name, and `ICF::archForObject` returns `optional<LinkArch>` so unknown machine codes are skipped rather than misclassified as x86_64.
- ELF emits `PT_TLS` for TLS segments; TLS and `.bss`/`.tbss` use logical memory sizes without serializing zero-fill bytes. Mach-O `MH_SUBSECTIONS_VIA_SYMBOLS` splits `__TEXT,__text` per atom and merges same-named ObjC metadata from reader-created and linker-synthetic inputs. PE writer narrows through `checkedU32`/`checkedRva` overflow-checked helpers; dead-strip preserves EH / unwind and `.debug*` roots.

### Standard-library namespace de-clutter

- Seven root modules move under their documented `docs/viperlib/` taxonomy: `Lazy`/`LazySeq` → `Viper.Functional`, `Machine`/`Environment`/`Exec` → `Viper.System`, `Log` → `Viper.Diagnostics`, `Fmt` → `Viper.Text`. Hard rename with no compatibility aliases; `Math`, `String`, `Terminal`, and the compiler-intrinsic `Option`/`Result`/`Error` remain at root.
- `runtime.def` canonical paths, `obj<…>` type tokens for `Lazy`/`LazySeq`, twelve relocated `Environment`/`Machine` `RT_ALIAS` names, the four codegen native-vs-runtime classification predicates, and the rtgen-derived identifier `kFmtBool` → `kTextFmtBool` all moved in lockstep. Trap messages, REPL default binds, the audit policy fixture, and ~410 consumer files across `src/`, `tests/`, `examples/`, `docs/viperlib/`, and `misc/` were rewritten atomically.

### Zia language stability

- **Declaration-order independence.** Top-level / namespace types, type-alias placeholders, nominal inheritance, and interface-implementation relationships are pre-registered before body analysis, so signatures, globals, aliases, and class/interface relationships can refer to declarations that appear later. Base / interface layouts register before derived layouts and lowering entry points.
- **Type checks and casts.** `is`/`as` targets are resolved and validated — optional unwrapping, class/interface runtime checks via `rt_cast_as_iface`/`rt_typeid_of`/`rt_type_implements`, primitive exact checks. Match arms agree on a result type instead of falling back to `Unknown`; statement-bearing block expressions are parsed for lambda, match, and expression contexts while bare single-element braces stay set literals.
- **Interfaces, generics, `Result[T]`, `Unit`.** Interface method bodies act as default implementations that lower to concrete vtable slots. Generic constraints are carried through structs, classes, interfaces, and methods and validated at instantiation; explicit `recv.method[T](args)` lowers with concrete return typing. Postfix `?` propagates `Err` from `Result`-returning functions; `Unit` lowers as a real pointer-sized singleton; `Void`/`Never`/`Module` are rejected in value positions before lowering.
- **Names and visibility.** Qualified names are accepted in generic constraints, `extends`/`implements`, and struct-literal type positions. Module-scoped collisions get module-qualified semantic names and export through their bind module or alias (`A.WishDup` / `Beta.WishDup` instead of `V-ZIA-DUPLICATE`), with short-name compatibility preserved for unique imports; external construction and struct literals cannot bind private fields.

### ViperIDE and IntelliSense

- The `zia` binary force-loads `fe_zia` (`-force_load` on macOS, `--whole-archive` on Linux) so IntelliSense / hover / diagnostics / symbols are no longer satisfied by the weak stubs in `rt_zia_completion_stub.c`. A new `rt_zia_highlight.cpp` bridge exposes `rt_zia_is_keyword`; the GUI tokenizer reads the live 52-keyword set from `Lexer::lookupKeyword`.
- The `rt_zia_*` completion bridge and its `ZiaCompletion.cpp` public query surface were broadened so the editor can ask the live semantic engine for symbol lookups, kind/signature metadata, and dotted-path completions. Matching weak-stub parity and `RuntimeSurfacePolicy.inc` registration ensure the frontend resolves the surface the IDE calls.
- A text-input enable path and broadened keyboard handling landed in the input runtime (`rt_input.c`, `rt_gui_app.c`, `rt_gui.h`) so the editor receives text-entry events distinct from raw key events; the editor wires text input on setup and clamps diagnostic highlight columns to a valid 1-based span.
- Editor surface: `codeeditor_max_scroll_y` uses half-viewport `scrollBeyondLastLine` padding; mouse-wheel drops to 0.3 lines per delta on macOS trackpads; toolbar uses BMP-only Geometric Shapes / Arrows glyphs. Untitled and welcome buffers begin with `module UntitledN;` / `module Welcome;` so the completion engine parses them on the first keystroke.

### Windows, MSVC, and HiDPI

- Top-level CMake opts into CMP0141 and requests embedded MSVC debug info; expanded Windows import policy for `GetFullPathNameA`, `DragQueryFileW`, `CoTaskMemFree`, `__CxxFrameHandler4`, `_open_osfhandle`, `rand_s`, `_wcsnicmp`, `_wchmod`, and `CreateWaitableTimerExW`/`SetWaitableTimer`. The Windows CRT import flavour is threaded through both backends; hidden `viper run`/`package` plumbing forces release-runtime CRT imports for packaged payloads even when invoked from a Debug tool build.
- Win32 HiDPI: client, resize, mouse, and presentation dimensions are physical pixels; native window client area is sized from the already-scaled framebuffer via `AdjustWindowRectExForDpi`; public `Canvas` sizing stays behind `coord_scale` so Windows matches macOS Retina semantics. Frame limiting moved from `Sleep`-based to a thread-local high-resolution waitable timer.
- D3D11 bone-palette cbuffer sized from the shared 256-bone constant (the old 128-bone hard-coded buffer let a 16 KiB upload overrun an 8 KiB mapping under the Windows debug UCRT).

### Packaging

- Windows VAPS installer: PE32+ payload validation against the x64/arm64 target, recursive adjacent-DLL discovery with redistributable classification, proper Add/Remove metadata, `windows-install-scope machine|user`, `windows-sign-thumbprint` parity across `package`/`install-package`, and a `meta/manifest.sha256` integrity check with duplicate/uncovered-entry rejection. A non-elevated user-scope installer smoke and a headless XenoScape package-smoke ctest gate it.
- Windows toolchain installers default to per-user `LocalAppData` / HKCU / `asInvoker` and reject MSVC Debug-CRT payloads unless `--allow-debug-toolchain` is given. New policy switches cover install scope, PATH mutation, opt-in source/IL file associations, and Start-Menu shortcuts; PATH update removes stale owned entries before appending; registry string types are validated and force-terminated. A post-build hardening pass validates the emitted `.msi`/`.exe` against the staged manifest before the run is reported as successful.
- macOS toolchain generation no longer shells out to `pkgbuild`/`productbuild`: Viper writes product/component XAR archives, gzip-compressed portable ASCII CPIO `Payload`/`Scripts`, SHA-1/zlib metadata, CMake discovery wrappers under `/usr/local/lib/cmake/Viper`, command/manpage symlinks, and upgrade cleanup scripts. `install-package` gained native `.pkg` verification plus Developer ID Installer signing, notarization, and stapling; a follow-on round closed gaps in the staged-bundle metadata, payload-script line endings, and post-build XAR TOC verification.
- Linux toolchain packages advertise runtime/developer dependencies; tarballs carry `install.sh`/`uninstall.sh` and a file manifest for prefix/DESTDIR installs. A follow-on round validated `.deb` control-field encoding, `.rpm` header sanity, and shared-MIME registration for `.zia`/`.il`. VAPS asset packaging reads through a resolved safe-directory iterator validated against the project root (not a mutable symlink path) and fails on escaping symlinks; archive writers harden zero-byte `ar` members, USTAR long directory-path splitting, normalized ZIP/TAR symlink targets, and `SOURCE_DATE_EPOCH` thread-safety.

### Tests

~26K new test SLOC.

- **Memory / GC / MessageBus / Box / Parse** — contract suites for the validated `Memory.*` surface, weak-ref CAS retain and resurrected-cycle finalizers, `Box.ValueType` alignment + tag validation, `rt_trap_string` control-byte escaping, MessageBus payload retain through publish and lock-held CAS traversal, and `Parse.*Option` typed-string ABI with NaN/Inf round-trip.
- **Collections** — `RTCollectionsCorrectnessTests` for class-ID distinctness, retain-on-return, and owning value snapshots; extended Bytes (negative signed reads), Queue/Stack (`owns_elements` + finalize-while-owning), Seq (slice/keep/reject/apply ownership), and Convert.* suites.
- **Codegen / bytecode VM** — cross-block SIB and IMUL→LEA fold-liveness MIR cases, `AsmEmitter` operand-class diagnostics for the rejected `CALL`/`JMP`/`JCC`/`LEA`/`SETcc`/`MOVZX` and non-`RCX` shift-count forms, plus direct-bytecode regressions covering wrapping arithmetic, conversion traps, negative alloca, invalid locals, and null memory loads.
- **Native linker** — coverage for `parseSize`, archive symbol-candidate ordering, `CodeSection` identity, ELF symbol-size preservation, COFF reloc-overflow records and the new addend convention, AArch64 reloc instruction-class validators, COFF weak-external `SEARCH_NOLIBRARY` paths, Mach-O `SIGNED_4` bias, PE 32-bit overflow guards, and branch-trampoline overflow.
- **Zia alpha hardening** — `42_try_catch_promises`, `43_alpha_hardening`, and `44_language_promises` cover interpreted, optimized `viper run`, and native paths for structured catch bindings, multi-typed catches, bare rethrow, branchy catch/finally, namespace globals, struct interface dispatch, constrained generics, tuple destructuring, `Result[T]`, weak fields, and function references.
- **Graphics, GUI, crypto, packaging** — alias type identity, texture-region sampler isolation, raw-vs-Color pixel APIs, animation replay/stop; stale tab/menu/context/toolbar/statusbar handles, file-dialog modal-stack removal, app-font inheritance; null-vs-empty string handling, AES-CBC empty plaintext, SAN matching beyond the public extraction cap; PE VERSIONINFO, Windows thumbprint normalisation, ZIP manifest duplicate / uncovered-entry, and the non-elevated `windows_installer_user_smoke` end-to-end.

---

Demos and docs tracked the runtime work above; stale Windows debug/O0 pins for Chess, XENOSCAPE, and Baseball were removed after optimized x86-64 builds were restored, the XENOSCAPE Windows installer reached parity with the macOS path, and `docs/viperlib/` plus the native-linker / native-assembler design docs were refreshed alongside.

### Commits

See `git log 929a6d787..HEAD -- .` for the full 148-commit history since v0.2.5.

<!-- END DRAFT -->
