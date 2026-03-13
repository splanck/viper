---
status: active
audience: contributors
last-verified: 2026-03-04
---

# Viper Runtime API — Per-Function Robustness, Security & Optimization Review

**Reviewed:** 2026-02-17
**Scope:** All C functions in `src/runtime/` (~200 files, ~2,900 functions, ~112,694 LOC)
**Dimensions:** Robustness · Security · Optimization
**Legend:** ✅ Clean · ⚠️ Warning · ❌ Critical issue · 🚀 Significant opportunity

---

## Executive Summary

| Severity | Robustness | Security | Optimization | Total |
|----------|-----------|---------|-------------|-------|
| ❌ Critical | 28 | 23 | 4 | 55 |
| ⚠️ Warning | 87 | 31 | 62 | 180 |
| ✅ Clean | — | — | — | — |

**Total issues: 235** across all runtime files. Critical issues that should be fixed immediately are listed in §2 below.

---

## 1. Critical Findings (All ❌)

These findings represent exploitable bugs, data-loss risks, or correctness-breaking defects.

### Security Criticals

| # | File | Function | Issue |
|---|------|----------|-------|
| S-01 | `rt_tls.c` | `verify_cert` / all TLS paths | Certificate validation is dead code — all HTTPS/TLS is trivially MITM-able |
| S-02 | `rt_tls.c` | `process_server_hello` | `session_id_len` from server not bounds-checked → OOB read/write |
| S-03 | `rt_crypto.c` | `rt_crypto_random_bytes` | LCG fallback PRNG seeded with pointer address when `/dev/urandom` fails → all derived keys are weak |
| S-04 | `rt_crypto.c` | `rt_hkdf_expand_label` | 512-byte stack buffer with no `label_len` bounds check → stack buffer overflow |
| S-05 | `rt_aes.c` | `pkcs7_unpad` | Non-constant-time padding check → padding oracle attack |
| S-06 | `rt_aes.c` | `derive_key` / `rt_aes_encrypt_str` / `rt_aes_decrypt_str` | Unsalted double-SHA256 KDF — weak key derivation |
| S-07 | `rt_network_http.c` | `do_http_request` | `tls_config.verify_cert = 0` set unconditionally → all HTTPS MITM-able |
| S-08 | `rt_network_http.c` | `add_header` / `build_request` | No CR/LF validation on header values → HTTP header injection |
| S-09 | `rt_network_http.c` | `read_body_chunked` / `read_body_fixed` | No maximum body size → DoS via server-controlled malloc(HUGE) |
| S-10 | `rt_websocket.c` | `ws_recv_frame` | No max payload length → DoS via `malloc(server_controlled_len)` |
| S-11 | `rt_regex.c` | All `rt_pattern_*` / `find_match` / `match_quant` | Backtracking NFA with no step limit or timeout → ReDoS |
| S-12 | `rt_regex.c` | `pattern_cache[]` | Global pattern cache accessed without locks → data race, double-free |
| S-13 | `rt_markdown.c` | `process_inline` | Unescaped URLs in link `href` → XSS via `javascript:` scheme |
| S-14 | `rt_toml.c` | `rt_toml_is_valid` | Always returns `1` regardless of input — broken validator |
| S-15 | `rt_toml.c` | `rt_toml_get` / `rt_toml_get_str` | Raw type-punning via `*(uint64_t *)root` — undefined behavior |
| S-16 | `rt_json.c` | `parse_value` (recursive) | No recursion depth limit → stack-overflow DoS for deeply nested input |
| S-17 | `rt_xml.c` | `parse_element` / `format_element` / `find_all_recursive` | No recursion depth limit → stack-overflow DoS |
| S-18 | `rt_yaml.c` | `parse_value` / `parse_block_sequence` / `parse_block_mapping` | No recursion depth limit → stack-overflow DoS |
| S-19 | `rt_dir.c` | `rt_dir_remove_all` | Uses `stat()` (follows symlinks) before recursing → out-of-tree deletion |
| S-20 | `rt_compress.c` | `inflate_huffman` / `inflate_data` / `gunzip_data` | No output size cap → decompression bomb fills memory |
| S-21 | `rt_tempfile.c` | All temp file/dir creation | Predictable names (PID + `time(NULL)`) + non-atomic creation → symlink race |
| S-22 | `rt_exec.c` | `build_cmdline` (Windows) | Backslash-quoting of `"` in args missing → argument injection |
| S-23 | `rt_bigint.c` | `rt_bigint_pow_mod` | Non-constant-time modular exponentiation → timing side-channel for crypto use |

### Robustness Criticals

| # | File | Function | Issue |
|---|------|----------|-------|
| R-01 | `rt_object.c` | `rt_weak_store` / `rt_weak_load` | NULL `addr` not checked before dereference → immediate crash |
| R-02 | `rt_concmap.c` | `free_entry` / `rt_concmap_set` / `cm_clear_unlocked` / `rt_concmap_remove` | `rt_obj_free` never called after `rt_obj_release_check0` → all entry removals leak memory |
| R-03 | `rt_concqueue.c` | `rt_concqueue_enqueue` | `malloc` return not checked → NULL dereference on OOM |
| R-04 | `rt_concqueue.c` | All dequeue paths | Same `rt_obj_free` missing bug as concmap |
| R-05 | `rt_parallel.c` | All `*_pool` functions | Missed-signal deadlock AND use-after-stack-free (stack-allocated mutex/cond accessed after function returns) |
| R-06 | `rt_monitor.c` | `ensure_table_cs_init` (Windows) | Data race without atomics — concurrent first calls corrupt table |
| R-07 | `rt_async.c` | `async_any_entry` | Infinite spin-poll loop if any future never resolves → thread leak |
| R-08 | `rt_scheduler.c` | `rt_scheduler_poll` | `rt_string_unref(e->name)` called after `rt_seq_push(result, e->name)` → use-after-free |
| R-09 | `rt_bloomfilter.c` | `rt_bloomfilter_new` | `calloc` for `bf->bits` not checked → NULL deref on first add/contains |
| R-10 | `rt_defaultmap.c` | `dm_resize` / `rt_defaultmap_new` | `calloc` not checked → NULL deref |
| R-11 | `rt_sortedset.c` | `ensure_capacity` | `realloc` result overwrites `set->data` — old pointer lost on failure; also no GC finalizer |
| R-12 | `rt_trie.c` | `collect_keys` / `rt_trie_keys` / `rt_trie_with_prefix` | Fixed `char buf[4096]` used recursively → stack overflow for keys >4095 chars |
| R-13 | `rt_duration.c` | `rt_duration_abs` / `rt_duration_neg` | `-INT64_MIN` is signed integer overflow — undefined behavior |
| R-14 | `rt_dateonly.c` | `rt_dateonly_format` | `snprintf` with long tokens can write past `buf[255]` → stack buffer overrun |
| R-15 | `rt_graphics.c` | `rt_canvas_flip` | Calls `exit(0)` inside a library function — unacceptable for embedded use |
| R-16 | `rt_scene.c` | `rt_scene_draw` | `rt_seq_new()` nodes sequence created each frame, never freed → unbounded memory leak |
| R-17 | `rt_spritebatch.c` | `ensure_capacity` | `rt_trap()` called with `batch->items` not updated → use-after-free if trap returns |
| R-18 | `rt_spritesheet.c` | `ensure_cap` | ~~Two separate `realloc` calls; second failure corrupts parallel array capacities~~ **✅ FIXED 2026-02-23** — replaced with malloc+memcpy so both allocations can be rolled back independently |
| R-19 | `rt_tilemap.c` | `rt_tilemap_collide_body` | Unchecked ABI cast to locally-defined struct with hardcoded offsets |
| R-20 | `rt_fmt.c` | `rt_fmt_to_words` | `value = -value` for `INT64_MIN` is signed overflow UB |
| R-21 | `rt_pixels.c` | `rt_pixels_resize` | OOB read when source is exactly 1 pixel wide |
| R-22 | `rt_action.c` | `rt_action_load` | Malformed JSON can cause infinite loop; loaded key codes not range-validated |
| R-23 | `rt_bigint.c` | `bigint_ensure_capacity` | `realloc` return not checked; NULL overwrites `digits` — UB |
| R-24 | `rt_bigint.c` | `rt_bigint_to_str_base` | Buffer size estimate too small → buffer underwrite (OOB write before allocation) |
| R-25 | `rt_bigint.c` | `rt_bigint_and` / `rt_bigint_or` / `rt_bigint_xor` | Returns zero for any negative operand — semantically wrong |
| R-26 | `rt_numeric_conv.c` | `rt_f64_to_i64` | Finite doubles outside `INT64` range cast to `long long` — undefined behavior |
| R-27 | `rt_mat4.c` | `rt_mat4_perspective` / `rt_mat4_ortho` | No validation of `fov`/`near`/`far`/`aspect`; zero/negative/equal silently produce NaN/Inf matrices |
| R-28 | `rt_compress.c` | `build_huffman_tree` / `decode_symbol` | Codes >9 bits silently fail → valid DEFLATE streams rejected |

### Optimization Criticals

| # | File | Function | Issue |
|---|------|----------|-------|
| O-01 | `rt_file_ext.c` | `rt_file_write_bytes` | One `write()` syscall per byte — catastrophically slow for binary files |
| O-02 | `rt_file_ext.c` | `rt_io_file_read_all_bytes` / `rt_file_read_bytes` | Byte-by-byte copy into `rt_bytes` instead of `memcpy` |
| O-03 | `rt_graphics.c` | `rt_canvas_flood_fill` | O(r²) heap allocation — 266 MB for a 4K canvas |
| O-04 | `rt_xml.c` | `rt_xml_text_content` | O(n²) string concatenation across child nodes |

---

## 2. Per-File Per-Function Review

### `rt_exec.c`

#### `exec_spawn(cmd, args, env, flags, stdin_fd, stdout_fd, stderr_fd) → ExecHandle*`
- **Robustness:** ⚠️ WARNING — `waitpid` not retried on `EINTR`; interrupted wait leaves zombie processes
- **Security:** ✅ — `execvp` args passed directly without shell; safe
- **Optimization:** ✅

#### `build_cmdline(args) → char*` (Windows only)
- **Robustness:** ✅
- **Security:** ❌ CRITICAL — No backslash-escaping of `"` in argument strings → argument injection via crafted arguments
- **Optimization:** ✅

#### `rt_exec_shell(cmd) → int`
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Uses `system()` — inherent shell injection surface; caller must sanitize `cmd`
- **Optimization:** ✅

#### `rt_exec_shell_capture(cmd) → rt_string*`
- **Robustness:** ⚠️ WARNING — `popen` failure returns NULL; unchecked before read
- **Security:** ⚠️ WARNING — Uses `popen()` — inherent shell injection surface
- **Optimization:** ✅

#### `rt_exec_run(path, args) → int`, `rt_exec_run_env(...)`, `rt_exec_async(...)`, `rt_exec_wait(...)`, `rt_exec_kill(...)`, `rt_exec_pid(...)`
- ✅✅✅ — No significant issues

---

### `rt_crypto.c`

#### `rt_crypto_random_bytes(buf, len) → void`
- **Robustness:** ✅ — tries `/dev/urandom`, falls back
- **Security:** ❌ CRITICAL — Fallback is LCG PRNG seeded with pointer address (`(uintptr_t)buf`). All keys, IVs, nonces derived when `/dev/urandom` is unavailable are cryptographically weak.
- **Optimization:** ⚠️ — Opens `/dev/urandom` per call; use `getrandom(2)` on Linux to avoid fd overhead

#### `rt_hkdf_extract(salt, salt_len, ikm, ikm_len, prk) → void`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ✅

#### `rt_hkdf_expand(prk, info, info_len, out, out_len) → void`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ✅

#### `rt_hkdf_expand_label(secret, label, context, len, out) → void`
- **Robustness:** ❌ CRITICAL — 512-byte stack buffer; `label_len` not bounds-checked before `memcpy` → stack buffer overflow with long labels
- **Security:** ❌ CRITICAL — Same issue enables stack smashing attack
- **Optimization:** ✅

#### `x25519_scalarmult(out, scalar, point) → void`
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Non-constant-time conditional swap in field operations → timing side-channel; use `crypto_uint32_barrier` or similar
- **Optimization:** ✅

#### `rt_sha256(data, len, out) → void`, `rt_sha256_string(s) → rt_string*`, `rt_hmac_sha256(...)`, `rt_hkdf(...)`
- ✅✅✅ — Implementation correct, no critical issues

---

### `rt_aes.c`

#### `pkcs7_unpad(buf, len) → int`
- **Robustness:** ✅ — validates pad byte range
- **Security:** ❌ CRITICAL — Padding check uses early-exit loop → non-constant-time → padding oracle attack. Use constant-time comparison across all pad bytes.
- **Optimization:** ✅

#### `derive_key(password, key_out, iv_out) → void`
- **Robustness:** ✅
- **Security:** ❌ CRITICAL — Unsalted double-SHA256 KDF: `SHA256(SHA256(password))`. No salt → rainbow table attack; no iterations → fast brute-force. Use PBKDF2/Argon2 with random salt.
- **Optimization:** ✅

#### `rt_aes_encrypt_str(plaintext, password) → rt_string*`, `rt_aes_decrypt_str(ciphertext, password) → rt_string*`
- **Robustness:** ✅
- **Security:** ❌ CRITICAL — Inherits `derive_key` weakness; also `pkcs7_unpad` timing oracle
- **Optimization:** ✅

#### `gf_mul(a, b) → uint8_t` (GF(2⁸) multiplication)
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Data-dependent branching on secret bits → timing side-channel. Use table lookup or bit-slicing.
- **Optimization:** ✅

#### `aes_encrypt_block`, `aes_decrypt_block`, `aes_key_expansion`, `rt_aes_encrypt_ecb`, `rt_aes_decrypt_ecb`, `rt_aes_encrypt_cbc`, `rt_aes_decrypt_cbc`
- ✅✅✅ — ECB/CBC mode correct; no additional critical issues beyond inherited ones

---

### `rt_tls.c`

#### `verify_cert(ctx) → int`
- **Robustness:** ✅ — function exists
- **Security:** ❌ CRITICAL — Function body is a stub that always returns success; the `verify_cert` flag in `TLSConfig` is never honored. All TLS connections are trivially MITM-able. Must implement chain verification against a trusted CA store.
- **Optimization:** ✅

#### `process_server_hello(ctx, data, len) → int`
- **Robustness:** ❌ CRITICAL — `session_id_len` read from server packet, used as offset without bounds check against `len` → OOB read/write
- **Security:** ❌ CRITICAL — Same OOB exploitable remotely
- **Optimization:** ✅

#### `rt_tls_recv(ctx, buf, len) → int`
- **Robustness:** ⚠️ WARNING — Recursive call for non-app-data records; deeply fragmented handshake messages → unbounded stack growth
- **Security:** ✅
- **Optimization:** ✅

#### `rt_tls_connect`, `rt_tls_send`, `rt_tls_close`, `rt_tls_free`
- ✅✅ robustness/optimization — inherit security flaw from missing cert verification

---

### `rt_password.c`

#### `pbkdf2_sha256(password, pw_len, salt, salt_len, iterations, out, out_len) → void`
- **Robustness:** ⚠️ WARNING — `malloc` for intermediate HMAC buffer not checked; OOM returns with uninitialized `out`
- **Security:** ✅ — PBKDF2 correctly implemented
- **Optimization:** ✅

#### `rt_password_hash(password) → rt_string*`, `rt_password_verify(password, hash) → int`
- ✅✅✅ — Uses proper salt + iteration count; verify is constant-time

---

### `rt_hash.c`

#### `rt_fnv1a_hash(data, len) → uint64_t`, `rt_fnv1a_str_hash(s) → uint64_t`
- ✅✅✅ — FNV-1a implementation is correct

#### `rt_siphash(data, len, k0, k1) → uint64_t`
- ✅✅✅ — SipHash-2-4 correctly implemented

---

### `rt_crc32.c`

#### `rt_crc32_init() → void`
- **Robustness:** ⚠️ WARNING — Lazy initialization via `static int initialized` without atomics; concurrent first calls race → data race on table
- **Security:** ✅
- **Optimization:** ✅ — Could use platform CRC instruction

#### `rt_crc32(data, len) → uint32_t`, `rt_crc32_update(crc, data, len) → uint32_t`
- ✅✅✅ — Correct; `rt_crc32_init` race is the only issue

---

### `rt_object.c`

#### `rt_weak_store(addr, obj) → void`
- **Robustness:** ❌ CRITICAL — `addr` not checked for NULL before store → immediate crash if caller passes NULL address
- **Security:** ✅
- **Optimization:** ✅

#### `rt_weak_load(addr) → rt_obj*`
- **Robustness:** ❌ CRITICAL — `addr` not checked for NULL before load
- **Security:** ✅
- **Optimization:** ✅

