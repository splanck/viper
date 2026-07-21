---
status: active
audience: developers
last-verified: 2026-07-20
---

# Windows Runtime Reliability Audit

This audit covers the Direct3D 11 backend and the Windows-specific runtime adapters for sockets,
entropy, TLS verification, process/ConPTY launch, paths and assets, locale detection, file watching,
concurrency, stack safety, and UI Automation. It is a robustness pass only: no IL opcode, grammar,
verifier rule, runtime C ABI, or cross-layer dependency changed.

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

## Regression coverage

- `test_rt_windows_runtime` exercises finite wait slicing, concurrent WinSock initialization,
  WinSock error contracts, entropy argument handling, and strict Windows path transcoding.
- `test_rt_locale` exercises sentinel, malformed-tag, normalization, and cleared-output behavior.
- `test_rt_tls_cert` exercises exact CertificateVerify framing and null-session rejection.
- `test_rt_exec` relaunches itself through both Process and ConPTY with intentionally unsorted,
  non-ASCII explicit environments; it also rejects duplicate variables and malformed UTF-8.
- `test_rt_asset` mounts a non-ASCII pack path and verifies Windows ordinal case identity.
- `test_rt_uia_provider` covers full-width runtime IDs, strict text conversion, invalid geometry,
  stale bridge generations, deterministic failure outputs, and normalized range values.
- `test_rt_watcher`, `test_rt_future`, `test_rt_concqueue`, `test_rt_threads_monitor`,
  `test_rt_threads_thread`, and `test_rt_threads_primitives` cover the affected runtime contracts.
- `test_vgfx3d_backend_d3d11_shared` covers both timestamp and depth-probe poll budgets;
  `zia_smoke_d3d11_rtt_readback`,
  `g3d_test_canvas3d_point_shadows_d3d11`, and the Ridgebound D3D11 smoke exercise the backend.

The required end-to-end gates are `scripts/build_zanna_win.ps1` and
`scripts/build_demos_win.ps1`; the platform-policy lint remains mandatory for future changes in
these adapters.
