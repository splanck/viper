# ViperOS Microkernel Audit & Migration Plan

**Date:** 2025-12-31  
**Scope:** `os/` only (do **not** touch `../compiler/`)  
**Goal:** Identify where the current tree diverges from microkernel architecture and provide a safe, phased plan to migrate without breaking boot, IO, or developer workflows.

**Reference note:** The `path:line` pointers below were spot-checked against the current tree on the date above. Expect line numbers to drift; when in doubt, prefer symbol search (e.g., `rg "sys_channel_create"`).

---

## Implementation Progress

This section is updated as phases are implemented.

- **2025-12-31 — Phase 0 (Docs):** Completed initial documentation sync for the syscall ABI and microkernel-critical interfaces.
  - Updated syscall ABI docs and filled missing device/SHM syscall docs: `docs/syscalls.md`
  - Fixed syscall ABI description drift in the shared header: `include/viperos/syscall_nums.hpp`
  - Fixed user-facing wrapper docs for channel endpoints: `user/syscall.hpp`
  - Marked the older migration document as superseded to prevent confusion: `docs/microkernel_plan.md`
- **2025-12-31 — Phase 0 (Safety + tests + logging):** Added migration safety toggles, server bring-up tests, and quiet-by-default logging controls.
  - Added kernel build-time mode/service toggles: `kernel/include/config.hpp`, `kernel/CMakeLists.txt`, `kernel/main.cpp`
  - Added network/TLS syscall gating behind toggles: `kernel/syscall/table.cpp`, `kernel/arch/aarch64/timer.cpp`
  - Added QEMU integration tests for server bring-up: `tests/CMakeLists.txt`, `tests/tools/run_qemu_test.sh`
  - Added a static ABI guardrail for microkernel-critical syscalls: `tests/host/test_syscall_table.py`
  - Added `scripts/build_viper.sh --no-run` to support build/test without launching QEMU
  - Reduced SSH packet-level debug spam to require `ssh -vv` (verbose levels): `user/libssh/ssh.c`, `user/ssh/ssh.c`
  - Fixed interactive console ergonomics (TTY echo/backspace) and further reduced debug spam in interactive apps:
    - libc stdin line discipline (termios-driven echo + erase/kill handling): `user/libc/src/unistd.c`, `user/libc/src/stdio.c`
    - Net syscall/TCP/virtio IRQ debug logs require kernel log level `Debug` even when compiled in: `kernel/syscall/table.cpp`, `kernel/net/ip/tcp.cpp`, `kernel/drivers/virtio/net.cpp`
  - Validated end-to-end via `scripts/build_viper.sh --test --no-run` (all tests passing)
- **2025-12-31 — Phase 1 (Bootstrap cap delegation + device syscall enforcement):** Implemented spawn-time bootstrap channel and required device capabilities for microkernel device syscalls.
  - `SYS_TASK_SPAWN` now returns a parent bootstrap send endpoint handle in `x3` for capability delegation: `kernel/syscall/table.cpp`, `user/syscall.hpp`, `docs/syscalls.md`
  - Added `cap::Kind::Device` and bootstrapped a root device capability for init (`vinit`) only: `kernel/cap/table.hpp`, `kernel/viper/viper.cpp`, `include/viperos/cap_info.hpp`
  - `vinit` now delegates a derived, transferable device capability to `blkd`/`netd`/`fsd` over the bootstrap channel: `user/vinit/vinit.cpp`
  - Servers accept delegated caps from the bootstrap recv endpoint (handle `0`): `user/servers/blkd/main.cpp`, `user/servers/netd/main.cpp`, `user/servers/fsd/main.cpp`
  - Enforced capability checks for `SYS_MAP_DEVICE`, `SYS_IRQ_REGISTER`, `SYS_DMA_ALLOC`, `SYS_VIRT_TO_PHYS`: `kernel/syscall/table.cpp`
  - Note: the current `Device` capability is coarse-grained (“root device”) and does not yet encode per-device MMIO/IRQ/DMA limits (syscalls still validate against `known_devices[]`).
  - Validated via `scripts/build_viper.sh --test --no-run` (all tests passing)
- **2025-12-31 — Phase 2 (blkd hardening: SHM lifecycle + robustness):** Made shared-memory transfers safe for long-running microkernel IO and tightened block driver behavior.
  - Added `SYS_SHM_CLOSE` and documented it: `include/viperos/syscall_nums.hpp`, `kernel/syscall/table.cpp`, `user/syscall.hpp`, `docs/syscalls.md`
  - Fixed `SYS_SHM_UNMAP` to unmap full regions and release mapping references (tracked per-process mappings): `kernel/syscall/table.cpp`
  - Prevented capability-table exhaustion by explicitly closing transferred SHM handles in `blkd`, `netd`, and `fsd`’s block client: `user/servers/blkd/main.cpp`, `user/servers/netd/main.cpp`, `user/servers/fsd/blk_client.hpp`
  - Added request hygiene in `blkd` (size checks + cleanup on failure) and bounded virtio-blk flush polling (avoid infinite hangs): `user/servers/blkd/main.cpp`, `user/libvirtio/src/blk.cpp`
  - Validated via `scripts/build_viper.sh --test --no-run` (all tests passing)
