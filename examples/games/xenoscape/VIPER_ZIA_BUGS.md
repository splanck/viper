# Viper/Zia Bugs Found During Xenoscape Overhaul

This list is limited to Viper/Zia compiler, runtime, or tooling issues noticed
while working on Xenoscape. Gameplay/data bugs are tracked in the code changes
and validation probe instead.

## Confirmed

1. **Native-linked macOS aggregate-return miscompile**
   - Evidence: `physics.zia` documents that `MoveResult` must remain a heap
     class because the native-linked macOS demo binary currently miscompiles
     aggregate returns.
   - Impact: Zia code that should naturally return a lightweight record/struct
     has to allocate or reuse a class instance instead.
   - Workaround in Xenoscape: `PhysicsHelper` owns one reusable `MoveResult`
     and mutates it in place.

## Tooling Candidates

1. **Ambiguous imported symbol warning for common names**
   - Evidence: the targeted Xenoscape build emits `warning[V3001]` because
     `Viper.Math.Lerp` conflicts with `Viper.Graphics.Color.Lerp` when both
     modules are bound.
   - Impact: broad module binds make common helper names fragile and noisy.
   - Follow-up: prefer or add clearer namespace-qualified/aliased import
     patterns so demos can bind graphics and math APIs without warning churn.

2. **No obvious discard pattern for intentional count loops**
   - Evidence: the build emits `warning[W001]` for range-loop variables such as
     `for i in 0..MAX_ENEMIES` when the variable is only used to repeat work.
   - Impact: intentional fixed-count loops produce warning noise unless the
     code is rewritten as a manual `while`.
   - Follow-up: document or add an idiomatic discard variable/suppression
     pattern for intentional unused loop variables.
