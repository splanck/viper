# Recommendation 6: Platform Policy Lint

## Problem

The original draft lint idea was directionally right but too naive.

A simple grep that says "this file mentions Windows but not macOS/Linux" will generate too much noise in a repo that intentionally has:

- platform adapter files
- OS-specific backend translation units
- tests that are allowed to be host-specific

What matters is not "every file must mention all three OSes." What matters is:

1. raw platform macros should be rare and intentional
2. platform policy should live in adapter layers, not arbitrary shared code
3. new cross-platform chokepoints should be visible in review

## Solution

Add a repo-wide lint that enforces **where** platform macros are allowed and how they are documented.

## Implementation Outline

### 1. Lint for raw platform macro usage outside allowlisted files

Flag new uses of:

- `_WIN32`
- `_WIN64`
- `__APPLE__`
- `__linux__`
- `_MSC_VER`

outside an allowlist of files that are expected to talk directly to host toolchains or SDKs.

Initial allowlist categories:

- `src/runtime/rt_platform.h`
- platform backend source files
- top-level CMake and leaf platform probes
- deliberately OS-specific bridge files

### 2. Enforce shared capability headers in normal code

If a non-allowlisted file needs platform behavior, it should include the shared capability header and use repo-standard macros instead of raw host macros.

### 3. Add a light-touch chokepoint reminder rule

For selected high-risk files, require a small header note such as:

`Cross-platform touchpoints: Windows import rules, Linux archive naming, macOS dylib routing.`

This is not for every file. It is only for files where one edit can break multiple hosts.

### 4. Run in two modes

- advisory mode on the whole tree
- error mode on changed files in CI or pre-push workflows

That keeps rollout practical.

## Proposed Files

- new `scripts/lint_platform_policy.sh`
- new allowlist file, for example `scripts/platform_policy_allowlist.txt`
- optionally a small CI wrapper that scopes the lint to changed files

## Effort

Medium.

The script itself is straightforward. The meaningful work is curating the initial allowlist and avoiding noisy rules.

## How It Prevents Breakage

**Before:** platform-specific policy keeps leaking into shared code and staying there forever.

**After:** raw macro use becomes rare, reviewable, and concentrated in places that are explicitly responsible for platform adaptation.
