# Plan 02 — Metal AA-Text Resource Identity and Upload Lifetime

## Outcome

Ensure every textured overlay command samples the `Pixels` content submitted
for that command. Distinct same-size AA-text rasters must never alias, and an
in-place mutation must upload the correct generation. Close the confirmed Metal
failure without changing the public AA-text API.

## Evidence

`examples/games/3dbowling/known_viper_issues/overlay_aa_text_repro.zia`
queues same-size AA text in different colors. Current software output is
correct. Current Metal can show the later blue texture in the earlier green
draw region. The symptom strongly suggests identity, upload, or command-lifetime
aliasing, but the plan does not assume which layer is wrong.

Relevant current mechanisms:

- `DrawText2DAA` creates a frame-bound temporary `Pixels` object and queues it
  through the screen-image path.
- `Pixels` has a process-unique `cache_identity` and a mutation `generation`.
- `vgfx3d_get_pixels_cache_key` combines stable identity/content metadata.
- Metal's texture cache currently looks up `Pixels` entries through a pointer
  key and compares stored generation/cache key.
- The overlay queue retains temporary objects through final-overlay replay.

The implementation must prove where these mechanisms fail to preserve the two
commands' identities.

## Scope

In scope:

- AA-text temporary `Pixels` creation, retention, and queued command fields;
- Metal `Pixels` texture cache keys, entries, upload progress, and pruning;
- same-frame and cross-frame pointer/generation reuse;
- defensive parity tests for D3D11/OpenGL/software;
- `DrawImage2D` because it uses the same textured overlay path.

Out of scope:

- font replacement or a glyph-atlas redesign;
- changing `DrawText2DAA` rasterization appearance;
- broad texture streaming changes for TextureAsset3D unless the same proven
  identity flaw affects them;
- overlay alpha blending, owned by plan 01.

## Dependencies and ADR decision

Dependency: plan 00. It may execute in parallel with plan 01 if owners avoid
overlapping final-overlay queue code or coordinate changes explicitly.

No ADR is expected for an internal resource-correctness fix. A new public cache
API or a change to documented texture mutation behavior requires ADR review.

## Primary source owners

- `src/runtime/graphics/3d/render/rt_canvas3d_overlay.c`
- `src/runtime/graphics/3d/render/rt_canvas3d_draw.inc`
- `src/runtime/graphics/3d/render/rt_canvas3d_internal.h`
- `src/runtime/graphics/3d/render/rt_canvas3d_tempmgr.c`
- `src/runtime/graphics/2d/rt_pixels.c` and `rt_pixels_internal.h`
- `src/runtime/graphics/3d/backend/vgfx3d_backend_utils.c`
- `src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m`
- `vgfx3d_backend_metal_texture.inc`, target/prune/draw includes
- corresponding D3D11/OpenGL texture cache tests for parity
- Canvas3D unit/GPU/production tests and Graphics3D fixtures

## Identity contract

For a `Pixels`-backed draw, cache identity is the tuple:

```text
(logical Pixels identity, current content generation, relevant format/extent)
```

Pointer address alone is not logical identity because allocator addresses can
be reused. Extent alone is never identity. A queued command must keep its
source object alive until its upload and draw have completed. An upload in
progress for one identity/generation must not be redirected to another source.

## Implementation sequence

### Phase 1 — Build a failure matrix

Extend the minimal repro or add a focused fixture with these cases:

1. two same-size AA strings, different colors, one frame;
2. three same-size AA strings to detect first/last alias patterns;
3. same text/color but distinct Pixels identities;
4. different sizes as a control;
5. two same-size raw `DrawImage2D` images with solid distinct colors;
6. one Pixels object mutated between frames;
7. allocator-reuse stress across hundreds of temporary objects/frames;
8. upload-budget pressure large enough to exercise incremental Metal upload;
9. cache pruning followed by redraw;
10. resize/render-scale changes between submissions.

Capture regions with exact color/dominance assertions rather than whole-image
hashes. Confirm which cases fail on Metal and remain correct on software.

### Phase 2 — Prove object lifetime

1. Verify `rt_canvas3d_add_temp_object` retains the AA Pixels before the local
   reference is released.
2. Verify final-overlay command materials keep the exact Pixels reference used
   at queue time.
3. Verify temp objects are released only after final-overlay replay completes,
   not after the pre-post-FX scene pass.
4. Add a test-only lifetime counter or weak sentinel if existing test hooks can
   observe finalization. Remove temporary instrumentation before commit.
