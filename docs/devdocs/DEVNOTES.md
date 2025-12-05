# Developer Notes

## OOP Milestone C

This section summarizes the implementation details for nominal interfaces,
interface dispatch, and RTTI, focusing on invariants and integration points
that maintainers and integrators need to know.

- Interface slots:
  - Slot indices are assigned in interface declaration order and are immutable.
  - Each class that implements an interface binds a separate itable for that
    interface; multiple interfaces result in multiple itables per class.

- Itable storage:
  - `rt_bind_interface(type_id, iface_id, void **itable_slots)` records the
    (class, interface) binding and takes ownership of a pointer to the caller-
    managed slot array. The array’s lifetime must cover the class lifetime.
  - Lookup uses `rt_itable_lookup(obj, iface_id)` to produce the itable, then
    loads the slot to obtain a function pointer for dispatch.

- Type and interface ids:
  - `type_id` and `iface_id` are process-local identifiers assigned at module
    load time by the runtime registry. They are fixed for the life of the
    process but not across processes or runs.
  - Duplicate registration is idempotent; re-registering a known entity is a no-op.

- Lowering choice (C8A vs C8B):
  - We chose the C8B “indirect” path: interface calls compute a callee pointer
    via itable lookup and then emit IL `call.indirect`. This keeps VM and native
    behavior identical and reduces ad‑hoc bridges.
  - The C8A “intrinsic” view is limited to the helper used to materialize the
    callee pointer; the actual call remains indirect at the IL level.

- RTTI (`IS`/`AS`):
  - `IS` for classes uses `rt_typeid_of` + `rt_type_is_a` (dynamic type equals
    the class or derives from it). `IS` for interfaces uses `rt_type_implements`.
  - `AS` is a safe cast: returns the original object pointer on success, or
    returns NULL on failure. NULL is represented as a null pointer in IL/VM.

- Diagnostics (new):
  - `E_IFACE_DUP_METHOD`: duplicate method name in an interface declaration.
  - `E_CLASS_MISSES_IFACE_METHOD`: class does not implement a required method
    from an interface it claims to implement.

- Public APIs:
  - Exposed under `include/viper/runtime/rt_oop.h`:
    `rt_register_interface`, `rt_bind_interface`, `rt_typeid_of`,
    `rt_type_is_a`, `rt_type_implements`, `rt_cast_as`.

See also: `docs/oop.md` for a higher‑level overview and `docs/grammar.md` for
language syntax covering `INTERFACE …`, `IMPLEMENTS`, and `IS`/`AS`.