- **2025-12-31 — Phase 3 (fsd client scaffolding):** Started building the user-space client plumbing needed for libc→fsd filesystem forwarding.
  - Added a reusable `fsd` IPC client library (open/read/write/close/seek; inline-only replies for now): `user/libfsclient/include/fsclient.hpp`, `user/libfsclient/src/fsclient.cpp`, `user/libfsclient/CMakeLists.txt`, `user/CMakeLists.txt`
  - Fixed `opendir()` syscall numbers to match the current ABI (was calling the wrong syscall IDs): `user/libc/src/dirent.c`
  - Validated via `scripts/build_viper.sh --test --no-run` (all tests passing)
- **2025-12-31 — Phase 4 (spawn decoupling: Stage 1):** Added a capability-based “spawn from memory” path to reduce kernel FS coupling.
  - Added `SYS_TASK_SPAWN_SHM` (0x0C) to spawn an ELF image from a `SharedMemory` handle (returns PID/TID + bootstrap channel): `include/viperos/syscall_nums.hpp`, `kernel/syscall/table.cpp`, `user/syscall.hpp`, `docs/syscalls.md`
  - Validated via `scripts/build_viper.sh --test --no-run` (all tests passing)
- **2025-12-31 — Phase 4 (spawn decoupling: Stage 2):** Exercised `SYS_TASK_SPAWN_SHM` end-to-end by loading executables via `fsd`.
  - Added `RunFSD` shell command to read an ELF via `fsd` and spawn via SHM: `user/vinit/cmd_misc.cpp`, `user/vinit/shell.cpp`, `user/vinit/readline.cpp`, `user/vinit/cmd_system.cpp`, `user/vinit/vinit.hpp`
  - Linked `vinit.elf` with `viperfsclient` and ensured fsd client connections are closed (avoid handle leaks): `user/CMakeLists.txt`, `user/libfsclient/include/fsclient.hpp`
  - Fixed a bootstrap channel handle leak in `Run` (always close unused `SYS_TASK_SPAWN` bootstrap send endpoint): `user/vinit/cmd_misc.cpp`
  - Validated via `scripts/build_viper.sh --test --no-run` (all tests passing)
- **2026-01-01 — Phase 3 (libc→fsd integration + validation):** Routed libc file/dir APIs through `fsd` when present and added an automated smoke test.
  - Implemented `FS_READDIR` and `FS_RENAME` in `fsd` with offset-tracking directory iteration: `user/servers/fsd/main.cpp`, `user/servers/fsd/viperfs.hpp`, `user/servers/fsd/viperfs.cpp`
  - Extended `viperfsclient` with `stat/fstat/mkdir/rmdir/unlink/rename/readdir_one`: `user/libfsclient/include/fsclient.hpp`, `user/libfsclient/src/fsclient.cpp`
  - Wired libc to prefer `fsd` for file and directory operations (kernel fallback preserved for `/dev` and when `fsd` is unavailable): `user/libc/src/fsd_backend.cpp`, `user/libc/src/unistd.c`, `user/libc/src/stat.c`, `user/libc/src/stdio.c`, `user/libc/src/dirent.c`, `user/libc/include/unistd.h`, `user/libc/CMakeLists.txt`
  - Added `fsd_smoke.elf` (run automatically when `FSD` is ready) and a new QEMU test `qemu_libc_fsd_smoke`: `user/fstest/fsd_smoke.c`, `user/vinit/vinit.cpp`, `user/CMakeLists.txt`, `scripts/build_viper.sh`, `tests/CMakeLists.txt`
  - Validated via `scripts/build_viper.sh --test --no-run` (19/19 tests passing)
- **2026-01-01 — Phase 5 (networking groundwork: socket FDs + poll + netd smoke):** Fixed libc socket/poll plumbing needed before switching the default networking provider to `netd`.
  - Virtualized libc socket descriptors (kernel socket IDs start at 0 and collide with stdio): `user/libc/src/socket.c`, `user/libc/src/unistd.c`
  - Replaced bogus `poll()/select()` syscall stubs with a pollset-based implementation using `SYS_POLL_*` and pseudo-handles: `user/libc/src/poll.c`
  - Added `netd_smoke.elf` (run automatically when `NETD` is ready) and a new QEMU test `qemu_netd_smoke`: `user/fstest/netd_smoke.cpp`, `user/vinit/vinit.cpp`, `user/CMakeLists.txt`, `scripts/build_viper.sh`, `tests/CMakeLists.txt`
  - Refreshed user-facing docs for the microkernel bring-up path and `RunFSD`: `README.md`, `docs/README.md`, `docs/shell-commands.md`, `docs/index.md`, `docs/syscalls.md`, `docs/microkernel_plan.md`
  - Validated via `scripts/build_viper.sh --test --no-run` (20/20 tests passing)
