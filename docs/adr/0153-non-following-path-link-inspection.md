---
status: active
audience: contributors
last-verified: 2026-07-22
---

# ADR 0153: Reject Linked Descendants at Explorer Boundaries

Date: 2026-07-22

Status: Accepted

## Context

Zanna Studio constrains Explorer mutations to lexical descendants of an opened
workspace root. The runtime file index does not recurse through directory
symlinks, but it can publish a linked directory as a visible node because the
link target is a directory. New File, New Folder, drag-move, duplicate, and
project-trash flows could then follow that node or another linked ancestor and
write outside the workspace displayed to the user.

Lexical normalization cannot detect this condition, and rejecting links based
on an operating-system name is insufficient: Windows directory junctions and
other reparse points have the same boundary concern.

## Decision

Expose `Zanna.IO.Path.IsLink(path)` through the runtime C entry point
`rt_path_is_link`. The predicate inspects the final component without following
it. POSIX uses `lstat` and Windows checks `FILE_ATTRIBUTE_REPARSE_POINT`, which
also covers directory junctions. Invalid, missing, inaccessible, and ordinary
paths return false without trapping.

Before an Explorer mutation, Zanna Studio walks the candidate's ancestors from
the mutation parent back to its owning root and rejects any linked component.
Creation and drag-move include the target directory itself. Rename, duplicate,
and trash allow a link as the source leaf so Studio may move the link itself,
but reject linked source parents. Project trash is checked both before and
after its directory is created.

The opened root itself is trusted and is not tested as a descendant component.
This preserves the intentional workflow of opening a workspace through a
symlink while preventing nested links from silently widening its mutation
boundary.

Explorer directory loading and workspace-watcher discovery apply the same
ancestor policy. A linked descendant remains visible in the tree so its
presence is not hidden, but it has no expansion affordance and cannot queue a
directory-page job. Watcher discovery skips the link rather than attaching an
OS watcher to its external target. Click and refresh-restore paths repeat the
check defensively so stale widget state cannot reopen the boundary.

## Consequences

- Explorer create, move, duplicate, rename, and trash flows cannot knowingly
  follow a nested symlink or Windows reparse point outside an opened root.
- Linked descendant directories cannot reveal external target content or add
  external OS watchers through the project tree.
- A workspace intentionally opened through a linked root remains usable.
- Link inspection is reusable by other tools and does not resolve or expose a
  canonical target path.
- The runtime C header, registry, class catalog, authored/generated docs, and
  runtime/Studio regressions must evolve together.
