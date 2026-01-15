# Assigns (Logical Names Like `SYS:` and `C:`)

ViperDOS v0.2.0 introduces an Assign system: logical names like `SYS:` map to directories (and potentially
chains of directories) and become the basis for path resolution outside of Unix-style `/` roots.

This page explains what Assigns are, how they’re stored, and how resolution turns names into capabilities.

## The core idea

An Assign is a mapping:

`NAME:` → “directory inode in ViperFS”

Examples:

- `SYS:` → root directory inode
- `D0:` → boot disk root directory inode

Assigns are intended to be used in user-facing command lines and scripts; they also become a convenient way to establish
“known roots” without exposing global filesystem authority everywhere.

Key files:

- `kernel/assign/assign.hpp`
- `kernel/assign/assign.cpp`

## Data model: a fixed-size table with optional chains

The implementation uses:

- `AssignEntry assign_table[MAX_ASSIGNS]`
- case-insensitive name matching
- optional multi-directory assigns, represented as a linked chain of entries with the same name

There are flags to mark assigns as:

- system (read-only)
- multi (chain)

This is intentionally simple for bring-up and keeps Assign behavior predictable.

## Initialization narrative

At boot (after ViperFS and VFS are initialized), `viper::assign::init()`:

1. clears the table
2. installs “system assigns”:
    - `SYS:` (points at ViperFS root inode)
    - `D0:` (currently also points at ViperFS root inode)

The intention is that later:

- additional disks (`D1:` etc.) can be added
- startup scripts can create user-modifiable assigns like `C:`, `S:`, `HOME:`, `WORK:`

## Resolution narrative: from string to handle

The key operation is `viper::assign::resolve_path("SYS:foo/bar")`.

Resolution steps (conceptually):

1. Parse the assign name (`SYS`) and locate the assign entry.
2. Use the assign’s `dir_inode` as the base directory inode.
3. Walk the remaining path components using ViperFS directory operations.
4. When the target is found:
    - create a `kobj::DirObject` or `kobj::FileObject` representing the result
    - insert it into the caller’s capability table
    - return the new handle

This is an important design choice: Assign resolution produces **capabilities** (handles) rather than just “inode
numbers”. The handle becomes the authority-bearing reference that syscalls can operate on.

Key related files:

- `kernel/kobj/dir.*`
- `kernel/kobj/file.*`
- `kernel/fs/viperfs/*`
- `kernel/cap/*`

## Syscall surface

Assigns are exposed via syscalls in `kernel/syscall/dispatch.cpp` (set/unset/resolve style calls). This is how user
space will create and use assigns as part of the v0.2.0 shell and command model.

## Current limitations and next steps

- Only system assigns (`SYS`, `D0`) are created automatically today.
- Multi-directory assigns exist structurally, but higher-level “search path” semantics are still evolving.
- Resolution is currently tied to ViperFS inodes; future versions may generalize this across mount points or other
  filesystem types.

