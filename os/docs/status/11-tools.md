# Build Tools

**Status:** Functional host-side utilities
**Location:** `tools/`
**SLOC:** ~1,334

## Overview

The tools directory contains host-side utilities for building ViperOS disk images. These are compiled for the development machine (macOS/Linux) and generate artifacts used by the kernel at boot time.

---

## Components

### 1. mkfs.viperfs (`mkfs.viperfs.cpp`)

**Status:** Complete filesystem image builder

A host-side utility that creates ViperFS filesystem images from a set of input files. The tool outputs a complete, bootable disk image.

**Usage:**
```
mkfs.viperfs <image> <size_mb> [options...] [files...]

Options:
  --mkdir <path>        Create directory at path (e.g., SYS/certs)
  --add <src>:<dest>    Add file from src to dest path
  <file>                Add file to root directory (legacy)
```

**Examples:**
```bash
# Create 8MB image with vinit and certificates
mkfs.viperfs disk.img 8 vinit.elf \
    --mkdir SYS/certs \
    --add roots.der:SYS/certs/roots.der
```

**Implemented:**
- Superblock initialization (magic, version, layout)
- Block allocation bitmap
- Inode table (256-byte inodes)
- Root directory with `.` and `..`
- File data block writing (direct + single indirect)
- Directory creation with parent path creation
- Variable-length directory entries
- Random UUID generation
- Layout calculation and metadata finalization

**Filesystem Layout:**
```
Block 0:        Superblock
Blocks 1-N:     Block bitmap (1 bit per block)
Blocks N+1-M:   Inode table (16 inodes per 4KB block)
Blocks M+1-end: Data blocks
```

**Inode Structure (256 bytes):**
| Field | Size | Description |
|-------|------|-------------|
| inode_num | 8 | Inode number |
| mode | 4 | Type + permissions |
| flags | 4 | Reserved |
| size | 8 | File size |
| blocks | 8 | Block count |
| atime/mtime/ctime | 24 | Timestamps |
| direct[12] | 96 | Direct block pointers |
| indirect | 8 | Single indirect |
| double_indirect | 8 | Double indirect |
| triple_indirect | 8 | Triple indirect |
| generation | 8 | Reserved |

**Directory Entry:**
| Field | Size | Description |
|-------|------|-------------|
| inode | 8 | Inode number |
| rec_len | 2 | Total record length |
| name_len | 1 | Name length |
| file_type | 1 | Entry type |
| name | variable | Entry name |

**Limitations:**
- Single indirect blocks only (max ~2MB files without double indirect)
- Single data block per directory
- No error handling for very large files

---

### 2. gen_roots_der (`gen_roots_der.cpp`)

**Status:** Complete CA certificate bundle generator

Generates a `roots.der` bundle containing trusted root CA public keys for TLS certificate verification.

**Usage:**
```
gen_roots_der <output.der>
```

**Output Format:**
```
[u32 count]              - Number of CA entries
For each entry:
  [u32 der_len]          - Length of DER data
  [der_len bytes]        - DER-encoded SubjectPublicKeyInfo
```

**Embedded Root CAs:**
| CA | Type | Size |
|----|------|------|
| ISRG Root X1 | RSA 4096 | 550 bytes |
| DigiCert Global Root CA | RSA 2048 | 294 bytes |
| DigiCert Global Root G2 | RSA 2048 | 294 bytes |
| Amazon Root CA 1 | RSA 2048 | 294 bytes |
| GlobalSign Root CA | RSA 2048 | 294 bytes |
| GTS Root R1 | RSA 4096 | 550 bytes |

**Total bundle size:** ~2.3KB

**Purpose:**
The kernel TLS stack uses this bundle for:
- Server certificate chain validation
- Trust anchor comparison
- RSA signature verification during handshake

---

## Build Process

These tools are built automatically by `build_viper.sh`:

```bash
# Compile mkfs.viperfs if needed
if [[ ! -x "$TOOLS_DIR/mkfs.viperfs" ]]; then
    c++ -std=c++17 -O2 -o "$TOOLS_DIR/mkfs.viperfs" "$TOOLS_DIR/mkfs.viperfs.cpp"
fi

# Compile gen_roots_der if needed
if [[ ! -x "$TOOLS_DIR/gen_roots_der" ]]; then
    c++ -std=c++17 -O2 -o "$TOOLS_DIR/gen_roots_der" "$TOOLS_DIR/gen_roots_der.cpp"
fi

# Generate certificate bundle
"$TOOLS_DIR/gen_roots_der" "$BUILD_DIR/roots.der"

# Create disk image
"$TOOLS_DIR/mkfs.viperfs" "$BUILD_DIR/disk.img" 8 \
    "$BUILD_DIR/vinit.elf" \
    --mkdir SYS \
    --mkdir SYS/certs \
    --add "$BUILD_DIR/roots.der:SYS/certs/roots.der"
```

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `mkfs.viperfs.cpp` | ~1,070 | Filesystem image builder |
| `gen_roots_der.cpp` | ~264 | CA bundle generator |

---

## Priority Recommendations

1. **Medium:** Add double indirect block support for larger files
2. **Medium:** Add filesystem verification/fsck tool
3. **Low:** Add image inspection/dump tool
4. **Low:** Support multiple directories per directory inode
5. **Low:** Add certificate bundle update script from Mozilla CA list
