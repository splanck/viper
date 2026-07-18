---
status: active
audience: developers
last-verified: 2026-07-16
---

# Windows Runtime Reliability Audit

This audit covers the Direct3D 11 backend and the Windows-specific runtime adapters for sockets,
entropy, TLS verification, process launch, locale detection, file watching, and concurrency. It is
a robustness pass only: no IL opcode, grammar, verifier rule, runtime C ABI, or cross-layer
dependency changed.

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

## Regression coverage

- `test_rt_windows_runtime` exercises finite wait slicing, concurrent WinSock initialization,
  WinSock error contracts, and entropy argument handling.
- `test_rt_locale` exercises sentinel, malformed-tag, normalization, and cleared-output behavior.
- `test_rt_tls_cert` exercises exact CertificateVerify framing and null-session rejection.
- `test_rt_exec` relaunches itself on Windows with an intentionally unsorted, non-ASCII explicit
  environment and verifies the child receives a sorted Unicode block.
- `test_rt_watcher`, `test_rt_future`, `test_rt_concqueue`, `test_rt_threads_monitor`,
  `test_rt_threads_thread`, and `test_rt_threads_primitives` cover the affected runtime contracts.
- `test_vgfx3d_backend_d3d11_shared`, `zia_smoke_d3d11_rtt_readback`,
  `g3d_test_canvas3d_point_shadows_d3d11`, and the Ridgebound D3D11 smoke exercise the backend.

The required end-to-end gates are `scripts/build_zanna_win.ps1` and
`scripts/build_demos_win.ps1`; the platform-policy lint remains mandatory for future changes in
these adapters.
