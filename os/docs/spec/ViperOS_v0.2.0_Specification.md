# ViperOS v0.2.0 Specification

## From Prototype to Platform

**Version:** 0.2.0  
**Date:** December 2025  
**Status:** Specification  
**Base:** ViperOS v0.1.0 (Phases 1-6 complete)  
**Architecture:** AArch64 (ARM64) Exclusive

---

## Executive Summary

ViperOS v0.2.0 transforms the working prototype into a proper platform. This release focuses on four key
areas:

1. **Command Naming** — Replace Unix-style commands with retro-style names
2. **Assigns System** — Implement logical device names (SYS:, C:, HOME:, etc.)
3. **TLS/HTTPS** — Secure network communication for modern web access
4. **Expanded Syscalls** — Additional kernel functionality for richer applications

**Target:** A cohesive operating system capable of secure network communication.

---

## Table of Contents

1. [Command Renaming](#1-command-renaming)
2. [Assigns System](#2-assigns-system)
3. [TLS Implementation](#3-tls-implementation)
4. [HTTPS Support](#4-https-support)
5. [Expanded Syscalls](#5-expanded-syscalls)
6. [Shell Enhancements](#6-shell-enhancements)
7. [Implementation Plan](#7-implementation-plan)
8. [Testing Requirements](#8-testing-requirements)

---

## 1. Command Renaming

### 1.1 Command Name Mapping

All commands must use retro-style naming. Unix conventions are explicitly rejected.

| Current (Wrong) | Correct (v0.2.0) | Purpose                              |
|-----------------|------------------|--------------------------------------|
| `ls`            | `Dir`            | Brief directory listing              |
| `ls -l`         | `List`           | Detailed directory listing           |
| `cat`           | `Type`           | Display file contents                |
| `cp`            | `Copy`           | Copy files                           |
| `rm`            | `Delete`         | Delete files                         |
| `mkdir`         | `MakeDir`        | Create directory                     |
| `mv`            | `Rename`         | Rename/move files                    |
| `pwd`           | `Cd` (no args)   | Show current directory               |
| `cd`            | `Cd`             | Change directory                     |
| `echo`          | `Echo`           | Print text                           |
| `clear`         | `Cls`            | Clear screen                         |
| `ps`            | `Status`         | Show running tasks                   |
| `free`          | `Avail`          | Show memory availability             |
| `ping`          | `Ping`           | Network ping (keep)                  |
| `wget`          | `Fetch`          | HTTP fetch                           |
| `ifconfig`      | `NetInfo`        | Network information                  |
| `nslookup`      | `Resolve`        | DNS lookup                           |
| `grep`          | `Search`         | Search text in files                 |
| `sort`          | `Sort`           | Sort lines                           |
| `head`/`tail`   | `Head`/`Tail`    | Show file start/end                  |
| `wc`            | `Count`          | Count lines/words/bytes              |
| `touch`         | `Touch`          | Create empty file / update timestamp |
| `chmod`         | `Protect`        | Set file protection bits             |
| `date`          | `Date`           | Show/set date                        |
| `time`          | `Time`           | Show/set time                        |
| `uptime`        | `Uptime`         | Show system uptime                   |
| `kill`          | `Break`          | Send break to task                   |
| `reboot`        | `Reboot`         | Restart system                       |
| `shutdown`      | `Shutdown`       | Power off system                     |
| `help`          | `Help`           | Show command help                    |
| `man`           | `Help`           | Same as Help                         |
| `which`         | `Which`          | Find command location                |
| `env`           | `Set` (no args)  | Show environment                     |
| `export`        | `Set`            | Set environment variable             |
| `unset`         | `Unset`          | Remove environment variable          |
| `alias`         | `Alias`          | Create command alias                 |
| `history`       | `History`        | Show command history                 |
| `exit`          | `EndShell`       | Exit shell                           |
| `logout`        | `EndShell`       | Same as EndShell                     |

### 1.2 New Commands (Not in v0.1.0)

| Command      | Purpose                      | Synopsis                   |
|--------------|------------------------------|----------------------------|
| `Assign`     | Manage logical devices       | `Assign NAME: PATH`        |
| `Path`       | Manage command search path   | `Path [dir] [ADD\|REMOVE]` |
| `Info`       | Device/volume information    | `Info [device:]`           |
| `Version`    | Show version info            | `Version [file]`           |
| `Why`        | Explain last error           | `Why`                      |
| `Wait`       | Wait for time/condition      | `Wait [seconds]`           |
| `Run`        | Run program in background    | `Run command [args]`       |
| `Execute`    | Execute script file          | `Execute script.bas`       |
| `NewShell`   | Open new shell               | `NewShell`                 |
| `Resident`   | Make command memory-resident | `Resident command`         |
| `Mount`      | Mount filesystem             | `Mount device:`            |
| `DiskChange` | Notify disk change           | `DiskChange device:`       |

### 1.3 Command Output Format

Commands should produce retro-style output:

**Dir (brief listing):**

```
SYS:c> Dir
   vsh.vpr          dir.vpr          list.vpr
   copy.vpr         delete.vpr       type.vpr
6 files - 847 blocks used
```

**List (detailed listing):**

```
SYS:c> List
Directory "SYS:c" on Wednesday 25-Dec-25
vsh.vpr                           12480  rwed  25-Dec-25 10:30:00
dir.vpr                            3200  rwed  25-Dec-25 10:30:00
list.vpr                           4096  rwed  25-Dec-25 10:30:00
copy.vpr                           5632  rwed  25-Dec-25 10:30:00
delete.vpr                         2048  rwed  25-Dec-25 10:30:00
type.vpr                           2560  rwed  25-Dec-25 10:30:00
6 files - 30016 bytes - 59 blocks used
```

**Type:**

```
SYS:s> Type startup.bas
' ViperOS Startup Script
Print "ViperOS starting..."
Shell "Assign C: SYS:c"
Shell "Path C: ADD"
```

### 1.4 Return Codes

All commands must use standard return codes:

| Code | Name  | Meaning             |
|------|-------|---------------------|
| 0    | OK    | Success             |
| 5    | WARN  | Warning (non-fatal) |
| 10   | ERROR | Operation failed    |
| 20   | FAIL  | Complete failure    |

Example usage in scripts:

```basic
Dim rc As Integer = Shell("Copy source.txt TO dest.txt")
If rc >= 10 Then
    Print "Copy failed!"
End If
```

---

## 2. Assigns System

### 2.1 Overview

Assigns provide logical device names that map to directory capabilities. They are central to ViperOS path resolution.

### 2.2 System Assigns (Read-Only)

These assigns are created at boot and cannot be modified by users:

| Assign             | Points To        | Purpose          |
|--------------------|------------------|------------------|
| `SYS:`             | D0:\             | Boot device root |
| `D0:`              | Physical drive 0 | Boot disk        |
| `D1:`, `D2:`, etc. | Physical drives  | Additional disks |

### 2.3 Standard Assigns (User-Modifiable)

These assigns are created by startup scripts and can be modified:

| Assign   | Default          | Purpose             |
|----------|------------------|---------------------|
| `C:`     | SYS:c            | Commands directory  |
| `S:`     | SYS:s            | Startup scripts     |
| `L:`     | SYS:l            | Handlers/libraries  |
| `LIBS:`  | SYS:libs         | Viper libraries     |
| `FONTS:` | SYS:fonts        | Font files          |
| `T:`     | SYS:t            | Temporary files     |
| `HOME:`  | SYS:home\default | User home directory |
| `WORK:`  | HOME:work        | User work directory |
| `RAM:`   | (RAM disk)       | Volatile storage    |

### 2.4 Assign Syscalls

```c
// Create or modify an assign
// name: Assign name without colon (e.g., "WORK")
// dir_handle: Directory capability to assign
// Returns: VError
VError AssignSet(const char* name, size_t name_len, Handle dir_handle);

// Remove an assign
VError AssignRemove(const char* name, size_t name_len);

// Get assign target (returns directory handle)
// Returns: VError, X1=dir_handle (or 0 if not found)
VError AssignGet(const char* name, size_t name_len);

// List all assigns
// buffer: Array of VAssignInfo structures
// max_count: Maximum entries to return
// Returns: VError, X1=count
VError AssignList(VAssignInfo* buffer, size_t max_count);
```

### 2.5 VAssignInfo Structure

```c
typedef struct VAssignInfo {
    char name[32];        // Assign name (without colon)
    uint32_t handle;      // Directory handle
    uint32_t flags;       // ASSIGN_SYSTEM, ASSIGN_DEFERRED, etc.
    uint8_t _reserved[24];
} VAssignInfo;

// Flags
#define ASSIGN_SYSTEM   (1 << 0)  // System assign (read-only)
#define ASSIGN_DEFERRED (1 << 1)  // Deferred assign (path, not handle)
#define ASSIGN_ADD      (1 << 2)  // Multi-directory assign
```

### 2.6 Path Resolution

When resolving a path like `C:dir.vpr`:

1. Extract assign name (`C`)
2. Look up assign → get directory handle
3. Open `dir.vpr` relative to that handle
4. Return file handle

For paths like `SYS:c\programs\app.vpr`:

1. Look up `SYS:` → root directory handle
2. Open `c` → subdirectory handle
3. Open `programs` → subdirectory handle
4. Open `app.vpr` → file handle

### 2.7 Assign Command

```
Usage: Assign [name: [path]] [options]

  Assign                  List all assigns
  Assign NAME: PATH       Create assign NAME pointing to PATH
  Assign NAME:            Remove assign NAME
  Assign NAME: PATH ADD   Add PATH to multi-directory assign
  Assign NAME: PATH REMOVE Remove PATH from multi-directory assign

Options:
  ADD      Add to existing assign (multi-directory)
  REMOVE   Remove from multi-directory assign
  PATH     Show paths in multi-directory assign
  EXISTS   Return 0 if assign exists, 5 if not

Examples:
  Assign WORK: HOME:projects
  Assign C: SYS:c SYS:contrib\c ADD
  Assign TEMP:
```

### 2.8 Path Command

```
Usage: Path [dir] [ADD|REMOVE|RESET|SHOW]

  Path                    Show current path
  Path dir ADD            Add directory to path
  Path dir REMOVE         Remove directory from path
  Path RESET              Reset to default path
  Path SHOW               Show path with full paths

The path determines where commands are searched.
Default path: C:

Examples:
  Path HOME:bin ADD
  Path SYS:contrib\c ADD
```

### 2.9 Kernel Assign Table

```cpp
// kernel/assign/assign.hpp

namespace viper::assign {

struct AssignEntry {
    char name[32];
    Handle directory;
    u32 flags;
    AssignEntry* next;  // For multi-directory assigns
    bool active;
};

constexpr int MAX_ASSIGNS = 64;

// Initialize assign system
void init();

// Set an assign (creates or replaces)
VError set(const char* name, Handle dir, u32 flags = 0);

// Add to multi-directory assign
VError add(const char* name, Handle dir);

// Remove an assign
VError remove(const char* name);

// Get assign handle
Result<Handle, VError> get(const char* name);

// Resolve full path to handle
Result<Handle, VError> resolve_path(const char* path);

// List assigns
int list(AssignInfo* buffer, int max_count);

} // namespace viper::assign
```

---

## 3. TLS Implementation

### 3.1 Overview

TLS (Transport Layer Security) 1.3 implementation for secure communication. TLS 1.2 support optional but recommended for
compatibility.

### 3.2 Scope

**In Scope (v0.2.0):**

- TLS 1.3 client
- TLS 1.2 client (fallback)
- Server certificate validation
- Common cipher suites
- SNI (Server Name Indication)

**Out of Scope (Future):**

- TLS server
- Client certificates
- Session resumption
- 0-RTT

### 3.3 Cipher Suites

**Required (TLS 1.3):**

- `TLS_AES_128_GCM_SHA256`
- `TLS_AES_256_GCM_SHA384`
- `TLS_CHACHA20_POLY1305_SHA256`

**Required (TLS 1.2):**

- `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256`
- `TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384`
- `TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256`

### 3.4 Cryptographic Primitives

The following primitives must be implemented:

**Symmetric Encryption:**

- AES-128-GCM
- AES-256-GCM
- ChaCha20-Poly1305

**Hash Functions:**

- SHA-256
- SHA-384

**Key Exchange:**

- X25519 (Curve25519 ECDH)
- secp256r1 (P-256) ECDH

**Digital Signatures:**

- RSA-PSS (for certificate verification)
- ECDSA with P-256 (for certificate verification)
- Ed25519 (optional)

**Key Derivation:**

- HKDF-SHA256
- HKDF-SHA384

### 3.5 Implementation Structure

```
kernel/net/tls/
├── tls.cpp/.hpp              # TLS session management
├── handshake.cpp/.hpp        # TLS handshake state machine
├── record.cpp/.hpp           # TLS record layer
├── crypto/
│   ├── aes.cpp/.hpp          # AES implementation
│   ├── gcm.cpp/.hpp          # GCM mode
│   ├── chacha20.cpp/.hpp     # ChaCha20 stream cipher
│   ├── poly1305.cpp/.hpp     # Poly1305 MAC
│   ├── sha256.cpp/.hpp       # SHA-256
│   ├── sha384.cpp/.hpp       # SHA-384
│   ├── x25519.cpp/.hpp       # Curve25519 ECDH
│   ├── p256.cpp/.hpp         # P-256 ECDH/ECDSA
│   ├── rsa.cpp/.hpp          # RSA verification
│   ├── hkdf.cpp/.hpp         # HKDF key derivation
│   └── random.cpp/.hpp       # Cryptographic RNG
├── cert/
│   ├── x509.cpp/.hpp         # X.509 certificate parsing
│   ├── verify.cpp/.hpp       # Certificate chain verification
│   └── roots.cpp/.hpp        # Root CA certificates
└── asn1/
    └── asn1.cpp/.hpp         # ASN.1 DER parser
```

### 3.6 TLS Session API

```cpp
// kernel/net/tls/tls.hpp

namespace viper::net::tls {

enum class TlsVersion {
    TLS_1_2 = 0x0303,
    TLS_1_3 = 0x0304,
};

enum class TlsState {
    Idle,
    Connecting,
    Handshaking,
    Established,
    Closing,
    Closed,
    Error,
};

struct TlsConfig {
    bool verify_certificates;     // Default: true
    bool allow_tls_1_2;          // Default: true
    const char* sni_hostname;    // Server Name Indication
    // Future: client certs, session cache, etc.
};

class TlsSession {
public:
    // Create TLS session over existing TCP socket
    static Result<TlsSession*, VError> create(int tcp_socket, const TlsConfig& config);
    
    // Perform TLS handshake
    VError handshake();
    
    // Send encrypted data
    int send(const void* data, size_t len);
    
    // Receive decrypted data
    int recv(void* buf, size_t max_len);
    
    // Close TLS session (sends close_notify)
    VError close();
    
    // Get session state
    TlsState state() const;
    
    // Get negotiated version
    TlsVersion version() const;
    
    // Get negotiated cipher suite
    const char* cipher_suite() const;
    
    // Get peer certificate info
    const CertInfo* peer_certificate() const;
    
private:
    int socket_;
    TlsState state_;
    TlsVersion version_;
    // ... handshake state, keys, etc.
};

} // namespace viper::net::tls
```

### 3.7 TLS Handshake (TLS 1.3)

```
Client                                           Server

ClientHello
  + key_share
  + signature_algorithms
  + supported_versions
  + server_name (SNI)
                            -------->
                                                 ServerHello
                                                   + key_share
                                                   + supported_versions
                                         {EncryptedExtensions}
                                         {Certificate}
                                         {CertificateVerify}
                                         {Finished}
                            <--------
{Finished}                  -------->

[Application Data]          <------->    [Application Data]
```

### 3.8 Certificate Validation

**Required Checks:**

1. Certificate chain builds to trusted root
2. Certificates not expired
3. Hostname matches certificate (CN or SAN)
4. Key usage appropriate for TLS
5. Signature valid

**Root CA Store:**

- Embed minimal set of major CAs (Let's Encrypt, DigiCert, etc.)
- ~10-20 root certificates
- Store in `SYS:certs\roots.pem` or compiled in

**For Development:**

- Option to skip certificate validation
- Controlled via `TlsConfig.verify_certificates`

### 3.9 Random Number Generation

Cryptographic operations require secure random numbers.

```cpp
// kernel/net/tls/crypto/random.hpp

namespace viper::net::tls::crypto {

// Initialize RNG (seed from hardware if available)
void random_init();

// Generate cryptographically secure random bytes
void random_bytes(void* buf, size_t len);

// Get random 32-bit value
uint32_t random_u32();

// Get random 64-bit value
uint64_t random_u64();

} // namespace viper::net::tls::crypto
```

**Entropy Sources:**

- ARM CPU cycle counter (CNTVCT_EL0)
- Timer interrupts
- Keyboard/mouse input timing
- Network packet timing
- Initial seed from UEFI RNG if available

---

## 4. HTTPS Support

### 4.1 HTTPS Client API

```cpp
// kernel/net/http/https.hpp

namespace viper::net::http {

struct HttpsConfig {
    bool verify_certificates;    // Default: true
    int timeout_ms;              // Request timeout
    const char* user_agent;      // Default: "ViperOS/0.2"
};

// HTTPS GET request
// Returns response or error
Result<HttpResponse*, VError> https_get(
    const char* url,
    const HttpsConfig* config = nullptr
);

// HTTPS POST request
Result<HttpResponse*, VError> https_post(
    const char* url,
    const char* content_type,
    const void* body,
    size_t body_len,
    const HttpsConfig* config = nullptr
);

// Free response
void http_response_free(HttpResponse* resp);

} // namespace viper::net::http
```

### 4.2 URL Parsing

URLs must support HTTPS scheme:

```
https://example.com/path
https://example.com:8443/path
http://example.com/path (still supported)
```

```cpp
struct ParsedUrl {
    char scheme[8];       // "http" or "https"
    char host[256];
    uint16_t port;        // 80 for http, 443 for https
    char path[512];
    bool is_https;
};

bool parse_url(const char* url, ParsedUrl* out);
```

### 4.3 Fetch Command

```
Usage: Fetch url [TO file] [options]

  Fetch https://example.com/file.txt
  Fetch https://example.com/data.zip TO RAM:data.zip

Options:
  TO file          Save to file instead of stdout
  QUIET            Don't show progress
  NOSSL            Force HTTP even for HTTPS URLs
  NOVERIFY         Skip certificate verification (dangerous)
  TIMEOUT n        Timeout in seconds (default: 30)
  HEADERS          Show response headers

Examples:
  Fetch https://viper.dev/version.txt
  Fetch https://releases.viper.dev/viper-0.2.zip TO T:update.zip
```

### 4.4 TLS Syscalls

```c
// Create TLS session over TCP socket
// tcp_socket: Existing connected TCP socket handle
// hostname: Server hostname for SNI (null-terminated)
// flags: TLS_VERIFY_CERTS, TLS_ALLOW_TLS12, etc.
// Returns: VError, X1=tls_handle
VError TlsCreate(Handle tcp_socket, const char* hostname, uint32_t flags);

// Perform TLS handshake
// Returns: VError (VOK when handshake complete)
VError TlsHandshake(Handle tls);

// Send data over TLS
// Returns: VError, X1=bytes_sent
VError TlsSend(Handle tls, const void* data, size_t len);

// Receive data over TLS
// Returns: VError, X1=bytes_received
VError TlsRecv(Handle tls, void* buf, size_t max_len);

// Close TLS session
VError TlsClose(Handle tls);

// Get TLS session info
VError TlsGetInfo(Handle tls, VTlsInfo* info);
```

### 4.5 VTlsInfo Structure

```c
typedef struct VTlsInfo {
    uint16_t version;           // TLS version (0x0304 = TLS 1.3)
    uint16_t cipher_suite;      // Negotiated cipher suite
    char cipher_name[32];       // Human-readable cipher name
    char peer_cn[256];          // Peer certificate CN
    uint64_t peer_cert_expiry;  // Certificate expiry (Unix timestamp)
    uint32_t flags;             // TLS_INFO_* flags
    uint8_t _reserved[32];
} VTlsInfo;

#define TLS_INFO_VERIFIED    (1 << 0)  // Certificate verified
#define TLS_INFO_RESUMED     (1 << 1)  // Session resumed
#define TLS_INFO_EARLY_DATA  (1 << 2)  // 0-RTT data sent
```

---

## 5. Expanded Syscalls

### 5.1 New Syscall Categories

v0.2.0 adds syscalls in these categories:

| Category | Range         | Purpose                      |
|----------|---------------|------------------------------|
| Assign   | 0x00C0-0x00CF | Logical device management    |
| TLS      | 0x00D0-0x00DF | Secure communication         |
| Process  | 0x0110-0x011F | Process management expansion |
| Memory   | 0x0120-0x012F | Memory operations            |
| Time     | 0x0130-0x013F | Time operations expansion    |
| Console  | 0x0140-0x014F | Console operations           |

### 5.2 Complete Syscall Table (v0.2.0)

```c
enum VSyscall {
    // === Existing (v0.1.0) ===
    
    // Task (0x0000-0x000F)
    VSYS_TaskYield       = 0x0000,
    VSYS_TaskExit        = 0x0001,
    VSYS_TaskSleep       = 0x0002,
    VSYS_TaskCurrent     = 0x0003,
    VSYS_TaskSpawn       = 0x0004,  // Create new task in current Viper
    
    // Channel (0x0010-0x001F)
    VSYS_ChannelCreate   = 0x0010,
    VSYS_ChannelSend     = 0x0011,
    VSYS_ChannelRecv     = 0x0012,
    VSYS_ChannelClose    = 0x0013,
    VSYS_ChannelPeek     = 0x0014,
    
    // Timer (0x0020-0x002F)
    VSYS_TimerCreate     = 0x0020,
    VSYS_TimerSet        = 0x0021,
    VSYS_TimerCancel     = 0x0022,
    VSYS_TimerClose      = 0x0023,
    
    // Poll (0x0030-0x003F)
    VSYS_PollCreate      = 0x0030,
    VSYS_PollAdd         = 0x0031,
    VSYS_PollRemove      = 0x0032,
    VSYS_PollWait        = 0x0033,
    VSYS_PollClose       = 0x0034,
    
    // Heap (0x0040-0x004F)
    VSYS_HeapAlloc       = 0x0040,
    VSYS_HeapRetain      = 0x0041,
    VSYS_HeapRelease     = 0x0042,
    VSYS_HeapGetLen      = 0x0043,
    VSYS_HeapSetLen      = 0x0044,
    VSYS_HeapGetBuffer   = 0x0045,
    
    // Capability (0x0050-0x005F)
    VSYS_CapDerive       = 0x0050,
    VSYS_CapRevoke       = 0x0051,
    VSYS_CapQuery        = 0x0052,
    
    // I/O (0x0060-0x006F)
    VSYS_IORead          = 0x0060,
    VSYS_IOWrite         = 0x0061,
    VSYS_IOSeek          = 0x0062,
    VSYS_IOClose         = 0x0063,
    VSYS_IOFlush         = 0x0064,
    
    // Filesystem (0x0070-0x007F)
    VSYS_FsOpenRoot      = 0x0070,
    VSYS_FsOpen          = 0x0071,
    VSYS_FsCreate        = 0x0072,
    VSYS_FsDelete        = 0x0073,
    VSYS_FsRename        = 0x0074,
    VSYS_FsReadDir       = 0x0075,
    VSYS_FsStat          = 0x0076,
    VSYS_FsSetInfo       = 0x0077,
    VSYS_FsTruncate      = 0x0078,
    VSYS_FsSync          = 0x0079,
    
    // Surface (0x0080-0x008F)
    VSYS_SurfaceAcquire  = 0x0080,
    VSYS_SurfaceGetBuffer = 0x0081,
    VSYS_SurfacePresent  = 0x0082,
    VSYS_SurfaceRelease  = 0x0083,
    
    // Input (0x0090-0x009F)
    VSYS_InputGetHandle  = 0x0090,
    VSYS_InputPoll       = 0x0091,
    
    // Network (0x00A0-0x00AF)
    VSYS_NetSocket       = 0x00A0,
    VSYS_NetBind         = 0x00A1,
    VSYS_NetConnect      = 0x00A2,
    VSYS_NetListen       = 0x00A3,
    VSYS_NetAccept       = 0x00A4,
    VSYS_NetSend         = 0x00A5,
    VSYS_NetRecv         = 0x00A6,
    VSYS_NetClose        = 0x00A7,
    VSYS_NetGetAddr      = 0x00A8,
    VSYS_NetSetOpt       = 0x00A9,
    VSYS_NetGetOpt       = 0x00AA,
    
    // DNS (0x00B0-0x00BF)
    VSYS_DnsResolve      = 0x00B0,
    VSYS_DnsResolveAsync = 0x00B1,
    VSYS_DnsSetServer    = 0x00B2,
    
    // Debug (0x00F0-0x00FF)
    VSYS_DebugPrint      = 0x00F0,
    VSYS_DebugBreak      = 0x00F1,
    VSYS_DebugPanic      = 0x00F2,
    VSYS_DebugLog        = 0x00F3,
    
    // Viper (0x0100-0x010F)
    VSYS_ViperSpawn      = 0x0100,
    VSYS_ViperExit       = 0x0101,
    VSYS_ViperWait       = 0x0102,
    VSYS_ViperGetInfo    = 0x0103,
    VSYS_ViperSetName    = 0x0104,
    
    // === New in v0.2.0 ===
    
    // Assign (0x00C0-0x00CF)
    VSYS_AssignSet       = 0x00C0,
    VSYS_AssignRemove    = 0x00C1,
    VSYS_AssignGet       = 0x00C2,
    VSYS_AssignList      = 0x00C3,
    VSYS_AssignResolve   = 0x00C4,  // Resolve path to handle
    
    // TLS (0x00D0-0x00DF)
    VSYS_TlsCreate       = 0x00D0,
    VSYS_TlsHandshake    = 0x00D1,
    VSYS_TlsSend         = 0x00D2,
    VSYS_TlsRecv         = 0x00D3,
    VSYS_TlsClose        = 0x00D4,
    VSYS_TlsGetInfo      = 0x00D5,
    
    // Process Extended (0x0110-0x011F)
    VSYS_ProcessGetEnv   = 0x0110,
    VSYS_ProcessSetEnv   = 0x0111,
    VSYS_ProcessGetCwd   = 0x0112,
    VSYS_ProcessSetCwd   = 0x0113,
    VSYS_ProcessGetArgs  = 0x0114,
    VSYS_ProcessGetPid   = 0x0115,
    VSYS_ProcessGetPpid  = 0x0116,
    
    // Memory (0x0120-0x012F)
    VSYS_MemoryInfo      = 0x0120,  // Get memory statistics
    VSYS_MemoryMap       = 0x0121,  // Map physical memory (privileged)
    VSYS_MemoryUnmap     = 0x0122,  // Unmap memory
    VSYS_MemoryProtect   = 0x0123,  // Change memory protection
    
    // Time Extended (0x0130-0x013F)
    VSYS_TimeNow         = 0x0130,  // Current time (nanoseconds)
    VSYS_TimeDate        = 0x0131,  // Current date/time structure
    VSYS_TimeSetDate     = 0x0132,  // Set system date/time
    VSYS_TimeZone        = 0x0133,  // Get/set timezone
    VSYS_Uptime          = 0x0134,  // System uptime
    
    // Console (0x0140-0x014F)
    VSYS_ConsoleClear    = 0x0140,
    VSYS_ConsoleSetColor = 0x0141,
    VSYS_ConsoleGetSize  = 0x0142,
    VSYS_ConsoleSetCursor = 0x0143,
    VSYS_ConsoleGetCursor = 0x0144,
    VSYS_ConsoleSetTitle = 0x0145,
    VSYS_ConsoleGetChar  = 0x0146,  // Read character (blocking)
    VSYS_ConsolePutChar  = 0x0147,  // Write character
    VSYS_ConsoleReadLine = 0x0148,  // Read line with editing
};
```

### 5.3 Syscall Details

#### 5.3.1 AssignResolve

```c
// Resolve a path to a file/directory handle
// path: Full path like "SYS:c\dir.vpr" or "C:dir.vpr"
// flags: VFS_READ, VFS_WRITE, VFS_CREATE, etc.
// Returns: X0=VError, X1=handle, X2=kind (FILE or DIRECTORY)
VError AssignResolve(const char* path, size_t path_len, uint32_t flags);
```

This is the primary way to open files. It combines assign lookup with path resolution.

#### 5.3.2 ProcessGetEnv / ProcessSetEnv

```c
// Get environment variable
// name: Variable name
// buf: Buffer for value
// buf_len: Buffer size
// Returns: X0=VError, X1=actual_length
VError ProcessGetEnv(const char* name, size_t name_len, 
                     char* buf, size_t buf_len);

// Set environment variable
// name: Variable name
// value: Variable value (NULL to unset)
// Returns: X0=VError
VError ProcessSetEnv(const char* name, size_t name_len,
                     const char* value, size_t value_len);
```

#### 5.3.3 MemoryInfo

```c
// Get memory statistics
typedef struct VMemoryInfo {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
    uint64_t kernel_bytes;
    uint64_t user_bytes;
    uint64_t cached_bytes;
    uint64_t page_size;
    uint32_t total_pages;
    uint32_t free_pages;
    uint8_t _reserved[32];
} VMemoryInfo;

// Returns: X0=VError
VError MemoryInfo(VMemoryInfo* info);
```

#### 5.3.4 TimeDate

```c
typedef struct VDateTime {
    uint16_t year;      // 1970-9999
    uint8_t month;      // 1-12
    uint8_t day;        // 1-31
    uint8_t hour;       // 0-23
    uint8_t minute;     // 0-59
    uint8_t second;     // 0-59
    uint8_t weekday;    // 0=Sunday, 6=Saturday
    uint32_t nanosecond;
    int16_t tz_offset;  // Minutes from UTC
    uint8_t _reserved[6];
} VDateTime;

// Get current date/time
// Returns: X0=VError
VError TimeDate(VDateTime* dt);

// Set system date/time (privileged)
VError TimeSetDate(const VDateTime* dt);
```

#### 5.3.5 Console Operations

```c
// Clear console
VError ConsoleClear(void);

// Set text color
// fg: Foreground color (0-15 or RGB)
// bg: Background color
VError ConsoleSetColor(uint32_t fg, uint32_t bg);

// Get console size
// Returns: X0=VError, X1=width, X2=height
VError ConsoleGetSize(void);

// Set cursor position
VError ConsoleSetCursor(uint32_t x, uint32_t y);

// Get cursor position
// Returns: X0=VError, X1=x, X2=y
VError ConsoleGetCursor(void);

// Read line with editing
// buf: Buffer for line
// max_len: Buffer size
// Returns: X0=VError, X1=length
VError ConsoleReadLine(char* buf, size_t max_len);
```

---

## 6. Shell Enhancements

### 6.1 Environment Variables

The shell must support environment variables:

| Variable   | Purpose                       | Default   |
|------------|-------------------------------|-----------|
| `$RC`      | Return code of last command   | 0         |
| `$Result`  | Result string of last command | ""        |
| `$Process` | Current process number        | (pid)     |
| `$Prompt`  | Shell prompt format           | "$Path> " |
| `$Path`    | Command search path           | "C:"      |
| `$User`    | Current user name             | "default" |
| `$Home`    | Home directory                | "HOME:"   |

### 6.2 Prompt Customization

```
Set Prompt "$User@$Host:$Path> "
```

Variables in prompt:

- `$Path` - Current directory
- `$User` - User name
- `$Host` - System name
- `$Date` - Current date
- `$Time` - Current time
- `$RC` - Last return code

### 6.3 Command Line Editing

| Key           | Action                       |
|---------------|------------------------------|
| ← →           | Move cursor                  |
| Home / Ctrl+A | Move to start                |
| End / Ctrl+E  | Move to end                  |
| Backspace     | Delete before cursor         |
| Delete        | Delete at cursor             |
| Ctrl+K        | Kill to end of line          |
| Ctrl+U        | Kill to start of line        |
| Ctrl+W        | Delete word before cursor    |
| ↑ ↓           | History navigation           |
| Tab           | Command/path completion      |
| Ctrl+C        | Cancel current line          |
| Ctrl+D        | End of input (exit if empty) |
| Ctrl+L        | Clear screen                 |

### 6.4 History

```
History [n]           Show last n commands (default: 20)
History CLEAR         Clear history
History SAVE          Save history to HOME:history
History LOAD          Load history from HOME:history
```

History file: `HOME:.history` (hidden file)

### 6.5 Tab Completion

- First word: Complete command names from Path
- Subsequent words: Complete file/directory names
- After `:` : Complete assigns

### 6.6 Startup Scripts

Executed in order at shell start:

1. `S:shell-startup.bas` (system-wide)
2. `HOME:shell-startup.bas` (per-user, if exists)

---

## 7. Implementation Plan

### 7.1 Phase Overview

| Phase | Duration | Focus              |
|-------|----------|--------------------|
| 7A    | 2 weeks  | Command renaming   |
| 7B    | 2 weeks  | Assigns system     |
| 7C    | 4 weeks  | TLS/Crypto         |
| 7D    | 2 weeks  | HTTPS integration  |
| 7E    | 2 weeks  | Syscall expansion  |
| 7F    | 2 weeks  | Shell enhancements |

**Total: 14 weeks**

### 7.2 Phase 7A: Command Renaming (Weeks 1-2)

**Week 1:**

- Rename all command functions in vinit
- Update help text
- Update output formats (Dir, List style)

**Week 2:**

- Implement return codes (OK/WARN/ERROR/FAIL)
- Add `Why` command for error explanation
- Update shell prompt

**Deliverable:** All commands have correct retro-style names and output.

### 7.3 Phase 7B: Assigns System (Weeks 3-4)

**Week 3:**

- Implement assign table in kernel
- Add assign syscalls
- Create `Assign` command

**Week 4:**

- Implement path resolution with assigns
- Add `Path` command
- Update file operations to use assigns

**Deliverable:** `Assign WORK: HOME:projects` works; `Dir WORK:` lists files.

### 7.4 Phase 7C: TLS/Crypto (Weeks 5-8)

**Week 5: Primitives I**

- SHA-256 implementation
- AES implementation (or use hardware AES if available)
- Random number generator

**Week 6: Primitives II**

- ChaCha20-Poly1305
- HKDF key derivation
- X25519 key exchange

**Week 7: TLS Core**

- TLS record layer
- TLS handshake state machine (client)
- Certificate parsing (X.509, ASN.1)

**Week 8: TLS Integration**

- Root CA store
- Certificate validation
- TLS session management
- TLS syscalls

**Deliverable:** TLS handshake with google.com succeeds.

### 7.5 Phase 7D: HTTPS (Weeks 9-10)

**Week 9:**

- Update HTTP client to use TLS
- URL parsing for https://
- `Fetch` command with HTTPS

**Week 10:**

- Certificate error handling
- Connection timeout handling
- Testing with major websites

**Deliverable:** `Fetch https://www.google.com` returns content.

### 7.6 Phase 7E: Syscall Expansion (Weeks 11-12)

**Week 11:**

- Environment variable syscalls
- Memory info syscall
- Date/time syscalls

**Week 12:**

- Console syscalls
- Process info syscalls
- Testing and documentation

**Deliverable:** All new syscalls implemented and tested.

### 7.7 Phase 7F: Shell Enhancements (Weeks 13-14)

**Week 13:**

- Environment variable support in shell
- Prompt customization
- History save/load

**Week 14:**

- Tab completion improvements
- Startup scripts
- Final testing

**Deliverable:** Full-featured shell with v0.2.0 functionality.

---

## 8. Testing Requirements

### 8.1 Command Tests

Each command must have tests verifying:

- Correct name and help text
- Expected output format
- Return codes for success/failure
- Error handling

### 8.2 Assign Tests

```
# Test basic assign
Assign TEST: T:
Dir TEST:
Assign TEST:

# Test path resolution
Assign WORK: HOME:projects
Type WORK:readme.txt

# Test multi-directory assign
Assign C: SYS:c
Assign C: SYS:contrib\c ADD
Which somecommand
```

### 8.3 TLS Tests

```
# Test TLS handshake
Fetch https://www.google.com HEADERS

# Test certificate validation
Fetch https://expired.badssl.com  (should fail)
Fetch https://wrong.host.badssl.com  (should fail)
Fetch https://self-signed.badssl.com  (should fail)

# Test with verification disabled
Fetch https://self-signed.badssl.com NOVERIFY
```

### 8.4 Syscall Tests

Each new syscall should have unit tests in the kernel test suite.

### 8.5 Integration Tests

```
# Full workflow test
Assign DOWNLOAD: T:downloads
MakeDir DOWNLOAD:
Fetch https://example.com/file.zip TO DOWNLOAD:file.zip
List DOWNLOAD:
Delete DOWNLOAD:file.zip ALL
```

---

## Appendix A: Cryptographic Implementation Notes

### A.1 AES

Use AES-NI instructions if available on x86, or NEON AES instructions on ARM64. Fallback to software implementation.

ARM64 AES instructions (ARMv8 Crypto Extension):

- `AESE` - AES single round encryption
- `AESD` - AES single round decryption
- `AESMC` - AES mix columns
- `AESIMC` - AES inverse mix columns

Check for crypto extension: `ID_AA64ISAR0_EL1.AES`

### A.2 ChaCha20-Poly1305

Pure software implementation. Use NEON for vectorization if available.

### A.3 X25519

Use constant-time implementation to prevent timing attacks. Consider using existing tested code (e.g., from SUPERCOP or
similar).

### A.4 Big Integer Arithmetic

For RSA certificate verification, need big integer operations:

- Modular exponentiation (for signature verification)
- No need for key generation (client only)

---

## Appendix B: Root CA Certificates

Minimum required root CAs for v0.2.0:

| CA                        | Purpose             | Size   |
|---------------------------|---------------------|--------|
| ISRG Root X1              | Let's Encrypt       | ~1KB   |
| ISRG Root X2              | Let's Encrypt ECDSA | ~0.5KB |
| DigiCert Global Root CA   | Major sites         | ~1KB   |
| DigiCert Global Root G2   | Major sites         | ~1KB   |
| Baltimore CyberTrust Root | Azure, Microsoft    | ~1KB   |
| Amazon Root CA 1          | AWS                 | ~1KB   |
| Google Trust Services     | Google              | ~1KB   |
| Cloudflare Origin CA      | Cloudflare          | ~1KB   |

Total: ~8KB of root certificates

Store as DER format in `SYS:certs\roots.der` or compile into kernel.

---

## Appendix C: Version Identification

### C.1 Version String

```
ViperOS 0.2.0 (December 2025)
```

### C.2 Version Syscall

```c
typedef struct VVersionInfo {
    uint16_t major;         // 0
    uint16_t minor;         // 2
    uint16_t patch;         // 0
    uint16_t build;         // Build number
    char version_string[32];
    char build_date[16];
    char platform[16];      // "AArch64"
    uint8_t _reserved[16];
} VVersionInfo;

VError VersionGet(VVersionInfo* info);
```

### C.3 Version Command

```
SYS:> Version
ViperOS 0.2.0 (December 2025)
Built: 25-Dec-2025 10:30:00
Platform: AArch64
Kernel: 45 components, 15000 lines
```

---

*"ViperOS - Software should be art."*
