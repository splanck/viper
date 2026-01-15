# Capabilities and Kernel Objects

ViperDOS is designed around a capability-style security model: “a handle is authority”. In practice, the kernel currently
implements:

- a per-Viper capability table (`cap::Table`)
- rights bits that constrain operations on a handle
- object kind tags (to prevent type confusion)
- optional reference-counted kernel objects (`kobj::Object` and derived types)

This page explains what exists today, how it’s used in IPC and Assigns, and where it’s headed.

## The capability table (`cap::Table`)

The capability table implementation is in `kernel/cap/table.*`. Each Viper gets its own instance during
`viper::create()`.

### Core structure

`cap::Table` is an array of `cap::Entry`:

- `Entry::object`: pointer to the underlying kernel object (or free-list link while unused)
- `Entry::kind`: what type of object this handle refers to (Channel, Directory, File, Blob, …)
- `Entry::rights`: bitmask of permissions
- `Entry::generation`: 8-bit generation counter for stale-handle detection

### Why generation counters matter

Handles are encoded as `(index, generation)`:

- when an entry is removed, its generation is incremented
- old handles still contain the old generation and no longer resolve

This prevents a classic bug class: “use-after-free handle now refers to a different object that happened to reuse the
same slot”.

Key files:

- `kernel/cap/table.hpp`
- `kernel/cap/table.cpp`
- `kernel/cap/handle.hpp`
- `kernel/cap/rights.hpp`

## Rights: what can you do with a handle?

Rights are bit flags that constrain actions like:

- read vs write
- transfer (send across IPC)
- derive (create a narrower capability from a broader one)

Two important patterns show up in code today:

- **Derivation**: `Table::derive(h, new_rights)` requires the original to have `CAP_DERIVE`, and the derived rights are
  intersected with the original.
- **Handle transfer**: channel messages can carry handles; the kernel checks `CAP_TRANSFER` before moving a capability
  from sender to receiver.

Key files:

- `kernel/cap/rights.hpp`
- `kernel/cap/rights.cpp` (helpers like `has_rights`)

## Kernel objects: `kobj`

Some kernel subsystems represent resources as heap-allocated objects with reference counts.

The shared base is `kobj::Object` in `kernel/kobj/object.hpp`:

- intrusive `ref_count_`
- a `cap::Kind` tag stored inside the object
- helpers to safely downcast without C++ RTTI (`as<T>()`)

Derived objects include things like:

- `kobj::Blob`
- `kobj::Channel`
- `kobj::FileObject`
- `kobj::DirObject`

These objects are what capability table entries often point to, especially in subsystems that need lifetime management
across multiple handles.

Key files:

- `kernel/kobj/object.hpp`
- `kernel/kobj/blob.*`
- `kernel/kobj/channel.*`
- `kernel/kobj/file.*`
- `kernel/kobj/dir.*`

## How capabilities show up in “real work”

### IPC handle passing

The IPC channel subsystem demonstrates the capability model clearly:

1. Sender provides a list of handles to transfer.
2. The kernel resolves each handle in the sender’s cap table.
3. Rights checks ensure the sender has `CAP_TRANSFER`.
4. The kernel removes those handles from the sender’s table and stores the underlying object/kind/rights in the channel
   message.
5. When the receiver dequeues the message, those objects are inserted into the receiver’s cap table, producing new
   receiver-local handle values.

See `kernel/ipc/channel.cpp` and `kernel/syscall/dispatch.cpp` (channel syscalls) for the end-to-end path.

### Files and directories via Assigns

The Assign system (`kernel/assign/assign.cpp`) resolves strings like `SYS:foo/bar` into:

- a `kobj::DirObject` handle (for directory targets), or
- a `kobj::FileObject` handle (for file targets),

and inserts those into the caller’s capability table.

This is an example of how “names” are reduced to “capabilities”: after resolution, code should prefer handles over
paths.

## Capability revocation

Capability revocation is supported through parent tracking:

- Each derived capability stores a `parent_index` pointing to the capability it was derived from
- When `Table::revoke(h)` is called, it recursively invalidates all capabilities derived from `h`
- This ensures that revoking a capability also revokes all narrower capabilities created from it

```cpp
// Example: Revoke a file handle and all derived read-only handles
cap::Table *table = viper::current_cap_table();
table->revoke(file_handle);  // Also revokes any read-only derived handles
```

Key files:

- `kernel/cap/table.cpp`: `revoke()` implementation with recursive traversal

## Current limitations and next steps

- Not all kernel subsystems are fully "capability-first" yet; some paths still use global tables or raw IDs (especially
  legacy channel APIs).
- Reference management between cap entries and `kobj::Object` lifetimes is still evolving; expect this area to change as
  user space grows.

