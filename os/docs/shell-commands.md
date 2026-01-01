# ViperOS Shell Command Reference

The ViperOS shell (vinit) provides a command-line interface with line editing, command history, and tab completion.

---

## Quick Reference

| Category | Commands |
|----------|----------|
| **Files** | `Dir`, `List`, `Type`, `Copy`, `Delete`, `MakeDir`, `Rename` |
| **Navigation** | `chdir`, `cwd`, `Path`, `Assign` |
| **Programs** | `Run`, `RunFSD` |
| **System** | `Version`, `Uptime`, `Avail`, `Status`, `Caps`, `Date`, `Time` |
| **Network** | `Fetch` |
| **Utility** | `Echo`, `Cls`, `History`, `Why`, `Help` |
| **Session** | `EndShell` |

---

## Return Codes

All commands set a return code accessible via the `Why` command:

| Code | Name | Description |
|------|------|-------------|
| 0 | OK | Command completed successfully |
| 5 | WARN | Command completed with warnings |
| 10 | ERROR | Command failed (recoverable error) |
| 20 | FAIL | Command failed (serious error) |

---

## File Commands

### Dir

Display a brief directory listing.

**Syntax:** `Dir [path]`

**Examples:**
```
SYS:> Dir
  c/                 certs/             l/
  s/                 t/                 vinit.elf
6 entries

SYS:> Dir /c
  hello.elf          fsinfo.elf         sysinfo.elf
  netstat.elf        ping.elf           edit.elf
6 entries
```

**Notes:**
- Directories are shown with a trailing `/`
- Default path is the current directory
- Entries are displayed in 3 columns

---

### List

Display a detailed directory listing with file information.

**Syntax:** `List [path]`

**Examples:**
```
SYS:> List
Directory "/"

vinit.elf                         rwed
c                                 <dir>    rwed
certs                             <dir>    rwed
l                                 <dir>    rwed
s                                 <dir>    rwed
t                                 <dir>    rwed

1 file, 5 directories
```

**Notes:**
- Shows permissions (rwed = read, write, execute, delete)
- Directories marked with `<dir>`

---

### Type

Display the contents of a text file.

**Syntax:** `Type <file>`

**Examples:**
```
SYS:> Type /s/startup
echo "Welcome to ViperOS"
```

**Notes:**
- Works best with text files
- Binary files may produce garbled output

---

### Copy

Copy a file to a new location.

**Syntax:** `Copy <source> [TO] <destination>`

**Examples:**
```
SYS:> Copy /c/hello.elf /t/hello_backup.elf
Copied 12345 bytes

SYS:> Copy myfile.txt TO newfile.txt
Copied 256 bytes
```

**Notes:**
- The `TO` keyword is optional
- Creates destination file if it doesn't exist
- Overwrites destination if it exists

---

### Delete

Delete a file or empty directory.

**Syntax:** `Delete <path>`

**Examples:**
```
SYS:> Delete /t/tempfile.txt
Deleted "/t/tempfile.txt"
```

**Notes:**
- Cannot delete non-empty directories
- No confirmation prompt

---

### MakeDir

Create a new directory.

**Syntax:** `MakeDir <path>`

**Examples:**
```
SYS:> MakeDir /t/mydir
Created "/t/mydir"
```

---

### Rename

Rename a file or directory.

**Syntax:** `Rename <old> [AS] <new>`

**Examples:**
```
SYS:> Rename oldfile.txt newfile.txt
Renamed "oldfile.txt" to "newfile.txt"

SYS:> Rename oldname AS newname
Renamed "oldname" to "newname"
```

**Notes:**
- The `AS` keyword is optional
- Cannot move files between directories (rename only)

---

## Navigation Commands

### chdir

Change the current working directory.

**Syntax:** `chdir [path]`

**Examples:**
```
SYS:> chdir /c
SYS:/c> chdir ..
SYS:> chdir
SYS:>
```

**Notes:**
- Without arguments, changes to root directory `/`
- The prompt shows the current directory

---

### cwd

Print the current working directory.

**Syntax:** `cwd`

**Examples:**
```
SYS:/c> cwd
/c
```

