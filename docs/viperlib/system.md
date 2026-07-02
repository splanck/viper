---
status: active
audience: public
last-verified: 2026-06-20
---

# System

> System interaction, environment, and process execution.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.System.Environment](#viperenvironment)
- [Viper.System.Clipboard](#vipersystemclipboard)
- [Viper.System.Shutdown](#vipersystemshutdown)
- [Viper.System.Exec](#viperexec)
- [Viper.System.Process](#vipersystemprocess)
- [Viper.System.Pty](#vipersystempty)
- [Viper.System.Machine](#vipermachine)
- [Viper.Runtime.Unsafe](#viperruntimeunsafe)
- [Viper.Runtime.GC](#viperruntimegc)
- [Viper.Memory](#vipermemory)
- [Viper.Memory.GC](#vipermemory-gc)
- [Viper.Terminal](#viperterminal)

---

## Viper.System.Environment

Command-line arguments and environment access.

**Type:** Static utility class

### Methods

| Method                     | Signature              | Description                                                             |
|----------------------------|------------------------|-------------------------------------------------------------------------|
| `GetArgumentCount()`       | `Integer()`            | Returns the number of program arguments                                 |
| `GetArgument(index)`       | `String(Integer)`      | Returns the program argument at the specified zero-based index           |
| `GetCommandLine()`         | `String()`             | Returns the program arguments joined as a single string                  |
| `GetVariable(name)`        | `String(String)`       | Returns the value of an environment variable, or `""` when missing      |
| `HasVariable(name)`        | `Boolean(String)`      | Returns `TRUE` when the environment variable exists                     |
| `IsNative()`               | `Boolean()`            | Returns `TRUE` when running native code, `FALSE` when running in the VM |
| `SetVariable(name, value)` | `Void(String, String)` | Sets or overwrites an environment variable (empty value allowed)        |
| `EndProgram(code)`         | `Void(Integer)`        | Terminates the program with the provided exit code                      |

### Zia Example

```rust
module EnvDemo;

bind Viper.Terminal;
bind Viper.System.Environment as Env;
bind Viper.Text.Fmt as Fmt;

func start() {
    Say("Args: " + Fmt.Int(Env.GetArgumentCount()));
    Say("Native: " + Fmt.Bool(Env.IsNative()));

    // Environment variables
    var home = Env.GetVariable("HOME");
    Say("HOME: " + home);
    Say("Has HOME: " + Fmt.Bool(Env.HasVariable("HOME")));
}
```

### BASIC Example

```basic
' Program invoked as: viper front basic -run app.bas -- arg1 arg2 arg3

DIM count AS INTEGER
count = Viper.System.Environment.GetArgumentCount()
PRINT "Argument count: "; count  ' Output: 3

FOR i = 0 TO count - 1
    PRINT "Arg "; i; ": "; Viper.System.Environment.GetArgument(i)
NEXT i
' Output:
' Arg 0: arg1
' Arg 1: arg2
' Arg 2: arg3

PRINT "Full command: "; Viper.System.Environment.GetCommandLine()

' Arguments before -- belong to the viper tool. GetArgument(0) is the first
' argument after --, not the tool or executable name.

' Environment variables
DIM name AS STRING
DIM value AS STRING
name = "VIPER_SAMPLE_ENV"
value = Viper.System.Environment.GetVariable(name)
IF Viper.System.Environment.HasVariable(name) THEN
    PRINT name; " is set to "; value
ELSE
    PRINT name; " is not set"
END IF

Viper.System.Environment.SetVariable(name, "abc")
PRINT "Updated value: "; Viper.System.Environment.GetVariable(name)

' Values round-trip as UTF-8 across platforms.
Viper.System.Environment.SetVariable("VIPER_UTF8_SAMPLE", "café")
PRINT Viper.System.Environment.GetVariable("VIPER_UTF8_SAMPLE")

' Process exit
' Viper.System.Environment.EndProgram(7)
```

---

## Viper.System.Clipboard

UTF-8 text clipboard access backed by the active desktop clipboard backend.

**Type:** Static utility class

### Methods

| Method      | Signature      | Description                                      |
|-------------|----------------|--------------------------------------------------|
| `Get()`     | `String()`     | Returns clipboard text, or `""` when unavailable |
| `HasText()` | `Boolean()`    | Returns `TRUE` when text is available            |
| `Set(text)` | `Void(String)` | Copies UTF-8 text to the system clipboard        |

### Zia Example

```rust
bind Viper.System.Clipboard as Clipboard;

Clipboard.Set("Copied from Viper")
if Clipboard.HasText() {
    var text = Clipboard.Get()
}
```

### BASIC Example

```basic
Viper.System.Clipboard.Set("Copied from Viper")
IF Viper.System.Clipboard.HasText() THEN
    PRINT Viper.System.Clipboard.Get()
END IF
```

The clipboard helpers are available on desktop graphics backends. In headless or graphics-disabled builds, `Get()` returns an empty string, `HasText()` returns `FALSE`, and `Set()` is a no-op.

---

## Viper.System.Shutdown

Poll-based graceful shutdown requests for long-running servers, games, and tools.

**Type:** Static utility class

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `NONE` | `0` | No shutdown request is pending |
| `INTERRUPT` | `1` | Ctrl-C / VM interrupt / cooperative interrupt request |
| `TERMINATE` | `2` | POSIX `SIGTERM`, Windows console close/logoff/shutdown, or cooperative terminate request |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Request(reason)` | `Void(Integer)` | Publish one or more shutdown reason bits |
| `Poll()` | `Integer()` | Return and clear pending reason bits |
| `Pending()` | `Boolean()` | Return `TRUE` when any reason is pending without clearing it |
| `Clear()` | `Void()` | Clear pending reasons and the VM interrupt epoch |

`Poll()` and `Pending()` arm graceful handling for the next VM interrupt. If Ctrl-C
arrives after a loop has polled, the VM records `INTERRUPT` and lets the program reach
its next `Shutdown.Poll()` call. If a program never polls, Ctrl-C still raises the
normal `Interrupt` trap. This is deliberately poll-based; signal and console handlers
only publish atomic state and never run managed callbacks.

### Zia Example

```rust
module GracefulServer;

bind Viper.System.Shutdown as Shutdown;
bind Viper.Terminal;

func start() {
    var running = true;
    while running {
        var reason = Shutdown.Poll();
        if reason != Shutdown.None {
            Say("shutdown requested");
            running = false;
        } else {
            tick();
        }
    }
}
```

### BASIC Example

```basic
DO
    reason = Viper.System.Shutdown.Poll()
    IF reason <> Viper.System.Shutdown.None THEN
        PRINT "shutdown requested"
        EXIT DO
    END IF

    ' frame/update work
LOOP
```

---

## Viper.System.Exec

External command execution for running system commands and capturing output.

**Type:** Static utility class

### Methods

| Method                       | Signature              | Description                                            |
|------------------------------|------------------------|--------------------------------------------------------|
| `Run(program)`               | `Integer(String)`      | Execute program, wait for completion, return exit code |
| `RunArgs(program, args)`     | `Integer(String, Seq)` | Execute program with arguments, return exit code       |
| `Capture(program)`           | `String(String)`       | Execute program, capture and return stdout             |
| `CaptureArgs(program, args)` | `String(String, Seq)`  | Execute program with arguments, capture stdout         |
| `Shell(command)`             | `Integer(String)`      | Run command through system shell, return exit code     |
| `ShellCapture(command)`      | `String(String)`       | Run command through shell, capture stdout              |
| `ShellResult(command)`       | `CommandResult(String)`| Run command through shell and return captured stdout plus exit code |
| `ShellFull(command)`         | `String(String)`       | Legacy stdout capture paired with `LastExitCode()`     |
| `LastExitCode()`             | `Integer()`            | Legacy exit code from the most recent `ShellFull()` call |

### Viper.System.CommandResult

Returned by `Exec.ShellResult(command)`.

| Property | Type | Description |
|----------|------|-------------|
| `Output` | String | Captured stdout from the shell command |
| `ExitCode` | Integer | Normalized shell command exit code, or `-1` for launch/signalled failure |
| `Succeeded` | Boolean | True when `ExitCode == 0` |

Prefer `ShellResult()` for new shell capture code. `ShellFull()` and `LastExitCode()` remain available for compatibility, but the exit code is thread-local state and can be overwritten by a later exec call.

### Security Warning

**Shell injection vulnerability:** The `Shell` and `ShellCapture` methods pass commands directly to the system shell (
`/bin/sh -c` on Unix, `cmd /c` on Windows). Never pass unsanitized user input to these functions. If you need to run a
command with user-provided data, use `RunArgs` or `CaptureArgs` instead, which safely handle arguments without shell
interpretation.

```basic
' DANGEROUS - shell injection risk:
userInput = "file.txt; rm -rf /"
Viper.System.Exec.Shell("cat " + userInput)  ' DO NOT DO THIS

' SAFE - use RunArgs with explicit arguments:
DIM args AS OBJECT = Viper.Collections.Seq.New()
args.Push(userInput)
Viper.System.Exec.RunArgs("/bin/cat", args)  ' Arguments are passed directly
```

### Zia Example

```rust
module ExecDemo;

bind Viper.Terminal;
bind Viper.System.Exec as Exec;
bind Viper.Text.Fmt as Fmt;

func start() {
    // Capture shell command output and exit status together
    var result = Exec.ShellResult("echo Hello from shell");
    Say("Shell: " + result.Output);
    Say("Exit code: " + Fmt.Int(result.ExitCode));

    // Run a command and get exit code
    var code = Exec.Shell("true");
    Say("Exit code: " + Fmt.Int(code));

    // Capture program output directly
    var captured = Exec.Capture("/bin/echo");
    Say("Echo: " + captured);
}
```

### BASIC Example

```basic
' Simple command execution
DIM exitCode AS INTEGER
exitCode = Viper.System.Exec.Shell("echo Hello World")
PRINT "Exit code: "; exitCode

' Capture command output
DIM shellResult AS OBJECT
shellResult = Viper.System.Exec.ShellResult("date")
PRINT "Current date: "; Viper.System.CommandResult.get_Output(shellResult)
PRINT "Exit code: "; Viper.System.CommandResult.get_ExitCode(shellResult)

' Execute program with arguments
DIM args AS OBJECT = Viper.Collections.Seq.New()
args.Push("-l")
args.Push("-a")
exitCode = Viper.System.Exec.RunArgs("/bin/ls", args)

' Capture output with arguments
DIM result AS STRING
args = Viper.Collections.Seq.New()
args.Push("--version")
result = Viper.System.Exec.CaptureArgs("/usr/bin/python3", args)
PRINT "Python version: "; result

' Check if command succeeded
IF Viper.System.Exec.Shell("test -f /etc/passwd") = 0 THEN
    PRINT "File exists"
ELSE
    PRINT "File does not exist"
END IF

' Run a script and check result
exitCode = Viper.System.Exec.Shell("./myscript.sh")
IF exitCode <> 0 THEN
    PRINT "Script failed with exit code: "; exitCode
END IF
```

### Platform Notes

- **Run/RunArgs/Capture/CaptureArgs**: Execute programs directly using `posix_spawn` (Unix) or `CreateProcess` (
  Windows). Arguments are passed without shell interpretation.
- **Shell/ShellCapture**: Use `/bin/sh -c` on Unix or `cmd /c` on Windows. Commands are interpreted by the shell.
- Exit codes: 0 typically indicates success. Negative values indicate the process was terminated by a signal (Unix) or
  failed to start.
- Capture functions return empty string if the program fails to start.

---

## Viper.System.Process

Streaming child-process control for tools, build jobs, and long-running IDE tasks.

**Type:** Static utility class plus process handle object

### Static Methods

| Method                         | Signature                  | Description                                             |
|--------------------------------|----------------------------|---------------------------------------------------------|
| `Start(program, args)`         | `Process.Handle(String, Seq)` | Start a process with inherited cwd and environment   |
| `StartIn(program, args, cwd)`  | `Process.Handle(String, Seq, String)` | Start a process in a working directory        |
| `StartWithEnv(program, args, cwd, env)` | `Process.Handle(String, Seq, String, Seq)` | Start with explicit cwd and environment |

`args` is a `Seq` of argument strings and does not include the program name. `env` is either `NULL` to inherit the
current environment or a `Seq` of `KEY=value` strings. `cwd` may be `NULL` or empty to inherit the current working
directory.

### Handle Methods

| Method         | Signature    | Description                                                  |
|----------------|--------------|--------------------------------------------------------------|
| `IsValid()`    | `Boolean()`  | Returns `TRUE` while the handle still owns process resources |
| `Poll()`       | `Boolean()`  | Polls process state; returns `TRUE` while still running      |
| `IsRunning()`  | `Boolean()`  | Alias for polling the current running state                  |
| `ReadStdout()` | `String()`   | Returns buffered stdout bytes available now, then clears them |
| `ReadStderr()` | `String()`   | Returns buffered stderr bytes available now, then clears them |
| `ExitCode()`   | `Integer()`  | Returns exit code, or `-1` while running/invalid             |
| `Kill()`       | `Boolean()`  | Requests process termination                                |
| `Wait()`       | `Integer()`  | Blocks until process exit and returns the exit code          |
| `Destroy()`    | `Void()`     | Closes process resources; terminates a still-running child   |

### Zia Example

```rust
module ProcessDemo;

bind Viper.Collections.Seq as Seq;
bind Viper.System.Process as Process;
bind Viper.Terminal;
bind Viper.Text.Fmt as Fmt;

func start() {
    var args = Seq.New();
    Seq.Push(args, "-c");
    Seq.Push(args, "echo build-start; echo warning >&2; exit 7");

    var proc = Process.Start("/bin/sh", args);
    while proc.IsRunning() {
        var out = proc.ReadStdout();
        var err = proc.ReadStderr();
        if out.Len() > 0 { Say(out); }
        if err.Len() > 0 { Say(err); }
    }

    Say(proc.ReadStdout());
    Say(proc.ReadStderr());
    Say("Exit: " + Fmt.Int(proc.Wait()));
    proc.Destroy();
}
```

### Platform Notes

- `Start*` executes the program directly; it does not invoke a shell. Use an explicit shell executable only when shell
  syntax is required.
- POSIX direct exec failures after a successful fork surface as process exit code `126` or `127`.
- `ReadStdout()` and `ReadStderr()` are non-blocking incremental reads intended for GUI frame loops.
- `Kill()` sends a termination request (`SIGTERM` on POSIX, `TerminateProcess` on Windows). `Destroy()` is idempotent
  and force-cleans a still-running child.
- Output buffers are capped at 16 MB per stream between reads.

---

## Viper.System.Pty

Pseudo-terminal-backed child-process control for interactive shells and
terminal-like IDE surfaces.

**Type:** Static factory class plus PTY session handle object

### Static Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Open(program, args, cwd, env, cols, rows)` | `PtySession(String, Object, String, Object, Integer, Integer)` | Open a PTY-backed child |
| `OpenResult(program, args, cwd, env, cols, rows)` | `Result(String, Object, String, Object, Integer, Integer)` | Open a PTY-backed child as `Ok(PtySession)` or `Err(message)` |
| `IsSupported()` | `Boolean()` | Returns `TRUE` when the current platform can create PTYs |
| `LastError()` | `String()` | Compatibility diagnostic for legacy `Open()` / support checks |

### Handle Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `IsValid()` | `Boolean()` | Returns `TRUE` while the handle owns PTY resources |
| `Poll()` | `Boolean()` | Polls child state; returns `TRUE` while still running |
| `IsRunning()` | `Boolean()` | Alias for polling the current running state |
| `Read()` | `String()` | Returns merged stdout/stderr terminal bytes available now |
| `Write(data)` | `Integer(String)` | Writes bytes to terminal input |
| `Resize(cols, rows)` | `Boolean(Integer, Integer)` | Updates terminal size |
| `ExitCode()` | `Integer()` | Returns exit code, or `-1` while running/invalid |
| `Kill()` | `Boolean()` | Requests child termination |
| `Wait()` | `Integer()` | Blocks until child exit and returns the exit code |
| `Destroy()` | `Void()` | Closes PTY resources; terminates a still-running child |

Prefer `OpenResult()` for production code. It returns `Err(message)` for
unsupported platforms, startup failures, and validation errors. `Open()` and
`LastError()` remain available for compatibility; existing callers can keep
checking the nullable `Open()` result.

---

## Viper.System.Machine

System information queries providing read-only access to machine properties.

**Type:** Static utility class

### Properties

| Property   | Type      | Description                                                              |
|------------|-----------|--------------------------------------------------------------------------|
| `OS`       | `String`  | Operating system name: `"linux"`, `"macos"`, `"windows"`, or `"unknown"` |
| `OSVer`    | `String`  | Operating system version string (e.g., `"14.2.1"` on macOS)              |
| `Host`     | `String`  | Machine hostname                                                         |
| `User`     | `String`  | Current username                                                         |
| `Home`     | `String`  | Path to user's home directory                                            |
| `Temp`     | `String`  | Path to system temporary directory                                       |
| `Cores`    | `Integer` | Number of logical CPU cores                                              |
| `MemTotal` | `Integer` | Total RAM in bytes                                                       |
| `MemFree`  | `Integer` | Available RAM in bytes                                                   |
| `Endian`   | `String`  | Byte order: `"little"` or `"big"`                                        |

### Zia Example

```rust
module MachineDemo;

bind Viper.Terminal;
bind Viper.System.Machine as Machine;
bind Viper.Text.Fmt as Fmt;

func start() {
    Say("OS: " + Machine.get_Os());
    Say("Endian: " + Machine.get_Endian());
    Say("Cores: " + Fmt.Int(Machine.get_Cores()));
    Say("Home: " + Machine.get_Home());
    Say("User: " + Machine.get_User());
}
```

### BASIC Example

```basic
' Operating system information
PRINT "OS: "; Viper.System.Machine.Os
PRINT "Version: "; Viper.System.Machine.OsVer

' User and host
PRINT "User: "; Viper.System.Machine.User
PRINT "Host: "; Viper.System.Machine.Host

' Directory paths
PRINT "Home: "; Viper.System.Machine.Home
PRINT "Temp: "; Viper.System.Machine.Temp

' Hardware information
PRINT "CPU Cores: "; Viper.System.Machine.Cores
PRINT "Total RAM: "; Viper.System.Machine.MemTotal / 1073741824; " GB"
PRINT "Free RAM: "; Viper.System.Machine.MemFree / 1073741824; " GB"

' System characteristics
PRINT "Byte Order: "; Viper.System.Machine.get_Endian

' Conditional behavior based on OS
IF Viper.System.Machine.Os = "macos" THEN
    PRINT "Running on macOS"
ELSEIF Viper.System.Machine.Os = "linux" THEN
    PRINT "Running on Linux"
ELSEIF Viper.System.Machine.Os = "windows" THEN
    PRINT "Running on Windows"
END IF

' Check available memory before large allocation
DIM requiredMem AS INTEGER = 1073741824  ' 1 GB
IF Viper.System.Machine.MemFree > requiredMem THEN
    PRINT "Sufficient memory available"
ELSE
    PRINT "Warning: Low memory"
END IF
```

### Platform Notes

- **OS**: Returns lowercase platform identifier. Compile-time detection.
- **OSVer**: On macOS reads `kern.osproductversion` via sysctl. On Linux reads `/etc/os-release` VERSION_ID. Falls back
  to `uname` release string.
- **Host**: Uses `gethostname()` on Unix, `GetComputerName()` on Windows.
- **User**: Uses `getpwuid()` on Unix, `GetUserName()` on Windows, with fallback to environment variables.
- **Home**: Uses `HOME` environment variable on Unix, `USERPROFILE` on Windows.
- **Temp**: Uses `TMPDIR`/`TMP`/`TEMP` environment variables on Unix (defaulting to `/tmp`), `GetTempPath()` on Windows.
- **Cores**: Returns logical (hyper-threaded) core count via `sysconf(_SC_NPROCESSORS_ONLN)` on Unix, `GetSystemInfo()`
  on Windows.
- **MemTotal/MemFree**: On macOS uses `sysctl` and `host_statistics64`. On Linux uses `sysinfo()`. On Windows uses
  `GlobalMemoryStatusEx()`.
- **Endian**: Runtime detection via union trick. Most modern systems are little-endian.

---

## Viper.Runtime.Unsafe

Sharp retain/release hooks for deterministic ownership handoff. Most programs
should rely on normal lexical lifetimes and class `deinit`; use these only when
integrating with runtime handles that require explicit ownership control.

**Type:** Static utility class

### Methods

| Method            | Signature        | Description                                                            |
|-------------------|------------------|------------------------------------------------------------------------|
| `Retain(handle)`  | `Void(Object)`   | Increment a live runtime object, array, or string handle's reference count |
| `Release(handle)` | `Integer(Object)`| Decrement a live runtime handle; returns remaining references           |
| `RetainStr(text)` | `Void(String)`   | String-typed retain wrapper for callers that cannot pass `String` as `Object` |
| `ReleaseStr(text)`| `Integer(String)`| String-typed release wrapper; returns the remaining string references   |
| `ValueType(size)` | `Object(Integer)`| Allocate a heap-owned boxed value-type payload for compiler/runtime interop |
| `ValueTypeAddField(obj, offset, kind, retainNow)` | `Void(Object, Integer, Integer, Boolean)` | Register a managed field inside a boxed value-type payload |
| `SetThrowMsg(message)` | `Void(String)` | Store the current throw message for compiler/runtime catch binding |
| `ClearThrowMsg()` | `Void()` | Clear the stored throw message |
| `SetTrapFields(kind, code, line)` | `Void(Integer, Integer, Integer)` | Store thread-local trap metadata fields before raising or lowering a trap |
| `RaiseKind(kind, code, line)` | `Void(Integer, Integer, Integer)` | Raise a runtime trap from explicit trap metadata |

### Notes

- `Release()` runs managed object finalization when it drops the last reference.
- If a finalizer resurrects an object, `Release()` returns the live post-finalizer refcount instead of the transient zero count.
- `ReleaseStr()` returns the actual post-release string refcount for both heap-backed and small-string handles; immortal strings return the maximum `Integer` value.
- Arrays released through `Release()` run element cleanup for object, string, and boxed-value arrays before freeing the array storage.
- Passing `Nothing` is a no-op. Passing a non-runtime, already-freed, raw string payload, or unsupported heap payload traps.
- Reference counts are checked for overflow and underflow in release builds.
- Value-type hooks are for compiler/runtime interop. Size `0` is valid and creates a managed empty value-type object; negative sizes trap. Field registration is idempotent for the same offset/kind and traps for conflicting field kinds.
- Trap-state hooks are for generated code, runtime bridges, and low-level tests.
  Applications should inspect trap data through `Viper.Diagnostics.CurrentTrap`
  instead of setting or polling mutable trap fields. `Viper.Error.SetThrowMsg`,
  `ClearThrowMsg`, `SetTrapFields`, and `RaiseKind` remain compatibility names
  for these unsafe hooks.

---

## Viper.Runtime.GC

Garbage collector diagnostics and tuning controls. Provides visibility into the
reference-counting GC's cycle-collection activity. Most programs do not need to
call these methods directly.

**Type:** Static utility class

### Methods

| Method              | Signature    | Description                                                          |
|---------------------|--------------|----------------------------------------------------------------------|
| `Collect()`         | `Integer()`  | Trigger a GC sweep immediately; returns count of objects collected   |
| `TrackedCount()`    | `Integer()`  | Return the number of objects currently tracked by the GC             |
| `TotalCollected()`  | `Integer()`  | Return cumulative count of all objects collected since program start  |
| `PassCount()`       | `Integer()`  | Return the number of GC passes (sweeps) performed so far             |
| `SetThreshold(n)`   | `Void(Integer)` | Run automatic cycle collection after every `n` heap allocations; `0` disables |
| `GetThreshold()`    | `Integer()`  | Return the current automatic collection threshold, or `0` if disabled |

### Notes

- The Viper runtime uses reference counting with a cycle-collector sweep. `Collect()` forces a sweep to run now rather than waiting for the next automatic trigger.
- Promoted long-lived roots are still used as restore roots during non-full passes, so newly attached young children remain reachable.
- Finalizers for collected objects run before weak references are cleared; if any finalizer resurrects an unreachable cycle member, the entire candidate set is restored and weak references remain live.
- If a finalizer traps during `Collect()` or shutdown finalizer sweeping, the collector balances retained snapshots and clears its in-progress state before re-raising the trap.
- `TrackedCount` is useful for detecting object leaks in long-running programs.
- `TotalCollected()` saturates at the maximum `Integer` value rather than wrapping.
- `SetThreshold()` treats negative values as `0`.
- These are diagnostic APIs; calling `Collect()` frequently in hot paths can reduce throughput.

### Zia Example

```rust
module GCDemo;

bind Viper.Terminal;
bind Viper.Runtime.GC as GC;
bind Viper.Text.Fmt as Fmt;

func start() {
    Say("Tracked: " + Fmt.Int(GC.TrackedCount()));
    Say("Total collected: " + Fmt.Int(GC.TotalCollected()));

    // Force a sweep and report how many objects were collected
    var freed = GC.Collect();
    Say("Freed this pass: " + Fmt.Int(freed));
    Say("GC passes so far: " + Fmt.Int(GC.PassCount()));
}
```

### BASIC Example

```basic
' Print GC state before a large operation
PRINT "Tracked objects: "; Viper.Runtime.GC.TrackedCount()
PRINT "Total collected: "; Viper.Runtime.GC.TotalCollected()

' Run a batch operation that allocates many temporaries
DoBatchWork()

' Force a GC sweep to reclaim cycle garbage
DIM freed AS INTEGER = Viper.Runtime.GC.Collect()
PRINT "Freed by GC: "; freed
PRINT "GC passes: "; Viper.Runtime.GC.PassCount()
```

---

## Viper.Memory

Compatibility namespace for `Viper.Runtime.Unsafe`.

**Type:** Static utility class

`Retain`, `Release`, `RetainStr`, and `ReleaseStr` remain available here for
existing source and IL. New code should prefer `Viper.Runtime.Unsafe` because
manual reference-count manipulation is intentionally sharp.

---

## Viper.Memory.GC

Compatibility namespace for `Viper.Runtime.GC`.

**Type:** Static utility class

`Collect`, `TrackedCount`, `TotalCollected`, `PassCount`, `SetThreshold`, and
`GetThreshold` remain available here for existing source and IL. New code should
prefer `Viper.Runtime.GC`.

---

## Viper.Terminal

Terminal input and output operations.

**Type:** Static utility class

### Methods

#### Output

| Method            | Signature       | Description                                             |
|-------------------|-----------------|---------------------------------------------------------|
| `Print(text)`     | `Void(String)`  | Writes text without a trailing newline (flushes output) |
| `PrintInt(value)` | `Void(Integer)` | Writes an integer without a trailing newline (flushes)  |
| `PrintNum(value)` | `Void(Float)`   | Writes a floating-point number without a trailing newline (flushes) |
| `Say(text)`       | `Void(String)`  | Writes text followed by a newline                       |
| `SayBool(value)`  | `Void(Boolean)` | Writes `true` or `false` followed by a newline          |
| `SayInt(value)`   | `Void(Integer)` | Writes an integer followed by a newline                 |
| `SayNum(value)`   | `Void(Float)`   | Writes a floating-point number followed by a newline    |

#### Input

| Method               | Signature          | Description                                                 |
|----------------------|--------------------|-------------------------------------------------------------|
| `TryReadLine()`      | `Option<String>()` | Reads a line from stdin; returns `None` on EOF              |
| `ReadLineResult()`   | `Result<String, String>()` | Reads a line from stdin; returns `Err` with an EOF message on EOF |
| `TryAsk(prompt)`     | `Option<String>(String)` | Prints prompt, reads a line from stdin; returns `None` on EOF |
| `AskResult(prompt)`  | `Result<String, String>(String)` | Prints prompt, reads a line from stdin; returns `Err` with an EOF message on EOF |
| `ReadLine()`         | `String()`         | Compatibility API; prefer `TryReadLine` or `ReadLineResult` for EOF |
| `Ask(prompt)`        | `String(String)`   | Compatibility API; prefer `TryAsk` or `AskResult` for EOF     |
| `GetKey()`           | `String()`         | Blocks for a single key press and returns a 1-character string |
| `GetKeyTimeout(ms)`  | `String(Integer)`  | Waits up to `ms` for a key; returns `""` on timeout (negative = block) |
| `InKey()`            | `String()`         | Non-blocking key poll; returns `""` if no key is available   |

#### Screen Control

| Method                    | Signature                | Description                                              |
|---------------------------|--------------------------|----------------------------------------------------------|
| `Clear()`                 | `Void()`                 | Clears the terminal screen (TTY only)                    |
| `SetPosition(row, col)`   | `Void(Integer, Integer)` | Move cursor to 1-based row/column (clamped to 1)          |
| `SetColor(fg, bg)`        | `Void(Integer, Integer)` | Set BASIC color codes; use `-1` to leave a channel unchanged |
| `SetCursorVisible(show)`  | `Void(Integer)`          | Show (`!= 0`) or hide (`0`) the cursor                   |
| `SetAltScreen(enable)`    | `Void(Boolean)`          | Enter or exit the alternate screen                       |
| `Bell()`                  | `Void()`                 | Emits the terminal bell                                  |

#### Output Batching

| Method          | Signature | Description                                               |
|-----------------|-----------|-----------------------------------------------------------|
| `BeginBatch()`  | `Void()`  | Begin batch mode (defers flushes for terminal control)     |
| `EndBatch()`    | `Void()`  | End batch mode and flush pending output                   |
| `Flush()`       | `Void()`  | Force buffered output to be written immediately           |

### Zia Example

```rust
module TerminalDemo;

bind Viper.Terminal;

func start() {
    Say("Hello from Terminal!");       // prints with newline
    Print("No newline: ");             // prints without newline
    Say("done.");                       // No newline: done.
}
```

### BASIC Example

```basic
DIM name AS STRING
name = Viper.Option.UnwrapOrStr(Viper.Terminal.TryAsk("What is your name? "), "there")
Viper.Terminal.Say("Hello, " + name + "!")
```

### Note

For most BASIC programs, the `PRINT` and `INPUT` statements are more convenient. Use `Viper.Terminal` when you need
explicit control or are working at the IL level.

---

## See Also

- [Input/Output](io/README.md) - File system operations and stream I/O
- [Network](network.md) - Network operations and DNS resolution