- **2025-12-31 — Phase 5 (libc→netd integration):** Wired libc socket/DNS APIs to prefer `netd` when available, with kernel fallback.
  - Added `libnetclient` library for netd IPC client operations (socket create/connect/send/recv/close/status, DNS resolve, event subscription): `user/libnetclient/include/netclient.hpp`, `user/libnetclient/src/netclient.cpp`, `user/libnetclient/CMakeLists.txt`
  - Added libc→netd backend bridge with blocking recv semantics and event channel integration: `user/libc/src/netd_backend.cpp`
  - Extended libc socket functions to select backend (kernel vs netd) based on availability: `user/libc/src/socket.c`
  - Extended libc poll to query netd socket status and wait on netd event channels: `user/libc/src/poll.c`
  - Extended libc DNS (`gethostbyname`/`getaddrinfo`) to prefer netd DNS with kernel fallback: `user/libc/src/netdb.c`
  - Added `NET_SOCKET_STATUS` and `NET_SUBSCRIBE_EVENTS` protocol messages to netd: `user/servers/netd/net_protocol.hpp`, `user/servers/netd/main.cpp`
  - Added socket status queries and event notification in netd network stack: `user/servers/netd/netstack.cpp`, `user/servers/netd/netstack.hpp`
  - Linked libc with `vipernetclient`: `user/libc/CMakeLists.txt`, `user/CMakeLists.txt`
  - Note: SSH/SFTP now work via netd when the second virtio-net device is available; kernel networking remains as fallback.
- **2025-12-31 — Phase 5 (networking bug fixes):** Fixed libc→netd fallback and netd TCP connect reliability.
  - Fixed DNS resolution to fall back to kernel when netd DNS fails: `user/libc/src/netdb.c`
  - Fixed socket creation to fall back to kernel when netd fails: `user/libc/src/socket.c`
  - Fixed connect to fall back to kernel socket when netd connect times out: `user/libc/src/socket.c`
  - Fixed TCP SYN retry logic in netd when ARP resolution is pending: `user/servers/netd/netstack.cpp`
- **2026-01-01 — Phase 5 (SSH stdin fix):** Fixed SSH interactive mode blocking on network reads, preventing stdin input.
  - Root cause: `__viper_netd_socket_recv` blocked indefinitely on netd event channel when waiting for data, ignoring console input.
  - Made netd socket recv non-blocking: returns `VERR_WOULD_BLOCK` immediately instead of waiting: `user/libc/src/netd_backend.cpp`
  - Added libc recv() EAGAIN conversion for POSIX compatibility: `user/libc/src/socket.c`
  - Updated libssh to handle EAGAIN: returns `SSH_AGAIN` when no data available, allowing caller to poll() for other events: `user/libssh/ssh.c`
  - Improved SSH client poll loop to use 100ms timeout and single-read-per-iteration to ensure stdin is regularly checked: `user/ssh/ssh.c`
  - Validated via `ctest --test-dir build` (20/20 tests passing)

---

## 1) Definitions (what “microkernel” means for this repo)

For ViperOS, the **microkernel core** should be limited to:

- **Isolation primitives:** address spaces, page tables, task/thread context switching (`kernel/viper/`, `kernel/mm/`, `kernel/arch/`, `kernel/sched/`)
- **IPC primitives:** channels + capability transfer (`kernel/ipc/`, `kernel/ipc/channel.hpp:1`, `kernel/syscall/table.cpp:542`)
- **Security primitives:** capability tables, rights enforcement, revocation (`kernel/cap/`)
- **Low-level interrupt/time handling:** GIC + timer + minimal dispatch to user-space (`kernel/arch/`)
- **Minimal device mediation:** map MMIO, DMA buffers, IRQ wait/ack **without** embedding full drivers (`SYS_MAP_DEVICE`/`SYS_DMA_*`/`SYS_IRQ_*` in `kernel/syscall/table.cpp:2750`)

Everything else (filesystem, network stack, most drivers, TLS/HTTP, naming policy) should migrate to **user-space servers**.

---

## 2) What is already “microkernel-ish” today (inventory)

### 2.1 Kernel primitives that match microkernel goals

