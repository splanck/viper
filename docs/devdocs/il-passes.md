# IL Optimization Passes

## Analysis Caching & Instrumentation

- `AnalysisManager` caches module/function analyses and drops entries after each pass based on `PreservedAnalyses`. Module passes must mark any preserved function analyses; otherwise function caches are cleared alongside module caches.
- Convenience helpers exist for common function analyses: `preserveCFG()`, `preserveDominators()`, `preserveLoopInfo()`, `preserveLiveness()`, and `preserveBasicAA()`.
- Enable per-pass statistics with `PassManager::setReportPassStatistics(true)` plus `setInstrumentationStream(...)` to receive lines like `[pass licm] bb 6 -> 6, inst 42 -> 40, analyses M:0 F:2, time 1500us`.
- Statistics track IR size (basic blocks and instructions), analysis recomputations, and wall-clock duration per pass to highlight redundant work.

## SimplifyCFG

The **SimplifyCFG** pass tidies the control-flow graph before and after SSA promotion:

- Folds trivial `cbr` instructions when their condition is constant or both edges converge.
- Eliminates empty forwarding blocks that merely branch to their successor.
- Merges blocks that have a single predecessor with their unique successor when it preserves semantics.
- Prunes blocks that have become unreachable.
- Canonicalizes block parameters and the arguments supplied by branches.

### Safety Notes

SimplifyCFG deliberately skips exception-handling sensitive blocks so that landing pads and dispatch regions keep their required structure.

### Execution Order

This pass runs once during the early pipeline, just before **Mem2Reg**, and again immediately after **Mem2Reg** to clean up any new opportunities introduced by SSA promotion.

## Differential Testing

- `tests/unit/test_il_opt_equivalence.cpp` randomly builds small IL modules (ints/floats with `br`, `cbr`, `switch.i32`) using `IRBuilder`. The generator constrains constants to avoid UB (no unchecked divides; `idx.chk` uses in-range operands).
- Each generated module is verified, cloned, and executed on the VM before and after `O0`, `O1`, and `O2` pipelines via `il::transform::PassManager`. Return values and trap outcomes must match.
- Execution runs in a forked child so unexpected traps or aborts do not bring down the test harness; discrepancies print the seed plus the IL text for repro.
- Seeds default to a fixed constant for stability; override with `VIPER_OPT_EQ_SEED=<u64>` to fuzz locally.
