# Installer Hardening — Windows Workstream

**Requires Windows (x64 + ARM64) to complete and verify.** Parent plan:
`~/.claude/plans/i-need-to-harden-transient-wren.md`. Sibling files: `macos.md`, `linux.md`.

## Context

The Windows toolchain installer is a hand-built PE32+ self-extracting binary whose install logic is
emitted as **raw x86-64 machine code** by `InstallerStubGen` (`src/tools/common/packaging/
InstallerStub.cpp`, `InstallerStubGen.{hpp,cpp}`, `WindowsPackageBuilder.cpp`, `PEBuilder.*`).
Today the **entire UI is three `MessageBoxW` calls** (welcome OK/Cancel, success, error) — hence
"bland." The chosen direction is a **full custom GUI wizard**, plus a **native ARM64 stub** (today
ARM64 payloads reuse the x86-64 bootstrap under WoA emulation — `InstallerStub.hpp:22-24`).

**Why a platform file:** the C++ that *emits* these bytes can be authored and structurally verified
on Mac (`PkgVerify` + `.text` disassembly), but whether the wizard renders, the license gate works,
progress advances, PATH/shortcuts/uninstall behave, the ARM64 stub runs natively, and Authenticode/
SmartScreen are happy — can only be proven on Windows.

## Scope (in / out)

**In:** WS-A (full wizard), WS-B (native ARM64 stub), W3 (ARP/VERSIONINFO metadata), W4 (show
license), W5 (multi-res icon), W6 (Authenticode flow). **Out:** everything in `macos.md`/`linux.md`.
Depends on **X1 config plumbing** (done in the macOS session) for license text + help URL.

## Tasks

### WS-A — Full custom wizard (decomposed; each step keeps the emitted PE structurally valid)
1. **Emitter primitives.** Add `leaCodeLabel(dst,labelId)` (`lea reg,[rip+disp32]` → code-label
   Rel32 fixup; reuse the existing `Rel32` fixup kind) and any missing helpers (`call reg`, extra
   `movMemReg`) to define/pass callback pointers. Unit-test encodings (host-runnable on Mac).
2. **New IAT imports.** Extend the stub import list (`InstallerStub.cpp:203-216,242-253`):
   `comctl32` (`InitCommonControlsEx`, progress via `SendMessageW`), `user32`
   (`DialogBoxIndirectParamW`/`CreateDialogIndirectParamW`, `EndDialog`, `GetDlgItem`,
   `SendMessageW`, `SetDlgItemTextW`, `EnableWindow`, `DestroyWindow`), `gdi32`
   (`CreateDIBSection`/`CreateDIBitmap`, `DeleteObject`), `kernel32` (`CreateThread`).
3. **Dialog template.** `DLGTEMPLATE`/`DLGITEMTEMPLATE` blobs via `embedBytes`: STATIC `SS_BITMAP`
   banner, multiline read-only `EDIT` license + "I accept" checkbox, install-scope radios,
   `msctls_progress32`, Back/Next/Cancel. DWORD-aligned, Unicode.
4. **Banner bitmap.** Decode `misc/images/zannalogo*.png` via `PkgPNG` → DIB; set via `STM_SETIMAGE`.
5. **Dialog procedure** (emitted callback, x64 ABI RCX/RDX/R8/R9): `WM_INITDIALOG`, `WM_COMMAND`
   (button IDs; accept-gate enables Next), custom `WM_APP+n` progress, `WM_CLOSE`; drives page flow
   + `EndDialog`.
6. **Worker thread proc** (emitted callback): refactor today's linear extraction/registry/shortcut
   body into a thread that posts `WM_APP+n` percentages.
7. **License text.** Embed repo `LICENSE` into stub data for the license page (also satisfies W4).
8. **Quiet/silent parity.** `/quiet`/`/silent` bypass the dialog and run the worker headless
   (preserve behavior near `InstallerStub.cpp:~2400`).
9. **Uninstaller** stub gets the same treatment (confirm page + progress).

### WS-B — Native ARM64 stub
- New `InstallerStubGenA64.{hpp,cpp}` covering the primitive set the stub uses; select it in
  `buildInstallerStub`/`buildUninstallerStub` when `arch=="arm64"`; set `StubResult::peArch` and
  have `PEBuilder` stamp the ARM64 machine type. The codegen AArch64 backend is the encoding
  reference. Port the WS-A wizard codegen to AArch64 once x64 is solid.

### W3/W5/W6 — Metadata, icon, signing
- W3: add `HelpLink`, `Comments`, `Contact` to the ARP registry writes; confirm `DisplayIcon`,
  `EstimatedSize`, `InstallDate`, `URLUpdateInfo` populated.
- W5: embed a multi-resolution icon (16/32/48/256) via `PkgPNG`+`IconGenerator`.
- W6: ensure `cmd_install_package.cpp` invokes `scripts/sign-windows-installer.ps1` cleanly with
  RFC3161 timestamping; validator checks the signature.

## Critical files

`src/tools/common/packaging/InstallerStub.cpp`, `InstallerStubGen.{hpp,cpp}`, new
`InstallerStubGenA64.{hpp,cpp}`, `WindowsPackageBuilder.cpp`, `PEBuilder.{hpp,cpp}`,
`LnkWriter.*`, `PkgPNG.*`, `IconGenerator.*`; `src/tools/zanna/cmd_install_package.cpp`;
`scripts/sign-windows-installer.ps1`, `scripts/validate-windows-toolchain-installer.ps1`.

## Verification

- Host (Mac) pre-checks: emitter encoder unit tests; `PkgVerify` structural pass on the emitted PE;
  disassemble `.text` to sanity-check the dialog proc / thread proc prologues + IAT calls.
- Windows E2E (required): `scripts/validate-windows-toolchain-installer.ps1` on **x64 and ARM64** —
  wizard pages, license-accept gate, live progress, PATH, Start-Menu/Desktop shortcuts, uninstall,
  `/quiet`. Manual visual walkthrough of the wizard. `signtool verify /pa` on the signed installer;
  confirm no SmartScreen block with a real cert.