- **Process / address space isolation (“Viper” processes):**
  - `kernel/viper/viper.hpp:77`
  - `kernel/viper/address_space.cpp:99`
- **Capability tables + rights + derivation:**
  - Rights include device-related bits (`CAP_DEVICE_ACCESS`, `CAP_IRQ_ACCESS`, `CAP_DMA_ACCESS`) in `kernel/cap/rights.hpp:47`
  - Capability table implementation: `kernel/cap/table.hpp:9`
- **Channel IPC + capability transfer:**
  - In-kernel channel subsystem supports payload + up to 4 transferred handles: `kernel/ipc/channel.hpp:18`
  - Syscall surface supports handle transfer (note: docs are stale; see §3.4): `kernel/syscall/table.cpp:542`
- **Shared memory for fast IPC / bulk transfer:**
  - `SYS_SHM_CREATE`, `SYS_SHM_MAP`, `SYS_SHM_UNMAP` in `kernel/syscall/table.cpp:3701`
- **Device syscalls intended for user-space drivers:**
  - `SYS_MAP_DEVICE`, `SYS_DMA_ALLOC/FREE`, `SYS_IRQ_REGISTER/WAIT/ACK/UNREGISTER`, `SYS_DEVICE_ENUM` in `kernel/syscall/table.cpp:3692`
  - Safety checks exist (known MMIO regions, IRQ ownership, etc.) in `kernel/syscall/table.cpp:2704`
- **Event multiplexing (poll/pollset):**
  - Pollset subsystem: `kernel/ipc/pollset.cpp:359`
  - Exposed via `SYS_POLL_CREATE/ADD/WAIT`: `kernel/syscall/table.cpp:750`

### 2.2 User-space servers + libraries (microkernel service layer scaffolding)

These exist and are launched opportunistically by init:

- Init starts servers and falls back if unavailable: `user/vinit/vinit.cpp:82`
- **Block device server** `blkd`: `user/servers/blkd/main.cpp:1`
- **Filesystem server** `fsd`: `user/servers/fsd/main.cpp:1` (talks to blkd via `user/servers/fsd/blk_client.hpp:1`)
- **Network server** `netd`: `user/servers/netd/main.cpp:1` + user-space stack in `user/servers/netd/netstack.cpp:1`
- **User-space virtio support library** (drivers use device syscalls): `user/libvirtio/include/device.hpp:1`

### 2.3 Boot-time “dedicated device” model already exists

The build/test scripts already provision separate devices for servers so they can coexist with kernel services during bring-up:

- `scripts/build_viper.sh:273` creates `build/microkernel.img` (copy of `disk.img`)
- `scripts/build_viper.sh:315` adds a second virtio-blk device (`disk1`) for servers
- `scripts/build_viper.sh:336` adds a second virtio-net device (`net1`) when `netd.elf` exists
- `scripts/test-qemu.sh:252` also adds these devices during CI-ish tests

Servers intentionally search for **unconfigured** virtio devices (STATUS==0) and skip ones the kernel already claimed:

- `user/servers/blkd/main.cpp:64`
- `user/servers/netd/main.cpp:71`

---

## 3) Where the implementation violates microkernel architecture (findings)

### 3.1 Large “OS services” are still inside the kernel (hybrid kernel)

These are the big deviations from microkernel design:

- **Kernel drivers**: virtio blk/net/gpu/input/rng, framebuffer, etc.
  - Example initialization wiring: driver includes in `kernel/main.cpp:33`, plus init functions like `virtio::net_init()` in `kernel/drivers/virtio/net.cpp:767`
- **Kernel filesystem**: ViperFS + VFS + kernel file/dir objects
  - ViperFS: `kernel/fs/viperfs/viperfs.hpp:117`
  - VFS syscalls call in-kernel VFS directly: `kernel/syscall/table.cpp:845`
- **Kernel networking stack**: TCP/IP + DNS + HTTP
  - `kernel/net/network.cpp:57`, `kernel/net/ip/tcp.cpp:1`, `kernel/net/dns/dns.cpp:224`, `kernel/net/http/http.cpp:113`
  - Sockets are kernel syscalls backed by the kernel TCP stack: `kernel/syscall/table.cpp:1033`
- **Kernel TLS**: TLS sessions and `SYS_TLS_*` are kernel-resident
  - TLS syscalls: `kernel/syscall/table.cpp:1898`
  - TLS implementation: `kernel/net/tls/tls.cpp:173`
- **Kernel timer IRQ does policy work**: input and network polling in interrupt context
  - `kernel/arch/aarch64/timer.cpp:189`
- **Kernel loader is required for spawn** and currently depends on kernel FS
  - `SYS_TASK_SPAWN` → `loader::spawn_process`: `kernel/syscall/table.cpp:258`
