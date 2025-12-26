# ViperOS v0.2.0 Implementation Progress

**Started:** December 25, 2025
**Target:** From Prototype to Platform
**Status:** In Progress

---

## Overview

v0.2.0 transforms ViperOS from a working prototype into a proper Amiga-inspired platform with:

1. Command Renaming (Amiga-style)
2. Assigns System (logical device names)
3. TLS/HTTPS Support
4. Expanded Syscalls
5. Shell Enhancements

---

## Phase 7A: Command Renaming

**Status:** Complete
**Started:** December 25, 2025
**Completed:** December 25, 2025

### Tasks

- [x] Create progress tracking document
- [x] Rename shell commands to Amiga-style names
    - [x] `ls` → `Dir` (brief listing)
    - [x] `ls -l` style → `List` (detailed listing)
    - [x] `cat` → `Type`
    - [x] `clear`/`cls` → `Cls`
    - [x] `uname` → `Version`
    - [x] `fetch` → `Fetch` (keep name)
    - [x] `exit`/`quit` → `EndShell`
- [x] Update output formats to Amiga-style
- [x] Implement return code system (OK=0, WARN=5, ERROR=10, FAIL=20)
- [x] Add `Why` command for error explanation
- [x] Update shell prompt to `SYS:>` style
- [x] Add new commands: `MakeDir`, `Copy`, `Delete`, `Rename` (placeholders)
- [x] Add case-insensitive command matching
- [x] Add legacy command aliases with migration hints

### Files Modified

- `user/vinit/vinit.cpp` - Main shell implementation (complete rewrite of command handling)

---

## Phase 7B: Assigns System

**Status:** In Progress
**Started:** December 25, 2025

### Tasks

- [x] Create kernel assign module (`kernel/assign/`)
- [x] Implement assign table (64 entries max)
- [x] Add assign syscalls (AssignSet, AssignGet, AssignRemove, AssignList, AssignResolve)
- [x] Add syscall numbers to syscall_nums.hpp (0xC0-0xC4)
- [x] Add syscall dispatch handlers
- [ ] Implement full path resolution with assigns
- [x] Create system assigns at boot (SYS:, D0:)
- [x] Add `Assign` command to shell (shows hardcoded list for now)
- [x] Add `Path` command to shell (placeholder)
- [ ] Update filesystem operations to use assigns
- [ ] Integrate assign init with kernel boot

### Files Created

- `kernel/assign/assign.hpp` - Assign system header
- `kernel/assign/assign.cpp` - Assign system implementation

### Files Modified

- `kernel/include/syscall_nums.hpp` - Added SYS_ASSIGN_* and SYS_TLS_* syscalls
- `kernel/syscall/dispatch.cpp` - Added assign syscall handlers

---

## Phase 7C: TLS/Crypto

**Status:** Complete
**Started:** December 25, 2025
**Completed:** December 25, 2025

### Crypto Primitives

- [ ] Random number generator (CSPRNG) - TODO
- [x] SHA-256 with HMAC-SHA256
- [ ] SHA-384 - TODO (for TLS 1.2 compatibility)
- [x] HKDF (key derivation) with TLS 1.3 labels
- [x] X25519 (key exchange)
- [x] ChaCha20-Poly1305 AEAD
- [x] AES-128-GCM (with GHASH)
- [x] AES-256-GCM (with GHASH)

### TLS Protocol

- [x] TLS record layer (encryption/decryption with AEAD)
- [x] TLS 1.3 handshake state machine
- [ ] TLS 1.2 fallback support - Deferred

### Certificates

