---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0060: SceneAsset Load Result APIs

Date: 2026-07-02
Status: Accepted

## Context

`Zanna.Graphics3D.SceneAsset` loaders historically returned `null` for routine
content failures and required callers to read
`AssetDiagnostics3D.LastLoadError` / `LastLoadErrorCode` for the reason. That
side-channel is useful for compatibility diagnostics and warning inspection, but
it is easy to overwrite with a later load attempt and it separates the failing
value from the failure message.

Scene asset loading is a production-facing workflow. Missing files, unsupported
formats, malformed assets, and invalid animation indexes should be explicit
values that compose with the runtime's existing `Result` APIs.

## Decision

Add Result-returning variants for the top-level scene asset load entry points:

- `SceneAsset.LoadResult(path) -> Result[SceneAsset]`
- `SceneAsset.LoadAssetResult(path) -> Result[SceneAsset]`
- `SceneAsset.LoadAnimationResult(path, index) -> Result[Animation3D]`
- `SceneAsset.LoadAnimationAssetResult(path, index) -> Result[Animation3D]`
- `SceneAsset.LoadNodeAnimationResult(path, index) -> Result[NodeAnimation3D]`
- `SceneAsset.LoadNodeAnimationAssetResult(path, index) -> Result[NodeAnimation3D]`

Each API returns `Ok(value)` on success and `Err(message)` for routine load
failures. Existing `Load*` functions and `AssetDiagnostics3D.LastLoadError*`
remain available for compatibility. Runtime API metadata marks those legacy rows
with migration targets to the matching Result APIs.

Warnings remain on `AssetDiagnostics3D` because partial degradation is attached
to the most recent outer asset load and does not make the returned value fail.

## Consequences

- New 3D code can handle loader failures without reading thread-local
  diagnostics after every call.
- Existing examples and applications that check `null` or inspect
  `AssetDiagnostics3D` keep working.
- API dumps, docs, and agent tooling now recommend the same `Result` failure
  vocabulary used by networking, crypto, data parsing, PTY, and scene document
  loading.
