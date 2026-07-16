# Cross-Platform Installer Review Report

Date: 2026-07-10  
Status: Implemented; Windows and Linux native lifecycle execution remains a
native-host handoff

## Scope and conclusion

This review covered toolchain staging, package generation, artifact
verification, signing, installer user experience, upgrades, rollback,
uninstall, release automation, and documentation for Windows, macOS, and Linux.
It examined `viper install-package`, the package builders and runtime stubs,
installer wrapper/signing scripts, CTest lifecycle smokes, and release workflow
definitions.

The review produced 54 concrete recommendations. All 54 are implemented in the
repository. macOS generation and structural behavior were exercised locally;
Windows and Linux code paths have unit/structural coverage and native workflows,
but their final install/upgrade/uninstall execution must run on the disposable
native hosts described in the release guide. The former toolchain `appimage`
compatibility target and `.AppImage` inference alias were removed completely;
standalone application AppImages remain a separate supported application
format.

## Shared generation and release process

| # | Priority | Finding and recommendation | Implemented result |
|---:|:---:|---|---|
| 1 | Critical | Do not package an unvalidated directory or infer its platform from the build host. | The staged manifest validates required tools, platform, architecture, paths, file modes, optional components, and object headers before generation. |
| 2 | High | Reject symlinks that escape the staged tree and preserve only safe internal links. | Manifest gathering canonicalizes the stage, rejects escape and directory-dereference cases, and preserves validated internal link targets. |
| 3 | High | Remove ambiguous `-o` behavior for extensionless single artifacts. | `--output-file` and `--output-dir` are explicit; single-target `-o` is a file unless it names an existing directory. |
| 4 | High | Give every artifact an independently verifiable digest. | Every successful artifact receives an adjacent lowercase SHA-256 sidecar. |
| 5 | High | Make multi-artifact output consumable by release automation. | Directory output receives `SHA256SUMS` plus a schema-versioned JSON inventory with format, OS, architecture, version, size, hash, verification, trust, and epoch fields. |
| 6 | High | Prevent metadata truncation and partially written checksum files. | Sidecars and inventories use atomic same-directory replacement after artifact verification. |
| 7 | Critical | Prevent concurrent release writers or accidental overwrite of a published artifact set. | Release mode takes a directory lock and refuses existing artifacts, sidecars, manifests, or consolidated checksums. |
| 8 | High | Clean up incomplete trusted releases after signing, notarization, verification, or metadata failure. | Release artifacts and generated metadata are tracked and removed until the complete set is committed. |
| 9 | Critical | Make “release” a policy, not a naming convention. | `--release` rejects verification bypasses and debug Windows payloads, then requires native signing/notarization credentials for the selected platform. |
| 10 | High | Require reproducible release metadata. | Release generation requires a numeric `SOURCE_DATE_EPOCH`; deterministic archive metadata and RPM build-time macros consume it. |
| 11 | High | Let consumers require a checksum rather than merely use one when present. | `--verify-only ... --require-checksum` rejects a missing, malformed, misnamed, or mismatched sidecar. |
| 12 | Medium | Reject CLI combinations whose security meaning is misleading. | `--release --verify-only` and generation-time `--require-checksum` now fail with actionable diagnostics. |
| 13 | High | Verify package contents, not just magic bytes. | Verification checks expected payload entries, required metadata, archive bounds, checksums, platform layout, and missing required files. |
| 14 | High | Prove upgrades and removals rather than only package construction. | Native lifecycle smokes install a stale-file baseline, upgrade, preserve an unrelated sentinel, exercise the installed compiler, and uninstall. |
| 15 | High | Recreate a documented, reviewable release pipeline on every native OS. | Manual Windows, macOS, and Linux workflows build with canonical scripts and upload only after enabled structure, trust, checksum, and lifecycle gates pass. |

## Windows installer

