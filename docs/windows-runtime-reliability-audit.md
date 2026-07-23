---
status: active
audience: developers
last-verified: 2026-07-22
---

# Windows Runtime Reliability Audit

This audit covers the Direct3D 11 backend and the Windows-specific runtime adapters for sockets,
entropy, TLS verification, process/ConPTY launch, paths and assets, locale detection, file watching,
large-file I/O, environment access, concurrency, stack safety, graphics, audio, native dialogs,
native builds, UI Automation, installer lifecycle, signing, and demo automation. It is a robustness
pass only: no IL opcode, grammar, verifier rule, runtime C ABI, or cross-layer dependency changed.

## Repaired findings

| ID | Area | Finding and repair |
|----|------|--------------------|
| WR-01 | WinSock startup | A failed `WSAStartup` attempt left concurrent callers spinning on the in-progress state forever. Failed owners now reset the state so waiters retry. |
| WR-02 | WinSock startup | Plain reads of the volatile startup state did not provide the atomic visibility used by its writes. All state observations now use interlocked operations. |
| WR-03 | WinSock startup | A successful call did not verify that WinSock 2.2 was actually negotiated. The adapter now validates `wVersion` and cleans up a mismatched startup. |
| WR-04 | WinSock startup | Failure to register `WSACleanup` with `atexit` was ignored. Registration failure now cleans up, resets the state, and traps. |
| WR-05 | WinSock connect | Non-blocking connect classification recognized only `WSAEWOULDBLOCK`. It now also recognizes `WSAEINPROGRESS` and `WSAEALREADY`. |
| WR-06 | WinSock accept | Listener shutdown could surface `WSAESHUTDOWN` as an unexpected accept failure. It is now classified as the normal close race. |
| WR-07 | WinSock readiness | The Windows `select` call derived `nfds` from the pointer-sized socket handle and narrowed it to `int`, although WinSock ignores that argument. It now passes zero. |
| WR-08 | WinSock readiness | Invalid readiness arguments returned `-1` without a deterministic last-error value. They now set `WSAEINVAL` or `WSAENOTSOCK`. |
| WR-09 | OS entropy | A chunked `BCryptGenRandom` failure could leave a fresh prefix followed by stale caller bytes. The complete destination is now securely erased on failure. |
| WR-10 | OS entropy | The 64-bit entropy helper could preserve an old scalar after failure. It now clears the output before requesting entropy. |
| WR-11 | `Zanna.Text.Rand` | Its independent BCrypt path had the same partial-output exposure. It now securely erases the entire destination on failure. |
| WR-12 | hash seeding | The BCrypt byte count narrowed `size_t` to `ULONG` in one call. Requests are now chunked to the native 32-bit limit. |
| WR-13 | hash seeding | A later hash-seed entropy chunk could fail after earlier chunks had populated the buffer. Failure now securely clears the whole buffer. |
| WR-14 | locale detection | POSIX-style fallback precedence was `LC_ALL`, `LANG`, `LC_MESSAGES`; category-specific `LC_MESSAGES` must precede `LANG`. All adapters now share the corrected order. |
| WR-15 | locale detection | `C.UTF-8`, `C@...`, and suffixed `POSIX` values were treated as real language tags. Sentinel recognition now occurs case-insensitively before encoding/modifier suffixes. |
| WR-16 | locale detection | Malformed, non-ASCII, empty, or overlong subtags passed through to later parsers. A shared helper now validates a strict near-BCP-47 ASCII shape. |
| WR-17 | locale detection | Detection failure could leave stale bytes in the caller's buffer. Every adapter and shared normalizer now clears writable output on failure. |
| WR-18 | TLS CertificateVerify | Null session/data arguments could be dereferenced by the platform verifier. Both Windows and portable implementations now reject them before parsing. |
| WR-19 | TLS CertificateVerify | A declared signature shorter than the remaining handshake payload silently accepted trailing bytes. The framing check now requires an exact length. |
| WR-20 | TLS CertificateVerify | An obsolete CryptoAPI fallback contained a fixed 4096-byte signature buffer and could verify using uninitialized/out-of-bounds data for a larger declared signature. Supported schemes already use CNG, so the dead fallback was removed. |
| WR-21 | process launch | `StartWithEnv` passed a UTF-16 block without `CREATE_UNICODE_ENVIRONMENT`, allowing ANSI reinterpretation. The flag is now always present for the Windows launch path. |
| WR-22 | process launch | Explicit Windows environment entries were emitted in caller order, contrary to the sorted-block contract. Entries are now sorted case-insensitively with deterministic tie-breaking. |
| WR-23 | process launch | Case-insensitive duplicate variable names produced an ambiguous block. The builder now rejects duplicates after sorting. |
| WR-24 | finite waits | Three Future timed operations could map a long finite duration to Win32's `INFINITE` sentinel. They now use a shared saturating absolute deadline and finite wait slices. |
| WR-25 | finite waits | ConcurrentQueue timeout could become indefinite at the sentinel and treated an intermediate maximum-size slice as final timeout. It now recomputes against the absolute deadline and traps on non-timeout wait failures. |
| WR-26 | finite waits | Monitor `TryEnterFor` and `WaitFor` clamped large finite durations to `INFINITE`. Both now retain the full absolute deadline across finite slices. |
| WR-27 | monitor errors | Monitor condition-variable failures were ignored in wait/reacquire loops, risking a spin or corrupt queued stack waiter. Failure paths now remove/requeue fairly, clean up, and trap. |
| WR-28 | thread joins | Timed joins narrowed long durations and could become indefinite; join condition failures were also ignored. Joins now use absolute finite slices and report native wait failure. |
| WR-29 | thread yield | `SwitchToThread` can report that no processor yielded. The adapter now falls back to `Sleep(0)` in that case. |
| WR-30 | file watcher | Packed `FILE_NOTIFY_INFORMATION` offsets and filename extents were trusted. The decoder now validates every record, stride, alignment, and in-buffer next-record target. |
| WR-31 | file watcher | Invalid UTF-16 names and conversion/allocation failures silently dropped events. Strict conversion now emits an overflow/rescan marker, whose dropped count is at least one. |
| WR-32 | file watcher | Wait, overlapped-result, reset, and rearm failures could leave a watcher apparently active with no pending read. Failures now emit overflow and retire invalid handles. |
| WR-33 | file watcher | `CancelIo` only cancels operations issued by the calling thread, and the event/OVERLAPPED storage could be closed before cancellation completed. Teardown now uses `CancelIoEx`, waits for completion, then closes the directory and event handles. |
| WR-34 | D3D11 shaders | Compiler diagnostics were formatted as an unbounded NUL-terminated string even though an `ID3DBlob` is a byte buffer. Logging now honors the blob size and caps diagnostic output. |
| WR-35 | D3D11 shaders | Invalid/empty compile arguments could leave a stale output blob; successful warning blobs were discarded, and success did not require bytecode. The helper now clears output first, validates non-empty inputs, logs warnings, and requires a blob. |
| WR-36 | D3D11 resources | Device creation, compact input layouts, timing-query recreation, and lazy material samplers had partial-COM-output leak or stale-cache paths. They now use centralized/defensive release, reset timing state, and publish sampler cache entries only after complete success with HRESULT diagnostics. |
| WR-37 | Windows native link | TOML float formatting pulled in `strpbrk`, which has no DLL mapping in the fixed native-runtime import surface. A small in-tree marker scan now preserves the zero-dependency native link. |
| WR-38 | D3D11 swapchain | Back-buffer RTV and depth resources were published piecemeal, so a late allocation failure left a partially valid context. Creation is now transactional and publishes only a complete target set. |
| WR-39 | D3D11 swapchain | A successful `GetBuffer`/view call was assumed to return a non-null resource, and the back-buffer descriptor was not checked against the backend contract. Null outputs and incompatible dimensions, format, subresources, or sampling are now rejected with complete cleanup. |
| WR-40 | D3D11 resize | A same-size resize returned immediately even when a previous failure had removed one of the main targets. It now detects and reconstructs incomplete target sets. |
| WR-41 | D3D11 depth probes | A perpetually busy non-blocking staging map kept one probe batch pending forever and starved every later request. Polling is now bounded and stale batches are discarded. |
| WR-42 | D3D11 depth probes | Failed maps and malformed mapped payloads cleared counts but retained the poisoned staging texture. Failure now unmaps when needed, evicts the staging resource, and resets all probe state. |
| WR-43 | D3D11 telemetry | Abandoned or failed timestamp reads cleared flags and immediately reused the same potentially active query objects. The complete query set is now recreated before telemetry resumes. |
| WR-44 | D3D11 readback | Several early failures preserved old caller pixels, while an invalid cached staging descriptor remained cached for every later call. Valid destinations are cleared up front and invalid/failed staging resources are evicted. |
| WR-45 | D3D11 buffers | Dynamic-buffer and staging-texture creation trusted success with a null output and did not release a partial output on failure. Both helpers now require a resource, release partial COM outputs, and preserve the old dynamic buffer until replacement succeeds. |
| WR-46 | D3D11 device loss | Query, buffer, staging, RTV, depth, and map failures often logged only the local HRESULT. These paths now also report the device-removed reason when applicable. |
| WR-47 | Windows path input | Directory conversion retried malformed UTF-8 without `MB_ERR_INVALID_CHARS`, silently redirecting operations to a replacement-character path. All filesystem boundary conversions now reject malformed input. |
| WR-48 | Windows path output | Directory, file, asset-decoder, and executable-path adapters accepted unpaired UTF-16 surrogates and produced lossy UTF-8. They now use `WC_ERR_INVALID_CHARS` and fail deterministically. |
| WR-49 | asset canonicalization | Mounted pack paths used ANSI `GetFullPathNameA`, so names outside the process code page did not round-trip. Canonicalization now uses a growing UTF-16 `GetFullPathNameW` buffer. |
| WR-50 | asset identity | Mounted pack equality used `_stricmp` on UTF-8 bytes, which is neither Unicode-aware nor Windows ordinal comparison. Paths are now converted strictly and compared with `CompareStringOrdinal` semantics. |
| WR-51 | executable path | The second UTF-16-to-UTF-8 conversion result was ignored and the source contract still described the retired ANSI/fixed-buffer implementation. The result is now checked and the documented contract matches the growing Unicode path. |
| WR-52 | ConPTY discovery | Concurrent first use raced on a plain loaded flag and partially initialized function pointers. Optional exports are now resolved through `InitOnceExecuteOnce`. |
| WR-53 | ConPTY text | Malformed UTF-8 in a command, argument, working directory, or environment entry could be replaced or truncated at the Win32 boundary. Launch now fails transactionally on any strict conversion failure. |
| WR-54 | ConPTY environment | Per-entry allocation/conversion failures silently omitted variables, and block allocation failure silently inherited the parent environment. Explicit environments are now all-or-nothing. |
| WR-55 | ConPTY environment | Explicit blocks were emitted in caller order, allowed case-insensitive duplicate names, and had unchecked aggregate-size arithmetic. Entries are now ordinal-case sorted, duplicate-rejected, and overflow-checked. |
| WR-56 | ConPTY startup | Failure after an attribute list was initialized freed its storage without `DeleteProcThreadAttributeList`. Initialization/update are now separate lifecycle stages with matching cleanup. |
| WR-57 | ConPTY handles | HRESULT-returning APIs reported unrelated `GetLastError` state, and a partial pseudo-console output on failed creation was not closed. Diagnostics now preserve HRESULTs and every partial handle is retired. |
| WR-58 | ConPTY lifecycle | Process waits, exit-code reads, and resize failures were incompletely checked; child-allocation failure also closed a terminated process without waiting. The adapter now publishes deterministic failure state, reports resize HRESULTs, and waits for successful termination. |
| WR-59 | captured execution | Failure to remove inheritance from the capture pipe's read end was ignored, allowing a child to keep the reader alive and prevent EOF. Launch now aborts and closes both ends when `SetHandleInformation` fails. |
| WR-60 | process execution | `WaitForSingleObject` and `GetExitCodeProcess` failures could return an uninitialized code or leave an asynchronous process permanently marked running. Wait/exit results are checked and failures become deterministic errors. |
| WR-61 | process environment | Environment pointer/block sizing could overflow, and locale-sensitive `_wcsnicmp` did not match Windows ordinal name semantics. Sizing is checked and ordering/duplicate detection use dynamically resolved ordinal comparison. |
| WR-62 | parallel workers | Failed completion-event signals or waits were ignored, after which caller-owned task arrays and locks could be freed while workers still borrowed them. Such unrecoverable synchronization failures now abort before cleanup. |
| WR-63 | scheduler clocks | Scheduler/debouncer code ignored QPF/QPC failure and could use an uninitialized counter. Results are checked and the Windows path falls back to monotonic `GetTickCount64`. |
| WR-64 | stack safety | `SetThreadStackGuarantee` is per-thread, but only the initializing thread received a reserve. Every thread created through the Windows Threads adapter now establishes its own emergency stack reserve. |
| WR-65 | stack diagnostics | The low-stack exception path dereferenced exception pointers and wrote to stderr without validation, and called `strlen` while stack was exhausted. It now validates inputs/handles and writes a compile-time-sized static message. |
| WR-66 | host integration | Stack-safety initialization replaced the process error mode, clobbering flags chosen by an embedding host. It now preserves existing flags and only adds the two required suppression modes. |
| WR-67 | UI Automation startup | Lazy `uiautomationcore.dll` initialization used an unsynchronized attempted flag and exposed partially filled function pointers. The API table is now initialized once atomically. |
| WR-68 | UI Automation lifetime | Providers borrowed a reusable fixed bridge slot, so a provider retained across detach could resolve against a different window/root attached in that slot. Per-slot generations now invalidate every detached provider. |
| WR-69 | UI Automation tree | Provider construction did not prove that a widget belonged to the bridge root, and later reparenting could leave it representing another tree. Construction and every resolution now validate ancestry and immutable IDs. |
| WR-70 | UI Automation IDs | Runtime IDs retained only 31 bits of a 64-bit widget ID, allowing collisions. The SafeArray now carries the complete low/high 32-bit identity after `UiaAppendRuntimeId`. |
| WR-71 | UI Automation arrays | `SafeArrayPutElement` failures were ignored and a partially populated array could be returned. Every write is checked and failed arrays are destroyed through the dynamically resolved OleAut export. |
| WR-72 | UI Automation text | Accessible UTF-8 and `IValueProvider::SetValue` UTF-16 were converted leniently, and the second conversion result was unchecked. Both directions are strict; malformed provider input returns `E_INVALIDARG`. |
| WR-73 | UI Automation allocation | BSTR or child-provider allocation failure was frequently reported as `S_OK` with a null value. Property, navigation, hit-test, focus, selection-container, and Value paths now return `E_OUTOFMEMORY`. |
| WR-74 | UI Automation geometry | Non-finite/out-of-range hit-test coordinates were narrowed to `LONG`, invalid bounds entered comparisons, and failed client/screen transforms were ignored. Inputs, bounds, and Win32 transforms are now validated. |
| WR-75 | UI Automation ranges | Corrupt non-finite, inverted, or out-of-range slider/progress/spinner state leaked through RangeValue. Published ranges are now finite, ordered, and clamped. |
| WR-76 | UI Automation outputs | Toggle and RangeValue getters could leave caller output unchanged on stale-provider failure. Outputs are initialized to deterministic safe values before resolution. |
| WR-77 | UI Automation events | Live-region announcement allocation failure could pass null BSTRs into UIA. Both strings must now exist before an event is raised. |
| WR-78 | Windows compile | FBX camera-channel locals named `NEAR` and `FAR` collided with Win32 header macros and stopped the MSVC build. Backend-private prefixed names now avoid the global macro namespace. |
| WR-79 | Windows native link | The software wireframe rasterizer called CRT `llabs`, which is absent from the fixed CRT-less native import surface. Its already-widened 32-bit coordinate deltas now use in-tree signed magnitude arithmetic. |
| WR-80 | MSVC portability | Tiled artwork bounds and software texture indexes relied on implicit narrowing/conversion that produced C4244 diagnostics under the Windows warning policy. Range checks now precede explicit conversions at the exact assignment boundaries. |
| WR-81 | D3D11 readback | Resizing the reusable readback staging texture released the last usable surface before allocation. A replacement now stages locally and is published only after a non-null successful creation. |
| WR-82 | D3D11 snapshots | Presented-backbuffer snapshot replacement also destroyed the cached texture before `CreateTexture2D` succeeded. Creation now preserves the old snapshot resource and metadata on failure. |
| WR-83 | D3D11 scene targets | Scene color, motion, and depth attachments were rebuilt directly in the live context; a late depth/view failure discarded the previous complete scene. All nine COM resources now stage and publish as one transaction. |
| WR-84 | D3D11 overlay | Overlay resize evicted its prior texture/RTV/SRV before replacement allocation. The complete replacement is now created before bound state is retired and the old set released. |
| WR-85 | D3D11 post-FX | Primary and scratch post-processing targets had the same release-before-create failure mode. Each target set now stages independently and keeps the last complete resource authoritative on allocation failure. |
| WR-86 | D3D11 bloom | Bloom resize published mip resources one at a time and destroyed both the old chain and a partial new chain after a later allocation failure. The complete mip chain and dimensions now stage in local arrays before one commit. |
| WR-87 | D3D11 TAA | TAA history resize could leave no usable history pair after the second target failed. Both history textures/RTVs/SRVs now stage before the previous pair is replaced. |
| WR-88 | D3D11 SSR | SSR resize released its cached target before replacement creation. Texture, RTV, and SRV now stage as a complete set before publication. |
| WR-89 | D3D11 RGBA cache | Starting a changed RGBA texture upload evicted a known-good cache entry before texture/SRV allocation. Allocation now stages locally, so allocation failure preserves the resident generation. |
| WR-90 | D3D11 native texture cache | Compressed native-texture replacement had the same early eviction window. The replacement texture/SRV must now both exist before cache metadata and ownership change. |
| WR-91 | D3D11 cubemap cache | Cubemap replacement likewise discarded a usable cube before allocation. Cube texture/SRV creation is now staged before entry release. |
| WR-92 | Windows file metadata | High-level file APIs used `_stat64i32`, whose name means 64-bit time but only a 32-bit file size. They now use `_stat64`/`_fstat64`/`_wstat64`, preserving sizes and metadata beyond 2 GiB. |
| WR-93 | Windows file seek | Low-level seek called `_lseeki64` but rejected offsets using Windows' unrelated 32-bit `off_t` range first. Windows now accepts the full `int64_t` offset contract of the actual adapter. |
| WR-94 | Windows IDE files | Workspace modification-time lookup used `_wstat64i32`, which can fail solely because an otherwise valid file exceeds its 32-bit size field. It now uses `_wstat64`. |
| WR-95 | Windows environment | A variable that grew between `GetEnvironmentVariableW`'s size probe and read returned a new required capacity that was misused as the undersized buffer's string length, enabling an out-of-bounds read. The adapter now retries boundedly until one snapshot fits. |
| WR-96 | Windows environment | UTF-8/UTF-16 conversion allocation and output-size arithmetic were unchecked. Both conversion directions now reject `size_t` overflow before allocating or accumulating encoded bytes. |
| WR-97 | Windows environment | `HasVariable` treated every query error except `ERROR_ENVVAR_NOT_FOUND` as proof that the variable existed. Unexpected Win32 errors now trap and return a deterministic false fallback. |
| WR-98 | argument initialization | The Windows legacy-argument once wait ignored a failed `SwitchToThread`, unlike the Threads adapter. It now falls back to `Sleep(0)` so a contended initializer yields deterministically. |
| WR-99 | recursive directory removal | A malformed UTF-16 child name or allocation failure became the canonical empty runtime string; recursion could then resolve `""` as the process cwd. Recursive deletion now requires an explicit successful conversion and rejects empty child paths. |
| WR-100 | directory enumeration | Windows list/files/dirs/entries silently inserted empty names after UTF-16 conversion failure. Non-trapping enumerators now clear the partial result; the trapping `Entries` API reports the read failure. |
| WR-101 | current directory | `Dir.Current` had a size/read race when another thread changed cwd and silently returned empty after UTF-16 conversion failure. A growing snapshot helper retries the Win32 call and conversion failure now traps. |
| WR-102 | deletion protection | Full-path/cwd sizing races could produce an unchecked buffer or make the recursive-delete guard fail open after resolution/allocation failure. Both paths now use growing buffers and every inability to prove safety refuses deletion. |
| WR-103 | deletion identity | Recursive-delete protection compared Windows paths with locale-sensitive `_wcsnicmp`. It now uses dynamically resolved `CompareStringOrdinal` case folding, matching Windows path identity. |
| WR-104 | WinSock shutdown | Shutting down the invalid-socket sentinel returned `SOCKET_ERROR` while preserving an unrelated thread-local error. It now sets `WSAENOTSOCK`. |
| WR-105 | WinSock pending error | `SO_ERROR` was read directly into caller storage, so a failed or short `getsockopt` could partially modify the output. The adapter now stages locally and publishes only an exact successful result. |
| WR-106 | WinSock startup wait | Startup waiters used only `Sleep(0)` while the rest of the runtime prefers `SwitchToThread` with a zero-sleep fallback. The once wait now follows the shared scheduling policy. |
| WR-107 | Windows native imports | The fixed PE import planner recognized only the old 32-bit-size stat variants, so native programs using the corrected `_stat64`, `_fstat64`, or `_wstat64` calls failed to link. The exact Windows-only exports now map to UCRT and remain rejected on other targets. |
| WR-108 | Windows native math | `remainder` was accepted by the shared dynamic-symbol policy but missing from the Windows UCRT planner, breaking the complete native Studio link. Both double and float UCRT exports are now planned and regression-tested. |
| WR-109 | Windows demo validation | The Windows demo driver could build and stage demos but had no launch-smoke mode, unlike its Unix counterparts. `--run` now launches each host-architecture binary with bounded diagnostics and removes only newly created run artifacts. |
| WR-110 | D3D11 shaders | FXC's DXBC validator rejected the shared shadow/light pixel shader because early-return control flow left a temporary component apparently uninitialized. The helpers now initialize one result and return it after structured control flow; real D3D11 probes verify hardware initialization. |
| WR-111 | D3D11 diagnostics | Shader initialization failures shared the short success-warning diagnostic cap, which truncated the validator's actionable error and obscured software fallback. Failures now retain a bounded extended diagnostic while successful compilation keeps the smaller cap. |
| WR-112 | tiled runtime import | Boxed `INT64_MAX` values were converted through `double`, rounded to the exclusive positive limit, and then cast back to `int64_t`. Exact boxed integers now bypass floating-point conversion, and floating values use an exclusive `2^63` upper bound. |
| WR-113 | BASIC Windows input | Horizontal-whitespace skipping consumed carriage returns before the cursor could atomically normalize CRLF. Windows-authored examples consequently lost end-of-line tokens; CR/LF pairs and lone CR characters now each produce one EOL. |
| WR-114 | model-loader tests | A runtime test depended on an untracked website JPEG, making a clean Windows checkout fail independently of product behavior. The test now decodes a tiny known-good embedded JPEG fixture and removes the external tree dependency. |
| WR-115 | Windows demo processes | Windows PowerShell could return a blank `Process.ExitCode` for a fast redirected child because its native process handle was not materialized before the wait. The launch driver now acquires and validates the handle immediately, preserving exact failure codes. |
| WR-116 | Windows native demos | Four more curated demos exposed the known Windows checked-integer optimizer miscompile during launch validation: invalid string handles in 3dbowling/Crackman, invalid pixels in Chess, and early Ridgebound termination. The Windows driver now uses its existing conservative `-O0` policy for every affected demo until that separately tracked compiler defect is resolved. |
| WR-117 | Windows demo cleanup | Launch cleanup snapshotted only top-level names, so a new run artifact nested beneath an existing staged asset directory could survive. Snapshots now track validated relative paths recursively and remove new entries from deepest to shallowest. |
| WR-118 | D3D11 mesh cache | Replacing a static mesh evicted its usable vertex/index pair before both immutable buffers existed. Both buffers now stage locally and commit together. |
| WR-119 | D3D11 morph cache | Position deltas were published before optional normal deltas, so a late allocation failure destroyed a complete morph entry. A full replacement entry now stages and publishes as one transaction. |
| WR-120 | D3D11 opaque depth | Opaque-depth resize released the old texture/SRV before replacement allocation. The pair now stages locally and preserves the previous target on failure. |
| WR-121 | D3D11 RTT | Render-to-texture replacement destroyed color, depth, and readback resources before the new set was complete. All five resources now stage before one commit. |
| WR-122 | D3D11 shadow slots | Per-light shadow resize discarded a valid depth texture/DSV/SRV before replacement creation. The complete slot now stages before eviction. |
| WR-123 | D3D11 shadow atlas | Atlas resize released its usable texture and views before the replacement was complete. The new atlas now stages transactionally and resets completeness only at commit. |
| WR-124 | D3D11 shadow binding | Atlas replacement could release a DSV still bound as the active output-merger target. Active atlas passes now unbind output targets before release. |
| WR-125 | D3D11 target factories | Color, depth, and staging helpers trusted successful HRESULTs with null COM outputs and leaked partial outputs. Every required resource/view is now validated and partial state is released. |
| WR-126 | D3D11 snapshots | Presented-backbuffer capture trusted a successful `GetBuffer` with a null texture. The path now normalizes this broken COM contract to `E_POINTER` and cleans up. |
| WR-127 | D3D11 resource factories | Static buffers, float SRV buffers, RGBA textures, native compressed textures, and cubemaps could accept missing successful outputs. The shared allocation boundaries now reject null resources/views without disturbing live cache entries. |
| WR-128 | Win32 allocation | The aligned allocator accepted non-power-of-two alignments after silently clamping small values. Invalid zero/non-power-of-two requests are now rejected before normalization. |
| WR-129 | Win32 framebuffer | DIB recreation destroyed the current bitmap before replacement creation and ignored partial handles or `SelectObject` failure. It now stages and selects a complete DIB before retiring the old one. |
| WR-130 | Win32 input | Raw-input parsing accepted short packets, and file-drop path allocation failure silently lost an event. Packet size/header framing is exact and allocation failure now emits the overflow contract. |
| WR-131 | Win32 window state | `SetWindowLongPtrW` failure while attaching runtime state was ignored, leaving a live HWND with no safe WndProc context. Creation now checks the zero-result/last-error contract and tears down on failure. |
| WR-132 | Win32 relative input | Destroying a relative-mouse window left raw-input registration and cursor clipping active. Teardown now unregisters the mouse device, releases the clip, and clears both mode flags. |
| WR-133 | Win32 event dispatch | A failed message wait was reported as an available event, while lazy DWM resolution raced between presentation threads. Wait failures are explicit and `DwmFlush` resolution now uses `INIT_ONCE`. |
| WR-134 | Win32 graphics timing | QPF/QPC and waitable-timer wait failures were unchecked. Frequency is initialized once, clock failure falls back to `GetTickCount64`, and failed timer waits fall back to `Sleep`. |
| WR-135 | Win32 clipboard | The fallback text and owner HWND were unsynchronized, and fallback allocation failure erased the last usable text. An SRW lock protects shared state and replacement publishes only after allocation. |
| WR-136 | Win32 clipboard | External `CF_UNICODETEXT` was treated as unbounded NUL-terminated memory. Reads now honor `GlobalSize`, use an in-tree bounded terminator scan, and convert only the validated UTF-16 span; this also avoids the unavailable MSVC `_Avx2WmemEnabled` dependency, which the native planner now rejects. |
| WR-137 | Win32 clipboard | Clipboard contents were cleared before UTF-8 validation, conversion, or allocation succeeded. The complete HGLOBAL is now prepared first and `EmptyClipboard` failure is checked. |
| WR-138 | WASAPI rendering | Format conversion wrote only logical samples, leaving channel padding or unsupported channel bytes stale; size arithmetic was also unchecked. The complete acquired frame span is overflow-checked and zeroed before mixing. |
| WR-139 | WASAPI worker | Unexpected wait results, impossible padding, and successful null render buffers could underflow counts or publish garbage. Each native contract is validated and failures become deterministic silent/error paths. |
| WR-140 | WASAPI startup | Initialization returned success before the worker established COM, successful COM calls could return null interfaces, a zero buffer size was accepted, and timer calls were unchecked. A readiness handshake, output validation, and monotonic fallback close those gaps. |
| WR-141 | native file dialogs | Win32 dialog paths and labels used lenient UTF conversion and ignored the second conversion result. Both directions now reject malformed text and require exact conversion. |
| WR-142 | native file dialogs | A process-global unsynchronized availability/COM latch was reused across apartments, while option and result HRESULTs or partial outputs were ignored. Every public call now owns a balanced thread-local COM scope and cleans every partial interface/string. |
| WR-143 | drawn file dialog | Narrow borrowed environment pointers, first-logical-drive fallback, and pre-epoch FILETIME subtraction produced races, floppy-drive roots, or timestamp underflow. Retrying UTF-16 snapshots, once-only system-drive discovery, saturation, and the matching `GetWindowsDirectoryW` native import now define the behavior. |
| WR-144 | machine runtime | Fixed buffers and borrowed `_wgetenv` pointers truncated/raced user, home, and temp values; root `C:\\` became drive-relative `C:`, and processor counts ignored other groups. Growing UTF-16 snapshots preserve roots, `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` reports the host, and its Kernel32 import is available to native output. |
| WR-145 | installer defaults | Environment-folder reads raced size changes and failed known-folder calls could leak a partial COM allocation. Reads now retry boundedly and every returned `PWSTR` is released. |
| WR-146 | installer clipboard | Diagnostic-copy size arithmetic could overflow and the current clipboard was cleared before allocation succeeded. The payload is overflow-checked and fully prepared before opening/emptying the clipboard. |
| WR-147 | installer folder picker | Destination text reads and folder-picker mutations/results were incompletely checked, including successful null or failed partial outputs. Exact text results, HRESULTs, and every COM/PWSTR lifetime are now validated. |
| WR-148 | installer registry | General string queries trusted a stale size/type and an external NUL, so concurrent changes or malformed values could truncate or overread. Reads now retry `ERROR_MORE_DATA`, cap and align bytes, revalidate type, and bound the terminator. |
| WR-149 | installer PATH rollback | PATH snapshots had the same size race and could restore the wrong registry type. Snapshot reads now retry and preserve the type returned with the successful payload. |
| WR-150 | installer elevation | Elevated launch assumed a process handle and ignored wait failure. The lifecycle now requires the handle and accepts only a signaled wait before reading the exact exit code. |
| WR-151 | installer cleanup launch | Detached-helper inspection ignored wait/exit failures and unchecked termination, which could let a helper outlive cleanup state. Inspection records exact errors and unsuccessful self-delete requires confirmed termination. |
| WR-152 | cleanup helper | Parent-open failure was conflated with an already-exited parent, timeout hid other wait errors, and executable-path growth mishandled exact-fit buffers. Exact Win32 status is returned and path discovery is bounded with correct truncation rules in both helper and host. |
| WR-153 | installer signing | Timestamp URLs accepted embedded credentials or fragments. Signing now permits only credential-free absolute HTTPS endpoints with a host. |
| WR-154 | installer signing | In-place signing and direct metadata writes could corrupt or replace a known-good release after signer/verifier failure. Artifact and metadata are staged in the destination directory, verified, published in order, and always cleaned. |
| WR-155 | Windows demo architecture | Build type was unchecked and host detection missed native ARM64 when invoked from an emulated shell. The driver validates configurations and honors `PROCESSOR_ARCHITEW6432`. |
| WR-156 | Windows demo tool discovery | The driver assumed a multi-config executable path and reused CMake trees without checking declared architecture. It now resolves multi- and single-config layouts and rejects incompatible caches before reuse. |
| WR-157 | Windows demo manifests | Asset/project paths could escape their roots and duplicate or unsafe manifest names could overwrite outputs. Lexical confinement, safe-name rules, and case-insensitive duplicate rejection now protect staging. |
| WR-158 | Windows CTest scheduling | The editor hot-path probe enforced 250 ms wall-clock budgets while sharing the host with up to seven unrelated tests, producing a clean-build-only false failure. The absolute-timing probe now runs serially. |
| WR-159 | D3D11 telemetry queries | Frame-timing query creation trusted successful HRESULTs with null objects. Every required query now passes through the shared successful-null normalizer before it can be published. |
| WR-160 | D3D11 resource factories | Rasterizer and constant-buffer factories could return nominal success without a usable object, allowing a null cache entry or later dereference. Both factory boundaries now require their output. |
| WR-161 | D3D11 fallback resources | White 2D/cube textures, their SRVs, and the BRDF lookup texture/SRV all trusted successful-null creation. Each mandatory fallback resource is now validated before initialization continues. |
| WR-162 | D3D11 pipeline state | Required depth-stencil, blend, and sampler states accepted successful-null driver outputs. Context creation now rejects every missing state at the call that created it. |
| WR-163 | D3D11 startup assets | Shader objects, required and optional input layouts, and the skybox vertex buffer could publish null outputs after successful HRESULTs. All startup assets now normalize that contract to `E_POINTER`; optional compact layouts remain optional only after an actual failure is handled. |
| WR-164 | D3D11 device creation | `D3D11CreateDeviceAndSwapChain` success was accepted without proving that the swapchain, device, and immediate context trio existed. Initialization now requires all three outputs before continuing. |
| WR-165 | D3D11 DXGI outputs | Factory-parent lookup and the dedicated readback `GetBuffer` path trusted successful-null outputs and did not defensively release a failed partial output. Both paths now validate and retire partial interfaces. |
| WR-166 | D3D11 presentation | Non-failing DXGI status codes such as occlusion were treated as proof that a captured backbuffer reached the display. Only `S_OK` now publishes a presented-frame snapshot; all other statuses invalidate it. |
| WR-167 | D3D11 scene replacement | Publishing a newly staged scene called the full teardown routine, unnecessarily discarding the valid native-size overlay before the route was complete. Scene-only teardown is now separate and preserves the overlay transactionally. |
| WR-168 | D3D11 render scale | Scale changes destroyed the last usable scene route before replacement allocation and committed the requested scale before resources existed. Overlay and scene resources now become ready first, and only then is the scale committed. |
| WR-169 | D3D11 route repair | Same-scale requests returned success even when route resources were missing, while returning to native scale could retain an unused offscreen route and stale temporal/present state. Calls now repair incomplete routes, retire unnecessary targets, and invalidate dependent state. |
| WR-170 | Windows TLS CA paths | Custom CA files were opened through the narrow CRT path boundary, failing for names outside the active ANSI code page. Paths now convert strictly from UTF-8 and open with `_wfopen`. |
| WR-171 | Windows TLS CA sizing | CA loading used 32-bit `long` seek/tell results and had no file-size limit. It now uses 64-bit CRT offsets and rejects bundles larger than 16 MiB before allocation. |
| WR-172 | Windows TLS CA snapshots | A CA file that grew after sizing was silently accepted as a verified prefix. The loader now requires exact content and rejects any trailing byte or read error after the snapshot. |
| WR-173 | Windows TLS CA framing | Null stores/paths and embedded NUL bytes in PEM input were not rejected at the parser boundary, permitting leaked allocations or truncated interpretation. Inputs are validated before reading and PEM must be NUL-free. |
| WR-174 | Windows TLS PEM parsing | A valid certificate followed by a truncated, undecodable, unallocatable, or unaddable PEM block still accepted the earlier prefix. Bundle parsing is now all-or-nothing. |
| WR-175 | Windows TLS CA count | PEM bundles had no certificate-count ceiling. Custom stores now reject more than 1,024 certificates before unbounded decode/store work. |
| WR-176 | Windows TLS DER input | Raw DER bundle length was narrowed to CryptoAPI's `DWORD` without a boundary check. Oversized input is now rejected before the cast. |
| WR-177 | Windows TLS chain input | `tls_verify_chain(NULL)` could dereference its session, and an oversized leaf length narrowed into `CertCreateCertificateContext`. Null sessions and leaf sizes beyond the CryptoAPI contract now fail deterministically. |
| WR-178 | Windows TLS chain construction | Intermediate lengths could narrow, and failed chain-engine/chain-context APIs could leave partial outputs allocated. Lengths are checked and all failed partial outputs are released. |
| WR-179 | Windows TLS CertificateVerify | Oversized leaf lengths and successful-null CNG key imports could reach CertificateVerify processing; failed imports could also leak a partial key. Bounds and required key outputs are now enforced with cleanup. |
| WR-180 | installer update configuration | Supplying only part of the manifest URL/modulus/exponent tuple silently disabled update verification. Only a wholly absent tuple is unconfigured; partial security configuration is now an error. |
| WR-181 | installer update key modulus | The pinned RSA modulus had no public-boundary size or canonical-hex validation. Verification now requires a lowercase, non-zero-leading 2,048- to 4,096-bit modulus. |
| WR-182 | installer update key exponent | The pinned exponent could be oversized, non-canonical, even, or otherwise invalid before CNG import. It is now bounded to 32 bits, minimally encoded, odd, and at least three. |
| WR-183 | installer update signatures | Signature text was decoded and allocated before proving its encoding or expected RSA width. It must now be lowercase hex with exactly the pinned modulus length. |
| WR-184 | installer update digests | Download SHA-256 text was decoded before enforcing its exact representation. Manifests now require exactly 64 lowercase hexadecimal characters. |
| WR-185 | installer update URLs | Raw backslashes and spaces were left for WinHTTP canonicalization, allowing the signed URL text and requested resource to differ ambiguously. Such characters are now rejected before parsing. |
| WR-186 | installer update origins | Same-origin comparison lowercased hosts with the process locale. Host identity now uses Windows ordinal case-insensitive comparison without mutation. |
| WR-187 | installer update transport | The update session did not explicitly require the supported TLS floor or enable certificate revocation checks. WinHTTP now requires TLS 1.2 and enables SSL revocation before sending. |
| WR-188 | installer update reads | A corrupted or shimmed `WinHttpReadData` byte count could exceed the supplied buffer yet still be appended. Returned counts are now bounded by both the buffer and manifest cap. |
| WR-189 | installer path identity | Lifecycle path comparisons and mutex/cache hashes depended on `towlower` and the process locale. Comparisons now use ordinal Windows semantics and hashes use invariant case folding over preferred separators. |
| WR-190 | installer protected roots | Windows-directory and protected-known-folder lookup failures made destination protection fail open, and partial known-folder outputs could leak. Resolution is now mandatory and every returned allocation is released. |
| WR-191 | installer registry boundary | Registry opens could report success with no key, malformed string queries were conflated with missing values, and string-write size/type/NUL constraints were unchecked. The adapter now validates handles, exact reads, and bounded writes. |
| WR-192 | installer reparse defense | Unexpected `GetFileAttributesW` errors on a destination ancestor were treated like a nonexistent path, so safety could not actually be proven. Only file/path-not-found is skippable; every other error fails closed. |
| WR-193 | installer metadata reads | Transaction and ownership text files were read without a bound, and unreadable files were conflated with absent files. Reads now require a regular file, cap it at 32 MiB, and consume one exact snapshot. |
| WR-194 | installer atomic writes | Staged metadata used stream flush only and could leave temporary files after write/flush exceptions. Native writes now loop exactly, call `FlushFileBuffers`, publish with write-through, and clean failed staging. |
| WR-195 | installer recovery journal | Journal state was selected by substring, so corrupt or contradictory text could drive destructive recovery; a missing journal could also discard a preserved old tree. Parsing now requires the exact schema and retains ambiguous transactions. |
| WR-196 | installer PATH updates | PATH mutation used a lossy optional query, overwrote malformed reads with an empty value, forced `REG_EXPAND_SZ`, and compared entries with locale folding. It now uses the exact snapshot reader, preserves the existing type, and compares ordinally. |
| WR-197 | installer Shell Links | Successful-null Shell Link/persistence interfaces could be dereferenced, and getter buffers were assumed to contain a terminator. Required COM outputs and bounded NUL termination are now verified before ownership matching. |
| WR-198 | installer shortcut cleanup | Missing shortcut records still triggered parent cleanup, directories/reparse points could be removed as links, and a desktop shortcut could cause the Desktop root itself to be removed if empty. Cleanup now skips absent links, requires plain files, and protects all shell roots. |