5. If lifetime is wrong, fix it in the common temp/final-overlay owner and rerun
   software plus all textured overlay tests before touching Metal.

### Phase 3 — Trace Metal cache lookup/upload

For each failing command, record in a debugger or temporary trace:

- source pointer;
- stable Pixels cache identity;
- generation and combined cache key;
- width/height;
- Objective-C dictionary key;
- cache-entry object;
- pending/current generation;
- upload start/completion frame and row range;
- MTLTexture object;
- command submission using that texture.

Check these failure modes explicitly:

- dictionary lookup keyed by a reused pointer returns an entry owned by a new
  logical Pixels object;
- the cache key lacks stable identity or generation;
- entry reuse preserves an old texture or in-progress upload state;
- incremental upload reads from a source that has been freed or replaced;
- two commands resolve texture lazily after one temp has changed;
- material/command references point at the later Pixels;
- cache pruning mutates the dictionary while commands still need entries.

### Phase 4 — Apply an identity-safe fix

The preferred design is for the Metal dictionary key itself to use stable
logical cache identity, with generation validated in the entry. Pointer may be
retained as a diagnostic/source reference but must not be the sole key. Mirror
the render-target cache pattern, which already keys by monotonic identity and
checks the underlying object.

If the proven fault is elsewhere, retain these requirements:

- new Pixels at a reused address cannot hit an old entry;
- an in-place mutation invalidates/restarts upload for the new generation;
- an in-progress upload retains or otherwise safely reads its exact source;
- stale async/incremental completion cannot publish into a newer generation;
- pruning never drops a texture still referenced by the active frame;
- failed upload produces the established fallback/diagnostic, not another
  image's texture.

Do not disable caching globally or force all AA text to synchronous uncached
uploads as the final solution. That masks identity bugs and creates a common
HUD performance regression.

### Phase 5 — Defensive parity

Review D3D11 and OpenGL texture caches for the same pointer-reuse pattern. They
already use cache keys/hints differently; add the same logical tests even if
they currently pass. Avoid unnecessary backend rewrites.

Verify TextureAsset3D and cubemap caches still use their own monotonic identity
and native revision semantics. Do not unify unrelated key formats without
evidence.

### Phase 6 — Regression and documentation closure

- Register the fixture as a display/GPU test with software coverage where
  possible.
- Keep the game repro and update its README from open issue to regression test
  after all backend cells pass.
- Add a concise code comment at the cache key explaining why pointer identity
  is insufficient.
- Document any upload-budget behavior the test exposed.

## Tests

Required test assertions:

- same-size distinct images remain distinct in submission order;
- pointer-reuse stress never samples stale content;
- mutation generation updates the texture exactly once;
- multi-frame unchanged content hits the cache;
- incremental upload cannot cross-publish generations;
- pruning and context teardown release cache entries;
- final-overlay temporary objects survive until replay;
- DrawImage2D, AA text, material textures, and TextureAsset3D remain correct.

Run:

```sh
ctest --test-dir build -R 'test_rt_canvas3d|test_rt_canvas3d_gpu_paths|test_rt_canvas3d_production|test_vgfx3d_backend_metal_shared' --output-on-failure
ctest --test-dir build -L graphics3d --output-on-failure
./scripts/lint_platform_policy.sh --changed-only
```

Run the original repro manually on Metal and the new fixture through software.
Complete D3D11/OpenGL cells before release.

## Performance budget

- unchanged cached Pixels lookup remains amortized constant time;
- no per-draw heap allocation is added to the hot path beyond existing
  Objective-C cache behavior;
- AA-text creation remains its existing raster allocation cost;
- cache pruning remains bounded and does not scan on every draw;
- record cache-hit/upload counts before and after a repeated-HUD scenario.

## Acceptance criteria

- The original green/blue AA-text repro is correct on Metal.
- Raw same-size DrawImage2D and pointer-reuse stress tests pass.
- Software, D3D11, and OpenGL pass the same semantic fixture.
- Unchanged textures still cache; mutations invalidate by generation.
- No public API changes and no font appearance redesign.
- Root cause and exact identity/lifetime fix are documented.

## Stop conditions

Stop if correctness requires retaining arbitrary script objects past the frame,
making texture upload thread-unsafe, or changing all `Pixels` ABI/layout without
review. A broader cache ABI change needs an ADR and separate compatibility
analysis.

## Handoff evidence

Provide the failure matrix, identity trace for two conflicting commands,
before/after samples, cache hit/upload performance, lifetime test, and full
backend matrix.