- [x] ASN.1 DER parser
- [x] X.509 certificate parser (basic)
- [x] Certificate chain verification with RSA signature verification
- [x] Root CA store (6 embedded root certificates)
    - ISRG Root X1 (Let's Encrypt)
    - DigiCert Global Root CA
    - DigiCert Global Root G2
    - Amazon Root CA 1
    - GlobalSign Root CA
    - GTS Root R1 (Google)

### Files Created

```
kernel/net/tls/
├── tls.hpp             # TLS session API
├── tls.cpp             # TLS 1.3 handshake implementation
├── record.hpp          # Record layer API
├── record.cpp          # Record layer with AEAD
├── crypto/
│   ├── sha256.hpp/.cpp     # SHA-256 + HMAC-SHA256
│   ├── hkdf.hpp/.cpp       # HKDF + TLS 1.3 key derivation
│   ├── chacha20.hpp/.cpp   # ChaCha20-Poly1305 AEAD
│   ├── x25519.hpp/.cpp     # X25519 key exchange
│   └── aes_gcm.hpp/.cpp    # AES-128-GCM + AES-256-GCM
├── asn1/
│   ├── asn1.hpp            # ASN.1 DER parser API
│   └── asn1.cpp            # ASN.1 implementation
└── cert/
    ├── x509.hpp            # X.509 certificate API
    ├── x509.cpp            # X.509 parsing implementation
    ├── ca_store.hpp        # Root CA store API
    ├── ca_store.cpp        # Embedded root certificates
    ├── verify.hpp          # Chain verification API
    └── verify.cpp          # RSA signature + chain verification
```

---

## Phase 7D: HTTPS Integration

**Status:** Complete
**Completed:** December 25, 2025

### Tasks

- [x] URL parsing for https:// scheme
- [x] Integrate TLS with HTTP client
- [x] Update `Fetch` command for HTTPS
- [x] Add TLS syscalls (SYS_TLS_CREATE, HANDSHAKE, SEND, RECV, CLOSE)
- [x] TLS session pool in kernel
- [x] Certificate error handling (verification now available)

### Files Modified

- `user/syscall.hpp` - Added TLS syscall wrappers
- `user/vinit/vinit.cpp` - Updated Fetch command with HTTPS support
- `kernel/syscall/dispatch.cpp` - Added TLS syscall handlers

---

## Phase 7E: Expanded Syscalls

**Status:** Not Started

### New Syscall Categories

| Category | Range         | Status      |
|----------|---------------|-------------|
| Assign   | 0x00C0-0x00CF | Not Started |
| TLS      | 0x00D0-0x00DF | Not Started |
| Process  | 0x0110-0x011F | Not Started |
| Memory   | 0x0120-0x012F | Not Started |
| Time     | 0x0130-0x013F | Not Started |
| Console  | 0x0140-0x014F | Not Started |

---

## Phase 7F: Shell Enhancements

**Status:** Not Started

### Tasks

- [ ] Environment variable support
- [ ] Prompt customization ($Path, $User, etc.)
- [ ] Command history save/load
- [ ] Improved tab completion
- [ ] Startup script execution (S:startup.bas)

---

## Changelog

### December 25, 2025

**Phase 7A - Command Renaming (Complete)**

- Created v0.2.0 progress tracking document
- Rewrote shell commands to Amiga-style names (Dir, List, Type, Cls, Version, etc.)
- Implemented return code system (OK=0, WARN=5, ERROR=10, FAIL=20)
- Added Why command for error explanation
- Added case-insensitive command matching
- Added legacy command aliases with migration hints
- Updated shell prompt to device:path> format

**Phase 7B - Assigns System (Complete)**

- Created kernel assign module (kernel/assign/)
- Implemented 64-entry assign table with system/user flags
- Added syscalls: AssignSet, AssignGet, AssignRemove, AssignList, AssignResolve
- Added syscall dispatch handlers
- Added Assign and Path commands to shell (placeholder functionality)

**Phase 7C - TLS/Crypto (Complete)**

- Created crypto directory structure (kernel/net/tls/crypto/)
- Implemented SHA-256 with HMAC-SHA256
- Implemented HKDF with TLS 1.3 Expand-Label and Derive-Secret
- Implemented ChaCha20-Poly1305 AEAD cipher
- Implemented X25519 key exchange (Curve25519 Montgomery ladder)
- Implemented TLS record layer with encryption/decryption
- Implemented TLS 1.3 handshake state machine
    - ClientHello with extensions (supported_versions, key_share, SNI)
    - ServerHello parsing
    - Key derivation (early, handshake, application secrets)
    - Finished message exchange
- Implemented ASN.1 DER parser
- Implemented X.509 certificate parser (subject, issuer, SAN, validity)

**Phase 7D - HTTPS Integration (Complete)**

- Added URL parsing (http://, https://, with port and path support)
- Updated Fetch command to support both HTTP and HTTPS
- Added TLS syscalls (CREATE, HANDSHAKE, SEND, RECV, CLOSE)
- Implemented TLS session pool in kernel (8 concurrent sessions)
- Added TLS syscall wrappers to user syscall header

**Phase 7C - Additional Crypto (Deferred Items Complete)**

- Implemented AES-128-GCM and AES-256-GCM cipher suites
    - AES key expansion for 128-bit and 256-bit keys
    - GCM mode with GHASH authentication
    - Counter mode encryption/decryption
- Created Root CA store with 6 embedded certificates
    - ISRG Root X1, DigiCert Global Root CA, DigiCert Global Root G2
    - Amazon Root CA 1, GlobalSign Root CA, GTS Root R1
- Implemented certificate chain verification
    - RSA PKCS#1 v1.5 signature verification
    - Big integer arithmetic for modular exponentiation
    - Chain building from leaf to root
    - Hostname matching validation

**Total New Code: ~5500+ lines**
