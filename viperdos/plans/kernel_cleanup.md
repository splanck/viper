# ViperDOS Kernel Cleanup Plan: Microkernel to Monolithic Transition

**Date:** January 2026
**Purpose:** Comprehensive audit of microkernel artifacts that need to be removed or updated

---

## Executive Summary

ViperDOS is transitioning from a microkernel design to a monolithic kernel architecture. This document identifies all artifacts (code, documentation, comments) that reference the microkernel design and need to be updated or removed.

**Current State:**
- `VIPER_MICROKERNEL_MODE` is set to `0` in `kernel/include/config.hpp:37`
- Kernel FS, NET, TLS, and BLK services are all enabled (lines 48-68)
- vinit only starts `displayd` and `netd` (not fsd, blkd, consoled)
- fsd and blkd are commented out as "using kernel services directly (monolithic mode)"

---

## 1. Server Code to Remove

These user-space servers exist but are no longer used in monolithic mode:

### 1.1 `user/servers/fsd/` - Filesystem Server (HIGH PRIORITY)
**Status:** DISABLED in vinit, code still exists
**Files:**
- `user/servers/fsd/main.cpp` - Main server logic
- `user/servers/fsd/viperfs.cpp` / `viperfs.hpp` - ViperFS implementation (duplicate of kernel)
- `user/servers/fsd/blk_client.hpp` - Client to talk to blkd
- `user/servers/fsd/fs_protocol.hpp` - IPC protocol definitions
- `user/servers/fsd/format.hpp` - Format constants
- `user/servers/fsd/CMakeLists.txt` - Build config

**Action:** Remove entire directory

### 1.2 `user/servers/blkd/` - Block Device Server (HIGH PRIORITY)
**Status:** DISABLED in vinit, code still exists
**Files:**
- `user/servers/blkd/main.cpp` - VirtIO-blk driver in userspace
- `user/servers/blkd/blk_protocol.hpp` - IPC protocol definitions
- `user/servers/blkd/CMakeLists.txt` - Build config

**Action:** Remove entire directory

### 1.3 `user/servers/netd/` - Network Server (KEEP FOR NOW)
**Status:** STILL ACTIVE - kernel net stack exists but netd provides socket abstraction
**Note:** vinit.cpp:52-53 says "kernel net stack not implemented, use netd"

**Action:** Keep for now; evaluate if kernel net stack is complete

### 1.4 `user/servers/consoled/` - Console Server (KEEP - GUI COMPONENT)
**Status:** Used for GUI terminal, launched on-demand from workbench
**Files:**
- `user/servers/consoled/main.cpp`
- `user/servers/consoled/console_protocol.hpp`
- `user/servers/consoled/CMakeLists.txt`

**Action:** Keep - this is a GUI application, not a microkernel server

### 1.5 `user/servers/displayd/` - Display Server (KEEP - GUI COMPONENT)
**Status:** ACTIVE and required for GUI
**Action:** Keep - this is the GUI compositor

### 1.6 `user/servers/CMakeLists.txt`
**Action:** Update to only build displayd, consoled, and netd

---

## 2. Client Libraries to Review

### 2.1 `user/libfsclient/` (if exists)
**Status:** Routes filesystem calls to fsd server
**Action:** Remove if exists (check if directory exists)

### 2.2 `user/libnetclient/` (if exists)
**Status:** Routes network calls to netd server
**Action:** Keep if netd is kept

### 2.3 `user/libc/src/consoled_backend.cpp`
**Status:** ACTIVE - Routes stdout/stderr to consoled for GUI display
**Lines 62-78:** Connects to CONSOLED service via assign
**Action:** Keep - needed for GUI console output

---

## 3. Code References to Update

### 3.1 `user/vinit/vinit.cpp`
**Lines 17-30:** Comment block says "monolithic mode (VIPER_MICROKERNEL_MODE=0)" - OK
**Lines 49-59:** Server list with fsd/blkd commented out - CLEAN UP comments
**Line 547:** Still says "Start microkernel servers" in a comment
**Line 68:** `g_fsd_available` variable - can be removed