- **Kernel service discovery (“assigns”) is kernel-resident policy**
  - Assign implementation: `kernel/assign/assign.cpp:1`
  - Used heavily by vinit + servers for discovery/registration: `user/vinit/vinit.cpp:68`

### 3.2 User-space servers exist but are not the default service providers

- `vinit` attempts to launch servers, but the system explicitly falls back to kernel services today:
  - `user/vinit/vinit.cpp:104`
- Most user programs (`ssh`, `sftp`, utilities, libc) still call kernel FS/network syscalls directly.
  - Example: kernel VFS syscalls: `kernel/syscall/table.cpp:845`
  - Example: kernel socket syscalls: `kernel/syscall/table.cpp:1033`

### 3.3 Device capability policy is temporary and not “real microkernel security” yet

Device syscalls are protected by a **bring-up policy** that allows init (Viper ID 1) and descendants, even without explicit device capability objects:

- `device_syscalls_allowed()` in `kernel/syscall/table.cpp:2704`

This is acceptable for bring-up but is a major gap for microkernel correctness (least privilege, explicit delegation, “servers only get the devices they’re granted”).

### 3.4 Documentation and ABI drift (microkernel-sensitive)

Some docs don’t match the syscall ABI implemented in `kernel/syscall/table.cpp`, which is dangerous during microkernel refactors:

- `SYS_CHANNEL_CREATE` returns **two handles** (send+recv) in reality: `kernel/syscall/table.cpp:542`
- `SYS_CHANNEL_SEND/RECV` support **handle transfer** in reality: `kernel/syscall/table.cpp:594`
- `SYS_POLL_WAIT` uses an event array + `max_events` + `timeout_ms` in reality: `kernel/syscall/table.cpp:791`
- `docs/syscalls.md` still documents older/partial signatures for several IPC/event syscalls:
  - Channels: `docs/syscalls.md:202`
  - Poll: `docs/syscalls.md:270`
  - Spawn: `docs/syscalls.md:127` (docs diverge from `user/syscall.hpp:352` and `kernel/syscall/table.cpp:258`)

Before migrating services to user-space, ABI docs must be made authoritative and kept in sync.

### 3.5 Kernel scope audit (directory map)

This is a coarse but actionable map of what is currently in the kernel, and what a microkernel target implies for each area.

| Area | Today | Microkernel target | Notes |
| --- | --- | --- | --- |
| `kernel/arch/` | CPU/MMU/exception/IRQ/timer **plus** policy polling in timer IRQ | Keep; move policy polling out | `timer_irq_handler()` polls input + net: `kernel/arch/aarch64/timer.cpp:189` |
| `kernel/cap/` | Capability tables + rights + derive/transfer scaffolding | Keep | Foundation for least-privilege servers |
| `kernel/ipc/` | Channels, poll/pollset, timers | Keep | Microkernel IPC core; docs must match ABI (§3.4) |
| `kernel/mm/`, `kernel/sched/`, `kernel/viper/` | Memory + scheduling + address spaces/process objects | Keep | Consider “policy vs mechanism” audits over time, but these belong in-kernel |
| `kernel/syscall/` | Mix of microkernel primitives and full OS service syscalls (VFS/sockets/TLS/etc.) | Split core vs services; progressively disable/redirect services | Do this only behind toggles (§4.2) |
| `kernel/drivers/` | VirtIO blk/net/gpu/input/rng + fwcfg/ramfb | Move to user space | Keep only what is strictly required for boot + debug (serial + interrupt/timer) |
| `kernel/fs/` | VFS + ViperFS + kernel fd tables + path resolution | Move to `fsd` + client libs | Loader/spawn coupling must be resolved (Phase 4) |
| `kernel/net/` | TCP/IP + DNS + HTTP + TLS components | Move to `netd` + user-space TLS/HTTP libs | Timer-based polling should go away (see `kernel/net/network.cpp:65`) |
| `kernel/console/`, `kernel/input/` | Graphics console + input polling | Move to `consoled`/`inputd` (Phase 7) | Keep serial console as minimal recovery/debug |
| `kernel/assign/` | Kernel-resident name/service registry | Optional to move late | Consider a user-space name server once the system can bootstrap it safely |
| `kernel/loader/` | Loads ELFs from kernel FS | Decouple | Leverage `loader::spawn_process_from_blob` + a new syscall (Phase 4) |
| `kernel/tests/` | Boot-time tests and diagnostics | Keep behind debug config | Ensure “microkernel mode” isn’t permanently coupled to bring-up tests |

---

## 4) Safety constraints (do not skip; this is where microkernels get bricked)

### 4.1 Non-negotiable invariants to preserve

