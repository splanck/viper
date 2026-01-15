#!/usr/bin/env python3
"""
Syscall table invariant tests for ViperDOS.

Tests:
1. Unique syscall numbers (no duplicates)
2. No NULL handlers
3. Non-empty names
4. argcount <= 6
5. Microkernel-critical syscall ABI is stable
"""

import re
import sys
from pathlib import Path

# Path to the syscall table source
SCRIPT_DIR = Path(__file__).parent
TABLE_CPP = SCRIPT_DIR.parent.parent / "kernel" / "syscall" / "table.cpp"

def parse_syscall_table(source_path):
    """Parse the syscall table from table.cpp and return list of entries."""
    content = source_path.read_text()

    # Find the syscall_table array
    # Pattern: {SYS_XXX, sys_xxx, "name", argcount}
    # Or: {SYS_XXX, nullptr, "name", argcount} for unimplemented
    table_pattern = r'static\s+const\s+SyscallEntry\s+syscall_table\s*\[\s*\]\s*=\s*\{(.*?)\};'
    match = re.search(table_pattern, content, re.DOTALL)

    if not match:
        print("ERROR: Could not find syscall_table array")
        return None

    table_content = match.group(1)

    # Parse individual entries
    # Matches: {SYS_NAME, handler_or_nullptr, "string_name", number}
    entry_pattern = r'\{\s*(SYS_\w+)\s*,\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*(\d+)\s*\}'
    entries = []

    for match in re.finditer(entry_pattern, table_content):
        syscall_const, handler, name, argcount = match.groups()
        entries.append({
            'syscall_const': syscall_const,
            'handler': handler,
            'name': name,
            'argcount': int(argcount),
        })

    return entries

def test_unique_numbers(entries):
    """Test that syscall constants are unique."""
    passed = 0
    failed = 0

    print("\n[Unique syscall numbers]")
    seen = {}
    for entry in entries:
        const = entry['syscall_const']
        if const in seen:
            print(f"  [FAIL] Duplicate syscall: {const}")
            failed += 1
        else:
            seen[const] = True
            passed += 1

    if failed == 0:
        print(f"  [PASS] All {passed} syscall numbers are unique")

    return passed, failed

def test_no_null_handlers(entries):
    """Test that no handlers are nullptr."""
    passed = 0
    failed = 0

    print("\n[No NULL handlers]")
    for entry in entries:
        if entry['handler'] == 'nullptr':
            print(f"  [FAIL] NULL handler for {entry['syscall_const']}")
            failed += 1
        else:
            passed += 1

    if failed == 0:
        print(f"  [PASS] All {passed} handlers are non-NULL")

    return passed, failed

def test_nonempty_names(entries):
    """Test that all syscall names are non-empty."""
    passed = 0
    failed = 0

    print("\n[Non-empty names]")
    for entry in entries:
        if not entry['name']:
            print(f"  [FAIL] Empty name for {entry['syscall_const']}")
            failed += 1
        else:
            passed += 1

    if failed == 0:
        print(f"  [PASS] All {passed} names are non-empty")

    return passed, failed

def test_argcount_valid(entries):
    """Test that argcount is between 0 and 6."""
    passed = 0
    failed = 0

    print("\n[Valid argcount (0-6)]")
    for entry in entries:
        if entry['argcount'] < 0 or entry['argcount'] > 6:
            print(f"  [FAIL] Invalid argcount {entry['argcount']} for {entry['syscall_const']}")
            failed += 1
        else:
            passed += 1

    if failed == 0:
        print(f"  [PASS] All {passed} argcounts are valid (0-6)")

    return passed, failed

def test_microkernel_critical_abi(entries):
    """Test that microkernel-critical syscalls exist with expected argcounts."""
    passed = 0
    failed = 0

    required = {
        # Task / loader
        "SYS_TASK_SPAWN": 3,

        # IPC core
        "SYS_CHANNEL_CREATE": 0,
        "SYS_CHANNEL_SEND": 5,
        "SYS_CHANNEL_RECV": 5,

        # Event mux
        "SYS_POLL_CREATE": 0,
        "SYS_POLL_ADD": 3,
        "SYS_POLL_WAIT": 4,

        # Device + SHM (microkernel)
        "SYS_MAP_DEVICE": 3,
        "SYS_IRQ_REGISTER": 1,
        "SYS_IRQ_WAIT": 2,
        "SYS_IRQ_ACK": 1,
        "SYS_IRQ_UNREGISTER": 1,
        "SYS_DMA_ALLOC": 2,
        "SYS_DMA_FREE": 1,
        "SYS_VIRT_TO_PHYS": 1,
        "SYS_DEVICE_ENUM": 2,
        "SYS_SHM_CREATE": 1,
        "SYS_SHM_MAP": 1,
        "SYS_SHM_UNMAP": 1,
    }

    by_const = {e["syscall_const"]: e for e in entries}

    print("\n[Microkernel-critical ABI]")
    for const, expected_argc in required.items():
        entry = by_const.get(const)
        if entry is None:
            print(f"  [FAIL] Missing syscall entry for {const}")
            failed += 1
            continue
        if entry["argcount"] != expected_argc:
            print(
                f"  [FAIL] {const} argcount is {entry['argcount']} (expected {expected_argc})"
            )
            failed += 1
            continue
        passed += 1

    if failed == 0:
        print(f"  [PASS] All {passed} microkernel-critical syscall argcounts match")

    return passed, failed

def main():
    print("=== Syscall Table Invariant Tests ===")

    if not TABLE_CPP.exists():
        print(f"ERROR: Cannot find {TABLE_CPP}")
        return 1

    entries = parse_syscall_table(TABLE_CPP)
    if entries is None:
        return 1

    print(f"Found {len(entries)} syscall entries")

    total_passed = 0
    total_failed = 0

    p, f = test_unique_numbers(entries)
    total_passed += p
    total_failed += f

    p, f = test_no_null_handlers(entries)
    total_passed += p
    total_failed += f

    p, f = test_nonempty_names(entries)
    total_passed += p
    total_failed += f

    p, f = test_argcount_valid(entries)
    total_passed += p
    total_failed += f

    p, f = test_microkernel_critical_abi(entries)
    total_passed += p
    total_failed += f

    print(f"\n--- Results: {total_passed} passed, {total_failed} failed ---")

    if total_failed == 0:
        print("OK")
        return 0
    else:
        print("FAILED")
        return 1

if __name__ == '__main__':
    sys.exit(main())
