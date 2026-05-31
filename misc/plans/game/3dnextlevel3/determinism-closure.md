# NL3-031 Determinism Closure

Date: 2026-05-31
Host: local macOS Apple Silicon debug build (`cmake-build-debug`)

## Contract

Simulation-facing changes must preserve all three determinism arms:

- worker on/off parity for fixed-step Game3D runs;
- deterministic ordered worker merge before visible simulation state advances;
- VM/native parity for the language/runtime behavior that simulation code can
  depend on.

Any future change that touches simulation scheduling, Game3D fixed-step behavior,
worker merge ordering, IL/VM semantics used by simulation code, or native codegen
semantics used by simulation code must rerun the determinism gate:

```bash
ctest --test-dir cmake-build-debug -R '^(g3d_3dnext2_surface_probe|test_rt_game3d|test_codegen_env_is_native|native_run_zia_42_try_catch_promises|native_run_zia_43_alpha_hardening|native_run_zia_44_language_promises|test_crosslayer_arith)$' --output-on-failure -V
```

If such a change also touches IL, VM, or native codegen policy, it must carry the
GATE-009 ADR note/proof with the same determinism evidence.

## Evidence

The focused local run passed 7/7:

- `g3d_3dnext2_surface_probe`: ordered `Parallel.MapPool` replay and
  `World3D.runFramesOnly` worker-count replay parity.
- `test_rt_game3d`: Game3D worker-count replay parity, worker animator batch
  determinism, fixed-step accounting, synthetic input preservation, and
  deterministic controller paths.
- `test_codegen_env_is_native`: VM reports non-native and native ARM64 reports
  native.
- `native_run_zia_42_try_catch_promises`,
  `native_run_zia_43_alpha_hardening`, and
  `native_run_zia_44_language_promises`: Zia native builds/runs still report
  `RESULT: ok`.
- `test_crosslayer_arith`: VM/native arithmetic equivalence on the ARM64 native
  backend for the conformance edge cases in the suite.