- Kernel must always boot to a shell prompt even if **all servers fail**.
- “Recovery mode” must exist: a build configuration where kernel services are enabled (current behavior).
- No change should require simultaneous edits across unrelated subsystems without a toggle/guard.
- Every IPC interface must be versioned or backward-compatible (message structs are effectively ABI).
- Every migration step needs:
  - A clear rollback switch (compile-time or boot-time)
  - A targeted test that fails if the new path regresses

### 4.2 Recommended toggles (add early; use forever)

Add a single “microkernel mode” switch that gates *behavior* rather than deleting code immediately:

- `KERNEL_SERVICES=on/off` (FS/net/TLS in kernel)
- `USERSPACE_SERVERS=on/off` (attempt to start servers; already done in vinit)
- `FORWARD_SYSCALLS_TO_SERVERS=on/off` (kernel shims or libc shims)

This prevents “half migrated” states from trapping you in a non-bootable system.

### 4.3 Operating procedure (how to change this safely)

The single biggest risk is making multiple interdependent changes without a recovery path. Treat each phase as an isolated, revertible patch series.

- Prefer `scripts/build_viper.sh` for build + test + boot (it already provisions dedicated microkernel devices).
- Always validate in both modes before declaring a phase “done”:
  - **HYBRID mode** (kernel services on, servers optional) must keep working forever.
  - **MICROKERNEL mode** (kernel services partially/fully off) is allowed to lag behind, but must never remove recovery.
- Never land a change unless you can answer (in the commit/PR description) all of:
  - What toggle(s) gate it?
  - What breaks if the new server fails to start?
  - What is the rollback path (1 command, 1 flag, or 1 config change)?
  - What is the minimal smoke test sequence to prove it works?

---

## 5) Phased work plan (detailed and dependency-aware)

This plan is intentionally conservative and assumes frequent regressions are likely.

### Phase 0 — Baseline + instrumentation (make the system safe to change)

**Goal:** Ensure we can detect regressions early and always recover.

1. **Make ABI docs authoritative**
   - Pick a single source of truth:
     - Recommended: treat `kernel/syscall/table.cpp` + `include/syscall.hpp` + `user/syscall.hpp` as the authoritative ABI, and update `docs/syscalls.md` to match them.
   - Update `docs/syscalls.md` to match reality for at least the microkernel-critical surface:
     - `SYS_CHANNEL_CREATE` (two handles)
     - `SYS_CHANNEL_SEND/RECV` (handle transfer args)
     - `SYS_POLL_*` (event array + return count)
     - `SYS_TASK_SPAWN` actual signature used by `user/syscall.hpp:352`
     - Device syscalls (`SYS_MAP_DEVICE`, `SYS_IRQ_*`, `SYS_DMA_*`, `SYS_DEVICE_ENUM`)
   - Add a small CI guardrail:
     - A “microkernel-critical syscall ABI” guardrail in `tests/host/test_syscall_table.py` that fails fast if argcounts drift (catching ABI breakage before it bricks servers).
2. **Add explicit microkernel/hybrid boot mode**
   - A kernel config header / CMake option (e.g., `VIPER_KERNEL_ENABLE_FS`, `VIPER_KERNEL_ENABLE_NET`, etc.)
   - A visible boot banner: “HYBRID” vs “MICROKERNEL MODE”
3. **Expand QEMU tests to cover server bring-up**
   - New tests in the CTest/QEMU harness (`tests/CMakeLists.txt` + `tests/tools/run_qemu_test.sh`), since `scripts/build_viper.sh --test` runs `ctest`:
     - Assert `vinit` attempts server startup
     - Assert servers register assigns (`BLKD`, `FSD`, `NETD`) when dedicated devices exist
     - Assert fallback text when they don’t
4. **Logging discipline**
   - Keep kernel debug logs off by default (already started for network/ssh), but provide opt-in flags.
   - Add per-subsystem log toggles so microkernel debugging doesn’t flood interactive apps.

**Exit criteria:** No functional change; only toggles + docs + tests. Default path remains current behavior.

---

### Phase 1 — Device Manager + real capability delegation (security foundation)

**Goal:** Replace bring-up “allow init descendants” with explicit, least-privilege delegation.

1. **Define a real device capability model**
   - Introduce a `cap::Kind::Device` (or similar) representing:
     - Which MMIO range(s) may be mapped
     - Which IRQ(s) may be registered
     - Whether DMA is permitted (and maximum size)
   - Update device syscalls to require a *specific* device capability handle, not “any cap entry with CAP_DEVICE_ACCESS”.
2. **Add kernel-side device ownership tracking**
   - Track “claimed” devices so that:
     - Kernel drivers cannot silently steal devices intended for servers
     - Servers can’t claim a device already owned by another server
3. **Bootstrap policy**
   - Decide who holds initial device caps:
     - Option A: kernel grants vinit a “device root” capability; vinit spawns servers and transfers narrowed caps.
     - Option B: introduce a dedicated `devd` user-space server that owns device caps and leases them.
   - Remove `device_syscalls_allowed()` once capability delegation works.