| # | Priority | Finding and recommendation | Implemented result |
|---:|:---:|---|---|
| 16 | Critical | ARM64 generation failed when the full GPL text exceeded the bootstrap's embedded-text limit. | ARM64 displays a bounded setup notice while retaining the complete license inside the installed payload. |
| 17 | High | ARM64 should not depend on an x64-emulated setup path. | The installer and detached self-cleaning uninstaller use native ARM64 PE bootstraps. |
| 18 | Critical | x64 and ARM64 had divergent file-install semantics. | Both bootstraps invoke one gzip-packed transactional PowerShell backend. |
| 19 | Critical | Encoded bootstrap commands could exceed Windows' real process command-line limit. | Bootstrap data is compressed, inputs move through environment variables, and generation enforces the 32,767-character boundary. |
| 20 | Critical | A signed executable's appended payload could be read or hashed incorrectly. | The backend reads exact overlay ranges from the signed installer and streams SHA-256 verification before extraction. |
| 21 | Critical | A ZIP that differed from its manifest could install silently. | Install preflight rejects missing files, unexpected files, duplicate/unsafe paths, and manifest hash mismatch. |
| 22 | Critical | Junctions and reparse points could redirect writes outside the selected destination. | Preflight rejects reparse-point traversal throughout the destination and transaction paths. |
| 23 | Critical | Upgrade could overwrite a file not owned by Viper. | Unowned collisions abort before mutation; ownership is derived from the installed manifest and recorded metadata. |
| 24 | Critical | A failed install or metadata update could leave a mixed old/new tree. | Same-volume staging, backups, a journal, explicit commit phases, and rollback cover files, PATH, and registry mutations. |
| 25 | High | A process interruption could strand a transaction that later setup ignored. | Either architecture's installer or uninstaller detects and recovers an interrupted transaction before continuing. |
| 26 | High | Upgrades accumulated files removed from newer releases. | Baseline-to-current validation proves stale owned files are deleted while unrelated files survive. |
| 27 | Critical | Uninstall removed a textual PATH entry without proving ownership. | Setup records whether it added the exact normalized token; uninstall removes only that owned token. |
| 28 | High | Registry and file-association rollback was incomplete and association value type was wrong. | Affected registry trees are snapshotted/restored, file associations use `REG_NONE` where required, and install date metadata is recorded. |
| 29 | High | Shortcut cleanup could claim an unrelated shortcut with the same name. | Setup records shortcut ownership explicitly, with target inspection only as a legacy fallback. |
| 30 | High | Silent automation, cancellation, and GUI failures returned misleading results. | `/quiet`, `/silent`, and `/norestart` are forwarded consistently; cancellation returns 1602; fatal wizard failures abort installation. |
| 31 | Medium | The wizard did not clearly show license acceptance, effective scope, or destination. | The compact native wizard displays those values and reports honest completion or failure. |
| 32 | Medium | The setup executable looked and behaved like a legacy application on modern displays. | Its manifest enables modern common controls, DPI awareness, and long-path awareness. |
| 33 | Critical | Timestamp transport and post-sign trust checks were too weak. | Timestamp URLs must be HTTPS and signing is followed by `signtool verify /pa /all /tw /v`. |
| 34 | High | PFX passwords could be exposed casually in process arguments. | Certificate-store thumbprints are preferred; PFX argv use requires explicit acknowledgement, and the helper validates and prioritizes a 40-hex thumbprint. |
| 35 | Medium | Batch wrapper inspection could reinterpret metacharacters from extra CMake arguments. | The wrapper queries the environment without expanding its value through `echo` before adding the ViperIDE option. |

## macOS package and disk image