---

### Path

Resolve and display information about a logical path.

**Syntax:** `Path [name]`

**Examples:**
```
SYS:> Path
Current path: SYS:

SYS:> Path SYS:
Path "SYS:"
  Handle: 0x00000001
  Kind:   Directory
  Rights: rwed
```

---

### Assign

List or manage logical device assignments.

**Syntax:** `Assign`

**Examples:**
```
SYS:> Assign
Current assigns:
  Name         Handle     Flags
  -----------  ---------  ------
  SYS:         0x00000001 SYS
  C:           0x00000002 SYS

2 assigns defined
```

**Notes:**
- System assigns (SYS flag) are created at boot
- Setting assigns not yet implemented

---

## Program Commands

### Run

Execute a user program.

**Syntax:** `Run <program> [arguments]`

**Examples:**
```
SYS:> Run hello
Started process 2 (task 3)
Hello from ViperOS!
Process 2 exited with status 0

SYS:> Run /c/ping.elf 10.0.2.2
Started process 3 (task 4)
PING 10.0.2.2: ...
Process 3 exited with status 0

SYS:> Run edit /t/myfile.txt
Started process 4 (task 5)
[editor opens]
Process 4 exited with status 0
```

**Notes:**
- If program is not found as typed, searches in `/c/` directory
- Automatically adds `.elf` extension if needed
- Shell waits for program to exit before returning to prompt

**Available Programs:**

| Program | Description |
|---------|-------------|
| `hello` | Simple hello world test |
| `fsinfo` | Display filesystem information |
| `sysinfo` | Display system information |
| `netstat` | Display network statistics |
| `ping` | Send ICMP echo requests |
| `edit` | Text editor (nano-like) |
| `devices` | List system devices |
| `mathtest` | Floating-point math tests |

---

### RunFSD

Execute a user program by reading the ELF via `fsd` and spawning it from shared memory.

**Syntax:** `RunFSD <path> [arguments]`

**Notes:**
- Requires `fsd` to be running (microkernel bring-up path).
- Useful for running executables stored on the `fsd`-managed disk while spawn is being decoupled from the kernel filesystem.

---

## System Commands

### Version

Display ViperOS version information.

**Syntax:** `Version`

**Examples:**
```
SYS:> Version
ViperOS 0.2.0 (December 2025)
Platform: AArch64
```

---

### Uptime

Display system uptime.

**Syntax:** `Uptime`

**Examples:**
```
SYS:> Uptime
Uptime: 5 minutes, 32 seconds

SYS:> Uptime
Uptime: 1 hour, 23 minutes, 45 seconds
```

---

### Avail

Display memory availability.

**Syntax:** `Avail`

**Examples:**
```
SYS:> Avail

Type      Available         In-Use          Total
-------  ----------     ----------     ----------
chip     98304 K       32768 K       131072 K

Memory: 25600 pages free (32768 total, 4096 bytes/page)
```

---

### Status

Display running processes.

**Syntax:** `Status`

**Examples:**
```
SYS:> Status

Process Status:

  ID  State     Pri  Name
  --  --------  ---  --------------------------------
    0  Ready     255  idle [idle] [kernel]
    1  Running   128  vinit
    2  Blocked   128  netd [kernel]

3 tasks total
```

**Process States:**
- `Ready` - Waiting to run
- `Running` - Currently executing
- `Blocked` - Waiting for I/O or event
- `Exited` - Terminated

---

### Caps

Display capability table for the current process.

**Syntax:** `Caps [handle]`

**Examples:**
```
SYS:> Caps

Capability Table:

  Handle   Kind        Rights       Gen
  ------   ---------   ---------    ---
  0x0001   Directory   rwed         1
  0x0002   File        rw--         1
  0x0003   Socket      rw--         2

3 capabilities total
```

**Capability Kinds:**
- `Directory` - Directory handle
- `File` - File handle
- `Socket` - Network socket
- `Channel` - IPC channel
- `Timer` - Timer object

---

### Date

Display the current date.

**Syntax:** `Date`

**Notes:** Date/time not yet available (requires RTC support).

---

### Time