**Validation:**
- QEMU test: a server without device caps cannot map MMIO or register IRQ; with caps it can.

**Exit criteria:** Servers can be launched with explicit device rights; no more “init descendants get devices”.

---

### Phase 2 — Block service hardening (blkd becomes dependable)

**Goal:** Make `blkd` robust enough to be the only block driver in microkernel mode.

1. **Audit `blkd` correctness + failure handling**
   - Ensure request validation is complete (sector range, size, overflow).
   - Ensure all transferred handles are closed/unmapped on every error path.
   - Add timeouts where the driver waits on IRQ/poll loops.
2. **Performance & safety**
   - Prefer shared memory + DMA buffers with strict size caps.
   - Consider request batching and a fixed pool of SHM/DMA buffers.
3. **Service protocol stability**
   - Version protocol structs (`blk_protocol.hpp`) or reserve fields.
   - Define error codes (map to `VERR_*` or POSIX-ish errors consistently).

**Validation:**
- Dedicated QEMU test for read/write/flush/info via IPC.
- Fault injection: server crash during request should not deadlock clients forever (client-side timeout).

**Exit criteria:** `fsd` can mount and run basic filesystem ops using only `blkd` on a dedicated disk.

---

### Phase 3 — Filesystem service integration (fsd becomes the default FS for user programs)

**Goal:** User processes should no longer call in-kernel VFS for normal operation.

1. **Decide forwarding strategy**
   - Option A (cleaner kernel): libc calls `fsd` directly (user-space client library).
   - Option B (compatibility): kernel keeps POSIX-like syscalls but forwards them to `fsd` internally.
   - Recommendation: start with **A** to avoid expanding kernel TCB, but keep **B** as fallback if libc refactor is too risky.
2. **Build a reusable FS client library**
   - Implement `libfsclient` that:
     - Resolves `FSD` via assign
     - Maintains per-process connection state and request ids
     - Implements open/read/write/close/readdir/mkdir/unlink/rename/stat
     - Uses pollset for blocking instead of spin-yield loops
3. **Wire libc to prefer fsd**
   - Update `user/libc` syscalls (`open`, `read`, `write`, etc.) to:
     - If `FSD` is available, use fsd IPC
     - Else fallback to current kernel syscalls
4. **Fix boot/runtime split-brain**
   - Today, kernel loads ELFs from kernel-mounted ViperFS; fsd mounts a separate disk (`microkernel.img`).
   - If user programs write or install new files via fsd, kernel `spawn()` won’t see them.
   - Short-term mitigation:
     - Treat fsd disk as “user FS” and accept limitations for now (document clearly).
   - Long-term fix (required): see Phase 4 (spawn/loader).

**Validation:**
- QEMU test: `Dir`, `MakeDir`, `Type`, `Copy` (or equivalent) exercise libc → fsd path.
- Regression test: if fsd fails to start, libc falls back and system still works.

**Exit criteria:** Common user workflows use fsd when present; kernel VFS remains only as fallback.

---

### Phase 4 — Process loading and spawn decoupling (critical for true microkernel)

**Goal:** Eliminate the kernel’s dependency on a full filesystem implementation for `spawn()`.

This is the hardest coupling in the system today (`SYS_TASK_SPAWN` → kernel loader → kernel FS).

1. **Choose an end-state**
   - Option A: Keep kernel loader, but make it “IO-agnostic” by reading executables via a minimal file-provider interface (fsd).
   - Option B: Move ELF loading to user space and add new syscalls for “create process + map pages + start thread”.
2. **Recommend a staged approach**
   - Stage 1 (lower risk): keep kernel loader but introduce a new syscall:
     - `SYS_TASK_SPAWN_FD` (spawn from an already-open FD) OR
     - `SYS_TASK_SPAWN_MEM` (spawn from a user buffer containing the ELF)
   - Note: the kernel already has a loader entry point for “spawn from memory” (`loader::spawn_process_from_blob` in `kernel/loader/loader.cpp:410`); a syscall can be a thin wrapper over that.
   - Prefer a capability-based memory passing scheme for safety:
     - `SYS_TASK_SPAWN_SHM(shm_handle, offset, length, name, args)` (avoids copying large ELFs through the syscall boundary)
     - Or `SYS_TASK_SPAWN_BLOB(blob_handle, name, args)` if blobs are already a stable user-visible object in the ABI.
   - Stage 2: when fsd is stable, make the standard `spawn(path)` resolve via fsd by default.
3. **Add the minimum missing kernel primitives if needed**
   - If you go user-space loader (Option B), you’ll need explicit VM syscalls (map pages, set permissions, map stack, set entry point).