| # | Priority | Finding and recommendation | Implemented result |
|---:|:---:|---|---|
| 36 | Critical | Signing only the outer product package left nested executable code untrusted. | Release generation requires a Developer ID Application identity, signs every Mach-O/helper before packaging, and verifies each signature. |
| 37 | Critical | A signed package was not enough proof of distributable trust. | Product signing, notarization, stapling validation, and Gatekeeper install assessment are mandatory in release mode. |
| 38 | High | The Installer experience lacked the normal explanatory and legal panes. | The distribution contains welcome, license, read-me, destination, and conclusion panes. |
| 39 | Medium | Installer visuals were absent or unreadable across appearance modes. | Generated 620×418 light and dark backgrounds ship by default, with an explicit custom background override. |
| 40 | High | Installer eligibility metadata did not fully describe the payload. | The distribution declares root-volume policy, install domains, supported host architectures, and an architecture-based or explicit minimum macOS version. |
| 41 | Critical | Malformed fat Mach-O headers could mislabel or read slices outside the file. | Fat32/fat64 parsing bounds-checks slices, rejects mismatched architecture, and never treats a single slice as universal. |
| 42 | High | The file-handler app could advertise support below its real deployment floor. | `LSMinimumSystemVersion` is written into the helper application's `Info.plist`. |
| 43 | Medium | The DMG lacked a polished drag-to-install presentation. | It receives generated artwork, a volume icon, positioned package icon, and Applications link. |
| 44 | High | Finder/AppleScript input could break on names and hang a headless release job. | Volume/item names are validated and escaped, Finder styling is bounded to 15 seconds, and headless failure is non-fatal. |
| 45 | Critical | A writable or colliding disk image could pass creation without proving its final form. | Input/output collision is rejected; the image is converted to read-only, remounted, structurally verified, and detached before acceptance. |
| 46 | Critical | The DMG itself was not subjected to the same release trust outcome as the PKG. | It is separately notarized, optionally stapled and validated, then assessed by Gatekeeper as an openable artifact. |
| 47 | High | Upgrade/uninstall behavior lacked stale-file and unrelated-file proof. | Manifest-driven preinstall/uninstall scripts remove only owned paths, and the native smoke now covers baseline upgrade, preservation, receipt removal, and cleanup. |

## Linux packages, portable installer, and bundle

| # | Priority | Finding and recommendation | Implemented result |
|---:|:---:|---|---|
| 48 | High | The self-extractor was called AppImage despite not implementing the AppImage filesystem/interface contract. | The public toolchain target is only `linux-bundle`, the suffix is `.run`, and the compatibility target/suffix inference aliases are rejected. |
| 49 | Critical | Reusing a mutable extraction directory made tampering and cross-version contamination possible. | The bundle uses a private XDG cache keyed by the complete embedded payload hash. |
| 50 | Critical | Cache symlinks, wrong ownership, permissive modes, and partial extraction could redirect execution. | The runtime rejects symlink components and unsafe ownership, forces mode 0700, verifies the payload, extracts to a private stage, and atomically publishes it. |
| 51 | High | Concurrent first launches could race or consume an abandoned lock forever. | A per-payload directory lock serializes extraction, waits for a live owner, and recovers missing/stale owner records. |
| 52 | Medium | Bundle status output was noisy in automation and inconsistent in terminals. | Quiet mode, `NO_COLOR`, TTY-aware color, extraction/help/cache commands, and clear failures are supported without toolchain AppImage aliases. |
| 53 | High | Debian/RPM hard dependencies included build and desktop-cache tools not required to execute Viper. | Runtime libraries remain hard dependencies; CMake, compilers, make, cache helpers, and man-db tools are recommendations. |
| 54 | High | Native package trust, metadata, and lifecycle validation were incomplete. | Packages include contact/homepage/license/copyright/desktop/MIME data, verify `dpkg-sig`/RPM signatures and RPM structure, and exercise baseline upgrade, installed CMake/native codegen, removal, and unrelated-file preservation. The portable tar installer separately provides conflict preflight, same-filesystem journal/rollback, stale cleanup, recorded prefix, dry-run/force/quiet controls, and transactional uninstall. |

## Validation status

The implementation was validated on macOS with warnings-as-errors compilation,
the complete packaging unit executable, CLI regression tests, real tarball
install/upgrade/rollback/uninstall smokes, application package signing smoke,
real toolchain PKG generation and Installer choice evaluation, DMG generation,
platform-policy lint, runtime-surface audit, cross-platform smoke tests, and a
staged CMake install. The locally generated debug PKG was 193 MiB and its JSON
inventory and SHA-256 sidecar were independently checked.

The privileged macOS filesystem lifecycle was not run locally because
non-interactive sudo is unavailable. The release workflow runs that phase on a
disposable runner. Windows PowerShell/batch execution and Linux package-manager
installation cannot be executed on the macOS host; their exact native commands
are in [Installer and Package Release Guide](installer-release.md#native-validation-handoff).

One repository-wide canonical test remains red outside this scope:
`g3d_game3d_showcase` traps while loading a model asset in the concurrently
modified Graphics3D work. Installer/package tests, policy lint, runtime-surface
audit, cross-platform smoke tests, and staged installation are green.