**Action:** Clean up comments referencing microkernel

### 3.2 `kernel/include/config.hpp`
**Lines 34-38:** Defines `VIPER_MICROKERNEL_MODE` with comment about "microkernel mode"
**Lines 17-28:** Doc comment talks about "microkernel migration"

**Action:** Remove VIPER_MICROKERNEL_MODE entirely; update comments

### 3.3 `kernel/main.cpp`
**Action:** Search for microkernel references and update

### 3.4 `user/vinit/cmd_misc.cpp`
**Action:** Check for microkernel references

### 3.5 `user/vinit/shell.cpp`
**Action:** Check for microkernel references

### 3.6 `user/vinit/cmd_fs.cpp`
**Action:** Check for FSD: references (should use kernel FS)

### 3.7 `user/fsinfo/fsinfo.cpp`
**Action:** Check for FSD: references

### 3.8 `user/edit/edit.cpp`
**Action:** Check for FSD: references

---

## 4. Documentation Updates Required

### 4.1 `README.md` (HIGH PRIORITY)
**Line 3:** "designed to explore microkernel architecture" → update
**Line 7:** "Microkernel status: User-space servers operational" → remove/update
**Lines 147-149:** Architecture diagram shows "Microkernel Servers" layer
**Lines 207-213:** Project structure shows servers with microkernel descriptions

**Action:** Major rewrite to describe monolithic kernel

### 4.2 `docs/status/00-overview.md` (HIGH PRIORITY)
**Line 9:** "capability-based microkernel" → "capability-based monolithic kernel"
**Line 12:** "Microkernel core" → update
**Line 16:** "User-space servers: netd (TCP/IP stack), fsd (filesystem), blkd (block devices)..." → update
**Lines 62-63:** References 13-servers.md as "Microkernel servers"
**Lines 99:** "Microkernel (EL1)"
**Lines 173-180:** "Microkernel Design" section
**Lines 197-205:** User-space servers table with fsd/blkd

**Action:** Major rewrite; remove fsd/blkd references

### 4.3 `docs/status/01-architecture.md`
**Status:** Contains microkernel references
**Line 3:** "Complete for QEMU virt platform (microkernel)"
**Line 9:** "ViperDOS microkernel"
**Lines 12-15:** Build configuration references VIPER_MICROKERNEL_MODE
**Line 327:** "Device Primitives - Microkernel Mode"

**Action:** Update architecture description

### 4.4 `docs/status/13-servers.md` (REMOVE OR MAJOR REWRITE)
**Status:** ~746 lines of microkernel server documentation
**Line 9:** "ViperDOS follows a microkernel architecture"
**Contents:** Documents all 6 servers (netd, fsd, blkd, consoled, inputd, displayd)

**Action:** Remove fsd/blkd documentation; rename to "GUI Servers" or similar

### 4.5 `docs/status/14-summary.md`
**Line 9:** "capability-based microkernel"
**Line 14:** "Microkernel core" in table
**Lines 29-51:** "Microkernel Core" and "User-Space Servers" tables with fsd/blkd
**Lines 151-180:** "Microkernel Architecture" section
**Line 297:** "Microkernel Design" as confirmed direction
**Line 450:** "microkernel architecture" in conclusion

**Action:** Major rewrite

### 4.6 `bugs/microkernel.md`
**Status:** ~570 lines documenting microkernel migration phases
**Contents:** 8-phase migration plan, now mostly complete

**Action:** Archive or remove; migration is complete

### 4.7 `docs/status/10-userspace.md`
**Status:** Large file (~32K tokens), likely has microkernel references
**Action:** Review and update references to servers

### 4.8 Other docs/status files to review:
- `docs/status/02-memory-management.md`
- `docs/status/03-console.md`
- `docs/status/04-drivers.md`
- `docs/status/05-filesystem.md`
- `docs/status/06-ipc.md`
- `docs/status/07-networking.md`
- `docs/status/08-scheduler.md`
- `docs/status/09-viper-process.md`
- `docs/status/11-tools.md`
- `docs/status/12-crypto.md`
- `docs/status/15-boot.md`
- `docs/status/16-gui.md`

