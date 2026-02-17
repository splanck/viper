# System

> System interaction, environment, and process execution.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Environment](#viperenvironment)
- [Viper.Exec](#viperexec)
- [Viper.Machine](#vipermachine)
- [Viper.Terminal](#viperterminal)

---

## Viper.Environment

Command-line arguments and environment access.

**Type:** Static utility class

### Methods

| Method                     | Signature              | Description                                                             |
|----------------------------|------------------------|-------------------------------------------------------------------------|
| `GetArgumentCount()`       | `Integer()`            | Returns the number of command-line arguments                            |
| `GetArgument(index)`       | `String(Integer)`      | Returns the argument at the specified index (0-based)                   |
| `GetCommandLine()`         | `String()`             | Returns the full command line as a single string                        |
| `GetVariable(name)`        | `String(String)`       | Returns the value of an environment variable, or `""` when missing      |
| `HasVariable(name)`        | `Boolean(String)`      | Returns `TRUE` when the environment variable exists                     |
| `IsNative()`               | `Boolean()`            | Returns `TRUE` when running native code, `FALSE` when running in the VM |
| `SetVariable(name, value)` | `Void(String, String)` | Sets or overwrites an environment variable (empty value allowed)        |
| `EndProgram(code)`         | `Void(Integer)`        | Terminates the program with the provided exit code                      |

### Zia Example

```zia
module EnvDemo;

bind Viper.Terminal;
bind Viper.Environment as Env;
bind Viper.Fmt as Fmt;

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
' Program invoked as: ./program arg1 arg2 arg3

DIM count AS INTEGER
count = Viper.Environment.GetArgumentCount()
PRINT "Argument count: "; count  ' Output: 3

FOR i = 0 TO count - 1
    PRINT "Arg "; i; ": "; Viper.Environment.GetArgument(i)
NEXT i
' Output:
' Arg 0: arg1
' Arg 1: arg2
' Arg 2: arg3

PRINT "Full command: "; Viper.Environment.GetCommandLine()

' Environment variables
DIM name AS STRING
DIM value AS STRING
name = "VIPER_SAMPLE_ENV"
value = Viper.Environment.GetVariable(name)
IF Viper.Environment.HasVariable(name) THEN
    PRINT name; " is set to "; value
ELSE
    PRINT name; " is not set"
END IF

Viper.Environment.SetVariable(name, "abc")
PRINT "Updated value: "; Viper.Environment.GetVariable(name)

' Process exit
' Viper.Environment.EndProgram(7)
```

---

## Viper.Exec

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

### Security Warning

**Shell injection vulnerability:** The `Shell` and `ShellCapture` methods pass commands directly to the system shell (
`/bin/sh -c` on Unix, `cmd /c` on Windows). Never pass unsanitized user input to these functions. If you need to run a
command with user-provided data, use `RunArgs` or `CaptureArgs` instead, which safely handle arguments without shell
interpretation.

```basic
' DANGEROUS - shell injection risk:
userInput = "file.txt; rm -rf /"
Viper.Exec.Shell("cat " + userInput)  ' DO NOT DO THIS

' SAFE - use RunArgs with explicit arguments:
DIM args AS OBJECT = Viper.Collections.Seq.New()
args.Push(userInput)
Viper.Exec.RunArgs("/bin/cat", args)  ' Arguments are passed directly
```

### Zia Example

```zia
module ExecDemo;

bind Viper.Terminal;
bind Viper.Exec as Exec;
bind Viper.Fmt as Fmt;

func start() {
    // Capture shell command output
    var output = Exec.ShellCapture("echo Hello from shell");
    Say("Shell: " + output);

    // Run a command and get exit code
    var code = Exec.Shell("true");
    Say("Exit code: " + Fmt.Int(code));

    // Capture program output directly
    var result = Exec.Capture("/bin/echo");
    Say("Echo: " + result);
}
```

### BASIC Example

```basic
' Simple command execution
DIM exitCode AS INTEGER
exitCode = Viper.Exec.Shell("echo Hello World")
PRINT "Exit code: "; exitCode

' Capture command output
DIM output AS STRING
output = Viper.Exec.ShellCapture("date")
PRINT "Current date: "; output

' Execute program with arguments
DIM args AS OBJECT = Viper.Collections.Seq.New()
args.Push("-l")
args.Push("-a")
exitCode = Viper.Exec.RunArgs("/bin/ls", args)

' Capture output with arguments
DIM result AS STRING
args = Viper.Collections.Seq.New()
args.Push("--version")
result = Viper.Exec.CaptureArgs("/usr/bin/python3", args)
PRINT "Python version: "; result

' Check if command succeeded
IF Viper.Exec.Shell("test -f /etc/passwd") = 0 THEN
    PRINT "File exists"
ELSE
    PRINT "File does not exist"
END IF

' Run a script and check result
exitCode = Viper.Exec.Shell("./myscript.sh")
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

## Viper.Machine

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

```zia
module MachineDemo;

bind Viper.Terminal;
bind Viper.Machine as Machine;
bind Viper.Fmt as Fmt;

func start() {
    Say("OS: " + Machine.get_OS());
    Say("Endian: " + Machine.get_Endian());
    Say("Cores: " + Fmt.Int(Machine.get_Cores()));
    Say("Home: " + Machine.get_Home());
    Say("User: " + Machine.get_User());
}
```

### BASIC Example

```basic
' Operating system information
PRINT "OS: "; Viper.Machine.OS
PRINT "Version: "; Viper.Machine.OSVer

' User and host
PRINT "User: "; Viper.Machine.User
PRINT "Host: "; Viper.Machine.Host

' Directory paths
PRINT "Home: "; Viper.Machine.Home
PRINT "Temp: "; Viper.Machine.Temp

' Hardware information
PRINT "CPU Cores: "; Viper.Machine.Cores
PRINT "Total RAM: "; Viper.Machine.MemTotal / 1073741824; " GB"
PRINT "Free RAM: "; Viper.Machine.MemFree / 1073741824; " GB"

' System characteristics
PRINT "Byte Order: "; Viper.Machine.Endian

' Conditional behavior based on OS
IF Viper.Machine.OS = "macos" THEN
    PRINT "Running on macOS"
ELSEIF Viper.Machine.OS = "linux" THEN
    PRINT "Running on Linux"
ELSEIF Viper.Machine.OS = "windows" THEN
    PRINT "Running on Windows"
END IF

' Check available memory before large allocation
DIM requiredMem AS INTEGER = 1073741824  ' 1 GB
IF Viper.Machine.MemFree > requiredMem THEN
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
| `ReadLine()`         | `String?()`        | Reads a line from stdin; returns null on EOF                |
| `Ask(prompt)`        | `String?(String)`  | Prints prompt, reads a line from stdin; null on EOF          |
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
| `SetAltScreen(enable)`    | `Void(Integer)`          | Enter (`!= 0`) or exit (`0`) the alternate screen        |
| `Bell()`                  | `Void()`                 | Emits the terminal bell                                  |

#### Output Batching

| Method          | Signature | Description                                               |
|-----------------|-----------|-----------------------------------------------------------|
| `BeginBatch()`  | `Void()`  | Begin batch mode (defers flushes for terminal control)     |
| `EndBatch()`    | `Void()`  | End batch mode and flush pending output                   |
| `Flush()`       | `Void()`  | Force buffered output to be written immediately           |

### Zia Example

```zia
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
name = Viper.Terminal.Ask("What is your name? ")
Viper.Terminal.Say("Hello, " + name + "!")
```

### Note

For most BASIC programs, the `PRINT` and `INPUT` statements are more convenient. Use `Viper.Terminal` when you need
explicit control or are working at the IL level.

---

## See Also

- [Input/Output](io.md) - File system operations and stream I/O
- [Network](network.md) - Network operations and DNS resolution
