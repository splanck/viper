# Linux Platform Implementation

Viper's Linux support is split across small native adapters rather than hidden behind a third-party
portability library. The principal adapters are X11/GLX graphics, ALSA audio, inotify file
watching, Linux/POSIX pseudo-terminals, Linux machine-information probes, and ELF native linking.

## Process-global library state

The X11 backend initializes Xlib threading with `pthread_once` before opening its first display.
Fully constructed windows are published to clipboard and cursor services only after all native
resources have been created. Global cursor and clipboard operations retain the backend lifecycle
lock while using a selected window, so destruction cannot invalidate the selection concurrently.

GLX and XInput2 are loaded once with local, immediate symbol binding. Their shared-object handles
remain loaded for the process lifetime: unloading either library while Xlib or a driver retains
callbacks into it is unsafe. XInput extension opcodes remain display-local because different X
server connections are not required to assign the same opcode.

`VIPER_GRAPHICS_BACKEND=HEADLESS` selects the dependency-free in-memory framebuffer backend while
keeping the public graphics surface enabled. `AUTO`, `NATIVE`, and `X11` currently select X11 on
Linux; Wayland desktops are supported through XWayland when `DISPLAY` is available. See
[ADR 0104](adr/0104-linux-graphics-backend-selection.md) for the native-Wayland boundary and why a
failed desktop display connection does not silently become an invisible headless application.

The ALSA backend does not replace `snd_lib_error_set_handler`. That handler is process-global and
ALSA provides no operation for retrieving and restoring an embedding application's previous
handler. Applications that want to redirect ALSA diagnostics should install their handler before
creating Viper audio contexts.

## File watcher behavior

Linux watchers drain nonblocking inotify data until `EAGAIN`. Create, delete, move, write-close,
content-modification, and attribute changes are translated into the portable watcher event set.
Malformed records, native queue overflow, unmount, a removed watch, and descriptor error/hangup
conditions emit an overflow event requesting a caller rescan. Terminal conditions also make
`IsWatching` false; already queued events remain readable after that transition.

Watcher creation never changes the process-wide `RLIMIT_NOFILE`. Resource exhaustion leaves the
watcher inactive so the caller can report the condition or use periodic rescans. Operators should
configure descriptor and inotify limits outside the process when workloads require more watches.

## Machine information in containers

Linux CPU reporting caps the online host CPU count with cgroup v2 `cpu.max` and
`cpuset.cpus.effective` constraints when available. Total and available memory are similarly capped
by `memory.max` and `memory.current`. Missing, malformed, or unlimited cgroup controls fall back to
the host probes. Arithmetic saturates rather than wrapping when kernel counters exceed `int64`.

Account lookup uses `getpwuid_r`. Empty environment variables are treated as absent, and temporary
directory overrides must name an existing absolute directory.

## Pseudo-terminal lifecycle

On Linux the PTY startup-status pipe is created atomically with `O_CLOEXEC`. Every controlling-TTY,
window-size, and standard-stream setup operation is checked in the child and reported to the parent
before `exec`. Status messages handle interruption and partial transfer. PTY destruction signals the
child session so descendants do not survive merely because the immediate shell spawned them.

## Validation

Run targeted Linux platform tests after an incremental build:

```sh
ctest --test-dir build -R 'test_rt_(watcher|machine|open_load_result_apis)' --output-on-failure
ctest --test-dir build -R 'test_vaud_(audit_fixes|core_fixes)' --output-on-failure
ctest --test-dir build -R 'test_(window|pixels|input)' --output-on-failure
ctest --test-dir build -R linux_headless_graphics_smoke --output-on-failure
./scripts/lint_platform_policy.sh
```

Before proposing changes, run the complete Linux build and test pipeline with
`./scripts/build_viper_linux.sh`.