#### `rt_obj_new_i64(size) → rt_obj*`
- ✅✅✅ — Traps on OOM, safe

#### `rt_obj_retain_maybe(obj) → rt_obj*`, `rt_obj_release_check0(obj) → int`, `rt_obj_free(obj) → void`
- ✅✅✅ — Reference counting correctly implemented

#### `rt_obj_set_finalizer(obj, fn) → void`, `rt_obj_get_class(obj) → int`, `rt_obj_set_class(obj, cls) → void`
- ✅✅✅

---

### `rt_type_registry.c`

#### `ensure_cap(arr, needed) → void`
- **Robustness:** ✅ — `realloc` checked
- **Security:** ✅
- **Optimization:** ✅

#### `rt_register_class_entry(name, size, vtable) → int`
- **Robustness:** ⚠️ WARNING — Calls `ensure_cap` then uses `set_classes(arr)` which stores the pre-realloc pointer; stale pointer if realloc moves allocation
- **Security:** ✅
- **Optimization:** ✅

#### `rt_register_interface(name) → int`, `rt_bind_interface(class_id, iface_id) → void`
- **Robustness:** ⚠️ WARNING — Same stale-pointer-after-realloc pattern
- **Security:** ✅
- **Optimization:** ✅

#### `rt_type_is_a(obj, class_id) → int`, `rt_type_implements(obj, iface_id) → int`
- **Robustness:** ⚠️ WARNING — No cycle detection in inheritance chain; cyclic inheritance → infinite loop
- **Security:** ✅
- **Optimization:** ✅

---

### `rt_string.c`

#### `rt_string_alloc(len) → rt_string*`
- ✅✅✅ — traps on OOM via `rt_obj_new_i64`

#### `rt_string_from_cstr(s) → rt_string*`, `rt_string_from_buf(buf, len) → rt_string*`
- ✅✅✅

#### `rt_string_concat(a, b) → rt_string*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Allocates fresh string every call; callers building strings in a loop should use `rt_string_builder`

#### `rt_string_eq(a, b) → int`, `rt_string_cmp(a, b) → int`
- ✅✅✅

#### `rt_string_ref(s) → rt_string*`, `rt_string_unref(s) → void`
- ✅✅✅

---

### `rt_string_ops.c`

#### `rt_str_split(s, delim) → rt_seq*`
- **Robustness:** ⚠️ WARNING — `rt_string_alloc` result not checked before write (traps on OOM, consistent but abrupt)
- **Security:** ✅
- **Optimization:** ✅

#### `rt_str_jaro(a, b) → double`
- **Robustness:** ⚠️ WARNING — Inner loop `while (!b_matched[k])` lacks `k < blen` guard → potential OOB read when no matching character exists
- **Security:** ✅
- **Optimization:** ✅

#### `rt_str_trim`, `rt_str_upper`, `rt_str_lower`, `rt_str_replace`, `rt_str_starts_with`, `rt_str_ends_with`, `rt_str_has`, `rt_str_index_of`, `rt_str_repeat`, `rt_str_pad_left`, `rt_str_pad_right`
- ✅✅✅ — Standard operations, no significant issues

---

### `rt_string_builder.c`

#### `rt_sb_grow(sb, min_cap) → void`
- **Robustness:** ⚠️ WARNING — Has a dead-code guard after an `if (new_cap <= sb->cap)` check; realloc failure not handled — traps are acceptable per convention but not explicit
- **Security:** ✅
- **Optimization:** ✅

#### `rt_sb_new() → rt_string_builder*`, `rt_sb_append(sb, s) → void`, `rt_sb_append_cstr(sb, s) → void`, `rt_sb_append_char(sb, c) → void`, `rt_sb_build(sb) → rt_string*`, `rt_sb_free(sb) → void`
- ✅✅✅

---

### `rt_seq.c`

#### `seq_ensure_capacity(seq, needed) → void`
- **Robustness:** ⚠️ WARNING — If `seq->cap` is 0 and `needed` is 0, the doubling loop `while (cap < needed)` never exits → infinite loop. Guard with `if (needed == 0) return;`
- **Security:** ✅
- **Optimization:** ✅

#### `rt_seq_new() → rt_seq*`, `rt_seq_push(seq, obj) → void`, `rt_seq_pop(seq) → rt_obj*`, `rt_seq_get(seq, i) → rt_obj*`, `rt_seq_set(seq, i, obj) → void`, `rt_seq_len(seq) → int`, `rt_seq_free(seq) → void`
- ✅✅✅

---

### `rt_list.c`

#### `rt_list_new() → rt_list*`, `rt_list_push(list, obj) → void`, `rt_list_pop(list) → rt_obj*`
- ✅✅✅ — Delegates to seq; clean

#### `rt_list_insert(list, i, obj) → void`
- **Robustness:** ⚠️ WARNING — `i < 0` or `i > len` not validated; out-of-range shifts corrupt list
- **Security:** ✅
- **Optimization:** ✅

#### `rt_list_remove(list, i) → void`
- **Robustness:** ⚠️ WARNING — Same bounds validation missing
- **Security:** ✅
- **Optimization:** ✅

#### `rt_list_sort(list, cmp) → void`, `rt_list_reverse(list) → void`, `rt_list_has(list, obj) → int`
- ✅✅✅

---

### `rt_map.c`

#### `rt_map_new() → rt_map*`
- **Robustness:** ⚠️ WARNING — `calloc` for initial buckets not checked
- **Security:** ✅
- **Optimization:** ✅

#### `map_resize(map) → void`
- **Robustness:** ⚠️ WARNING — `calloc` for new bucket array not checked → NULL deref when inserting
- **Security:** ✅
- **Optimization:** ✅

#### `rt_map_set(map, key, val) → void`, `rt_map_get(map, key) → rt_obj*`, `rt_map_remove(map, key) → void`, `rt_map_keys(map) → rt_seq*`, `rt_map_values(map) → rt_seq*`, `rt_map_len(map) → int`
- ✅✅✅ — Assuming `map_resize` fixed

---

### `rt_concmap.c`

#### `free_entry(e) → void`
- **Robustness:** ❌ CRITICAL — Calls `rt_obj_release_check0(e->val)` but NEVER calls `rt_obj_free` on non-zero return → every map entry removal leaks the stored value
- **Security:** ✅
- **Optimization:** ✅

#### `rt_concmap_set(map, key, val) → void`
- **Robustness:** ❌ CRITICAL — Same `rt_obj_free` omission when evicting an existing entry
- **Security:** ✅
- **Optimization:** ✅

#### `cm_clear_unlocked(map) → void`
- **Robustness:** ❌ CRITICAL — Same bug; clearing the entire map leaks all values
- **Security:** ✅
- **Optimization:** ✅

#### `rt_concmap_remove(map, key) → void`
- **Robustness:** ❌ CRITICAL — Same missing `rt_obj_free`
- **Security:** ✅
- **Optimization:** ✅

#### `rt_concmap_new(buckets) → rt_concmap*`, `rt_concmap_get(map, key) → rt_obj*`, `rt_concmap_len(map) → int`
- ✅✅✅

---

### `rt_concqueue.c`

#### `rt_concqueue_enqueue(q, obj) → void`
- **Robustness:** ❌ CRITICAL — `malloc` for new node not checked → NULL dereference crash on OOM
- **Security:** ✅
- **Optimization:** ✅

#### `rt_concqueue_dequeue(q) → rt_obj*`, `rt_concqueue_drain(q) → rt_seq*`
- **Robustness:** ❌ CRITICAL — Same `rt_obj_free` missing pattern as concmap; dequeued items' refcount leaks
- **Security:** ✅
- **Optimization:** ✅

#### `rt_concqueue_new() → rt_concqueue*`, `rt_concqueue_len(q) → int`
- ✅✅✅

---

### `rt_parallel.c`

#### All `rt_parallel_*_pool` functions
- **Robustness:** ❌ CRITICAL — Two independent bugs: (1) Tasks submitted before main thread re-acquires mutex → missed-signal deadlock; (2) `pthread_mutex_t`/`pthread_cond_t` allocated on stack, used by pool threads after function returns → use-after-stack-free
- **Security:** ✅
- **Optimization:** ✅

#### `rt_parallel_map(seq, fn) → rt_seq*`, `rt_parallel_filter(seq, fn) → rt_seq*`, `rt_parallel_foreach(seq, fn) → void`
- ✅✅✅ — Thread-per-item, no shared mutable state issues

---

### `rt_monitor.c`

#### `ensure_table_cs_init()` (Windows)
- **Robustness:** ❌ CRITICAL — Guard variable checked and set without atomics → data race on concurrent first calls → CRITICAL_SECTION initialized multiple times
- **Security:** ✅
- **Optimization:** ✅

#### `rt_monitor_new`, `rt_monitor_lock`, `rt_monitor_unlock`, `rt_monitor_wait`, `rt_monitor_notify`, `rt_monitor_notify_all`
- ✅✅✅ — pthread implementation correct

---

### `rt_async.c`

#### `async_any_entry(arg) → void*`
- **Robustness:** ❌ CRITICAL — Spin-polls `futures[i].done` without a sleep or condition variable; if any future never resolves, this thread spins forever consuming 100% CPU → thread leak on abandoned futures
- **Security:** ✅
- **Optimization:** ❌ CRITICAL — Busy-wait instead of condition variable is architecturally wrong

#### `rt_async_new(fn, arg) → rt_future*`, `rt_async_await(fut) → rt_obj*`, `rt_async_all(futs, n) → rt_future*`, `rt_async_any(futs, n) → rt_future*`
- ✅✅ robustness (except noted) / security

---

### `rt_scheduler.c`

#### `rt_scheduler_poll(sched, now_ms) → rt_seq*`
- **Robustness:** ❌ CRITICAL — `rt_string_unref(e->name)` called after `rt_seq_push(result, e->name)` → seq holds reference to freed string → use-after-free on caller access
- **Security:** ✅
- **Optimization:** ✅

#### `rt_scheduler_new`, `rt_scheduler_add`, `rt_scheduler_remove`, `rt_scheduler_free`
- ✅✅✅

---

### `rt_cancellation.c`

#### `rt_cancellation_linked(parent) → rt_cancellation*`
- **Robustness:** ⚠️ WARNING — Retained `parent` reference not released in the finalizer → refcount leak; parent never freed if child outlives it
- **Security:** ✅
- **Optimization:** ✅

#### `rt_cancellation_new`, `rt_cancellation_cancel`, `rt_cancellation_is_cancelled`, `rt_cancellation_register`, `rt_cancellation_unregister`
- ✅✅✅

---

### `rt_network_http.c`

#### `do_http_request(url, method, headers, body, config) → rt_http_response*`
- **Robustness:** ⚠️ — TLS config not freed on all error paths
- **Security:** ❌ CRITICAL — `tls_config.verify_cert = 0` set unconditionally before every request; all HTTPS traffic is MITM-able regardless of user config
- **Optimization:** ✅

#### `add_header(buf, name, value) → void`
- **Robustness:** ✅
- **Security:** ❌ CRITICAL — No CR/LF (`\r\n`) validation in `name` or `value` → HTTP response splitting / header injection
- **Optimization:** ✅

#### `build_request(method, url, headers, body) → rt_string*`
- **Robustness:** ❌ CRITICAL — Body bytes written to a fixed-offset position in the assembled buffer; if header growth differs from estimate, body bytes are silently misaligned (architecturally broken)
- **Security:** ❌ CRITICAL — Inherits header injection from `add_header`
- **Optimization:** ✅

#### `read_body_fixed(conn, content_length) → rt_string*`, `read_body_chunked(conn) → rt_string*`
- **Robustness:** ❌ CRITICAL — No maximum body size; `content_length` or sum of chunk sizes is server-controlled → DoS via `malloc(4GB)`
- **Security:** ❌ CRITICAL — Same DoS via allocation size
- **Optimization:** ✅

#### `rt_http_get(url) → rt_http_response*`, `rt_http_post(url, body) → rt_http_response*`, `rt_http_request(...)`, `rt_http_response_free(...)`
- ✅✅ (inherit issues from above)

---

### `rt_websocket.c`

#### `ws_recv_frame(conn) → ws_frame`
- **Robustness:** ❌ CRITICAL — No maximum payload length; `payload_len` from frame header directly used in `malloc` → server can cause `malloc(2^63)` → OOM crash
- **Security:** ❌ CRITICAL — Same DoS attack
- **Optimization:** ✅

#### `ws_handshake(conn, url, host) → int`
- **Robustness:** ⚠️ WARNING — `Sec-WebSocket-Accept` value not validated against expected SHA-1 hash → accepts invalid WebSocket upgrades
- **Security:** ✅
- **Optimization:** ✅

#### `rt_ws_recv_bytes(ws) → rt_bytes*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Element-by-element copy into `rt_bytes` instead of `memcpy`; for large messages this is needlessly slow

#### `rt_ws_connect`, `rt_ws_send_text`, `rt_ws_send_bytes`, `rt_ws_close`
- ✅✅✅

---

### `rt_network.c`

#### `wait_socket(fd, timeout_ms, for_read) → int`
- **Robustness:** ⚠️ WARNING — Uses `select()` with `FD_SET`; for `fd >= FD_SETSIZE` (1024 on most platforms) this is undefined behavior. Use `poll()` instead.
- **Security:** ✅
- **Optimization:** ✅

#### `rt_tcp_recv_line(conn, max_len) → rt_string*`
- **Robustness:** ⚠️ WARNING — `max_len` parameter exists but line growth is unbounded in some paths; one `recv()` syscall per byte is also catastrophically slow
- **Security:** ✅
- **Optimization:** ⚠️ — Byte-by-byte recv; should use buffered read

#### `rt_tcp_connect`, `rt_tcp_send`, `rt_tcp_recv`, `rt_tcp_close`, `rt_udp_send`, `rt_udp_recv`, `rt_dns_resolve`
- ✅✅✅

---

### `rt_restclient.c`

#### `rt_restclient_new() → rt_restclient*`
- **Robustness:** ⚠️ WARNING — `rt_obj_new_i64` return not checked before `memset`; though `rt_obj_new_i64` traps, `memset(NULL, ...)` would be the symptom
- **Security:** ✅
- **Optimization:** ✅

#### `rt_restclient_set_header(rc, name, value) → void`, `rt_restclient_set_auth_bearer(rc, token) → void`
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — No CR/LF validation on `name` or `value` → HTTP header injection (same pattern as `rt_network_http.c`)
- **Optimization:** ✅

#### `rt_restclient_get`, `rt_restclient_post`, `rt_restclient_put`, `rt_restclient_delete`, `rt_restclient_free`
- ✅✅✅

---

### `rt_dir.c`

#### `rt_dir_remove_all(path) → int`
- **Robustness:** ✅ — recursive removal works for normal trees
- **Security:** ❌ CRITICAL — Uses `stat()` (follows symlinks) before deciding to recurse; attacker can replace a directory with a symlink between `stat` and `opendir` → deletion of out-of-tree files. Use `lstat()` and `openat()`/`unlinkat()`.
- **Optimization:** ✅

#### `rt_dir_make_all(path) → int`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Calls `stat()` before each `mkdir()` (two syscalls per level); just call `mkdir()` and handle `EEXIST`

#### `rt_dir_exists`, `rt_dir_list`, `rt_dir_make`, `rt_dir_remove`, `rt_dir_cwd`, `rt_dir_change`
- ✅✅✅

---

### `rt_compress.c`

#### `out_ensure(state, extra) → void`
- **Robustness:** ✅ — grows output buffer
- **Security:** ❌ CRITICAL — No maximum output size; decompression bomb (e.g., 1 MB compressed → 1 TB output) exhausts memory
- **Optimization:** ✅

#### `inflate_huffman` / `inflate_data` / `gunzip_data`
- **Robustness:** ❌ CRITICAL — Same unbounded output growth
- **Security:** ❌ CRITICAL — DoS via decompression bomb
- **Optimization:** ✅

#### `build_huffman_tree(lengths, n_syms) → HuffTree`
- **Robustness:** ❌ CRITICAL — Codes >9 bits silently treated as invalid → rejects valid DEFLATE streams; many real compressors generate codes up to 15 bits
- **Security:** ✅
- **Optimization:** ✅

#### `decode_symbol(tree, state) → int`
- **Robustness:** ❌ CRITICAL — Same 9-bit limit
- **Security:** ✅
- **Optimization:** ✅

#### `init_fixed_trees() → void`
- **Robustness:** ⚠️ WARNING — `static int done` guard not atomic → data race on concurrent first calls (same pattern as `rt_crc32_init`)
- **Security:** ✅
- **Optimization:** ✅

#### `lz77_init(state) → void`
- **Robustness:** ⚠️ WARNING — `malloc` for LZ77 window not checked → NULL deref on first use
- **Security:** ✅
- **Optimization:** ✅

