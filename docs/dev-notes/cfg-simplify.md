# SimplifyCFG Notes

SimplifyCFG normalizes how data flows between blocks while cleaning their control edges. During canonicalization it:

- Orders block parameters deterministically so equivalent blocks converge on a single signature.
- Updates every branch to supply arguments in that canonical order, inserting or removing parameters when redundant.
- Drops unused parameters and rewrites branch arguments so each parameter has exactly one defining incoming value.

By keeping parameter and argument lists synchronized, later passes such as Mem2Reg can rely on stable SSA form without redundant block interfaces.
