# Phase 7: Signing, Notarization, and Release Automation

## Goal

Keep signing and release distribution explicit and separate from core package generation.

Unsigned artifacts should already be structurally valid and installable in developer scenarios. This phase adds the release-quality trust and publishing steps on top.

## Why This Is Separate

Core builder responsibilities:
- generate valid package bytes
- embed correct metadata
- pass native verification

Release responsibilities:
- sign the artifact
- notarize where required
- publish checksums and signatures
- attach artifacts to releases
- preserve the unsigned-artifact provenance needed to reproduce or re-verify a release later

Blending those concerns into the core writers makes local development and CI harder, and it increases platform-specific branching inside the packaging code.

## Reuse First

Use existing outputs from earlier phases:
- `viper install-package`
- native verification from `PkgVerify`
- staged install smoke artifacts

Use host tools only in release lanes:
- Windows: `signtool`
- macOS: `productsign`, `notarytool`, `stapler`
- Linux RPM: `rpm --addsign`
- Linux general: checksum/signature publishing

## Platform Plans

### Windows

Recommended release steps:
1. build unsigned installer
2. sign with Authenticode
3. verify signature in CI

Suggested checks:
- `signtool verify /pa`
- SmartScreen-oriented metadata sanity if release infra supports it

### macOS

Recommended release steps:
1. build unsigned `.pkg`
2. sign with `productsign`
3. submit for notarization with `notarytool`
4. staple if applicable
5. verify with `pkgutil --check-signature`

Keep the unsigned `.pkg` builder usable without an Apple signing identity.

### Linux

### RPM
- sign with GPG in release CI
- verify with `rpm -K`

### `.deb`
- publishing a detached checksum/signature is sufficient initially
- if later desired, add repository-signing support separately from the package writer

## Release metadata

Every release should publish:
- versioned artifact filenames
- SHA-256 checksums
- unsigned-artifact SHA-256 checksums when signing changes the final bytes
- signature status
- target platform/arch
- short install instructions
- any required trust/install notes such as Gatekeeper/notarization status on macOS or Authenticode verification guidance on Windows

## Automation recommendations

Add a release job that:
1. builds staged install trees
2. runs `viper install-package`
3. runs structural verification
4. records unsigned artifact hashes and build metadata
5. signs artifacts per platform
6. runs signed-artifact verification
7. publishes artifacts + checksums

## Exit Criteria

This phase is done when:
- signing does not leak back into the core format writers
- release CI can sign and verify all supported installer artifacts
- published releases include reproducible metadata and checksums
