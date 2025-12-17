---
status: active
audience: public
last-updated: 2025-11-20
---

# Lifetime Model

Viper BASIC uses deterministic lifetimes with reference counting and explicit disposal. This section summarizes the
model and best practices.

## Reference Counting (RC)

- Objects are heap-allocated with a shared header that tracks a reference count.
- Passing objects to procedures follows a borrow/return pattern; ownership is not implicitly transferred.
- When the last reference is released, storage is reclaimed; if a destructor exists, it runs before free.

## Explicit Disposal

- Use `DISPOSE obj` to deterministically destroy an instance when it goes out of scope or when you finish with it
  earlier.
- Disposing `NULL` is a no-op.
- Multiple disposals of the same object are a programming error; debug builds trap to aid diagnosis.

## Destructors

- Each class may declare one destructor (`DESTRUCTOR ... END DESTRUCTOR`) that runs before storage is freed.
- Destructors chain from most-derived to base, automatically.
- Avoid side effects that rely on object fields after disposal; fields may be released before the destructor returns.

## Static Destructors

- A `STATIC DESTRUCTOR` runs once at program shutdown. Use this for module-level cleanup (e.g., releasing global
  resources).
- Shutdown order is class declaration order within the module.

## Guidance

- Prefer `DISPOSE` when you want timely cleanup of scarce resources (file handles, OS objects) and ordering relative to
  surrounding code matters.
- Use static destructors for module-scoped resources that must outlive all user code in the module.
- Do not dispose `ME` inside a destructor; this is diagnosed as an error to prevent recursion.