## Regression coverage

- `test_rt_windows_runtime` exercises finite wait slicing, concurrent WinSock initialization,
  deterministic WinSock error/output contracts, entropy argument handling, strict Windows path
  transcoding, checked directory conversion, ordinal comparison, fail-closed deletion guards,
  processor-count validity, drive-root temp preservation, and long environment-backed home paths.
- `test_rt_file_ext` creates a 3 GiB sparse file and verifies 64-bit seek, stat, visibility, and
  modification-time behavior without allocating the file's logical size.
- `test_rt_args` races small and 16 KiB environment values across the Win32 size/read boundary.
- `test_rt_locale` exercises sentinel, malformed-tag, normalization, and cleared-output behavior.
- `test_rt_tls_cert` exercises exact CertificateVerify framing, null-session rejection, non-ASCII
  custom-CA paths, malformed trailing PEM blocks, and the 16 MiB Windows bundle limit.
- `test_rt_exec` relaunches itself through both Process and ConPTY with intentionally unsorted,
  non-ASCII explicit environments; it also rejects duplicate variables and malformed UTF-8.
- `test_rt_asset` mounts a non-ASCII pack path and verifies Windows ordinal case identity.
- `test_rt_uia_provider` covers full-width runtime IDs, strict text conversion, invalid geometry,
  stale bridge generations, deterministic failure outputs, and normalized range values.