**Action:** Review each for microkernel references

---

## 5. Build System Updates

### 5.1 `user/servers/CMakeLists.txt`
**Action:** Remove fsd and blkd subdirectories from build

### 5.2 `CMakeLists.txt` (root)
**Action:** Check for VIPER_MICROKERNEL_MODE references

### 5.3 System disk image creation
**Action:** Verify fsd.sys and blkd.sys are not being copied to sys.img

---

## 6. Files That Reference Servers (May Need Updates)

Based on grep for "FSD:|BLKD:|NETD:|microkernel":

1. `kernel/main.cpp` - Check boot messages
2. `user/vinit/vinit.cpp` - Server startup (already reviewed)
3. `user/vinit/cmd_misc.cpp` - Miscellaneous commands
4. `user/vinit/shell.cpp` - Shell implementation
5. `user/vinit/cmd_fs.cpp` - Filesystem commands
6. `user/servers/netd/main.cpp` - Keep
7. `user/servers/blkd/main.cpp` - Remove
8. `user/servers/fsd/main.cpp` - Remove
9. `user/fsinfo/fsinfo.cpp` - May reference FSD
10. `user/edit/edit.cpp` - May reference FSD

---

## 7. Priority Order for Cleanup

### Phase 1: Code Removal (Immediate)
1. Remove `user/servers/fsd/` directory
2. Remove `user/servers/blkd/` directory
3. Update `user/servers/CMakeLists.txt`
4. Remove `user/libfsclient/` if it exists
5. Remove VIPER_MICROKERNEL_MODE from `kernel/include/config.hpp`

### Phase 2: Code Updates (Short-term)
1. Clean up vinit.cpp comments
2. Remove unused variables (g_fsd_available)
3. Update kernel boot messages
4. Remove any FSD:/BLKD: client code in applications

### Phase 3: Documentation (Medium-term)
1. README.md - Major rewrite
2. docs/status/00-overview.md - Major rewrite
3. docs/status/14-summary.md - Major rewrite
4. docs/status/13-servers.md - Rename and trim
5. bugs/microkernel.md - Archive
6. All other docs/status/*.md - Review and update

### Phase 4: Verification (Final)
1. `grep -r "microkernel" .` should return minimal/zero results
2. `grep -r "FSD:" .` should only be in server code (to remove)
3. `grep -r "BLKD:" .` should only be in server code (to remove)
4. Build and test complete system

---

## 8. Servers to Keep

| Server | Purpose | Status |
|--------|---------|--------|
| displayd | GUI compositor | KEEP - Required for windowing |
| consoled | GUI terminal | KEEP - Provides terminal window |
| netd | Network stack | KEEP FOR NOW - Kernel net stack may need work |

---

## 9. Summary Statistics

| Category | Count | Action |
|----------|-------|--------|
| Server directories to remove | 2 | fsd, blkd |
| Server directories to keep | 3 | displayd, consoled, netd |
| Documentation files needing major updates | 5 | README, 00-overview, 13-servers, 14-summary, microkernel.md |
| Documentation files needing review | 12 | All other docs/status/*.md |
| Source files with microkernel references | ~10 | Various (see Section 6) |

---

## 10. Verification Checklist

After cleanup is complete, verify:

- [ ] `grep -ri "microkernel" --include="*.cpp" --include="*.hpp" --include="*.md"` returns acceptable results
- [ ] `grep -ri "FSD:" --include="*.cpp"` only in historical/test code
- [ ] `grep -ri "BLKD:" --include="*.cpp"` only in historical/test code
- [ ] `ls user/servers/` shows only: displayd, consoled, netd, CMakeLists.txt
- [ ] System boots and runs without fsd.sys or blkd.sys
- [ ] All documentation consistently describes "monolithic kernel"
- [ ] VIPER_MICROKERNEL_MODE is removed from codebase

---

*This cleanup plan is a comprehensive guide for transitioning ViperDOS documentation and code from microkernel to monolithic kernel architecture.*
