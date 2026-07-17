# Runtime API Release Checklist

Use this checklist before declaring the runtime API stable.

## Inventory

- [ ] Fresh `zanna --dump-runtime-api` snapshot is checked in or archived.
- [ ] `rtgen --audit --summary-only src/il/runtime/runtime.def` passes.
- [ ] API diff from the previous snapshot is reviewed.
- [ ] ADR coverage exists for required ABI/runtime/catalog changes.

## Naming

- [ ] No duplicate stable public aliases.
- [ ] No non-PascalCase public leaves except accessor internals.
- [ ] No compressed abbreviation is canonical unless waived.
- [ ] Acronym casing follows the adopted policy.
- [ ] Examples and docs use canonical names.

## Types And Ownership

- [ ] No public `ptr`.
- [ ] Concrete handles use `obj<T>`.
- [ ] Bare `obj` rows are justified as dynamic.
- [ ] Stable object/string returns declare ownership.
- [ ] Stable properties declare ownership.
- [ ] Nullability is explicit through `Option`, `Result`, or metadata.

## Failure

- [ ] No stable side-channel error APIs.
- [ ] No stable sentinel APIs without waiver.
- [ ] Parse/load/open/connect/decrypt operations have canonical `Result`
      forms.
- [ ] Normal absence returns `Option`.
- [ ] Trap APIs are documented as strict or programmer-error behavior.
- [ ] Runtime trap mutation is unsafe/internal, not ordinary app API.

## Capabilities And Stubs

- [ ] Every capability-dependent API declares capabilities.
- [ ] Current binary availability is visible to tools.
- [ ] Disabled stable APIs do not silently return fake success values.
- [ ] Constructors do not silently return `NULL`.
- [ ] Probe APIs exist where users need to branch.
- [ ] Strict and disabled-build behavior is tested.

## Classes

- [ ] Class kind is declared.
- [ ] Thin classes are intentional handles/modules or completed/hidden.
- [ ] Constructor methods are explicit or marked generated.
- [ ] Same class/name/arity method collisions are audited.
- [ ] Large classes have grouped APIs, sub-objects, or waivers.
- [ ] Property/setter-method pairs are resolved or waived.

## Docs And Tooling

- [ ] Every stable row has a resolving docs anchor.
- [ ] Generated reference docs exist.
- [ ] Conceptual docs link to generated reference.
- [ ] Migration guide lists legacy names and replacements.
- [ ] Capability matrix exists.
- [ ] Error/result guide exists.
- [ ] Ownership/lifetime guide exists.
- [ ] Unsafe API guide exists.
- [ ] API migration diagnostics or scripts exist.

## Final Gate

- [ ] Full build script passes locally.
- [ ] Targeted runtime API audit suite passes.
- [ ] Cross-platform policy lint passes for touched platform-sensitive work.
- [ ] No new dependency has been introduced.
- [ ] Release notes identify any intentional breaking changes.