- `test_rt_watcher`, `test_rt_future`, `test_rt_concqueue`, `test_rt_threads_monitor`,
  `test_rt_threads_thread`, and `test_rt_threads_primitives` cover the affected runtime contracts.
- `test_vgfx3d_backend_d3d11_shared` covers timestamp/depth-probe poll budgets and source contracts
  requiring stage-before-publish ordering for all repaired D3D11 replacement paths, required
  startup-object validation, exact presentation status, overlay-preserving scene replacement, and
  the shader helpers' initialized single-result control flow; `zia_smoke_d3d11_rtt_readback`,
  `g3d_test_canvas3d_viewmodel_sprite`, `g3d_test_canvas3d_point_shadows_d3d11`, and the Ridgebound
  D3D11 smoke exercise the hardware backend.
- `test_linker_platform_import_planners` verifies that the 64-bit stat and floating-remainder
  exports map to UCRT, the new reliability APIs map to Kernel32, and Windows-only names stay
  excluded from Linux and macOS.
- `test_rt_scene_editor` preserves the full boxed signed-64-bit range in tiled properties;
  `test_basic_lexer` covers CRLF and lone-CR EOL normalization; and `test_rt_model3d` supplies its
  own strict-decoded JPEG fixture.
- `windows_automation_script_contracts` exercises failure-atomic signing, timestamp-URL rejection,
  staging cleanup, single-config tool discovery, architecture checks, path confinement, and
  duplicate demo-output rejection.
