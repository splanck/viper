# IL Optimization Passes

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