Display the current time.

**Syntax:** `Time`

**Notes:** Date/time not yet available (requires RTC support).

---

## Network Commands

### Fetch

Fetch a web page via HTTP or HTTPS.

**Syntax:** `Fetch <url>`

**Examples:**
```
SYS:> Fetch example.com
Resolving example.com...
Connecting to 93.184.215.14:80...
Connected! Sending request...
Request sent, receiving response...

HTTP/1.0 200 OK
Content-Type: text/html; charset=UTF-8
...

[Received 1256 bytes]

SYS:> Fetch https://example.com
Resolving example.com...
Connecting to 93.184.215.14:443 (HTTPS)...
Connected! Starting TLS handshake...
TLS handshake complete. Sending request...
Request sent, receiving response...

HTTP/1.0 200 OK
...

[Received 1256 bytes, encrypted]
```

**Supported Protocols:**
- `http://` - Plain HTTP (port 80)
- `https://` - TLS-encrypted HTTPS (port 443)
- No prefix - Defaults to HTTP

**Notes:**
- Uses TLS 1.3 for HTTPS connections
- Certificates are verified against built-in root CA store
- HTTP/1.0 protocol (no keep-alive)

---

## Utility Commands

### Echo

Print text to the console.

**Syntax:** `Echo [text]`

**Examples:**
```
SYS:> Echo Hello, World!
Hello, World!

SYS:> Echo
(blank line)
```

---

### Cls

Clear the screen.

**Syntax:** `Cls`

**Notes:**
- Alias: `Clear`
- Resets cursor to top-left corner

---

### History

Display command history.

**Syntax:** `History`

**Examples:**
```
SYS:> History
  1  Dir
  2  List /c
  3  Run hello
  4  History
```

**Notes:**
- Stores last 16 commands
- Duplicate consecutive commands not stored
- Use Up/Down arrows to navigate history

---

### Why

Explain the result of the last command.

**Syntax:** `Why`

**Examples:**
```
SYS:> Delete /nonexistent
Delete: cannot delete "/nonexistent"

SYS:> Why
Last return code: 10 - Directory not found

SYS:> Dir
  ...
SYS:> Why
No error.
```

---

### Help

Display help information.

**Syntax:** `Help`

**Notes:**
- Alias: `?`
- Shows all commands with brief descriptions
- Shows line editing shortcuts

---

## Session Commands

### EndShell

Exit the shell.

**Syntax:** `EndShell`

**Notes:**
- Aliases: `Exit`, `Quit`
- Displays "Goodbye!" message

---

## Line Editing

The shell supports full line editing:

| Key | Action |
|-----|--------|
| **Left/Right** | Move cursor |
| **Up/Down** | Navigate history |
| **Home** | Jump to start of line |
| **End** | Jump to end of line |
| **Tab** | Command completion |
| **Backspace** | Delete character before cursor |
| **Delete** | Delete character at cursor |
| **Ctrl+A** | Jump to start (same as Home) |
| **Ctrl+E** | Jump to end (same as End) |
| **Ctrl+U** | Clear entire line |
| **Ctrl+K** | Delete from cursor to end |
| **Ctrl+C** | Cancel current line |

---

## Command Completion

Press Tab to complete commands:

- Single match: Completes the command
- Multiple matches: Shows all possibilities
- No match: No action

**Example:**
```
SYS:> Di<Tab>
Dir

SYS:> D<Tab>
Date  Delete  Dir
SYS:> D
```

---

## Paging Output

Use the `read` prefix to page long output:

**Syntax:** `read <command>`

**Examples:**
```
SYS:> read Help
[displays help with paging]
-- More (Space=page, Enter=line, Q=quit) --
```

**Paging Controls:**
- **Space** - Next page
- **Enter** - Next line
- **Q** - Quit paging

---

## Legacy Command Aliases

For Unix users, these aliases are supported with a reminder:

| Unix Command | ViperOS Command |
|--------------|-----------------|
| `ls` | `Dir` |
| `cat` | `Type` |

```
SYS:> ls
Note: Use 'Dir' or 'List' instead of 'ls'
  c/    certs/    ...
```