- `test_windows_installer_update` covers partial pinned configurations, key/digest/signature bounds,
  ambiguous URLs, and ordinal origin matching. `test_windows_installer_lifecycle_contract` protects
  exact recovery schemas, durable staging, validated PATH mutation, required Shell Link outputs,
  and fail-closed destination/shortcut cleanup.

The required end-to-end gates are `scripts/build_zanna_win.ps1` and
`scripts/build_demos_win.ps1 --run`; repository-owned `.cmd` wrappers are intentionally absent under
[ADR 0113](adr/0113-windows-automation-powershell-entry-points.md). The platform-policy lint remains
mandatory for future changes in these adapters.

## Validation record

Revalidated on Windows x64/MSVC on 2026-07-22:

- The canonical `scripts/build_zanna_win.ps1` pipeline completed its clean warning-as-error build,
  native Studio link, 1,804-test non-slow selection, runtime/API audits, platform lint, host smoke
  checks, and install stage. A final incremental pass re-exercised those stages after the native
  import correction.
- A later clean revalidation exposed one contention-only failure in the editor hot-path timing
  probe; the probe passed immediately in isolation. After serializing that absolute-timing sample,
  the canonical eight-worker pipeline completed all build, non-slow CTest, audit, smoke, policy,
  and install stages successfully.
