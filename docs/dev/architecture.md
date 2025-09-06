<!--
File: docs/dev/architecture.md
Purpose: Overview note on deterministic naming and its rationale.
-->

# Architecture Notes

Deterministic label naming ensures that recompiling the same source yields identical IL.
Stable labels keep golden tests from drifting and make builds reproducible.

