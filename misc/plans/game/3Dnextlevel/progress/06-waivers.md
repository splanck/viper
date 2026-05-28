# Tracked waivers

This file is the canonical waiver location for Game3D plan items that cannot be
fully automated in this local pass. Waivers must name the gap, reason, owner,
fallback coverage, and re-open condition.

| ID | Gap | Reason | Owner | Fallback coverage | Re-open when |
|---|---|---|---|---|---|
| W-001 | Interpreted Zia cannot call native `World3D.run*` callback loops directly | Runtime callback-loop APIs currently require C-callable function pointers; managed Zia closures do not have a VM trampoline yet | Runtime/VM | `g3d_test_game3d_runframes_callback_reject`, `g3d_test_game3d_runframes_probe`, `g3d_test_game3d_docs_snippets`, showcase `runFramesOnly` helper, manual fixed-step loops | A managed callback trampoline lands or callback APIs are redesigned |
| W-002 | GPU interactive-framerate proof is not recorded in this local automated run | The correctness lane is the software backend; GPU timing depends on hardware and backend availability | Graphics/runtime CI | Software ctests, structural final-frame probes, capability-gated quality tests | A GPU smoke lane is available in CI or on reference hardware |
| W-003 | Showcase uses a procedural skeleton instead of a committed imported skinned character asset | No small public skinned glTF/GLB fixture is currently committed for the flagship sample | Samples/content | `g3d_test_game3d_anim_probe`, `test_rt_game3d`, and showcase `Animator3D` event/root-motion path | A small redistributable skinned fixture is added |
| W-004 | Showcase visual baseline is structural rather than exact image-tolerance binary | The flagship sample is broader and less stable than `walk_min`; exact image tolerance would be brittle during sample iteration | Graphics/runtime tests | `g3d_game3d_showcase` validates dimensions, crisp final-overlay pixels, visible scene structure, and saves `/tmp/game3d_showcase_software_current.png`; `g3d_walk_min_visual_probe` retains exact-tolerance PNG baseline | Showcase art/camera/content freeze |
| W-005 | 1280x720 software-backend 30 FPS acceptance is not proven in this Debug build | Local build is `CMAKE_BUILD_TYPE=Debug`; an ad hoc 30-frame 1280x720 software run took 2812 ms on this machine, below the release-mode target | Graphics/runtime performance | Functional software ctests, 128x96/160x90 sample final-frame coverage, and recorded Debug measurement | Release/reference hardware performance lane is available and optimized software backend work is scheduled |