**Validation:**
- QEMU test: write an executable via fsd, then spawn it successfully (this proves kernel is no longer tied to kernel FS).

**Exit criteria:** Kernel no longer needs ViperFS/VFS to spawn new tasks.

---

### Phase 5 — Network service integration (netd becomes the default networking provider)

**Goal:** User processes use `netd` sockets/DNS instead of kernel sockets and kernel DNS.

0. **Fix socket FD semantics + readiness plumbing**
   - Kernel TCP sockets are currently identified by small integer IDs starting at 0 (not POSIX FDs); libc must virtualize them so they don’t collide with `stdin/stdout/stderr`.
   - libc `poll()/select()` must be implemented on top of `SYS_POLL_*` (poll sets) so libssh (`ssh_channel_poll`) and interactive apps can multiplex socket I/O correctly.
   - This groundwork should be in place before attempting libc→netd forwarding.
1. **Harden netd + its stack**
   - Validate correctness and completeness of `user/servers/netd/netstack.cpp`.
   - Ensure IRQ-driven RX works reliably (avoid timer polling).
2. **Client library + libc integration**
   - Implement `libnetclient`:
     - Socket create/connect/send/recv/close via `net_protocol.hpp`
     - Blocking + poll integration
     - Shared memory for large payloads
   - Update libc `socket()/connect()/send()/recv()` to prefer netd if present; fallback to kernel syscalls otherwise.
3. **Deprecate kernel socket syscalls**
   - Keep `SYS_SOCKET_*` temporarily as fallback/compat.
   - Add a build mode where they are stubbed/forwarded or removed.

**Validation:**
- QEMU test: `ssh.elf` and `sftp.elf` work over netd (not kernel sockets) when netd is available.

**Exit criteria:** netd is the default provider; kernel net stack can be disabled without losing user networking.

---

### Phase 6 — TLS/HTTP/DNS policy moves out of kernel

**Goal:** Remove crypto/network policy from the kernel TCB.

- Replace `SYS_TLS_*` with a user-space library that runs atop netd sockets.
- Move HTTP client logic out of kernel (today it lives in `kernel/net/http/`).
- Keep only minimal crypto needed for kernel internal integrity (if any).

**Exit criteria:** No kernel TLS sessions; kernel does not parse certificates or perform TLS handshakes.

---

### Phase 7 — Optional: console + input servers (UI out of kernel)

**Goal:** Further reduce kernel footprint; keep only minimal serial debug.

- Introduce `consoled`/`inputd` servers using device syscalls for UART/virtio-input.
- Decide whether graphics console stays kernel-resident for now (acceptable during bring-up).
- Optional (late): move the assign/name registry out of the kernel once bootstrap is solved (e.g., a `named` server started by vinit, with a minimal kernel bootstrap handle).

---

### Phase 8 — Final cleanup: remove kernel services, enforce “microkernel-only”

**Goal:** Make the microkernel configuration the primary one.

- Remove or hard-disable in-kernel FS/net stacks in microkernel build.
- Remove bring-up-only policies.
- Ensure server crash isolation: restarting `fsd`/`netd` doesn’t require reboot.

---

## 6) Cross-check list (things to verify before each phase)

- `vinit` still boots and provides a prompt even if servers fail: `user/vinit/vinit.cpp:104`
- QEMU provides dedicated devices for servers when desired:
  - `scripts/build_viper.sh:315`, `scripts/build_viper.sh:336`
- Servers select unconfigured virtio devices (STATUS==0):
  - `user/servers/blkd/main.cpp:64`, `user/servers/netd/main.cpp:71`
- Kernel IRQ registration rules won’t block servers:
  - `sys_irq_register` refuses IRQs with existing kernel handlers: `kernel/syscall/table.cpp:2869`
- Docs match reality for IPC syscalls (channel create/send/recv):
  - `kernel/syscall/table.cpp:542`

---

## 7) Open questions / decisions required (resolve early)

1. **Who owns device capabilities?** vinit vs a dedicated `devd` server.
2. **Forwarding strategy:** libc→server vs kernel→server shims for FS and networking.
3. **Spawn/loader end-state:** kernel loader with server IO vs user-space loader with new VM syscalls.
4. **Single-disk vs dual-disk model:** dedicated “microkernel.img” is safe but creates split-brain unless spawn is fixed.
5. **Performance goals:** how much copying is acceptable vs SHM everywhere.

---

## 8) Immediate recommended next actions (lowest risk, highest leverage)

1. Phase 0 (docs + toggles + tests), because it prevents bricking the system later.
2. Phase 1 (real device caps), because it’s foundational for correctness/security.
3. Phase 2 (blkd hardening), because fsd depends on it and it exercises the microkernel device syscalls heavily.
