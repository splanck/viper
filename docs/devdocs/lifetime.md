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

## Cyclic References

Reference counting cannot automatically detect cyclic references (objects that reference each other, forming a cycle).
When cycles exist, the objects in the cycle will never have their reference count reach zero, causing a memory leak.

### Common Cycle Patterns

```basic
' Simple cycle: A -> B -> A
DIM a AS MyClass = NEW MyClass()
DIM b AS MyClass = NEW MyClass()
a.Other = b
b.Other = a
' Neither a nor b can be freed automatically
```

```basic
' Self-reference: A -> A
DIM node AS Node = NEW Node()
node.Next = node
' node can never be freed automatically
```

### Breaking Cycles

Cycles must be broken manually before the objects go out of scope:

```basic
' Break the cycle before leaving scope
a.Other = NULL
' Now b can be freed when it goes out of scope
' And a can be freed after that
```

For complex data structures like doubly-linked lists or graphs, use `DISPOSE` with explicit
cleanup that clears cyclic references:

```basic
' Clean up a doubly-linked list
DIM current AS Node = head
WHILE current <> NULL
    DIM next AS Node = current.Next
    current.Prev = NULL
    current.Next = NULL
    DISPOSE current
    current = next
WEND
```

### Design Patterns to Avoid Cycles

1. **Use weak references**: Store parent references as unmanaged pointers when possible.
2. **Use hierarchical ownership**: One object owns another; the child does not reference the parent.
3. **Break cycles in destructors**: Clear back-references in your destructor before the object is freed.
4. **Use explicit cleanup methods**: Provide a `Close()` or `Release()` method that breaks cycles.

### Detection

Viper does not include automatic cycle detection. If you suspect a memory leak from cycles:

1. Review object relationships for potential cycles
2. Use debug builds with refcount tracing (`-DVIPER_RC_DEBUG=1`)
3. Profile memory usage over time to identify growing allocations

## Guidance

- Prefer `DISPOSE` when you want timely cleanup of scarce resources (file handles, OS objects) and ordering relative to
  surrounding code matters.
- Use static destructors for module-scoped resources that must outlive all user code in the module.
- Do not dispose `ME` inside a destructor; this is diagnosed as an error to prevent recursion.
- Be mindful of cyclic references; break them explicitly before objects go out of scope.