- The WR-159 through WR-198 pass then completed a fresh warning-as-error rebuild; its clean-tree
  1,805-test non-slow CTest log reported all tests passed. An immediate canonical incremental run
  returned exit 0 across build, the same complete test selection, audits, platform policy, host
  smoke checks, and the install stage.
- The focused installer, automation, D3D11, machine-runtime, and native-import planner regressions
  passed. The user-scope native installer lifecycle smoke also completed install, integration,
  uninstall, and cleanup. The opt-in IntelliSense slow test also passes in 340.79 seconds under its
  measured 420-second timeout.
- An exploratory all-slow-label pass found only `source_health_audit` failing four stale baseline
  limits. A detached clean-HEAD audit produced the identical four metrics, proving this change did
  not add that debt; its baseline was deliberately not weakened here.
- `scripts/lint_platform_policy.sh --strict --changed-only`, the PowerShell parser checks, the
  source-header audit, and `git diff --check` passed; the new TLS regression uses the shared
  `ZANNA_HOST_WINDOWS` capability rather than a raw host macro.
- A `scripts/build_demos_win.ps1 --clean --run` invocation built and launch-smoked all nine curated
  x64 demos successfully: Ashfall, 3dbowling, Ridgebound, Xenoscape, Crackman, Chess, Baseball,
  Paint, and ZannaSQL.