#### `rt_compress_deflate`, `rt_decompress_inflate`, `rt_compress_gzip`, `rt_decompress_gunzip`
- ✅✅ — wrap above; inherit criticals

---

### `rt_tempfile.c`

#### `rt_tempfile_create() → rt_string*`, `rt_tempdir_create() → rt_string*`
- **Robustness:** ⚠️ WARNING — `malloc` for path buffer not checked
- **Security:** ❌ CRITICAL — Name constructed from `PID + time(NULL)` → predictable; `open(path, O_CREAT|O_WRONLY)` is non-atomic → TOCTOU symlink race. Use `mkstemp(3)` / `mkdtemp(3)`.
- **Optimization:** ✅

#### `rt_tempfile_delete(path) → void`
- ✅✅✅

---

### `rt_file_ext.c` (extended file I/O)

#### `rt_file_write_bytes(path, data, len) → void`
- **Robustness:** ✅ — opens, writes, closes
- **Security:** ✅
- **Optimization:** ❌ CRITICAL — Issues one `write()` syscall per byte. For a 1 MB binary file this is 1,048,576 syscalls. Use `fwrite()` or a single `write(fd, data, len)`.

#### `rt_io_file_read_all_bytes(path) → rt_bytes*`, `rt_file_read_bytes(path) → rt_bytes*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ❌ CRITICAL — Byte-by-byte copy into `rt_bytes` instead of `memcpy` after reading full buffer

#### `rt_io_file_write_all_text(path, text) → void`
- **Robustness:** ⚠️ WARNING — Silent on all failures (fopen fail, write fail); caller has no way to detect errors
- **Security:** ✅
- **Optimization:** ✅

#### `rt_file_move(src, dst) → int`
- **Robustness:** ⚠️ WARNING — If `rename()` fails (cross-device), falls back to copy+delete; if copy succeeds but delete fails, no error is returned AND source is deleted → data loss on some failure paths
- **Security:** ✅
- **Optimization:** ✅

---

### `rt_watcher.c`

#### `watcher_read_inotify_events(watcher) → void`
- **Robustness:** ⚠️ WARNING — `read()` return value not validated before accessing `event->len`; partial read produces garbage offset arithmetic
- **Security:** ✅
- **Optimization:** ✅

#### `rt_watcher_new`, `rt_watcher_add`, `rt_watcher_remove`, `rt_watcher_poll`, `rt_watcher_free`
- ✅✅✅

---

### `rt_binfile.c`

#### `rt_binfile_seek(bf, offset, whence) → void`
- **Robustness:** ⚠️ WARNING — `fseek` takes `long`; on platforms where `long` is 32 bits, offsets >2 GB are silently truncated. Use `fseeko` with `off_t`.
- **Security:** ✅
- **Optimization:** ✅

#### `rt_binfile_open`, `rt_binfile_read_i8/i16/i32/i64/f32/f64`, `rt_binfile_write_*`, `rt_binfile_close`
- ✅✅✅

---

### `rt_linereader.c`

#### `rt_linereader_read(lr) → rt_string*`
- **Robustness:** ⚠️ WARNING — No maximum line length; malicious peer sends infinite data without newline → unbounded memory allocation
- **Security:** ✅
- **Optimization:** ✅

#### `rt_linereader_new`, `rt_linereader_free`
- ✅✅✅

---

### `rt_path.c`

#### `rt_path_norm(path) → rt_string*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Allocates `len * sizeof(size_t)` for component-offset arrays; for a 4096-byte path this is ~32 KB. Use a stack-allocated small array with heap fallback.

#### `rt_path_join`, `rt_path_basename`, `rt_path_dirname`, `rt_path_ext`, `rt_path_is_abs`, `rt_path_exists`
- ✅✅✅

---

### `rt_bloomfilter.c`

#### `rt_bloomfilter_new(capacity, fp_rate) → rt_bloomfilter*`
- **Robustness:** ❌ CRITICAL — `calloc` for `bf->bits` not checked; `NULL` stored silently → crash on first `rt_bloomfilter_add` or `rt_bloomfilter_might_contain`
- **Security:** ✅
- **Optimization:** ✅

#### `rt_bloomfilter_add(bf, key) → void`, `rt_bloomfilter_might_contain(bf, key) → int`, `rt_bloomfilter_free(bf) → void`
- ✅✅✅ (assuming allocation fixed)

---

### `rt_defaultmap.c`

#### `dm_resize(dm) → void`
- **Robustness:** ❌ CRITICAL — `calloc` for new bucket array not checked → NULL deref on next insert
- **Security:** ✅
- **Optimization:** ✅

#### `rt_defaultmap_new(default_fn) → rt_defaultmap*`
- **Robustness:** ❌ CRITICAL — `calloc` for initial buckets not checked
- **Security:** ✅
- **Optimization:** ✅

#### `rt_defaultmap_get`, `rt_defaultmap_set`, `rt_defaultmap_remove`, `rt_defaultmap_free`
- ✅✅✅ (assuming allocation fixed)

---

### `rt_sortedset.c`

#### `ensure_capacity(set, needed) → void`
- **Robustness:** ❌ CRITICAL — `realloc` result assigned directly to `set->data`; on failure `set->data` becomes NULL (old pointer lost) → double free + subsequent OOB write
- **Security:** ✅
- **Optimization:** ✅

#### `rt_sortedset_new() → rt_sortedset*`
- **Robustness:** ❌ CRITICAL — No GC finalizer registered → `set->data` (raw malloc'd array) leaks when GC collects the object
- **Security:** ✅
- **Optimization:** ✅

#### `rt_sortedset_add`, `rt_sortedset_remove`, `rt_sortedset_has`, `rt_sortedset_len`, `rt_sortedset_to_seq`
- ✅✅✅ (assuming above fixed)

---

### `rt_trie.c`

#### `collect_keys(node, buf, depth, results) → void`
- **Robustness:** ❌ CRITICAL — `buf` is a fixed `char[4096]` on caller's stack, passed by pointer into recursive calls; keys >4095 chars write beyond the buffer → stack overflow / buffer overrun
- **Security:** ❌ — Same overflow exploitable for controlled writes
- **Optimization:** ✅

#### `rt_trie_keys(trie) → rt_seq*`, `rt_trie_with_prefix(trie, prefix) → rt_seq*`
- **Robustness:** ❌ CRITICAL — Both call `collect_keys`; inherit the stack overflow
- **Security:** ❌
- **Optimization:** ✅

#### `rt_trie_new`, `rt_trie_insert`, `rt_trie_lookup`, `rt_trie_remove`, `rt_trie_free`
- ✅✅✅

---

### `rt_duration.c`

#### `rt_duration_abs(d) → rt_duration`
- **Robustness:** ❌ CRITICAL — `if (d.ns < 0) return (rt_duration){.ns = -d.ns}` — `-INT64_MIN` is undefined behavior in C; result is unpredictable on all platforms
- **Security:** ✅
- **Optimization:** ✅

#### `rt_duration_neg(d) → rt_duration`
- **Robustness:** ❌ CRITICAL — Same `-INT64_MIN` UB
- **Security:** ✅
- **Optimization:** ✅

#### `rt_duration_add(a, b) → rt_duration`, `rt_duration_sub(a, b) → rt_duration`, `rt_duration_mul(d, factor) → rt_duration`
- **Robustness:** ⚠️ WARNING — No overflow checking on i64 arithmetic; saturate or return error on overflow
- **Security:** ✅
- **Optimization:** ✅

#### `rt_duration_from_ms`, `rt_duration_from_us`, `rt_duration_to_ms`, `rt_duration_to_us`, `rt_duration_cmp`, `rt_duration_to_str`
- ✅✅✅

---

### `rt_dateonly.c`

#### `rt_dateonly_format(d, fmt) → rt_string*`
- **Robustness:** ❌ CRITICAL — `snprintf(buf, sizeof(buf), ...)` where `buf` is `char[255]`; individual format token outputs (e.g., full weekday name + separator repeated) can exceed 255 bytes → stack buffer overrun
- **Security:** ✅ (no user-controlled format string)
- **Optimization:** ✅

#### `rt_dateonly_today() → rt_dateonly`
- **Robustness:** ⚠️ WARNING — Uses non-thread-safe `localtime()` (returns pointer to static buffer); use `localtime_r()`
- **Security:** ✅
- **Optimization:** ✅

#### `rt_dateonly_new`, `rt_dateonly_from_str`, `rt_dateonly_add_days`, `rt_dateonly_diff_days`, `rt_dateonly_cmp`
- ✅✅✅

---

### `rt_bag.c`

#### `rt_bag_new() → rt_bag*`
- **Robustness:** ⚠️ WARNING — `calloc` for initial storage not checked
- **Security:** ✅ · **Optimization:** ✅

#### `bag_resize(bag) → void`
- **Robustness:** ⚠️ WARNING — `realloc` result not checked; lost pointer on failure
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_bag_*` functions — ✅✅✅

---

### `rt_bimap.c`

#### `rt_bimap_new() → rt_bimap*`, `bimap_resize(bm) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc` (same pattern)
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_bimap_*` — ✅✅✅

---

### `rt_bitset.c`

#### `bitset_grow(bs, needed) → void`
- **Robustness:** ⚠️ WARNING — `realloc` result assigned directly to `bs->words` → lost pointer on failure (same pattern as sortedset)
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_bitset_*` — ✅✅✅

---

### `rt_countmap.c`

#### `rt_countmap_new() → rt_countmap*`, `countmap_resize(cm) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_countmap_*` — ✅✅✅

---

### `rt_deque.c`

#### `rt_deque_new() → rt_deque*`, `deque_grow(dq) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_deque_*` — ✅✅✅

---

### `rt_orderedmap.c`

#### `rt_orderedmap_new() → rt_orderedmap*`, `orderedmap_resize(om) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_orderedmap_*` — ✅✅✅

---

### `rt_pqueue.c`

#### `rt_pqueue_new() → rt_pqueue*`, `pqueue_grow(pq) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_pqueue_*` — ✅✅✅

---

### `rt_set.c`

#### `rt_set_new() → rt_set*`, `set_resize(s) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_set_*` — ✅✅✅

---

### `rt_sparsearray.c`

#### `rt_sparsearray_new(capacity) → rt_sparsearray*`
- **Robustness:** ⚠️ WARNING — `calloc` for dense/sparse arrays not checked
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_sparsearray_*` — ✅✅✅

---

### `rt_stack.c`

#### `rt_stack_new() → rt_stack*`, `stack_grow(st) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_stack_*` — ✅✅✅

---

### `rt_unionfind.c`

#### `rt_unionfind_new(n) → rt_unionfind*`
- **Robustness:** ⚠️ WARNING — `malloc` for `parent`/`rank` arrays not checked
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_unionfind_*` — ✅✅✅

---

### `rt_weakmap.c`

#### `rt_weakmap_new() → rt_weakmap*`, `weakmap_resize(wm) → void`
- **Robustness:** ⚠️ WARNING — Unchecked `calloc`/`realloc`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_weakmap_*` — ✅✅✅

---

### `rt_treemap.c`

#### `rt_treemap_len(tm) → int`, `rt_treemap_is_empty(tm) → int`
- **Robustness:** ⚠️ WARNING — No NULL check on `tm` before dereference; returns garbage for NULL
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_treemap_*` — ✅✅✅

---

### `rt_diff.c`

#### `split_lines(text, n_out) → char**`
- **Robustness:** ⚠️ WARNING — `malloc` for pointer array not checked
- **Security:** ✅
- **Optimization:** ✅

#### `rt_diff_lines(a, b) → rt_seq*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — O(nm) LCS space; for large files use Myers O(nd) or patience diff

#### `rt_diff_patch(original, diff) → rt_string*`
- **Robustness:** ⚠️ WARNING — `original` parameter declared but completely ignored in implementation; always patches from empty string
- **Security:** ✅
- **Optimization:** ✅

---

### `rt_msgbus.c`

#### `mb_ensure_topic(bus, topic) → topic_t*`, `rt_msgbus_new() → rt_msgbus*`, `rt_msgbus_subscribe(bus, topic, fn) → void`
- **Robustness:** ⚠️ WARNING — `calloc` for topic nodes and subscriber arrays not checked
- **Security:** ✅ · **Optimization:** ✅

#### `rt_msgbus_clear(bus) → void`
- **Robustness:** ⚠️ WARNING — Frees subscriber arrays but not topic name strings or topic nodes themselves → partial memory leak
- **Security:** ✅ · **Optimization:** ✅

#### `rt_msgbus_publish`, `rt_msgbus_unsubscribe`, `rt_msgbus_free` — ✅✅✅

---

### `rt_time.c`

#### `get_timestamp_ns()` (Windows, QPC path)
- **Robustness:** ⚠️ WARNING — `counter * 1,000,000,000 / freq`: the multiply overflows `uint64_t` after approximately 9.2 years of uptime. Use `(counter / freq) * NS + (counter % freq) * NS / freq`.
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_time_*` / `rt_timestamp_*` — ✅✅✅

---

### `rt_reltime.c`

#### `i64_abs(v) → int64_t` (internal helper)
- **Robustness:** ⚠️ WARNING — `-INT64_MIN` UB (same pattern as `rt_duration_abs`)
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_reltime_*` — ✅✅✅

---

### `rt_retry.c`

#### `rt_retry_next_delay(state) → int64_t`
- **Robustness:** ⚠️ WARNING — `delay *= 2` (i64 multiply) overflows before the `max_delay_ms` cap is applied; should clamp before multiply or use saturating arithmetic
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_retry_*` — ✅✅✅

---

### `rt_bloomfilter.c` — see §2.9 above

---

### `rt_graphics.c`

#### `rt_canvas_flip(canvas) → void`
- **Robustness:** ❌ CRITICAL — Calls `exit(0)` on display initialization failure; kills the entire process inside a library function. Should return an error code or call a user-registered error handler.
- **Security:** ✅
- **Optimization:** ✅

#### `rt_canvas_flood_fill(canvas, x, y, color) → void`
- **Robustness:** ✅ — functional
- **Security:** ✅
- **Optimization:** ❌ CRITICAL — Allocates a full `width × height` visited bitmap up front; for a 4K canvas (3840×2160) this is 8.3M entries → 266 MB heap allocation per fill. Use a scanline-based stack algorithm instead.

#### `rt_canvas_arc(canvas, cx, cy, r, start, end, color) → void`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — O(r²) pixel tests; use Bresenham arc algorithm for O(r) performance

#### `rt_canvas_gradient_h(canvas, x, y, w, h, c1, c2) → void`, `rt_canvas_gradient_v(...) → void`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ~~⚠️ — One API call per row/column; should directly write to pixel buffer~~ **✅ FIXED 2026-02-23** — now uses `vgfx_get_framebuffer` + direct writes; horizontal gradient precomputes one row then `memcpy` per scanline.

#### `rt_canvas_ellipse(canvas, cx, cy, rx, ry, color) → void`
- **Robustness:** ⚠️ WARNING — `rx2 * ry2` intermediate product can overflow `int32_t` for radii >46340
- **Security:** ✅ · **Optimization:** ✅

#### `rt_canvas_polygon(canvas, pts, n, color) → void`
- **Robustness:** ⚠️ WARNING — Fixed `intersections[64]` array; polygons with >62 edges per scanline produce wrong fill (intersections overflow array silently)
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_canvas_*` — ✅✅✅

---

### `rt_scene.c`

#### `rt_scene_draw(scene, canvas) → void`
- **Robustness:** ❌ CRITICAL — `rt_seq* nodes = rt_seq_new()` created every frame inside draw; `rt_seq_free` never called → unbounded per-frame leak
- **Security:** ✅ · **Optimization:** ✅

#### `mark_transform_dirty(node) → void`
- **Robustness:** ⚠️ WARNING — Recursive without depth limit; scene graph depth >~10,000 nodes → stack overflow
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_scene_*` — ✅✅✅

---

### `rt_spritebatch.c`

#### `ensure_capacity(batch, needed) → void`
- **Robustness:** ❌ CRITICAL — On OOM: calls `rt_trap()` but does NOT update `batch->items` before returning (if trap returns); subsequent use of stale `batch->items` pointer is use-after-free
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_spritebatch_*` — ✅✅✅

---

### `rt_spritesheet.c`

#### `ensure_cap(ss) → void`
- **Robustness:** ~~❌ CRITICAL — Performs two separate `realloc` calls for `ss->frames` and `ss->rects`; if the second fails, `ss->cap` has already been updated to reflect the first → corrupted inconsistent state~~ **✅ FIXED 2026-02-23** — replaced with malloc+memcpy so partial failure can be fully rolled back.
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_spritesheet_*` — ✅✅✅

---

### `rt_tilemap.c`

#### `rt_tilemap_collide_body(tilemap, body) → rt_seq*`
- **Robustness:** ❌ CRITICAL — Casts `body` to a locally-defined `RigidBody_*` struct with hardcoded byte offsets; if the actual runtime RigidBody layout differs (padding, version), all offset reads produce garbage
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_tilemap_*` — ✅✅✅

---

### `rt_fmt.c`

#### `rt_fmt_to_words(value) → rt_string*`
- **Robustness:** ❌ CRITICAL — `value = -value` for `INT64_MIN` is signed overflow UB. Use unsigned arithmetic for the absolute value.
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_fmt_*` — ✅✅✅

---

### `rt_pixels.c`

#### `rt_pixels_resize(pixels, new_w, new_h) → rt_pixels*`
- **Robustness:** ❌ CRITICAL — Bilinear sampling reads `src[(sy+1)*src_w + sx+1]` when `src_w == 1` → OOB read past allocation
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_pixels_*` — ✅✅✅

---

### `rt_action.c`

#### `rt_action_load(path) → rt_action_map*`
- **Robustness:** ❌ CRITICAL — (1) Malformed JSON where a string token is unterminated causes the parser to spin in an infinite loop. (2) Loaded key codes stored directly as integers without range-checking against valid key code table → OOB table lookup.
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_action_*` — ✅✅✅

---

### `rt_guid.c`

#### `get_random_bytes(buf, n) → void`
- **Robustness:** ⚠️ WARNING — POSIX path: partial `read()` from `/dev/urandom` not retried → GUIDs with zero bytes. Windows path: `CryptGenRandom` return not checked.
- **Security:** ✅ · **Optimization:** ✅

#### `rt_guid_new() → rt_guid`, `rt_guid_to_str(g) → rt_string*`, `rt_guid_from_str(s) → rt_guid`
- ✅✅✅

---

### `rt_audio.c`

#### All `rt_audio_*` functions
- **Robustness:** ⚠️ WARNING — Global `g_audio_ctx` accessed by init, play, and stop functions without a lock; concurrent initialization race
- **Security:** ✅ · **Optimization:** ✅

---

### `rt_screenfx.c`

#### All screen effect functions using `screenfx_rand_state`
- **Robustness:** ⚠️ WARNING — `screenfx_rand_state` is a global; concurrent calls from multiple render threads race on the PRNG state
- **Security:** ✅ · **Optimization:** ✅

---

### `rt_serialize.c`

#### All `rt_serialize_*` / `rt_deserialize_*` functions
- **Robustness:** ⚠️ WARNING — `g_last_error` is a `static char[]` global (not thread-local); concurrent serialization from multiple threads races on the error string
- **Security:** ✅ · **Optimization:** ✅

---

### `rt_numfmt.c`

#### `rt_numfmt_pad(n, width, pad_char) → rt_string*`
- **Robustness:** ⚠️ WARNING — `-n` for `n = INT64_MIN` is UB; also uses `%llu` which is not portable across MSVC/GCC for `int64_t`
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_numfmt_*` — ✅✅✅

---

### `rt_version.c`

#### `cmp_prerelease(a, b) → int`
- **Robustness:** ⚠️ WARNING — Manual decimal parse loop can accumulate `long long` overflow for unreasonably long numeric identifiers
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_version_*` — ✅✅✅

---

### `rt_regex.c`

#### `find_match(pattern, text, start) → match_t`, `match_quant(...)`, `match_concat_from(...)`, `collect_quant_positions(...)`
- **Robustness:** ❌ CRITICAL — Backtracking NFA engine with no step counter, timeout, or recursion depth limit → ReDoS: crafted input like `(a+)+b` against `aaaa...` causes exponential backtracking
- **Security:** ❌ CRITICAL — Same; any network-accessible regex match is a DoS surface
- **Optimization:** ❌ CRITICAL — O(2^n) worst-case; convert to NFA simulation (Thompson construction) for O(nm) guarantee

#### `pattern_cache[]` (global array)
- **Robustness:** ❌ CRITICAL — Global array accessed without mutex; concurrent `rt_pattern_compile` calls race → double-free or corrupted cache entries
- **Security:** ❌ — Same race allows cache poisoning
- **Optimization:** ✅

#### `class_add_shorthand(cls, sc, negated) → void`
- **Robustness:** ⚠️ WARNING — `\D`/`\W`/`\S` with `negated=1` incorrectly inverts individual ranges rather than the whole class → wrong character matching
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_pattern_*` functions — ✅✅ (inherit ReDoS risk)

---

### `rt_compiled_pattern.c`

#### All `rt_compiled_pattern_*` functions
- **Robustness:** ❌ CRITICAL — Inherits ReDoS from `rt_regex.c` backtracking engine
- **Security:** ❌ — Same DoS surface
- **Optimization:** ✅ — Avoids global cache race (compiled objects are per-instance)

---

### `rt_markdown.c`

#### `process_inline(text, output) → void`
- **Robustness:** ✅
- **Security:** ❌ CRITICAL — Link URLs written to output as `<a href="URL">` without sanitization; `[click](javascript:alert(1))` produces valid XSS payload. Validate scheme (allow only `http`, `https`, `mailto`, `ftp`).
- **Optimization:** ✅

#### `rt_markdown_to_html(md) → rt_string*`
- **Robustness:** ✅
- **Security:** ❌ CRITICAL — Inherits XSS from `process_inline`
- **Optimization:** ✅

#### All other `rt_markdown_*` — ✅✅✅

---

### `rt_toml.c`

#### `rt_toml_is_valid(text) → int`
- **Robustness:** ❌ CRITICAL — Always returns `1` (true) regardless of input; `rt_toml_parse` returns a non-NULL map on failure, so the null-check is always false
- **Security:** ✅ · **Optimization:** ✅

#### `rt_toml_get(root, key) → rt_obj*`, `rt_toml_get_str(root, key) → rt_string*`
- **Robustness:** ❌ CRITICAL — `*(uint64_t *)root` raw type-punning without alignment guarantee → undefined behavior on strict-alignment platforms
- **Security:** ✅ · **Optimization:** ✅

#### `rt_toml_parse(text) → rt_map*`
- **Robustness:** ⚠️ WARNING — No CRLF handling (stray `\r` left in values); no escape sequence processing; no multiline strings; no inline tables; no error reporting to caller
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_toml_*` — ✅✅✅

---

### `rt_json.c`

#### `parse_value(ctx) → rt_obj*` (recursive)
- **Robustness:** ❌ CRITICAL — No recursion depth limit; `{"a":{"a":{"a":...}}}` 10,000 levels deep causes stack overflow
- **Security:** ❌ CRITICAL — Remotely triggerable DoS
- **Optimization:** ✅

#### `format_value(obj) → rt_string*`
- **Robustness:** ⚠️ WARNING — Type detection based on heap allocation size is fragile; collisions possible if two types have identical sizes
- **Security:** ✅ · **Optimization:** ✅

#### `rt_json_type_of(obj) → rt_json_type`
- **Robustness:** ⚠️ WARNING — Same fragile size-based detection
- **Security:** ✅ · **Optimization:** ✅

#### `parse_string(ctx) → rt_string*`
- **Robustness:** ⚠️ WARNING — `\uXXXX` escape sequences: surrogate pairs (`\uD800`–`\uDFFF`) not paired → produces invalid UTF-8 output
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_json_*` — ✅✅✅ (assuming depth-check added)

---

### `rt_xml.c`

#### `parse_element(ctx) → rt_xml_node*` (recursive)
- **Robustness:** ❌ CRITICAL — No recursion depth limit → stack-overflow DoS for deeply nested XML
- **Security:** ❌ CRITICAL — Remotely triggerable
- **Optimization:** ✅

#### `format_element(node, depth) → rt_string*` (recursive)
- **Robustness:** ❌ CRITICAL — Same; deeply nested output triggers stack overflow
- **Security:** ❌ · **Optimization:** ✅

#### `find_all_recursive(node, tag, results) → void`
- **Robustness:** ❌ CRITICAL — Same unbounded recursion
- **Security:** ❌ · **Optimization:** ✅

#### `rt_xml_text_content(node) → rt_string*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ❌ CRITICAL — O(n²) string concatenation across child text nodes; use `rt_string_builder`

#### `rt_xml_remove_at(node, index) → void`
- **Robustness:** ⚠️ WARNING — Potential double-release of child object during removal
- **Security:** ✅ · **Optimization:** ✅

#### XML error buffer (global static)
- **Robustness:** ⚠️ WARNING — Global `static char error_buf[]` not thread-local → concurrent parse errors race on error string
- **Security:** ✅ · **Optimization:** ✅

#### `rt_xml_parse`, `rt_xml_format`, `rt_xml_find`, `rt_xml_find_all`, `rt_xml_attr`, `rt_xml_set_attr`, `rt_xml_free`
- ✅✅ (inherit recursion depth issue)

---

### `rt_yaml.c`

#### `parse_value(ctx)`, `parse_block_sequence(ctx)`, `parse_block_mapping(ctx)` (all recursive)
- **Robustness:** ❌ CRITICAL — No recursion depth limit → stack-overflow DoS
- **Security:** ❌ CRITICAL — Remotely triggerable
- **Optimization:** ✅

#### `yaml_resize(ctx) → void`
- **Robustness:** ⚠️ WARNING — `realloc` failure causes NULL pointer write (lost-pointer pattern)
- **Security:** ✅ · **Optimization:** ✅

#### `g_yaml_error` (global)
- **Robustness:** ⚠️ WARNING — Not thread-local; concurrent parse errors race
- **Security:** ✅ · **Optimization:** ✅

#### `rt_yaml_is_valid(text) → int`
- **Robustness:** ⚠️ WARNING — Returns `1` for empty input (empty YAML is technically valid but callers expecting content may be surprised)
- **Security:** ✅ · **Optimization:** ✅

#### `rt_yaml_to_str` (emitter)
- **Robustness:** ⚠️ WARNING — `rt_map_keys(val)` result `rt_seq*` never freed → memory leak per map node emitted
- **Security:** ✅ · **Optimization:** ✅

---

### `rt_csv.c`

#### `csv_extract_string(obj) → const char*`
- **Robustness:** ⚠️ WARNING — Raw type-punning `*(uint64_t *)obj` same as TOML → UB on strict-alignment platforms
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_csv_*` — ✅✅✅

---

### `rt_scanner.c`

#### `rt_scanner_read_quoted(sc) → rt_string*`
- **Robustness:** ❌ CRITICAL — Accumulates characters into fixed `char buf[4096]`; strings >4095 characters silently truncate without error → data loss / misparse
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_scanner_*` — ✅✅✅

---

### `rt_bigint.c`

#### `bigint_alloc(n_digits) → rt_bigint*`
- **Robustness:** ⚠️ WARNING — `calloc` for digit array not checked; finalizer set before checking allocation success
- **Security:** ✅ · **Optimization:** ✅

#### `bigint_ensure_capacity(bi, n) → void`
- **Robustness:** ❌ CRITICAL — `bi->digits = realloc(bi->digits, ...)` — realloc failure returns NULL, assigned directly → old `digits` pointer lost → memory leak + subsequent NULL deref
- **Security:** ✅ · **Optimization:** ✅

#### `rt_bigint_to_str_base(bi, base) → rt_string*`
- **Robustness:** ❌ CRITICAL — Buffer size estimated as `ceil(n_digits * 3.32)` which is correct for base-10 but the buffer is allocated before the loop that writes backwards; if the loop writes more digits than estimated (can happen for large exponents in non-base-10), it writes before the allocation → OOB write
- **Security:** ✅ · **Optimization:** ✅

#### `rt_bigint_and(a, b) → rt_bigint*`, `rt_bigint_or(a, b) → rt_bigint*`, `rt_bigint_xor(a, b) → rt_bigint*`
- **Robustness:** ❌ CRITICAL — Returns zero bigint unconditionally when either operand is negative; two's-complement bitwise ops on negative bigints are semantically valid and commonly expected
- **Security:** ✅ · **Optimization:** ✅

#### `rt_bigint_from_str(text, base) → rt_bigint*`
- **Robustness:** ⚠️ WARNING — Stops at first non-digit character, silently treating partial input as valid; `"123abc"` returns `123`
- **Security:** ✅ · **Optimization:** ✅

#### `rt_bigint_mul(a, b) → rt_bigint*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — O(n²) grade-school multiplication; for large numbers (>64 digits) use Karatsuba

#### `rt_bigint_pow_mod(base, exp, mod) → rt_bigint*`
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Square-and-multiply is not constant-time; timing reveals bits of `exp` → side-channel for crypto (RSA, DH key generation)
- **Optimization:** ✅

#### `rt_bigint_add`, `rt_bigint_sub`, `rt_bigint_div`, `rt_bigint_mod`, `rt_bigint_cmp`, `rt_bigint_neg`, `rt_bigint_abs`, `rt_bigint_is_zero`, `rt_bigint_to_i64`
- ✅✅✅

---

### `rt_numeric_conv.c`

#### `rt_f64_to_i64(v) → int64_t`
- **Robustness:** ❌ CRITICAL — Casts `double` to `long long` without first checking that `v` is in `[INT64_MIN, INT64_MAX]`; out-of-range values produce C undefined behavior (implementation-defined on most, but UB per standard)
- **Security:** ✅ · **Optimization:** ✅

#### `rt_i64_to_f64(v) → double`, `rt_i64_to_f32(v) → float`, `rt_f64_to_f32(v) → float`, `rt_f32_to_f64(v) → double`
- ✅✅✅ — Standard widening/narrowing, no UB

---

### `rt_mat4.c`

#### `rt_mat4_perspective(fov, aspect, near, far) → rt_mat4*`
- **Robustness:** ❌ CRITICAL — No validation; `fov=0`, `aspect=0`, `near=far`, or `near<=0` silently produce NaN/Inf/degenerate matrices. Should assert or return error.
- **Security:** ✅ · **Optimization:** ✅

#### `rt_mat4_ortho(left, right, bottom, top, near, far) → rt_mat4*`
- **Robustness:** ❌ CRITICAL — `left==right`, `bottom==top`, or `near==far` produces division by zero → NaN matrix
- **Security:** ✅ · **Optimization:** ✅

#### `rt_mat4_inverse(m) → rt_mat4*`
- **Robustness:** ⚠️ WARNING — Returns identity matrix for singular input without signaling; callers cannot detect the error
- **Security:** ✅ · **Optimization:** ✅

#### `rt_mat4_new`, `rt_mat4_identity`, `rt_mat4_mul`, `rt_mat4_translate`, `rt_mat4_scale`, `rt_mat4_rotate_x/y/z`, `rt_mat4_transpose`, `rt_mat4_to_str`
- ✅✅✅

---

### `rt_mat3.c`

#### `rt_mat3_new(...)  → rt_mat3*`
- **Robustness:** ⚠️ WARNING — Returns NULL without trapping on OOM (inconsistent with vec* which traps); callers may not check
- **Security:** ✅ · **Optimization:** ✅

#### `rt_mat3_inverse(m) → rt_mat3*`
- **Robustness:** ⚠️ WARNING — Silent identity return for singular matrix (same as mat4)
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_mat3_*` — ✅✅✅

---

### `rt_rand.c` (cryptographic)

#### `rt_crypto_rand_int(min, max) → int64_t`
- **Robustness:** ⚠️ WARNING — For full INT64 range (`min=INT64_MIN, max=INT64_MAX`), the range arithmetic `(uint64_t)(max - min + 1)` overflows to 0 → division by zero
- **Security:** ✅ — Uses `/dev/urandom` correctly
- **Optimization:** ⚠️ — Opens `/dev/urandom` on every call; use `getrandom(2)` or keep fd open

---

### `rt_random.c` (non-cryptographic)

#### `rt_random_new(seed) → rt_random*`
- **Robustness:** ⚠️ WARNING — Seeds a global PRNG state, not per-instance; concurrent `rt_random_new` calls race on global seed
- **Security:** ✅ (not for crypto use)
- **Optimization:** ✅

#### `rt_rand_range(rng, min, max) → int64_t`
- **Robustness:** ⚠️ WARNING — `max - min + 1` can overflow for extreme `min`/`max` values
- **Security:** ✅
- **Optimization:** ✅

#### `rt_rand_int(rng, n) → int64_t`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Modulo bias for non-power-of-2 `n`; use rejection sampling for uniform distribution

---

### `rt_safe_i64.c`

#### `rt_safe_i64_add(cell, delta) → int`
- **Robustness:** ⚠️ WARNING — `cell->value += delta` without overflow check; POSIX path has no saturation; result is UB on overflow
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_safe_i64_*` — ✅✅✅

---

### `rt_perlin.c`

#### `rt_perlin_octave2d(p, x, y, octaves, persistence) → double`, `rt_perlin_octave3d(...)  → double`
- **Robustness:** ⚠️ WARNING — If `persistence >= 1.0`, amplitude accumulates → result diverges to Inf. No `octaves` upper bound → exponential runtime for large values.
- **Security:** ✅ · **Optimization:** ✅

#### All other `rt_perlin_*` — ✅✅✅

---

### `rt_file.c` (BASIC file channel management)

#### `rt_file_find_channel(channel) → RtFileChannelEntry*` (static)
- **Robustness:** ✅ — Negative channel rejected immediately
- **Security:** ✅
- **Optimization:** ⚠️ — O(n) linear scan over channel table; fine for BASIC programs with few channels, but a hash map would scale better

#### `rt_file_prepare_channel(channel) → RtFileChannelEntry*` (static)
- **Robustness:** ✅ — `realloc` return checked; new slots zero-initialized
- **Security:** ✅ — `new_capacity * sizeof(*new_entries)` could overflow for extreme capacities, but geometric growth from small base makes this unreachable in practice
- **Optimization:** ✅ — Geometric growth

#### `rt_open_err_vstr(path, mode, channel) → int32_t`
- **Robustness:** ✅ — Mode string, path, and channel validated; `in_use` guard prevents double-open
- **Security:** ⚠️ WARNING — Path passed directly to `rt_file_open` without `../` sanitization; out-of-sandbox traversal possible if path is user-controlled
- **Optimization:** ✅

#### `rt_file_channel_fd(channel, out_fd) → int32_t`
- **Robustness:** ✅ — Channel resolved; `out_fd` NULL-safe
- **Security:** ⚠️ WARNING — Exposes raw host file descriptor to callers; if scripts are sandboxed, this is a capability escape
- **Optimization:** ✅

#### `rt_file_state_cleanup`, `rt_close_err`, `rt_write_ch_err`, `rt_println_ch_err`, `rt_line_input_ch_err`, `rt_file_channel_get_eof`, `rt_file_channel_set_eof`
- ✅✅✅

---

### `rt_file_io.c` (POSIX file descriptor wrappers)

#### `rt_file_line_buffer_grow(buffer, cap, len, out_err) → bool`
- **Robustness:** ✅ — Checks `len == SIZE_MAX`, `*cap > SIZE_MAX/2`, new cap ≤ len; frees buffer on all failure paths
- **Security:** ✅
- **Optimization:** ✅ — Doubles capacity geometrically

#### `rt_file_open(file, path, mode, basic_mode, out_err) → int8_t`
- **Robustness:** ✅ — NULL checks on all pointer params; fd set to -1 on failure; errno zeroed before open
- **Security:** ⚠️ WARNING — File permissions `0666` (world-writable before umask); on systems with permissive umask (`0000`), created files are world-writable
- **Optimization:** ✅ — `O_CLOEXEC` applied to prevent fd leaks across exec

#### `rt_file_read_line(file, out_line, out_err) → int8_t`
- **Robustness:** ✅ — Dynamic buffer with overflow-checked growth; all failure paths free buffer at `cleanup:` label
- **Security:** ⚠️ WARNING — No line length limit; malicious file with no newlines exhausts memory
- **Optimization:** ⚠️ — Reads one byte per `read()` syscall; buffered I/O (fgets or userspace buffer) would be far faster

#### `rt_file_write(file, data, len, out_err) → int8_t`
- **Robustness:** ✅ — EINTR retry; zero-length write treated as error; short-write retry loop
- **Security:** ✅
- **Optimization:** ✅

#### `rt_file_init`, `rt_file_close`, `rt_file_read_byte`, `rt_file_seek`
- ✅✅✅

---

### `rt_file_path.c` (file mode helpers)

#### `rt_file_path_from_vstr(path, out_path) → int8_t`
- **Robustness:** ✅ — NULL path and NULL path->data both guarded
- **Security:** ⚠️ WARNING — Returns raw borrowed pointer into the ViperString buffer; if string is freed while pointer is in use, this is a use-after-free. API comment documents the lifetime requirement, but it is a latent danger.
- **Optimization:** ✅ — Zero-copy borrow

#### `rt_file_mode_string`, `rt_file_mode_to_flags`, `rt_file_string_view`
- ✅✅✅

---

### `rt_archive.c` (ZIP archive read/write)

#### `parse_central_directory(ar) → bool`
- **Robustness:** ✅ — Multi-disk archives rejected; CD bounds validated against EOCD offset; each entry bounds-checked; `malloc` checked per name
- **Security:** ✅ — `total_entries` is `uint16_t` (max 65535); no integer overflow possible in `calloc`
- **Optimization:** ✅

#### `find_entry(ar, name) → zip_entry_t*`
- **Robustness:** ✅
- **Security:** ✅ — Used only after name normalization
- **Optimization:** ⚠️ — O(n) linear scan; a hash map would help for archives with many entries

#### `read_entry_data(ar, entry) → void*`
- **Robustness:** ✅ — Local header offset bounds-checked; CRC and size verified
- **Security:** ⚠️ WARNING — No decompression output size cap; since `rt_compress_inflate` has no output limit (see rt_compress.c critique), a crafted entry with `uncompressed_size = 4GB` and tiny compressed payload exhausts memory. CRC check occurs after decompression — too late to prevent the allocation.
- **Optimization:** ✅

#### `normalize_name(name, out) → name_result_t`
- **Robustness:** ✅ — Empty, absolute, `..`, drive letters, and colons all rejected
- **Security:** ✅ — **Zip-slip prevention is robust**: one of the strongest security points in the codebase
- **Optimization:** ✅

#### `rt_archive_open(path) → void*`
- **Robustness:** ✅ — File opened, fully read, and parsed; partial-read detected
- **Security:** ⚠️ WARNING — Reads entire archive into RAM without a size cap; multi-GB archives cause multi-GB allocations
- **Optimization:** ⚠️ — Full archive read into RAM; mmap or streaming would be far more efficient for selective entry access

#### `rt_archive_add_file(obj, name, src_path) → void`
- **Robustness:** ✅ — File opened; `fstat` checked; read verified; close-before-trap on failure
- **Security:** ⚠️ WARNING — `src_path` follows symlinks transparently; no file-size cap before allocation
- **Optimization:** ⚠️ — Reads entire source file into RAM before adding

#### `rt_archive_extract(obj, name, dest_path) → void`
- **Robustness:** ✅ — Reads and validates data before writing
- **Security:** ⚠️ WARNING — `dest_path` is not verified to be within a safe base directory; callers that pass raw archive entry names without prior normalization are vulnerable to zip-slip
- **Optimization:** ✅

#### `rt_archive_extract_all(obj, dest_dir) → void`
- **Robustness:** ✅ — Each entry name normalized; parent directories created
- **Security:** ✅ — Calls `normalize_name` on every entry before constructing output path; zip-slip prevented in this path
- **Optimization:** ⚠️ — Each entry decompressed fully before writing; for large archives, memory usage equals largest uncompressed entry

#### `rt_archive_finish(obj) → void`
- **Robustness:** ✅ — Entire archive built in RAM then written in one `write()` call
- **Security:** ✅
- **Optimization:** ⚠️ — Doubles memory usage: both the write buffer and the OS file cache contain the full archive simultaneously

#### `find_eocd`, `write_ensure`, `add_write_entry`, `ensure_trailing_slash`, `rt_archive_create`, `rt_archive_from_bytes`, `rt_archive_path`, `rt_archive_count`, `rt_archive_names`, `rt_archive_has`, `rt_archive_read`, `rt_archive_read_str`, `rt_archive_info`, `rt_archive_add`, `rt_archive_add_str`, `rt_archive_add_dir`, `rt_archive_is_zip`, `rt_archive_is_zip_bytes`
- ✅✅✅ (naming, zip-slip protection, and CRC validation all properly implemented)

---

### `rt_linewriter.c` (buffered text file writer)

#### `rt_linewriter_open_mode(path, mode) → void*`
- **Robustness:** ✅ — NULL path trapped; `fopen` failure trapped; `rt_obj_new_i64` failure closes `fp` before trapping
- **Security:** ✅
- **Optimization:** ✅

#### `rt_linewriter_write(obj, text) → void`, `rt_linewriter_write_ln(obj, text) → void`
- **Robustness:** ✅ — NULL obj trapped; closed trapped; NULL text is no-op / writes only newline
- **Security:** ✅
- **Optimization:** ✅ — `fwrite` bulk write; stdio buffering makes two-call write_ln efficient

#### `rt_linewriter_set_newline(obj, nl) → void`
- **Robustness:** ✅ — Old newline unrefed; NULL new newline resets to platform default
- **Security:** ✅
- **Optimization:** ✅

#### `rt_linewriter_open`, `rt_linewriter_append`, `rt_linewriter_close`, `rt_linewriter_write_char`, `rt_linewriter_flush`, `rt_linewriter_newline`, `rt_linewriter_finalize`
- ✅✅✅

---

### `rt_network.c` — Supplementary Per-Function Notes

#### `set_nonblocking(sock, nonblocking) → void` (static)
- **Robustness:** ⚠️ WARNING — `fcntl(F_GETFL)` return not checked; if it returns -1 (on invalid fd), `F_SETFL` ORs -1 with `O_NONBLOCK` producing a garbage flags value. `ioctlsocket` return also unchecked on Windows.
- **Security:** ✅ · **Optimization:** ✅

#### `set_nodelay(sock) → void`, `set_socket_timeout(sock, timeout_ms, is_recv) → void`
- **Robustness:** ⚠️ WARNING — `setsockopt` return silently ignored; failures degrade performance/behavior without any diagnostic
- **Security:** ✅ · **Optimization:** ✅

#### `rt_tcp_recv(obj, max_bytes) → void*`
- **Robustness:** ⚠️ WARNING — Double allocation: allocates `max_bytes` upfront, then allocates a second `received`-sized buffer and copies when receive is partial. `max_bytes` cast to `int` silently overflows for values >2 GB.
- **Security:** ⚠️ WARNING — `max_bytes` is caller-controlled with no upper bound; very large values cause large malloc
- **Optimization:** ⚠️ — Double allocation in the common partial-receive case; should allocate to `received` directly or realloc the initial buffer

#### `rt_tcp_send(obj, data) → int64_t`
- **Robustness:** ⚠️ WARNING — Issues single `send` call; returns partial count if send is short. `len` cast from `int64_t` to `int` overflows for buffers >2 GB.
- **Security:** ✅ · **Optimization:** ✅

#### `rt_tcp_server_listen(port) → void*`
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Binds to `0.0.0.0` accepting connections from all interfaces including public-facing ones; callers needing internal-only services should use `listen_at` with loopback address
- **Optimization:** ✅

#### `resolve_host(host, port, addr) → int` (static)
- **Robustness:** ⚠️ WARNING — No NULL check on `host` or `addr`; if `getaddrinfo` returns a non-`AF_INET` first result, casting `ai_addr` to `sockaddr_in*` extracts wrong `sin_addr`
- **Security:** ✅
- **Optimization:** ⚠️ — Resolves hostname on every call; no caching; DNS lookup per UDP datagram in send loops

#### `rt_net_init_wsa() → void` (Windows)
- **Robustness:** ⚠️ WARNING — `wsa_initialized` flag is a plain `static bool` with no synchronization; concurrent first calls race and double-call `WSAStartup`
- **Security:** ✅ · **Optimization:** ✅

---

### `rt_network_http.c` — Supplementary Per-Function Notes

#### `http_conn_send(conn, data, len) → int` (static)
- **Robustness:** ⚠️ WARNING — No NULL guard on `conn` or `data`
- **Security:** ✅
- **Optimization:** ❌ CRITICAL — For TCP path: allocates a new `rt_bytes` object and copies `len` bytes just to call `rt_tcp_send_all`, which extracts back the same pointer. This is an unnecessary allocation+copy on every HTTP request send. A direct socket `send` loop would eliminate this.

#### `read_line_conn(conn) → char*` (static)
- **Robustness:** ⚠️ WARNING — No maximum line length; malicious server sending headers without CRLF causes unbounded memory growth
- **Security:** ❌ CRITICAL — Same DoS as `rt_tcp_recv_line`
- **Optimization:** ✅ — Uses buffered `http_conn_recv_byte` (4 KiB amortized), better than raw recv

#### `rt_http_req_new(method, url) → void*`
- **Robustness:** ⚠️ WARNING — `strdup(method_str)` return not checked for NULL
- **Security:** ⚠️ WARNING — No HTTP method validation; caller could pass `"G\r\nET"` to inject into the request line; `build_request` uses `sprintf("%s %s HTTP/1.1\r\n")` directly

#### `rt_http_download(url, dest_path) → int8_t`
- **Robustness:** ⚠️ WARNING — Entire body loaded into memory before writing to disk; large downloads buffer everything in RAM; partial download leaves truncated file (no temp-then-rename)
- **Security:** ⚠️ WARNING — `dest_path` not validated for path traversal; `"../../../etc/cron.d/evil"` accepted

#### `parse_url_full(url_str, result) → int` (static)
- **Robustness:** ⚠️ WARNING — Individual `malloc` calls for each field (scheme, host, user, pass, path, query, fragment) not checked for NULL; OOM during multi-field parse leaves partially allocated struct
- **Security:** ⚠️ WARNING — Userinfo (user:password) parsed and stored in plaintext in the `rt_url_t` struct
- **Optimization:** ✅

#### `rt_url_set_query_param`, `rt_url_get_query_param`, `rt_url_has_query_param`, `rt_url_del_query_param`, `rt_url_query_map`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Full decode+encode round-trip of the entire query string for every individual param operation; O(n) per call

---

### `rt_websocket.c` — Supplementary Per-Function Notes

#### `ws_recv_frame(ws, opcode_out, data_out, len_out) → int` (static)
- **Robustness:** ⚠️ WARNING — 64-bit payload length: reads 8 bytes but only uses the low 32 bits (`ext[4..7]`), silently ignoring bytes `ext[0..3]`. A frame with length >4 GB is parsed with a too-small `payload_len`, causing stream desynchronization.
- **Security:** ❌ CRITICAL — No maximum payload length; confirmed DoS vector (already in main report)
- **Optimization:** ⚠️ — Fresh heap allocation per frame; no buffer reuse for high-frequency small messages

#### `rt_ws_recv(obj) → rt_string`
- **Robustness:** ⚠️ WARNING — Fragmented WebSocket messages (frames with `FIN=0`) are silently discarded; continuation frames fall into `else { free(data) }`. Applications expecting fragmented messages receive empty results with no error.
- **Security:** ✅ · **Optimization:** ✅

#### `rt_ws_recv_bytes(obj) → void*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Copies data into `rt_bytes` element-by-element via `rt_bytes_set` loop instead of `memcpy`; AND `ws_send_frame` also allocates a masked copy — binary frames are effectively copied twice

#### `rt_ws_close_with(obj, code, reason) → void`
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Close code is `int64_t` with no range validation against valid RFC 6455 codes `[1000, 4999]`; values outside range sent with silent byte truncation
- **Optimization:** ✅

---

### `rt_restclient.c` — Supplementary Per-Function Notes

#### `rt_restclient_set_auth_basic(obj, username, password) → void`
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Username containing `:` corrupts the `user:pass` credential format silently; password stored in plaintext on the heap and not zeroed after encoding; resulting `Authorization` header inherits the CR/LF injection risk from `add_header`
- **Optimization:** ✅

#### `join_url(base, path) → rt_string` (static)
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — No path traversal validation; `"../../../etc/passwd"` passed as `path` would be joined and sent as-is to the HTTP client
- **Optimization:** ✅

#### `create_request(client, method, path) → void*` (static)
- **Robustness:** ✅
- **Security:** ⚠️ WARNING — Default headers from `client->headers` map applied via `add_header` with no CR/LF validation; if any stored header value contains injection characters, they propagate to every request
- **Optimization:** ⚠️ — Calls `rt_map_keys` (allocates a new sequence) on every request to iterate default headers

---

### `rt_csv.c` (CSV parser/formatter)

#### `csv_extract_string(val) → rt_string` (static)
- **Robustness:** ⚠️ WARNING — Dereferences `val` as `int64_t*` to read a tag without checking alignment or type — same raw type-punning as in `rt_toml.c`. If `val` is not a properly-tagged box, this reads arbitrary memory.
- **Security:** ✅ · **Optimization:** ✅

#### `parse_field(p, at_line_end) → rt_string`
- **Robustness:** ✅ — Correctly handles quoted fields (embedded newlines, doubled-quote escaping), CRLF/LF/CR endings
- **Security:** ✅ · **Optimization:** ✅ — Unquoted fields are zero-copy slices of input

#### `format_field(field, field_len, delim, out) → size_t`
- **Robustness:** ⚠️ WARNING — `out` must be pre-allocated by caller with exactly `calc_field_size()` bytes; no internal bounds checking. If `calc_field_size` and `format_field` diverge (e.g., future change to one without the other), writes out-of-bounds.
- **Security:** ✅ — Correct RFC 4180 quoting
- **Optimization:** ✅ — Pre-calculated size avoids reallocation

#### `rt_csv_format_line_with(fields, delim) → rt_string`, `rt_csv_format_with(rows, delim) → rt_string`
- **Robustness:** ✅ · **Security:** ✅
- **Optimization:** ✅ — Two-pass (calculate then write) with single allocation; best allocation pattern in the parser batch

#### `rt_csv_parse_with(text, delim) → void*`, `rt_csv_parse(text) → void*`, `rt_csv_parse_line(line) → void*`, `rt_csv_parse_line_with(line, delim) → void*`
- ✅✅✅ — RFC 4180 compliant; handles all line ending styles; O(n) single-pass

---

### `rt_xml.c` — Additional Per-Function Notes

#### `buf_append(buf, cap, len, str) → void` (static)
- **Robustness:** ❌ CRITICAL — `realloc` failure sets `*buf = NULL` but then immediately calls `memcpy(NULL, ...)` → undefined behavior / crash. The formatter uses this for every character of output; OOM during XML formatting crashes rather than trapping. Same defect in `buf_append_n` and `buf_append_char`.
- **Security:** ✅ · **Optimization:** ✅

#### `buf_append_indent(buf, cap, len, spaces) → void` (static)
- **Robustness:** ⚠️ WARNING — Inherits OOM crash from `buf_append_char`
- **Security:** ✅
- **Optimization:** ⚠️ — Calls `buf_append_char` in a loop (one char at a time, potentially reallocating on each); should grow to `spaces` capacity then `memset`

#### `rt_xml_remove_at(node, index) → void`
- **Robustness:** ⚠️ WARNING — Gets child via `rt_seq_get` (may bump refcount), calls `rt_obj_release_check0` (may free it), then calls `rt_seq_remove` which returns a pointer to the same child. If the child was freed by `release_check0`, the subsequent `rt_seq_remove` returns a dangling pointer and attempts a second release — potential double-free.
- **Security:** ✅ · **Optimization:** ✅

#### `rt_xml_set_text(node, text) → void`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Removes children by calling `rt_seq_remove(0)` in a loop; each removal shifts remaining elements → O(n²) for nodes with many children

#### `rt_xml_children(node) → void*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Creates a full shallow copy of the children sequence on every call; callers needing only iteration pay unnecessary allocation cost

---

### `rt_yaml.c` — Additional Per-Function Notes

#### `parse_quoted_string(p, quote) → rt_string`
- **Robustness:** ❌ CRITICAL — Buffer `realloc` failure on growth returns NULL, assigned directly back to `buf`; subsequent `buf[len++] = c` writes to NULL → undefined behavior / crash.
- **Security:** ✅ · **Optimization:** ✅

#### `format_value(obj, indent, level, buf, cap, len) → void` (static)
- **Robustness:** ❌ CRITICAL — Calls `rt_box_type(obj)` (which dereferences `obj` as `rt_box_t*`) on every object regardless of actual type. If `obj` is an `rt_seq_impl` or `rt_map_impl`, this reads the wrong struct layout — treating the seq/map's first field as a box tag produces garbage type info. The subsequent `rt_string_cstr` / `rt_seq_len` / `rt_map_keys` calls may then operate on the wrong interpretation.
- **Security:** ✅
- **Optimization:** ⚠️ — `rt_map_keys(val)` called to detect complex values inside map-formatting loop; result **never freed** → memory leak per map node emitted

#### `parse_block_mapping(p, base_indent) → void*`
- **Robustness:** ⚠️ WARNING — Key-scan loop `while (peek != ':' && peek != '\n')` does not handle quoted keys containing `:` or `\n`; such keys are parsed incorrectly
- **Security:** ✅ · **Optimization:** ✅

---

### `rt_json.c` — Additional Per-Function Notes

#### `format_value(sb, obj, indent, level) → void` (static)
- **Robustness:** ❌ CRITICAL — Type detection uses heap allocation size (`hdr->len`) to distinguish `rt_box_t` (16 bytes), `rt_seq_impl` (24 bytes), and `rt_map_impl` (32 bytes). If any of these struct sizes change due to compiler padding or refactoring, the dispatcher silently misidentifies objects → wrong JSON output or crashes with no type-check error.
- **Security:** ✅ · **Optimization:** ✅

#### `parse_number(p) → void*`
- **Robustness:** ✅
- **Security:** ✅
- **Optimization:** ⚠️ — Allocates and frees a `malloc` copy of the number substring just to get a NUL-terminated string for `strtod`; `strtod` with a `NULL` end-pointer can be called directly on the original buffer since the character after a valid JSON number is always a non-digit delimiter

#### `format_string(sb, s) → void` (static)
- **Robustness:** ✅ — Correct RFC 8259 escaping
- **Security:** ✅
- **Optimization:** ⚠️ — Calls `sb_append` (which calls `strlen`) on each 2-char escape sequence (`"\\n"` etc.) per character; should use `sb_append_char` or direct writes for the escape bytes

---

## 3. Cross-Cutting Patterns

### Pattern A: Unchecked `realloc` with Pointer Overwrite

```c
// BUG PATTERN — appears in rt_sortedset, rt_bitset, rt_yaml, rt_bigint, etc.
ptr = realloc(ptr, new_size);   // ptr becomes NULL on failure; old allocation lost
```

**Fix:** Save old pointer, check result before overwriting:
```c
void *tmp = realloc(ptr, new_size);
if (!tmp) { handle_error(); return; }
ptr = tmp;
```
Found in: `rt_sortedset`, `rt_bitset`, `rt_yaml`, `rt_bigint`, `rt_spritesheet`, ~12 other files.

### Pattern B: Missing `rt_obj_free` After `rt_obj_release_check0`

```c
// BUG PATTERN — appears in rt_concmap, rt_concqueue, rt_cancellation
if (rt_obj_release_check0(obj)) {
    // rt_obj_free(obj) MISSING — object leaks
}
```
All callers of `rt_obj_release_check0` must call `rt_obj_free` when the return value is non-zero.

### Pattern C: Non-Atomic Lazy Initialization

```c
// BUG PATTERN — appears in rt_crc32, rt_compress, rt_monitor (Windows)
static int initialized = 0;
if (!initialized) {
    // ... initialize ...
    initialized = 1;  // RACE: another thread may pass the check before this line
}
```
**Fix:** Use `pthread_once` / `std::call_once` / `_Atomics`.

### Pattern D: Recursive Parsers Without Depth Limit

Affects: `rt_json.c`, `rt_xml.c`, `rt_yaml.c` — all recursive-descent parsers.

**Fix:** Thread a depth counter through recursive calls; return parse error at limit (e.g., 512).

### Pattern E: `-INT64_MIN` Undefined Behavior

```c
// BUG PATTERN — appears in rt_duration, rt_reltime, rt_fmt, rt_numfmt
int64_t abs_val = -value;   // UB when value == INT64_MIN
```
**Fix:** Use `(uint64_t)(-((uint64_t)value))` or check before negating.

### Pattern F: TLS Certificate Verification Bypassed

Both `rt_tls.c` (`verify_cert` stub) and `rt_network_http.c` (`tls_config.verify_cert = 0`) disable certificate verification unconditionally. These are separate but compounding defects — fixing one does not fix the other.

### Pattern G: HTTP Header Injection

Three sites independently accept user-supplied header values without CR/LF validation:
- `rt_network_http.c:add_header`
- `rt_network_http.c:build_request`
- `rt_restclient.c:rt_restclient_set_header` / `rt_restclient_set_auth_bearer`

**Fix:** Reject or strip any `\r` or `\n` in header name or value before use.

### Pattern H: Global Non-Thread-Local Error State

```c
// BUG PATTERN — appears in rt_xml, rt_yaml, rt_serialize
static char g_error[256];     // races when called from multiple threads
```
**Fix:** `thread_local char g_error[256]` (C11 `_Thread_local`) or return error through output parameter.

### Pattern I: `calloc` Return Unchecked Before Use

Appears in 15+ collection types (`rt_bloomfilter`, `rt_defaultmap`, `rt_bag`, `rt_bimap`, `rt_deque`, `rt_set`, `rt_pqueue`, `rt_orderedmap`, `rt_countmap`, `rt_sparsearray`, `rt_stack`, `rt_weakmap`, `rt_map`, `rt_concmap`, `rt_msgbus`). While Viper's convention is to trap on OOM, these use raw `calloc` rather than `rt_obj_new_i64`, so the trap never fires — the NULL is silently stored and crashes later.

### Pattern J: Byte-by-Byte I/O Instead of Bulk Operations

Appears in `rt_file_ext.c` (write) and `rt_file_ext.c` (read bytes). Both should use a single `write()`/`memcpy()` call. The write path issues one syscall per byte — for a 1 MB file that is 1,048,576 syscalls vs. 1.

---

## 4. Recommended Fix Priority

### Tier 1 — Fix Immediately (Security Critical)

1. **TLS cert verification** — `rt_tls.c` + `rt_network_http.c` — all HTTPS is broken
2. **ReDoS** — `rt_regex.c` — add step counter or convert to NFA simulation
3. **HTTP header injection** — three call sites — strip CR/LF from all header values
4. **Decompression bomb** — `rt_compress.c` — add output size cap parameter
5. **Tempfile race** — `rt_tempfile.c` — use `mkstemp`/`mkdtemp`
6. **Symlink attack** — `rt_dir_remove_all` — use `lstat` + `openat`/`unlinkat`
7. **LCG fallback PRNG** — `rt_crypto_random_bytes` — remove fallback or use OS entropy
8. **Stack overflow in `rt_hkdf_expand_label`** — add `label_len` bounds check
9. **WebSocket DoS** — `ws_recv_frame` — enforce max payload length
10. **HTTP body DoS** — `read_body_fixed`/`read_body_chunked` — enforce max body size

### Tier 2 — Fix Soon (Data Loss / Correctness Critical)

11. All `realloc`-overwrites-pointer bugs (Pattern A)
12. Missing `rt_obj_free` after `rt_obj_release_check0` (Pattern B, concmap/concqueue)
13. `rt_scheduler_poll` use-after-free
14. `rt_parallel.c` deadlock + use-after-stack-free
15. `rt_file_write_bytes` byte-by-byte I/O (O-01)
16. `rt_scene_draw` per-frame memory leak
17. Parser recursion depth limits (json/xml/yaml)
18. `rt_bigint` bitwise ops wrong for negatives
19. `rt_numeric_conv.c:rt_f64_to_i64` UB
20. `rt_mat4_perspective`/`rt_mat4_ortho` NaN/Inf outputs

### Tier 3 — Fix When Possible (Warnings)

- All unchecked `calloc` in collection types (Pattern I)
- Non-atomic lazy init patterns (Pattern C)
- `-INT64_MIN` UB in duration/fmt/numfmt (Pattern E)
- Global error buffers (Pattern H)
- `rt_xml_text_content` O(n²) concat
- `rt_canvas_flood_fill` O(r²) allocation
- Byte-by-byte file reads (Pattern J)
- `rt_linereader` unbounded line growth
- `rt_rand_int` modulo bias
- `rt_time.c` QPC overflow after 9.2 years

---

## 5. Supplementary Per-Function Review — Collections, Utilities, Time

### `rt_bag.c`

#### `rt_bag_new(void) → void *`
- **Robustness:** ⚠️ — `calloc(RT_BAG_INITIAL_CAP, sizeof(void *))` not null-checked; stored directly into `bag->items` — NULL deref on first insert.
- **Security:** ✅
- **Optimization:** ✅

#### `bag_resize(rt_bag_impl *, size_t) → void`
- **Robustness:** ⚠️ — `calloc` result not checked.

#### `maybe_resize(rt_bag_impl *) → void`
- **Robustness:** ⚠️ — `count * BAG_LOAD_FACTOR_DEN` signed multiplication overflows for huge counts.

#### All remaining `rt_bag_*` functions
- ✅✅✅ — Insert/remove/contains/iterate all null-guarded and bounds-safe.

---

### `rt_bimap.c`

#### `rt_bimap_new(void) → void *`, `resize_fwd`, `resize_inv`
- **Robustness:** ⚠️ — Both forward and inverse `calloc` bucket arrays not null-checked.

#### All remaining `rt_bimap_*` functions
- ✅✅✅ — Bidirectional lookup, put, remove all null-guarded.

---

### `rt_bitset.c`

#### `bitset_grow(rt_bitset_impl *, size_t) → void`
- **Robustness:** ⚠️ — `realloc` result assigned directly to `bs->words` — lost-pointer pattern on failure; old allocation freed, NULL stored, next access crashes.

#### All remaining `rt_bitset_*` functions
- ✅✅✅ — Word-index operations bounds-checked; shift counts safe.

---

### `rt_bloomfilter.c`

#### `rt_bloomfilter_new(int64_t capacity, double false_positive_rate) → void *`
- **Robustness:** ❌ — `calloc` for `bf->bits` not null-checked; `bf->bits = NULL` stored directly. Any `rt_bloomfilter_add` or `rt_bloomfilter_might_contain` call dereferences NULL.

#### All remaining `rt_bloomfilter_*` functions
- ✅✅✅ — MurmurHash-based multi-hash insertion/lookup correct.

---

### `rt_bytes.c`

#### `rt_bytes_copy(void *src, int64_t src_idx, void *dst, int64_t dst_idx, int64_t count) → void`
- **Robustness:** ⚠️ — `src_idx + count` not overflow-checked before bounds test.

#### `rt_bytes_concat(void *a, void *b) → void *`
- **Robustness:** ⚠️ — `a_len + b_len` not overflow-checked before allocation.

#### All remaining `rt_bytes_*` functions
- ✅✅✅

---

### `rt_binbuf.c`

#### `binbuf_ensure(rt_binbuf_impl *, int64_t needed) → void`
- **Robustness:** ⚠️ — `buf->position + needed` can overflow `int64_t`; no overflow check before capacity comparison.

#### `rt_binbuf_read_str(void *obj) → rt_string`
- **Robustness:** ⚠️ — Reads a 4-byte int32 length prefix; negative lengths not explicitly rejected — could trigger huge allocation or bounds violation.

#### All remaining `rt_binbuf_*` functions
- ✅✅✅ — Fixed-width read/write operations all bounds-checked.

---

### `rt_countmap.c`

#### `rt_countmap_new(void) → void *`, `resize(rt_countmap_impl *) → void`
- **Robustness:** ⚠️ — `calloc` for bucket arrays not null-checked.

#### All remaining `rt_countmap_*` functions
- ✅✅✅ — Increment, decrement, get, top-k all correct.

---

### `rt_defaultmap.c`

#### `rt_defaultmap_new(void *default_fn) → void *`, `dm_resize(rt_defaultmap_impl *) → void`
- **Robustness:** ❌ — `calloc` for bucket array not null-checked; NULL deref on first insert or lookup.

#### `rt_defaultmap_set(void *obj, rt_string key, void *value) → void`
- **Robustness:** ⚠️ — `malloc` for internal key copy not null-checked.

#### All remaining `rt_defaultmap_*` functions
- ✅✅✅ — Default-value factory invoked correctly on missing keys.

---

### `rt_deque.c`

#### `rt_deque_new(void) → void *`, `ensure_capacity(rt_deque_impl *) → void`
- **Robustness:** ⚠️ — `malloc` for ring buffer not null-checked.

#### All remaining `rt_deque_*` functions
- ✅✅✅ — Ring buffer wrap arithmetic correct; push/pop null-guarded.

---

### `rt_heap.c`

#### `rt_heap_retain(void *obj) → void`
- **Robustness:** ⚠️ — Non-atomic read before atomic increment in overflow guard — theoretical race.

#### All remaining `rt_heap_*` functions
- ✅✅✅ — Sift-up/sift-down correct; capacity growth checked.

---

### `rt_orderedmap.c`

#### `rt_orderedmap_new(void) → void *`, `om_resize(rt_orderedmap_impl *) → void`
- **Robustness:** ⚠️ — `calloc` for bucket array not null-checked.

#### `rt_orderedmap_key_at(void *obj, int64_t index) → rt_string`
- **Optimization:** ⚠️ — O(n) linear scan through insertion-order list; an index array would give O(1).

#### All remaining `rt_orderedmap_*` functions
- ✅✅✅ — Insertion-order preservation via linked list; all null-guarded.

---

### `rt_pqueue.c`

#### `rt_pqueue_new_min(void) → void *`, `rt_pqueue_new_max(void) → void *`, `heap_grow(rt_pqueue_impl *) → void`
- **Robustness:** ⚠️ — `malloc` for heap data array not null-checked.

#### All remaining `rt_pqueue_*` functions
- ✅✅✅ — Binary heap invariant maintained; peek/pop null-guarded on empty heap.

---

### `rt_ring.c`

- All `rt_ring_*` functions: ✅✅✅ — Fixed-capacity ring buffer; modulo arithmetic correct.

---

### `rt_set.c`

#### `rt_set_new(void) → void *`, `resize_set(rt_set_impl *) → void`
- **Robustness:** ⚠️ — `calloc` for bucket array not null-checked.

#### All remaining `rt_set_*` functions
- ✅✅✅ — Insert/remove/contains/intersect/union all correct.

---

### `rt_sortedset.c`

#### `rt_sortedset_new(void *compare_fn) → void *`
- **Robustness:** ❌ — No GC finalizer registered for `set->data` (a raw `malloc`'d array). When the GC collects the outer struct, `set->data` leaks permanently.

#### `ensure_capacity(rt_sortedset_impl *) → void`
- **Robustness:** ❌ — `realloc` result assigned directly to `set->data` — lost-pointer pattern. On failure, `set->data = NULL` discards the original pointer; previous contents are neither freed nor preserved.

#### All remaining `rt_sortedset_*` functions
- ✅✅✅ — Binary search insert/remove/contains correct; order maintained.

---

### `rt_sparsearray.c`

#### `rt_sparse_new(void) → void *`, `sa_grow(rt_sparse_impl *, int64_t) → void`
- **Robustness:** ⚠️ — `calloc` for bucket array not null-checked.

#### All remaining `rt_sparse_*` functions
- ✅✅✅ — OOB index returns NULL.

---

### `rt_stack.c`

#### `rt_stack_new(void) → void *`, `stack_ensure_capacity(rt_stack_impl *) → void`
- **Robustness:** ⚠️ — `realloc` result overwrites `stack->items` directly — lost-pointer pattern.

#### All remaining `rt_stack_*` functions
- ✅✅✅ — Push/pop/peek correct; underflow guarded.

---

### `rt_treemap.c`

#### `rt_treemap_len(void *obj) → int64_t`, `rt_treemap_is_empty(void *obj) → int8_t`
- **Robustness:** ⚠️ — No NULL check on `obj` — crash on NULL input rather than returning 0.

#### All remaining `rt_treemap_*` functions
- ✅✅✅ — AVL rotations correct; in-order traversal for keys/values.

---

### `rt_trie.c`

#### `alloc_node(void) → trie_node *`
- **Robustness:** ⚠️ — `calloc` return not null-checked.

#### `collect_keys(trie_node *, char *, int, void *) → void`
- **Robustness:** ❌ — Fixed `char buf[4096]` passed recursively. For keys > 4095 chars, writes past end of stack buffer — stack buffer overflow.
- **Security:** ❌ — Exploitable if key strings are user-controlled.

#### `free_node(trie_node *) → void`, `has_any_key(trie_node *) → int`
- **Robustness:** ⚠️ — Recursive descent; deeply nested trie nodes (depth > ~10,000) risk stack overflow.

#### `rt_trie_keys(void *obj) → void *`, `rt_trie_with_prefix(void *obj, rt_string prefix) → void *`
- **Robustness:** ❌ — Both call `collect_keys` with the fixed `buf[4096]` — same stack overflow for long keys.
- **Security:** ❌ — Same exploitable overflow.

#### All remaining `rt_trie_*` functions
- ✅✅✅ — Lookup, delete, contains all iterative or bounded-depth.

---

### `rt_unionfind.c`

#### `rt_unionfind_new(int64_t size) → void *`
- **Robustness:** ⚠️ — `malloc` for `parent` and `rank` arrays not null-checked; silently stored as NULL — crash on first `find`/`union` call.

#### All remaining `rt_unionfind_*` functions
- ✅✅✅ — Path compression and union-by-rank both implemented correctly; near-O(α(n)) amortized per operation.

---

### `rt_weakmap.c`

#### `rt_weakmap_new(void) → void *`, `wm_grow(rt_weakmap_impl *) → void`
- **Robustness:** ⚠️ — `calloc` for bucket array not null-checked.

#### All remaining `rt_weakmap_*` functions
- ✅✅✅ — Weak references do not prevent GC; tombstone cleanup on access correct.

---

### `rt_lazyseq.c`

#### `rt_lazyseq_reset(void *obj) → void`
- **Robustness:** ⚠️ — RANGE type does not reset `current` to `start` — post-reset seq continues from mid-sequence.

#### All remaining `rt_lazyseq_*` functions
- ✅✅✅ — MAP, FILTER, TAKE, DROP composition correct.

---

### `rt_convert_coll.c`

#### `rt_seq_of(int64_t count, ...) → void *`, `rt_list_of(...)`, `rt_set_of(...)`
- **Robustness:** ⚠️ — Variadic; wrong `count` from caller reads garbage pointers from va_list. No runtime validation possible.
- **Security:** ⚠️ — Caller error can produce arbitrary pointer reads.

#### `rt_stack_to_seq(void *stack) → void *`, `rt_queue_to_seq(void *queue) → void *`
- **Optimization:** ⚠️ — Destructive-restore pattern: pop all elements, push back in reverse. O(2n) operations.

#### All remaining conversion functions
- ✅✅✅ — `rt_set_to_list`, `rt_list_to_set`, `rt_seq_to_list` all correct.

---

### `rt_datetime.c`

#### `rt_datetime_create(int64_t year, int64_t month, int64_t day, int64_t hour, int64_t min, int64_t sec) → int64_t`
- **Robustness:** ⚠️ — `mktime()` returns `(time_t)-1` on failure, returned as `int64_t -1`. Caller cannot distinguish failure from a valid timestamp of -1 (one second before the epoch).

#### `rt_datetime_add_seconds(...)`, `rt_datetime_add_minutes(...)`, `rt_datetime_add_hours(...)`, `rt_datetime_add_days(...)`
- **Robustness:** ⚠️ — No overflow check on `ts + delta`; UB for extreme timestamp values.

#### `rt_datetime_diff(int64_t a, int64_t b) → int64_t`
- **Robustness:** ⚠️ — `a - b` can overflow for timestamps near INT64_MIN/MAX.

#### `rt_datetime_format(int64_t ts, rt_string fmt) → rt_string`
- **Robustness:** ⚠️ — Fixed 256-byte buffer; guard is per-character, not per-snprintf output; a single long token can write past the boundary between guard checks.
- **Security:** ✅ — Format specifiers handled in a switch; no printf-style injection.

#### `rt_datetime_now`, `rt_datetime_from_unix`, `rt_datetime_year` through `rt_datetime_second`, `rt_datetime_parse_iso`, `rt_datetime_to_string`, `rt_datetime_is_valid`
- ✅✅✅ — Use `gmtime_r` (thread-safe); ISO parse returns -1 on failure.

---

### `rt_dateonly.c`

#### `rt_dateonly_today(void) → void *`
- **Robustness:** ⚠️ — Uses `localtime(&now)` (not thread-safe). Concurrent calls corrupt each other's `struct tm` via the shared static buffer.
- **Security:** ⚠️ — Same thread-safety issue; concurrent calls produce nonsense dates.

#### `rt_dateonly_format(void *obj, rt_string fmt) → rt_string`
- **Robustness:** ❌ — Buffer overflow: outer loop checks `buf_pos < 255` per format character, but a single `snprintf` for a long token ("September" = 9 chars, "Wednesday" = 9 chars) advances `buf_pos` past 255. The 256-byte buffer has no re-check after each `snprintf`.
- **Security:** ❌ — Genuine stack buffer overrun for format strings producing long output near the 256-byte boundary.

#### `rt_dateonly_add_months(void *obj, int64_t months) → void *`
- **Optimization:** ⚠️ — While-loop month normalization is O(|months|); division arithmetic would give O(1).

#### All other `rt_dateonly_*` functions
- ✅✅✅ — Correct Julian Day Number arithmetic; all null-guarded.

---

### `rt_daterange.c`

- All `rt_daterange_*` functions: ✅✅✅ — Normalizes order if start > end; `is_adjacent` ±1 second check is a documented design choice.

---

### `rt_time.c`

#### `get_timestamp_ns(void) → int64_t`, `rt_clock_ns(void) → int64_t`
- **Robustness:** ⚠️ — Windows: `counter.QuadPart * 1000000000LL / freq.QuadPart` overflows `int64_t` for uptime > ~9.2 years.

#### `rt_sleep_ms`, `rt_clock_ms`, `rt_clock_us`, `rt_clock_sleep`, `rt_timer_ms`
- ✅✅✅ — EINTR retry loops; monotonic clock; correct clamp behaviour.

---

### `rt_timer.c`

#### `rt_timer_progress(void *obj) → int64_t`
- **Robustness:** ⚠️ — `(elapsed * 100) / duration`: multiplication overflows `int64_t` for `elapsed` near `INT64_MAX / 100`.

#### All other `rt_timer_*` functions
- ✅✅✅ — Start/stop/reset/update/elapsed/remaining all null-guarded and correct.

---

### `rt_stopwatch.c`

#### `get_frequency(void) → int64_t` (Windows, static)
- **Robustness:** ⚠️ — `static LARGE_INTEGER freq` lazy-init not thread-safe. Two threads can both see `freq.QuadPart == 0` and concurrently call `QueryPerformanceFrequency` — data race (UB in C11).

#### All other `rt_stopwatch_*` functions
- ✅✅✅ — `lap` correctly returns elapsed without stopping.

---

### `rt_duration.c`

#### `rt_duration_abs(int64_t duration) → int64_t`
- **Robustness:** ❌ — `-duration` is signed integer overflow UB for `INT64_MIN`. On two's-complement hardware the result equals `INT64_MIN`, giving a negative "absolute value."

#### `rt_duration_neg(int64_t duration) → int64_t`
- **Robustness:** ❌ — Same UB: `-INT64_MIN` is undefined behavior.

#### `rt_duration_add(...)`, `rt_duration_sub(...)`, `rt_duration_mul(...)`, `rt_duration_from_ms/sec/min/hours/days(...)`
- **Robustness:** ⚠️ — No overflow check on arithmetic (low practical risk but technically UB for `INT64_MAX` inputs).

#### `rt_duration_div`, `rt_duration_is_zero/positive/negative`, `rt_duration_max/min/cmp`, `rt_duration_to_*`, `rt_duration_to_iso`
- ✅✅✅

---

### `rt_reltime.c`

#### `i64_abs(int64_t x) → int64_t` (static)
- **Robustness:** ⚠️ — Same `-INT64_MIN` UB as `rt_duration_abs`.

#### All `rt_reltime_*` functions
- ✅✅✅ — `snprintf` output bounded; edge cases handled.

---

### `rt_result.c`

#### `rt_result_to_string(void *obj) → rt_string`
- **Robustness:** ⚠️ — For `VALUE_STR`, uses `snprintf(buf, 256, "Ok(\"%s\")", ...)`. Long string values truncate silently.
- **Security:** ⚠️ — Embedded null bytes in the string value cause `%s` to truncate output, producing a misleading representation.

#### `rt_result_map`, `rt_result_map_err`, `rt_result_and_then`, `rt_result_or_else`
- **Robustness:** ⚠️ — Silently pass through non-pointer Results (str/i64/f64) without transformation; may be unexpected behavior.

#### All other `rt_result_*` functions
- ✅✅✅ — Null-checked; variant- and type-verified before extraction; abort on misuse.

---

### `rt_retry.c`

#### `rt_retry_next_delay(void *policy) → int64_t`
- **Robustness:** ⚠️ — Exponential backoff: `delay *= 2` can overflow `int64_t` before `max_delay_ms` guard fires; UB for extreme `base_delay_ms` × many retries.

#### All other `rt_retry_*` functions
- ✅✅✅ — Clamps negative inputs; retry state management correct.

---

### `rt_ratelimit.c`

- `current_time_sec`: Windows `static freq` has same data race as `rt_stopwatch.c`.
- All `rt_ratelimit_*` functions: ✅✅✅ — `rt_ratelimit_new` correctly checks `rt_obj_new_i64` (one of few files that does); token bucket correct.

---

### `rt_debounce.c`

- All `rt_debounce_*` and `rt_throttle_*` functions: ✅✅✅ — Negative delay clamped to 0; first-call behavior correct; all null-guarded. Same Windows `static freq` data race as other time files.

---

### `rt_statemachine.c`

- All `rt_statemachine_*` functions: ✅✅✅ — State ID bounds-checked against `RT_STATE_MAX`; duplicate detection; flag management correct (manual clear required by design).

---

### `rt_diff.c`

#### `split_lines(const char *text) → line_array` (static)
- **Robustness:** ⚠️ — `malloc` for line pointer array not null-checked; NULL deref on first assignment.

#### `compute_lcs_table(line_array *a, line_array *b, int **table) → void` (static)
- **Optimization:** ⚠️ — O(m×n) time and space. Myers diff algorithm would be O(n+d) time, O(d) space.

#### `rt_diff_lines(rt_string a, rt_string b) → void *`
- **Robustness:** ⚠️ — `malloc`/`calloc` for LCS table not null-checked.
- **Optimization:** ⚠️ — O(m×n) allocation; for 1000-line files ~4 MB heap.

#### `rt_diff_unified(rt_string a, rt_string b, int64_t context) → rt_string`
- **Robustness:** ⚠️ — `context` parameter accepted but not applied; all lines included regardless. Header hardcoded as `"--- a\n+++ b\n"` with no actual filenames.

#### `rt_diff_patch(rt_string original, void *diff) → rt_string`
- **Robustness:** ⚠️ — `original` parameter entirely ignored (`(void)original`); if diff was computed from a different source, result is silently wrong.

#### `rt_diff_count_changes(rt_string a, rt_string b) → int64_t`
- **Optimization:** ⚠️ — Runs full O(nm) LCS just to count changes; a simpler comparison would suffice.

---

### `rt_msgbus.c`

#### `mb_ensure_topic(rt_msgbus_impl *, rt_string) → mb_topic *` (static)
- **Robustness:** ⚠️ — `calloc(1, sizeof(mb_topic))` not null-checked; NULL deref on `t->name = topic`.

#### `rt_msgbus_new(void) → void *`
- **Robustness:** ⚠️ — `calloc(32, sizeof(mb_topic *))` for bucket array not null-checked.

#### `rt_msgbus_subscribe(void *obj, rt_string topic, void *callback) → int64_t`
- **Robustness:** ⚠️ — `calloc(1, sizeof(mb_sub))` not null-checked; also calls `mb_ensure_topic` which can crash on NULL.

#### `rt_msgbus_unsubscribe(void *obj, int64_t sub_id) → int8_t`
- **Optimization:** ⚠️ — O(total_subs) linear scan; sub_id → topic index would give O(1).

#### `rt_msgbus_clear(void *obj) → void`
- **Robustness:** ⚠️ — Frees subscriber nodes but does NOT free `mb_topic` nodes or their retained `name` strings — partial resource leak differing from `mb_finalizer`.

#### All other `rt_msgbus_*` functions
- ✅✅✅ — `publish`, `subscriber_count`, `total_subscriptions`, `topics`, `clear_topic` all correct.

---

## 6. Supplementary Per-Function Review — GUI, Graphics, Audio, Utilities

### `rt_graphics.c`

#### `rt_canvas_flip(void *obj) → void`
- **Robustness:** ❌ — Calls `exit(0)` on window close — hard process termination inside a library function. All GC finalizers, file flush, and socket close are bypassed.

#### `sin_deg_fp(int deg) → int64_t` (static)
- **Robustness:** ❌ — First interpolation branch (degrees 0–9) is dead code; function always uses 10-degree quantized lookup without interpolation — incorrect for intermediate degree values.

#### `rt_canvas_flood_fill(void *obj, int64_t x, int64_t y, int64_t color) → void`
- **Robustness:** ❌ — O(r²) heap allocation for BFS frontier; no overflow check on `width * height` before `malloc`; for a 4K canvas this allocates up to 266 MB.
- **Optimization:** ❌ — Per-pixel allocation in BFS is the primary bottleneck; a scanline fill would be 10–100× faster.

#### `rt_canvas_arc(void *obj, int64_t cx, int64_t cy, int64_t r, ...) → void`
- **Optimization:** ⚠️ — O(r²) pixel tests per call (tests all pixels in bounding box); Bresenham's circle algorithm is O(r).

#### `rt_canvas_gradient_h(void *obj, ...) → void`, `rt_canvas_gradient_v(void *obj, ...) → void`
- **Optimization:** ~~⚠️ — One `vgfx_line` API call per column/row; direct pixel buffer write would be O(1) system calls vs. O(width/height).~~ **✅ FIXED 2026-02-23** — uses `vgfx_get_framebuffer` for direct writes; horizontal precomputes one row then `memcpy` per scanline.

#### `rt_canvas_ellipse(void *obj, ...) → void`
- **Robustness:** ⚠️ — `rx2 * ry2` intermediate can overflow `int64_t` for large radii.

#### `rt_canvas_polygon(void *obj, ...) → void`
- **Robustness:** ⚠️ — Fixed `intersections[64]`; polygons with more than 62 edges per scanline silently truncate, producing incorrect fill.

#### `rt_canvas_polyline(...)`, `rt_canvas_polygon(...)`
- **Robustness:** ⚠️ — `points_ptr` count not bounded; caller length mismatch causes OOB read.

#### `rt_color_from_hex(rt_string hex) → int64_t`
- **Robustness:** ⚠️ — `strtoul` overflow not checked for non-standard length hex inputs.

#### All other `rt_canvas_*` and `rt_color_*` functions
- ✅✅✅ — Bounds-checked; null-guarded.

---

### `rt_pixels.c`

#### `rt_pixels_load_bmp(rt_string path) → void *`
- **Robustness:** ⚠️ — `data_offset` not validated against file size; `(long)data_offset` truncates on 16-bit `long` platforms.

#### `rt_pixels_load_png(rt_string path) → void *`
- **Robustness:** ⚠️ — PNG chunk length not validated against remaining buffer; custom DEFLATE lacks hardened bounds checking.

#### `rt_pixels_save_png(void *obj, rt_string path) → int8_t`
- **Robustness:** ⚠️ — `rt_compress_deflate` return not null-checked before `.len`/`.data` access.

#### `rt_pixels_resize(void *obj, int64_t new_w, int64_t new_h) → void *`
- **Robustness:** ❌ — OOB read when source image is exactly 1 pixel wide: `src_x` computes to -1 due to bilinear sample offset, reading outside the image buffer.

#### `rt_pixels_clear(void *obj, int64_t color) → void`, `rt_pixels_fill(...) → void`
- **Robustness:** ⚠️ — `width * height` multiplication before `memset` lacks overflow check.

#### `rt_pixels_blur(void *obj, int64_t radius) → void *`
- **Optimization:** ~~⚠️ — O(w×h×r²) box blur; separable two-pass approach would reduce to O(w×h×r), 10–50× faster for radius > 3.~~ **✅ FIXED 2026-02-23** — now uses separable horizontal+vertical passes with a malloc'd temp buffer, reducing complexity to O(w×h×(2r+1)×2).

#### All other `rt_pixels_*` functions
- ✅✅✅

---

### `rt_scene.c`

#### `rt_scene_node_new(void) → void *`
- **Robustness:** ⚠️ — `rt_obj_new_i64` return not null-checked before `memset`.

#### `mark_transform_dirty(rt_scene_node_impl *) → void` (static)
- **Robustness:** ⚠️ — Recursive descent; stack overflow for deeply nested scene graphs (depth > ~10,000).

#### `rt_scene_draw(void *scene, void *canvas) → void`, `rt_scene_draw_with_camera(void *scene, void *canvas, void *camera) → void`
- **Robustness:** ❌ — `nodes` seq created via `rt_seq_new()` on every call and never freed — per-frame memory leak. At 60 FPS over one hour: ~216,000 unreleased seq objects.

#### All other `rt_scene_*` functions
- ✅✅✅ — Transform dirty-flagging and matrix composition correct; null-guarded.

---

### `rt_sprite.c`

#### `rt_sprite_overlaps(void *a, void *b) → int8_t`
- **Robustness:** ⚠️ — `width * scale` multiplication can overflow `int64_t` for large scale factors.

#### All other `rt_sprite_*` functions
- ✅✅✅

---

### `rt_spritebatch.c`

#### `ensure_capacity(rt_spritebatch_impl *) → void` (static)
- **Robustness:** ❌ — `realloc` failure: `rt_trap()` called but `batch->items` not updated; if trap returns, subsequent code uses freed/invalid memory — use-after-free.

#### `rt_spritebatch_new(void) → void *`
- **Robustness:** ⚠️ — After `malloc` failure + `rt_trap()`, struct initialization continues with `items = NULL`.

#### All other `rt_spritebatch_*` functions
- ✅✅✅

---

### `rt_spritesheet.c`

#### `ensure_cap(rt_spritesheet_impl *) → void` (static)
- **Robustness:** ❌ — Two separate `realloc` calls for parallel arrays. If the second fails after the first succeeds, arrays have different capacities but a single `cap` field is updated — subsequent writes to the second array go out of bounds.

#### All other `rt_spritesheet_*` functions
- ✅✅✅

---

### `rt_spriteanim.c`

#### `rt_spriteanim_new(void) → void *`
- **Robustness:** ⚠️ — `rt_obj_new_i64` return not null-checked before field assignment.

#### `rt_spriteanim_update(void *obj) → int8_t`
- **Optimization:** ⚠️ — `speed_accum` loop adds overhead for the common case where speed == 1.0; a fast-path check would skip the accumulator.

#### All other `rt_spriteanim_*` functions
- ✅✅✅ — Frame bounds enforced; speed clamped to [0, 10].

---

### `rt_tilemap.c`

#### `rt_tilemap_pixel_to_tile(...)`, `rt_tilemap_tile_to_pixel(...)`
- **Robustness:** ⚠️ — Division by `tile_width`/`tile_height`; if tileset not yet set (zero dimensions), division-by-zero UB.

#### `rt_tilemap_collide_body(void *tilemap, void *body_ptr) → int8_t`
- **Robustness:** ❌ — Casts `body_ptr` to a locally-defined `body_header` struct with hardcoded field offsets. If physics body struct changes layout, this silently reads garbage.
- **Security:** ⚠️ — Unchecked cast of external object; type confusion possible.

#### `rt_tilemap_draw(...)`, `rt_tilemap_draw_layer(...)`
- **Optimization:** ⚠️ — No frustum culling; all tiles drawn regardless of viewport visibility.

#### All other `rt_tilemap_*` functions
- ✅✅✅ — Bounds-checked; tile IDs > 4096 return 0 for collision.

---

### `rt_particle.c`

#### `rt_particle_emitter_new(int64_t max_particles) → rt_particle_emitter`
- **Robustness:** ⚠️ — `calloc` failure returns NULL without trapping; `rt_obj_new_i64` not null-checked.

#### `rand_range_i64(rt_particle_emitter, int64_t min, int64_t max) → int64_t` (static)
- **Robustness:** ⚠️ — If `rand_double` returns exactly 1.0 (float rounding), result is `max + 1`.

#### `emit_one(rt_particle_emitter, ...) → void` (static)
- **Optimization:** ⚠️ — O(N) linear scan for free slot; degrades for large pools at saturation.

#### `rt_particle_emitter_get(rt_particle_emitter, int64_t index, ...) → int8_t`
- **Optimization:** ⚠️ — O(N) scan for Nth active particle; iterating all active particles is O(N²).

#### All other `rt_particle_emitter_*` functions
- ✅✅✅ — Null-guarded; emitter bounds enforced.

---

### `rt_audio.c`

#### `rt_audio_get_master_volume`, `rt_audio_pause_all`, `rt_audio_resume_all`, `rt_audio_stop_all_sounds`
- **Robustness:** ⚠️ — Access `g_audio_ctx` without atomic load or spinlock — data race with `ensure_audio_init`.

#### `ensure_audio_init(void) → void` (static)
- **Optimization:** ⚠️ — Spinlock busy-waits with no backoff; wastes CPU cycles on contention during init.

#### All other `rt_audio_*` functions
- ✅✅✅ — Null-guarded; volume/pitch clamped.

---

### `rt_screenfx.c`

#### `screenfx_rand_state` (global)
- **Robustness:** ⚠️ — Non-thread-safe global; concurrent calls produce correlated pseudo-random values.

#### `rt_screenfx_update(void *canvas, double dt) → void`
- **Robustness:** ⚠️ — `e->elapsed += dt` has no overflow guard.

---

### `rt_action.c`

#### `find_action_str(rt_string name) → action_entry *` (static)
- **Robustness:** ⚠️ — Accesses `name->data` directly, bypassing `rt_string_cstr`.
- **Optimization:** ⚠️ — O(N) linear scan; hash table preferred for > 100 actions.

#### `strdup_rt_string(rt_string name) → char *` (static)
- **Robustness:** ⚠️ — Returns NULL for zero-length names; callers without null checks crash.

#### `create_binding(...) → Binding *` (static)
- **Robustness:** ⚠️ — `malloc` return not checked.

#### `rt_action_bindings_str(rt_string action) → rt_string`
- **Robustness:** ⚠️ — Fixed `buffer[1024]`; guard `pos < 1000` but final `strlen(desc)` write may overflow near-full buffer.

#### `rt_action_load(rt_string json) → int8_t`
- **Robustness:** ⚠️ — Nested `rt_json_stream_next` loops without termination guard; malformed JSON can cause infinite loop.
- **Security:** ⚠️ — Loaded key codes not range-validated; arbitrary codes injectable from untrusted JSON.

#### `rt_action_save(void) → rt_string`
- **Security:** ⚠️ — Control characters 0x00–0x1F (except `\n`, `\r`, `\t`) not escaped in JSON output — produces malformed JSON.

#### All other `rt_action_*` functions
- ✅✅✅ — Name length validated; duplicate detection; correct binding management.

---

### `rt_fmt.c`

#### `rt_fmt_to_words(int64_t value) → rt_string`
- **Robustness:** ❌ — `value = -value` is signed integer overflow UB for `INT64_MIN`; produces negative "absolute value," causing incorrect word output.

#### All other `rt_fmt_*` functions
- ✅✅✅ — Length calculations conservative; no overflow risks for typical inputs.

---

### `rt_numfmt.c`

#### `rt_numfmt_pad(int64_t n, int64_t width) → rt_string`
- **Robustness:** ⚠️ — `-n` is UB for `INT64_MIN`; `%llu`/`%lld` not portable (should use `PRId64`/`PRIu64`).

#### `rt_numfmt_thousands(int64_t n) → rt_string`
- **Robustness:** ⚠️ — Same `%llu` portability issue.

#### `rt_numfmt_to_words(int64_t value) → rt_string`
- ✅✅✅ — Correctly handles `INT64_MIN` via `(uint64_t)INT64_MAX + 1`.

---

### `rt_version.c`

#### `cmp_prerelease(const char *pa, const char *pb) → int` (static)
- **Robustness:** ⚠️ — Manual decimal parse `va = va * 10 + (pa[i] - '0')` without overflow check; very long numeric pre-release identifiers overflow `long long`.

#### All other `rt_version_*` functions
- ✅✅✅ — Leading zeros rejected per SemVer; `malloc` failure null-checked; error paths free pre-release/build strings correctly.

---

### `rt_guid.c`

#### `get_random_bytes(void *bytes, size_t n) → void` (static)
- **Robustness:** ⚠️ — POSIX: `read()` return checked but partial reads not retried; remaining bytes in UUID are uninitialized if `/dev/urandom` returns fewer than 16 bytes.
- **Security:** ⚠️ — Windows: `CryptGenRandom` return value not checked for failure.

#### `fallback_random_bytes(void *bytes, size_t n) → void` (static)
- **Security:** ⚠️ — `time(NULL)` has 1-second granularity; two processes started in the same second get the same seed. Fallback UUIDs not cryptographically random.

#### All other `rt_guid_*` functions
- ✅✅✅ — UUID format validation checks all 36 positions; byte conversion correct.

---

### `rt_easing.c`

#### `rt_ease_in_circ(double t) → double`
- **Robustness:** ⚠️ — `sqrt(1 - t*t)` goes negative (NaN) for `t > 1.0`; no input clamp.

#### All other `rt_ease_*` functions
- ✅✅✅ — Boundary checks for expo/elastic; pure math; no allocation.

---

### `rt_spline.c`

#### `tangent_catmull_rom(rt_spline_impl *, double t) → (double, double)` (static)
- **Robustness:** ⚠️ — Numerical derivative with `h = 0.0001`; at endpoints where `t0 == t1`, `dt = 0.0` returns zero tangent — not documented.

#### All other `rt_spline_*` functions
- ✅✅✅ — Null-guarded; index bounds-checked; minimum point count enforced.

---

### `rt_codec.c`

#### `rt_codec_hex_enc(void *bytes, int64_t len) → rt_string`
- **Robustness:** ⚠️ — `input_len * 2` can overflow `size_t` for input > 4 GB; no overflow check.

#### All other `rt_codec_*` functions
- ✅✅✅ — Length validation; padding enforcement; `malloc` failures trap; invalid characters trap.

---

### `rt_serialize.c`

#### `g_last_error` (static global)
- **Robustness:** ⚠️ — Global (not thread-local) error state; concurrent serialize calls race on this variable.

#### All other `rt_serialize_*` functions
- ✅✅✅ — Format detection heuristic documented; null-guarded.

---

### `rt_stream.c`

#### `rt_stream_open_file(...)`, `rt_stream_open_memory(...)`, `rt_stream_open_bytes(...)`
- **Robustness:** ⚠️ — `rt_obj_new_i64` return not null-checked before field assignment.

#### All other `rt_stream_*` functions
- ✅✅✅ — Null-guarded; delegates to type-specific implementations.

---

### `rt_memstream.c`

#### `ensure_capacity(rt_memstream_impl *, size_t) → void` (static)
- **Robustness:** ⚠️ — After `realloc` NULL + `rt_trap`: `ms->data` left with the freed pointer. If trap returns, next access is use-after-free.

#### All other `rt_memstream_*` functions
- ✅✅✅ — All null-checked; position clamped; byte-by-byte I/O correct.

---

### `rt_log.c`

#### `log_message(int level, rt_string msg) → void` (static)
- **Optimization:** ⚠️ — `time(NULL)` + `localtime_r` + `strftime` on every log call; caching with 1-second granularity would eliminate most overhead.
- **Security:** ✅ — Message printed with `%s` format literal; no injection risk.

#### All other `rt_log_*` functions
- ✅✅✅ — Level clamped; `fflush(stderr)` after each write.

---

### `rt_textwrap.c`

#### `rt_textwrap_wrap(rt_string text, int64_t width) → rt_string`
- **Robustness:** ⚠️ — `src_len * 2` can overflow `int64_t`; on `malloc` failure, silently returns original text rather than trapping.

#### `rt_textwrap_wrap_lines(rt_string text, int64_t width) → void *`
- **Robustness:** ⚠️ — Intermediate `wrapped` string not explicitly unref'd after splitting; potential leak.

#### All other `rt_textwrap_*` functions
- ✅✅✅ — Conservative allocation sizes; word-wrap logic correct.

---

### `rt_pluralize.c`

#### `rt_pluralize(rt_string word) → rt_string`
- **Robustness:** ⚠️ — Fixed `buf[512]`; silently truncates for words > 508 bytes.

#### `rt_singularize(rt_string word) → rt_string`
- **Robustness:** ⚠️ — Same fixed `buf[512]` truncation risk.

#### `rt_pluralize_count(int64_t count, rt_string word) → rt_string`
- **Robustness:** ⚠️ — Fixed `buf[600]`; long pluralized words near the 600-byte boundary truncate.
- **Security:** ✅ — `%lld %s` with null-terminated string; no injection.

---

### `rt_machine.c`

#### `rt_machine_user(void) → rt_string`
- **Security:** ⚠️ — POSIX falls back to `getenv("USER")` which is user-controlled; not safe for authentication.

#### All other `rt_machine_*` functions
- ✅✅✅ — Platform branches correct; sysctl/sysinfo return values checked; fallbacks provided.

---

### `rt_args.c`

#### `rt_env_set_var(rt_string name, rt_string value) → int8_t`
- **Security:** ⚠️ — Accepts keys/values containing `=` or null bytes; null bytes cause UB in POSIX `setenv`.

#### All other `rt_args_*` and `rt_env_*` functions
- ✅✅✅ — Overflow-checked capacity growth; index OOB traps; Windows error handling correct.

---

### `rt_tween.c`

- All `rt_tween_*` functions: ✅✅✅ — `duration` clamped to 1; `ease_type` range-checked; no heap allocation in hot path.
- **Note:** Easing logic duplicated from `rt_easing.c`; same `sqrt` domain issue for `ease_in_circ`. ⚠️

---

### Remaining Utility Files

- `rt_smoothvalue.c` — All functions: ✅✅✅ — Smoothing clamped to [0, 0.999]; snap-to-target epsilon prevents float drift.
- `rt_camera.c` — All functions: ✅✅✅ — Zoom clamped to [10, 1000]; coordinate transforms safe when set via API.
- `rt_collision.c` — All functions: ✅✅✅ — Pure arithmetic; no allocation; negative margin clamped to 0.
- `rt_buttongroup.c` — All functions: ✅✅✅ — Fixed-size array; all accessors null-guarded.
- `rt_font.c` — All functions: ✅✅✅ — Bounds-checks [32, 126]; returns `empty_glyph` for OOB; O(1) static lookup.
- `rt_output.c` — Global state not atomic/thread-safe ⚠️; all `rt_output_*` functions otherwise ✅✅✅.

---

*End of review. Total: ~67 critical issues, ~230 warnings across ~200 files.*
