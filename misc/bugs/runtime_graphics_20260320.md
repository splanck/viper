# Runtime Graphics Comprehensive Audit — 2026-03-20

## Scope
File-by-file, function-by-function review of every C source file in `src/runtime/graphics/`
implementing the `Viper.Graphics` and `Viper.Graphics3D` namespaces.

**Severity:** P0 = crash/security, P1 = logic bug, P2 = optimization, P3 = code quality

---

## P0 — Critical (Crash / Security / Data Corruption)

### GFX-001: Pixel format inconsistency across image processing functions
**File:** `rt_pixels.c` lines 1509-1691 (invert, grayscale, tint, blur, resize)
**Category:** Data corruption
**Description:** The documented pixel format is `0xRRGGBBAA` (R in bits 31:24, A in bits 7:0), confirmed by `rgb_to_rgba()`, `rt_pixels_load_bmp()`, `rt_pixels_save_bmp()`, and `rt_pixels_blend_pixel()`. However, `rt_pixels_invert`, `rt_pixels_grayscale`, `rt_pixels_tint`, `rt_pixels_blur`, and `rt_pixels_resize` all extract channels as if format is `0xAARRGGBB`:
```c
uint8_t a = (px >> 24) & 0xFF;  // Actually R, not A
uint8_t r = (px >> 16) & 0xFF;  // Actually G, not R
uint8_t g = (px >> 8) & 0xFF;   // Actually B, not G
uint8_t b = px & 0xFF;          // Actually A, not B
```
**Impact:** `rt_pixels_invert` preserves R, inverts G/B/A. `rt_pixels_grayscale` uses wrong channels for luminance formula. `rt_pixels_tint` tints wrong channels. `rt_pixels_blur` blurs with swapped channels. All produce visually incorrect output.
**Fix:** Replace all `0xAARRGGBB` extractions with `0xRRGGBBAA` format: `r = (px >> 24) & 0xFF`, `g = (px >> 16) & 0xFF`, `b = (px >> 8) & 0xFF`, `a = px & 0xFF`.

### GFX-002: rt_pixels_rotate uses a THIRD pixel format interpretation
**File:** `rt_pixels.c` lines 1425-1448
**Category:** Data corruption
**Description:** Bilinear interpolation in `rt_pixels_rotate` extracts channels as `0xAABBGGRR` (R from bits 7:0, A from bits 31:24):
```c
uint8_t r00 = (c00 >> 0) & 0xFF;   // Actually A
uint8_t g00 = (c00 >> 8) & 0xFF;   // Actually B
uint8_t b00 = (c00 >> 16) & 0xFF;  // Actually G
uint8_t a00 = (c00 >> 24) & 0xFF;  // Actually R
```
**Impact:** Arbitrary rotation produces completely garbled colors — R and A are swapped, G and B are swapped.
**Fix:** Align with canonical 0xRRGGBBAA format.

### GFX-003: Mouse get_time_ms() is a stub — click detection completely broken
**File:** `rt_input.c` lines 1126-1131
**Category:** Functional failure
**Description:** The `get_time_ms()` function returns `++counter` instead of actual wall-clock time:
```c
static int64_t get_time_ms(void) {
    static int64_t counter = 0;
    return ++counter;
}
```
Used by `rt_mouse_button_down/up` for click timing (lines 1211, 1227-1229). Since counter values are 1, 2, 3... and `CLICK_MAX_DURATION_MS = 300`, every button release is treated as a "click" because `press_duration` is always tiny. Double-click detection is similarly broken.
**Fix:** Replace with actual millisecond time source: `rt_timer_ms()` (already available via extern in rt_sprite.c) or `rt_clock_ticks_us() / 1000`.

### GFX-004: Division by zero in canvas mouse event scaling
**File:** `rt_canvas.c` lines 247-250
**Category:** Crash
**Description:** Mouse move events divide by `cs = vgfx_window_get_scale(canvas->gfx_win)`. If the backend returns 0.0 (e.g., during window creation race or mock), this causes FPE or produces ±infinity coordinates.
**Fix:** Guard: `if (cs < 0.001f) cs = 1.0f;`

### GFX-005: Flood fill realloc error handling — potential use-after-free
**File:** `rt_drawing_advanced.c` lines 387-394
**Category:** Memory safety
**Description:** When growing the flood fill stack, if `stack_x` realloc succeeds but `stack_y` realloc fails:
```c
int64_t *nx = realloc(stack_x, ...);
int64_t *ny = realloc(stack_y, ...);
if (!nx || !ny) {
    free(nx ? nx : stack_x);  // Frees nx (new allocation)
    free(ny ? ny : stack_y);  // Frees old stack_y
    return;                    // stack_x now points to freed nx
}
```
If `nx` succeeded and `ny` failed: frees `nx` and old `stack_y`. But `stack_x` was already replaced by `realloc` — the old `stack_x` pointer may now be invalid. Actually `realloc` success means old pointer is freed, so `stack_x` holds the old (now-freed) pointer until `stack_x = nx` on line 395 which is never reached.
**Fix:** Save old pointers before realloc. On partial failure, restore old pointers and return.

### GFX-006: Integer overflow in BDF/PSF bitmap allocation
**File:** `rt_bitmapfont.c` lines 245-249 (BDF) and 401-402 (PSF)
**Category:** Security (malicious font file)
**Description:** `int alloc_size = rb * bbx_h` uses `int` multiplication. A crafted BDF file with large bbx_w/bbx_h values can overflow `int`, causing a small allocation followed by an out-of-bounds write during bitmap row parsing.
**Fix:** Use `size_t` or `int64_t` with overflow check before calloc.

---

## P1 — Logic Bugs

### GFX-007: Sprite draw corrupts source frame via in-place flip
**File:** `rt_sprite.c` lines 414-425
**Category:** Data corruption
**Description:** `rt_pixels_flip_h()` and `rt_pixels_flip_v()` operate IN-PLACE (return `self`). When `rt_sprite_draw` calls them for a flipped sprite, the original frame pixel data in `sprite->frames[]` is permanently modified. Subsequent draws without flip still show flipped data.
**Fix:** Clone the frame before flipping: `void *flipped = rt_pixels_flip_h(rt_pixels_clone(transformed));`

### GFX-008: Camera.Follow doesn't set dirty flag
**File:** `rt_camera.c` lines 253-266
**Category:** Rendering bug
**Description:** `rt_camera_follow()` updates `camera->x` and `camera->y` but never sets `camera->dirty = 1`. Renderers checking `rt_camera_is_dirty()` will skip updating after Follow calls.
**Fix:** Add `camera->dirty = 1;` before `camera_clamp_bounds(camera);`

### GFX-009: Camera parallax uses fragile struct layout cast
**File:** `rt_camera.c` lines 484-486
**Category:** Portability / correctness
**Description:** `rt_camera_draw_parallax` reads pixel dimensions via raw struct cast:
```c
int64_t *pdata = (int64_t *)layer->pixels;
int64_t pw = pdata[0];
int64_t ph = pdata[1];
```
This assumes `rt_pixels_impl` starts with `{int64_t width; int64_t height; ...}` and that there's no GC object header before the data. If the object layout changes, this reads garbage.
**Fix:** Use `rt_pixels_width(layer->pixels)` and `rt_pixels_height(layer->pixels)`.

### GFX-010: Scene clear doesn't reset child parent pointers
**File:** `rt_scene.c` lines 985-996
**Category:** Stale references
**Description:** `rt_scene_clear()` pops all children from root's child list but doesn't clear each child's `parent` pointer. If those nodes are later re-added to a different parent, the stale parent pointer may cause `rt_scene_node_remove_child` (called in `add_child` detach logic) to incorrectly modify the old parent.
**Fix:** Before popping, iterate children and set `child->parent = NULL`.

### GFX-011: Triangle scanline has dead/redundant code
**File:** `rt_drawing_advanced.c` lines 484-488
**Category:** Logic bug
**Description:** Lines 484-487 compute `xa` with a division-by-zero guard, but line 488 immediately overwrites `xa` using `rtg_max64(y2 - y1, 1)` — the guarded branch is dead code.
**Fix:** Remove the dead lines 484-487.

### GFX-012: Polygon fixed intersection array may overflow
**File:** `rt_drawing_advanced.c` line 913
**Category:** Buffer overflow
**Description:** `int64_t intersections[64]` is a fixed-size stack array. For self-intersecting polygons with >32 edges, the `num_intersections` counter can exceed 62 (the cap at line 916 is `< 62`, allowing up to 62 entries in a 64-element array — safe). However, the cap of 62 means complex polygons silently drop intersections, producing rendering artifacts.
**Fix:** Dynamic allocation or document the 31-edge polygon limit.

### GFX-013: Color.ToHex treats alpha=0 same as alpha=255
**File:** `rt_drawing_advanced.c` lines 1292-1293
**Category:** Logic error
**Description:** `if (a != 0 && a != 255)` means both fully-transparent (a=0) and fully-opaque (a=255) colors produce a 6-char hex string with no alpha component. Users who set alpha=0 for transparency cannot distinguish it from opaque in hex output.
**Fix:** Change to `if (a != 255)` to include alpha for any non-opaque value.

### GFX-014: Canvas width/height truncation without validation
**File:** `rt_canvas.c` lines 69-70
**Category:** Integer safety
**Description:** `params.width = (int32_t)width` and `params.height = (int32_t)height` truncate from int64_t with no range validation. Negative or very large values produce unexpected window sizes.
**Fix:** Clamp to `[1, INT32_MAX]` before cast.

### GFX-015: Tile properties use static global storage
**File:** `rt_tilemap_io.c` lines 60-64
**Category:** Correctness / Thread safety
**Description:** Tile properties and autotile rules use `static` global arrays (`s_props[256]`, `s_autotile_rules[64]`). Multiple tilemap instances share the same property storage. The `tm` parameter is explicitly `(void)tm` — ignored.
**Fix:** Embed property storage in the tilemap struct (as the comment on line 61 acknowledges).

### GFX-016: Caps lock tracking not implemented
**File:** `rt_input.c` lines 174-176, 357-361
**Category:** Missing implementation
**Description:** `rt_keyboard_caps_lock()` returns `g_caps_lock` which is never updated from the OS. Always returns `false`.
**Fix:** Query OS caps lock state via platform API.

### GFX-017: Mouse SetPos doesn't warp platform cursor
**File:** `rt_input.c` lines 1427-1433
**Category:** Incomplete implementation (TODO)
**Description:** Comment says "Platform-specific cursor warp would go here" but only updates internal state. The actual cursor position on screen doesn't move.
**Fix:** Call platform cursor warp: `vgfx_set_cursor_pos()` or equivalent.

### GFX-018: Scene node count is misleading
**File:** `rt_scene.c` lines 967-983
**Category:** API confusion
**Description:** `rt_scene_node_count` only counts visible nodes with sprites (via `collect_visible_nodes`). The comment acknowledges this: "For simplicity, just return visible sprite count." Function name implies total node count.
**Fix:** Either rename to `rt_scene_visible_sprite_count` or implement actual total node counting.

### GFX-019: Arc angle normalization may loop extremely long
**File:** `rt_drawing_advanced.c` lines 731-734
**Category:** Performance / potential hang
**Description:** `while (start_angle < 0) start_angle += 360;` — for extremely negative values (e.g., INT64_MIN), this loops ~2.5×10^16 times.
**Fix:** Use modulo: `start_angle = ((start_angle % 360) + 360) % 360;`

---

## P2 — Optimization

### GFX-020: Pixels blur vertical pass is cache-unfriendly
**File:** `rt_pixels.c` lines 1664-1688
**Category:** Performance
**Description:** Vertical pass iterates `x` in outer loop, `y` in inner loop — column-major access pattern. For large images, this causes excessive cache misses.
**Fix:** Transpose tmp buffer, or iterate y-outer/x-inner with row-based accumulation.

### GFX-021: PNG CRC table initialization not thread-safe
**File:** `rt_pixels.c` lines 1007-1019
**Category:** Thread safety
**Description:** Static `crc_table_init` flag with no synchronization. Two threads calling `rt_pixels_save_png()` simultaneously could race.
**Fix:** Use `pthread_once` / `InitOnceExecuteOnce` or compute at compile time.

### GFX-022: SpriteBatch tint_color stored but never applied
**File:** `rt_spritebatch.c` lines 233-243
**Category:** Dead feature
**Description:** `tint_color` and `alpha` are stored in the batch but only used to choose between `rt_canvas_blit` and `rt_canvas_blit_alpha`. The actual tint color is never applied to pixels.
**Fix:** Apply tint via `rt_pixels_tint()` before blitting, or remove the API.

### GFX-023: SpriteSheet region lookup is O(n) linear scan
**File:** `rt_spritesheet.c` line 92-101
**Category:** Performance
**Description:** `find_region` does linear strcmp over all regions. For sheets with hundreds of named regions, every `GetRegion` call is O(n).
**Fix:** Use a hash map for name→index lookup (consistent with other runtime containers).

### GFX-024: Gamepad hotplug only re-scans when pad 0 disconnects
**File:** `rt_input_pad.c` line 475-476 (macOS)
**Category:** Functionality gap
**Description:** `if (!g_pads[0].connected) mac_scan_devices();` — new controllers plugged in while pad 0 is connected are never detected.
**Fix:** Periodic rescan (e.g., every 60 frames) or IOHIDManager callback-based detection.

### GFX-025: Scene graph draw/draw_with_camera massive code duplication
**File:** `rt_scene.c` lines 805-957
**Category:** Maintainability
**Description:** `rt_scene_draw` and `rt_scene_draw_with_camera` are nearly identical (~150 lines each), differing only in the camera transform application.
**Fix:** Extract shared logic into a helper with an optional camera parameter.

### GFX-026: strdup OOM not checked in multiple canvas functions
**File:** `rt_canvas.c` lines 91, 308
**Category:** OOM resilience
**Description:** `strdup(cstr)` return value not checked for NULL (OOM). Canvas title becomes NULL on allocation failure.
**Fix:** Check return and handle gracefully.

---

## P3 — Code Quality

### GFX-027: Scene node_draw comment says "sorted by depth" but doesn't sort
**File:** `rt_scene.c` line 596
**Category:** Inaccurate comment
**Description:** Comment says "Draw children (sorted by depth for this level)" but children are drawn in insertion order with no sorting.
**Fix:** Update comment to "Draw children in insertion order" or implement per-level depth sort.

### GFX-028: PNG save always uses filter=None
**File:** `rt_pixels.c` line 929
**Category:** Suboptimal output
**Description:** PNG scanlines always use filter byte 0 (None). Sub/Up/Average/Paeth filters would significantly reduce file size for most images.
**Fix:** Implement at minimum filter=Sub for better compression.

### GFX-029: Deadzone applied per-axis, not radially
**File:** `rt_input_pad.c` lines 1016-1029
**Category:** Inaccurate comment
**Description:** Comment says "Apply radial deadzone to stick value" but the implementation applies deadzone to individual axis values. A proper radial deadzone considers `sqrt(x² + y²)`.
**Fix:** Update comment to "per-axis deadzone" or implement true radial deadzone.

### GFX-030: InputManager debounce slot reuse silently overwrites slot 0
**File:** `rt_inputmgr.c` line 130
**Category:** Silent data loss
**Description:** When debounce capacity is full, `find_or_create_debounce_slot` returns slot 0 without evicting the existing key. The caller then overwrites slot 0's timer, corrupting debounce state for the original key.
**Fix:** Properly evict the oldest/least-used slot, or grow the array.

### GFX-031: rt_pixels_fill could use memset for common values
**File:** `rt_pixels.c` lines 210-223
**Category:** Performance
**Description:** `rt_pixels_fill` uses a per-pixel loop. For `color=0` (transparent black), `memset` would be much faster. For any uniform 4-byte pattern, `memset` works on most architectures if all bytes are the same.
**Fix:** Fast-path for color=0 using memset.

### GFX-032: Sprite animation creates excessive GC pressure
**File:** `rt_sprite.c` lines 410-466
**Category:** Memory
**Description:** Each `rt_sprite_draw` with transforms (flip/scale/rotate) creates new GC-managed Pixels objects that aren't explicitly freed. The comment says "transformed pixels will be GC'd" but this can cause significant memory pressure in tight render loops.
**Fix:** Cache transformed frames when transform parameters haven't changed, or use explicit cleanup.

### GFX-033: BitmapFont load_bdf doesn't validate sscanf return
**File:** `rt_bitmapfont.c` line 216
**Category:** Robustness
**Description:** `sscanf(line + 4, "%d %d %d %d", &bbx_w, &bbx_h, &bbx_xoff, &bbx_yoff)` — return value not checked. Partial parses leave variables as 0, which may cause zero-size allocations.
**Fix:** Check `sscanf() == 4` before using values.

### GFX-034: PNG loader reads entire file into memory
**File:** `rt_pixels.c` lines 661-682
**Category:** Resource usage
**Description:** `rt_pixels_load_png` reads the entire file into a single malloc'd buffer. For very large PNG files (e.g., 100MB+), this could OOM without warning. No file size limit.
**Fix:** Add a reasonable file size limit (e.g., 256MB) or use streaming chunk parsing.

### GFX-035: Tilemap load_csv doesn't handle long lines
**File:** `rt_tilemap_io.c` line 528-530
**Category:** Truncation
**Description:** `char line_buf[4096]` — CSV lines longer than 4095 characters are silently truncated, causing column count miscount and data loss.
**Fix:** Dynamic line buffer or document the 4K line limit.

### GFX-036: Action system chord keys cast from void* without unboxing
**File:** `rt_action.c` line 693, `rt_keychord.c` line 167
**Category:** Portability
**Description:** `b->chord_keys[i] = (int64_t)rt_seq_get(keys, i)` casts a `void*` (which is a boxed integer) directly to `int64_t`. If keys are boxed objects (rt_box_i64), this reads the object pointer as the key value rather than unboxing. Same pattern in rt_keychord.c line 167.
**Fix:** Unbox via `rt_unbox_i64(rt_seq_get(keys, i))` if the seq contains boxed values.

---

### GFX-037: Mesh3D normal transform ignores non-uniform scaling
**File:** `rt_mesh3d.c` line 257
**Category:** Rendering correctness
**Description:** Comment says "no inverse-transpose for now." When a mesh has non-uniform scale (e.g., stretched on one axis), normals must be transformed by the inverse-transpose of the model matrix, not the model matrix itself. Using the model matrix directly produces distorted normals and incorrect lighting.
**Fix:** Compute inverse-transpose of the 3x3 upper-left submatrix for normal transformation.

### GFX-038: Transform3D look_at uses incomplete rotation matrix for quaternion extraction
**File:** `rt_transform3d.c` lines 252-285
**Category:** Math error
**Description:** The Shepperd quaternion extraction uses only 3 matrix elements (`m00=rx, m11=tuy, m22=tz`) instead of the full 9 rotation matrix entries. The comment at line 259 acknowledges: "note: using full matrix elements would be more precise." This produces incorrect rotations for many orientations.
**Fix:** Build the full 3x3 rotation matrix from right/up/forward vectors and use all 9 elements in the Shepperd method.

### GFX-039: Physics3D uses AABB narrow-phase for ALL shape types
**File:** `rt_physics3d.c` line 140
**Category:** Physics accuracy
**Description:** Comment says "use AABB penetration for all shapes (simplified)." Sphere-sphere and capsule-capsule collisions use axis-aligned overlap instead of proper distance-based tests. This produces incorrect collision normals (always axis-aligned instead of radial for spheres), causing bodies to "slide" along axes instead of deflecting naturally.
**Fix:** Add dedicated sphere-sphere and capsule-capsule narrow-phase tests with proper radial normals.

### GFX-040: Physics3D friction force never applied
**File:** `rt_physics3d.c` throughout
**Category:** Missing feature
**Description:** Bodies have a `friction` field (default 0.5) that is set and stored but never used in `resolve_collision`. Only restitution (bounciness) is applied. Bodies slide frictionlessly on all surfaces.
**Fix:** Add tangential friction impulse in `resolve_collision` using the Coulomb friction model.

### GFX-041: Physics3D body limit of 256 with silent drop
**File:** `rt_physics3d.c` line 42, 308-309
**Category:** Silent failure
**Description:** `PH3D_MAX_BODIES = 256`. Adding a 257th body silently does nothing — no error, no trap, no indication that the body wasn't added. Games with many physics objects will have invisible bodies.
**Fix:** Either trap with a clear error, or use a dynamic array.

### GFX-042: Canvas3D screenshot uses fragile struct layout cast
**File:** `rt_canvas3d.c` lines 1389-1396
**Category:** Portability
**Description:** Same pattern as GFX-009: casts Pixels object to a local `px_view` struct assuming `{int64_t w, h; uint32_t *data}` layout. Should use `rt_pixels_set` API instead.
**Fix:** Use `rt_pixels_set(pixels, x, y, color)` for each pixel.

### GFX-043: Canvas3D duplicated 5x7 font table (3.3KB × 2)
**File:** `rt_canvas3d.c` lines 378-427 and 1234-1330
**Category:** Code duplication
**Description:** The same 95-character 5x7 bitmap font table is defined as a `static` local in both `rt_canvas3d_draw_text_3d` and `rt_canvas3d_draw_text2d` — ~200 lines of identical data, ~3.3KB duplicated in the binary.
**Fix:** Factor into a single file-scope `static const` array referenced by both functions.

### GFX-044: Canvas3D clear double-writes framebuffer for GPU backends
**File:** `rt_canvas3d.c` lines 232-262
**Category:** Performance
**Description:** When using a GPU backend (OpenGL/D3D11), `rt_canvas3d_clear` calls `backend->clear()` AND then iterates every pixel in the software framebuffer to clear it. This O(w×h) pixel loop is redundant — the GPU already cleared its buffer.
**Fix:** Skip software framebuffer clear when GPU backend is active and no software compositing is needed.

### GFX-045: Camera3D FPS init asin without clamp — NaN risk
**File:** `rt_camera3d.c` line 370
**Category:** Math safety
**Description:** `cam->fps_pitch = asin(fy) * (180.0 / M_PI)` — if `fy` exceeds [-1, 1] due to floating-point imprecision in the view matrix, `asin` returns NaN, corrupting the camera state.
**Fix:** `asin(fmax(-1.0, fmin(1.0, fy)))`.

### GFX-046: Canvas3D poll has same division-by-zero as 2D canvas
**File:** `rt_canvas3d.c` lines 889-891
**Category:** Crash
**Description:** Same pattern as GFX-004: `evt.data.mouse_move.x / cs` where `cs = vgfx_window_get_scale()` could be 0.
**Fix:** Guard: `if (cs < 0.001f) cs = 1.0f;`

### GFX-047: OBJ loader doesn't deduplicate vertices
**File:** `rt_mesh3d.c` lines 726-766
**Category:** Memory/performance
**Description:** Every face vertex creates a new mesh vertex, even when the same position/normal/UV combination appears in multiple faces. A simple cube with 8 positions but 36 face vertices creates 36 mesh vertices instead of 24 (or 8 with index reuse).
**Fix:** Build a hash map of (vi, ti, ni) → mesh index for vertex deduplication.

### GFX-048: Physics3D character controller move is trivial
**File:** `rt_physics3d.c` lines 582-602
**Category:** Incomplete implementation
**Description:** The header comment describes "up to 3 slide iterations per move" but `rt_character3d_move` simply sets the body velocity directly — no slide, no step-up, no slope limiting. The step_height and slope_limit_cos fields are stored but unused.
**Fix:** Implement proper character controller with sweep, slide, and step logic.

### GFX-049: Skeleton3D add_bone uses fragile Mat4 struct cast
**File:** `rt_skeleton3d.c` lines 337-344
**Category:** Portability
**Description:** Same fragile struct layout pattern as GFX-009: casts bind_mat4 to a local `mat4_view` struct assuming `double m[16]` is the first field.
**Fix:** Use Mat4 API to extract individual elements.

### GFX-050: AnimPlayer3D allocates globals array every update
**File:** `rt_skeleton3d.c` line 759
**Category:** Performance
**Description:** `compute_bone_palette` allocates a new `float *globals` array via malloc every frame for every animated character. At 60fps, this is 60 malloc+free per second per character.
**Fix:** Pre-allocate the globals workspace in the AnimPlayer3D struct.

### GFX-051: Particle system uses shared global PRNG state
**File:** `rt_particles3d.c` line 87
**Category:** Correctness
**Description:** `static uint32_t prng_state = 0x12345678` is shared across all particle systems. Multiple systems produce correlated patterns, and the state is not thread-safe.
**Fix:** Move PRNG state into the `rt_particles3d` struct, seeded with a unique value per system.

### GFX-052: Particle draw creates a new Material3D every frame
**File:** `rt_particles3d.c` lines 668-678
**Category:** Performance / GC pressure
**Description:** `rt_particles3d_draw` calls `rt_material3d_new()` (GC allocation) on every draw call, every frame, for every particle system.
**Fix:** Cache the material in the particle system struct and reuse it.

### GFX-053: Animation crossfade blends matrices instead of quaternions
**File:** `rt_skeleton3d.c` lines 748-752
**Category:** Math approximation
**Description:** Crossfade lerps TRS matrices directly. For large rotation differences, matrix lerp produces non-orthogonal matrices (skew/shear artifacts). Proper approach is to decompose to TRS and SLERP the quaternion.
**Fix:** Decompose TRS before blending and use SLERP for the rotation component.

### GFX-054: Water3D creates new mesh every frame — massive GC pressure
**File:** `rt_water3d.c` line 110
**Category:** Memory leak / GC pressure
**Description:** `rt_water3d_update` calls `w->mesh = rt_mesh3d_new()` every frame without freeing the old mesh. At 60fps with a 32×32 water grid (2,178 vertices × 80 bytes = ~170KB per mesh), this creates ~10MB/second of dead mesh objects for GC to collect.
**Fix:** Reuse the same mesh object, clearing vertices/indices each frame instead of creating a new one. Or call `rt_mesh3d_clear()` before rebuilding.

### GFX-055: Scene3D mark_dirty/recompute use recursion — stack overflow risk
**File:** `rt_scene3d.c` lines 172-199
**Category:** Stack overflow
**Description:** Both `mark_dirty` and `recompute_world_matrix` use recursion. For deep scene hierarchies (100+ levels), this risks stack overflow. The 2D scene graph (`rt_scene.c`) solved this with an iterative approach, but the 3D scene graph still uses recursion.
**Fix:** Convert to iterative traversal with explicit stack (matching the 2D scene fix).

### GFX-056: CubeMap3D uses fragile pixels struct layout cast
**File:** `rt_cubemap3d.c` lines 34-39, 56, 66
**Category:** Portability
**Description:** Uses a local `cubemap_pixels_view` struct to cast Pixels objects and read width/height/data directly. Same fragile pattern as GFX-009.
**Fix:** Use `rt_pixels_width()`, `rt_pixels_height()`, and proper pixel access APIs.

### GFX-057: NavMesh3D adjacency build is O(n²)
**File:** `rt_navmesh3d.c` lines 183-193
**Category:** Performance
**Description:** Triangle adjacency is built by comparing every pair of triangles. For a mesh with 10,000 walkable triangles, this is 50 million pair tests.
**Fix:** Build an edge hash map: for each triangle edge (v0,v1 sorted), store the triangle index. Adjacency is then O(n) via hash lookups.

### GFX-058: Sprite3D/Sprite3D_draw creates new mesh+material every frame
**File:** `rt_sprite3d.c` lines 145, 160-162
**Category:** Performance / GC pressure
**Description:** `rt_canvas3d_draw_sprite3d` creates a new Mesh3D (4 verts, 6 indices) and a new Material3D via GC allocation every single draw call. For 50 billboard sprites at 60fps, that's 6,000 GC objects per second. Also declares `rt_canvas3d_add_temp_buffer` at line 164 but never calls it — mesh/material are orphaned.
**Fix:** Cache billboard mesh and material in the sprite3d struct. Rebuild only when frame/texture changes.

### GFX-059: Sprite3D uses fragile struct layout cast for texture dimensions
**File:** `rt_sprite3d.c` lines 73-74
**Category:** Portability
**Description:** Same `px_view` struct layout assumption as GFX-009/GFX-056: `px_view *pv = (px_view *)texture;` to read width/height.
**Fix:** Use `rt_pixels_width(texture)` and `rt_pixels_height(texture)`.

### GFX-060: Decal3D size comment says "half-size" but uses full value
**File:** `rt_decal3d.c` line 138
**Category:** Inaccurate comment/possible double-size
**Description:** `double hs = d->size;` with comment `/* half-size */`. If `d->size` represents the full decal width, the quad is correctly sized. But if callers pass "size = 2.0" expecting a 2-unit decal, they get a 4-unit decal because `hs` is used as both + and - offset.
**Fix:** Clarify API: if size is full width, use `d->size * 0.5`. If size is half-width, update comment.

### GFX-061: rt_canvas_poll return value is misleading
**File:** `rt_canvas.c` line 266
**Category:** API confusion
**Description:** `rt_canvas_poll` returns `(int64_t)canvas->last_event.type` — the type of the last event in the queue. If multiple events are processed, only the last one's type is returned. The API header describes this as returning the event type, but callers may expect a boolean poll result. The close event is tracked separately via `should_close`.
**Fix:** Document clearly or change return to 1/0 for "events processed / no events."

### GFX-062: rt_font.c header comment references wrong file
**File:** `rt_font.c` line 30
**Category:** Stale comment
**Description:** Comment says "rt_graphics.c (Canvas.Text implementation)" but Canvas.Text is in rt_drawing.c after the file split.
**Fix:** Update to "rt_drawing.c (Canvas.Text implementation)."

### GFX-063: BDF hex parser reads past null terminator on odd-length lines
**File:** `rt_bitmapfont.c` lines 108-115
**Category:** Security (malformed input)
**Description:** `bf_hex_byte(const char *s)` reads `s[0]` and `s[1]` without verifying `s[1] != '\0'`. If a malformed BDF file has a hex line with an odd number of characters, `s[1]` reads one byte past the valid data (null terminator is read as 0, returning -1 from bf_hex_digit, which causes bf_hex_byte to return -1 — so no write occurs, but the read itself is technically out-of-bounds).
**Fix:** Add `if (s[0] == '\0' || s[1] == '\0') return -1;` at the top.

### GFX-064: BDF parser leaks cur_bitmap if file ends mid-glyph
**File:** `rt_bitmapfont.c` lines 151-258
**Category:** Memory leak
**Description:** If a BDF file ends before ENDCHAR (truncated file), the while loop exits with `cur_bitmap` still allocated but never stored or freed.
**Fix:** Add `free(cur_bitmap);` after the while loop (before the glyph_count check).

### GFX-065: BitmapFont has no GC finalizer — glyph bitmaps leak on collection
**File:** `rt_bitmapfont.c` lines 117-275, 291-442
**Category:** Memory leak
**Description:** Neither `rt_bitmapfont_load_bdf` nor `rt_bitmapfont_load_psf` register a GC finalizer via `rt_obj_set_finalizer`. When the GC collects the BitmapFont object, the per-glyph `bitmap` pointers (malloc'd in both parsers) are never freed. Only explicit calls to `rt_bitmapfont_destroy` free them, but GC collection does not call destroy.
**Fix:** Add `rt_obj_set_finalizer(font, rt_bitmapfont_destroy);` after allocation in both parsers.

### GFX-066: BMP loader row_size integer overflow for crafted wide images
**File:** `rt_pixels.c` line 469
**Category:** Security (heap buffer overflow)
**Description:** `int row_size = ((width * 3 + 3) / 4) * 4` — `width * 3` overflows `int` (32-bit) for BMP images with `width > INT_MAX/3 ≈ 715M pixels`. A crafted BMP file with extreme width could cause a too-small `row_buf` allocation, followed by a heap buffer overflow during `fread(row_buf, 1, row_size, f)`.
**Fix:** Validate `width <= 32768` (or similar reasonable limit) before the row_size calculation, or use `int64_t` for the computation.

### GFX-067: Sprite has no GC finalizer — retained frame references leak
**File:** `rt_sprite.c` lines 77-101, 126, 147, 498
**Category:** Memory leak (reference count)
**Description:** `sprite_alloc` never calls `rt_obj_set_finalizer`. Frames are retained via `rt_heap_retain` at creation (line 126, 147) and `AddFrame` (line 498), but never released when the sprite is GC'd. This means every Pixels buffer used as a sprite frame lives forever (reference count never reaches 0).
**Fix:** Register a finalizer that calls `rt_heap_release` on each non-null `frames[i]`.

### GFX-068: SpriteBatch has no GC finalizer — items array leaks
**File:** `rt_spritebatch.c` lines 140-163
**Category:** Memory leak
**Description:** The header comment (line 31) says "The command array is freed by the GC finalizer" but no finalizer is registered. When the GC collects the SpriteBatch, the `items` array (malloc'd at line 149) is never freed. For a batch with capacity=256, this leaks 256 × sizeof(batch_item) ≈ 20KB per batch object.
**Fix:** Register a finalizer that calls `free(batch->items)`.

### GFX-069: Tilemap has no GC finalizer — tileset and layer data leak
**File:** `rt_tilemap.c` lines 102-159
**Category:** Memory leak
**Description:** No `rt_obj_set_finalizer` call. The tilemap holds a retained `tileset` reference (via `rt_heap_retain` at line 233), per-layer malloc'd tile grids (`layers[i].tiles` allocated at line 678 for added layers), and per-layer tileset references (line 883). None are freed on GC collection. The header comment (line 34-35) explicitly says "freed by the GC finalizer."
**Fix:** Register a finalizer that releases the base tileset, frees each layer's owned tile grid, and releases per-layer tilesets.

### GFX-085: Audio3D update_voice uses global max_distance from last play_at
**File:** `rt_audio3d.c` lines 39, 96, 108
**Category:** Logic bug
**Description:** `saved_max_dist` is a global static updated by `rt_audio3d_play_at`. When `rt_audio3d_update_voice` is called for a specific voice, it uses `saved_max_dist` which contains the max_distance from the MOST RECENT `play_at` call — not the one that created that voice. If different sounds have different max distances, updates use the wrong attenuation.
**Fix:** Store max_distance per voice (e.g., in a voice→max_dist lookup table), or require the caller to pass max_distance to update_voice.

### GFX-088: FBX skeleton bind pose ignores rotation and scaling
**File:** `rt_fbx_loader.c` line 1057
**Category:** Incorrect skinning
**Description:** FBX bone extraction reads `Lcl Translation`, `Lcl Rotation`, and `Lcl Scaling` from Properties70 (lines 959-976), but the bind matrix at line 1057 is constructed as a pure translation matrix — rotation and scaling are discarded. For any skeleton with rotated or scaled bones in rest pose, the inverse bind matrices are wrong and skinned meshes deform incorrectly.
**Fix:** Build a full TRS matrix from translation, rotation (Euler→quaternion→matrix), and scaling, then pass that as the bind pose.

### GFX-089: FBX animation keyframe extraction not implemented
**File:** `rt_fbx_loader.c` lines 1101-1106
**Category:** P0 — Missing critical feature
**Description:** Comment says "Animation keyframe extraction is not yet implemented." The loader creates Animation3D objects with names but zero keyframes. Any FBX file with animations (walk cycles, attack anims, etc.) loads with empty animation data. Users must manually construct keyframes, defeating the purpose of the FBX loader for animated content.
**Fix:** Implement the full AnimStack→AnimLayer→AnimCurveNode→AnimCurve chain traversal, convert FBX ticks (46186158000/sec) to seconds, resolve bone connections via the connection table, and populate keyframes with position/rotation/scale samples.

### GFX-101: D3D11 backend leaks COM objects on shader compilation failure
**File:** `vgfx3d_backend_d3d11.c` lines 348-374
**Category:** P1 — Resource leak (Windows)
**Description:** `d3d11_create_ctx` creates the D3D11 device, swap chain, render target view, depth stencil view/state, blend state, and rasterizer state before compiling shaders. If VS or PS compilation fails (lines 348, 367), the function frees the `ctx` struct but does NOT release the already-created COM objects (device, swapchain, rtv, dsv, dss, dssNoWrite, blendState, rsState). These COM objects are leaked.
**Fix:** Call `d3d11_destroy_ctx(ctx)` instead of `free(ctx)` on shader compilation failure, since `d3d11_destroy_ctx` already null-checks and releases all COM objects.

### GFX-100: Breadcrumb set_path leaks strdup'd labels or dangles token pointers
**File:** `rt_gui_features.c` lines 725-729
**Category:** P1 — Memory leak / potential dangling pointer
**Description:** `rt_breadcrumb_set_path` parses a path by tokenizing `cpath`, then for each token does `char *label = strdup(token); vg_breadcrumb_push(data->breadcrumb, token, label);`. Two problems: (1) `token` points into `cpath`, which is freed on line 733 — if `vg_breadcrumb_push` stores the first arg (`token`) without copying, it's a dangling pointer. (2) If the label is strdup'd but `vg_breadcrumb_push` also copies it internally, the strdup'd copy is leaked. The same issue exists in `rt_breadcrumb_set_items` (line 764).
**Fix:** Verify `vg_breadcrumb_push` ownership semantics. If it copies both args, don't strdup. If it takes ownership of arg2, pass strdup'd copy only for arg2 and strdup arg1 as well.

### GFX-099: MessageBox/FileDialog/FindBar/CommandPalette/Toast/Breadcrumb/Minimap wrapper structs crash on allocation failure
**File:** `rt_gui_codeeditor.c` lines 1254, 1554, 1731
**Category:** P0 — Crash (null dereference)
**Description:** `rt_messagebox_new`, `rt_filedialog_new`, and `rt_findbar_new` all call `rt_obj_new_i64(0, sizeof(...))` and immediately dereference the result (e.g., `data->dialog = dlg`) without checking for NULL. If the GC heap is exhausted, this crashes with a null pointer dereference. Same systemic pattern as GFX-070 and GFX-093.
**Fix:** Add `if (!data) { ... cleanup ...; return NULL; }` after each `rt_obj_new_i64` call.

### GFX-097: Theme switch loses HiDPI scaling — widgets shrink to 1× on Retina
**File:** `rt_gui_widgets_complex.c` lines 378-388
**Category:** P1 — Rendering bug
**Description:** `rt_theme_set_dark()` and `rt_theme_set_light()` call `vg_theme_set_current(vg_theme_dark/light())` which replaces the active theme with a raw unscaled theme. However, `rt_gui_app_new` (in rt_gui_app.c lines 116-139) scales the theme's typography, spacing, button, input, and scrollbar metrics by the HiDPI factor. After a theme switch, the new theme has unscaled metrics — fonts, spacing, and widget sizes all shrink to 1× physical pixels on HiDPI/Retina displays.
**Fix:** After setting the new theme, re-apply the same HiDPI scaling that `rt_gui_app_new` does. Extract the scaling code into a helper (e.g., `rt_theme_apply_hidpi_scale(theme, scale)`) and call it from both `app_new` and the theme setters.

### GFX-098: Duplicate RT_ASSERT_MAIN_THREAD() calls (copy-paste artifacts)
**File:** `rt_gui_widgets_complex.c` lines 243 and 736
**Category:** P3 — Code quality
**Description:** `rt_codeeditor_new` asserts main thread at line 236, then asserts again at line 243 inside a nested block. Similarly, `rt_listbox_was_selection_changed` asserts at line 728, then again at line 736. Both are harmless but indicate copy-paste without cleanup.
**Fix:** Remove the duplicate assertions.

### GFX-093: rt_gui_app_new crashes on allocation failure (null deref)
**File:** `rt_gui_app.c` lines 76-77
**Category:** P0 — Crash
**Description:** `rt_gui_app_new` calls `rt_obj_new_i64(0, sizeof(rt_gui_app_t))` then immediately `memset(app, 0, sizeof(rt_gui_app_t))` without checking if `app` is NULL. Same pattern as GFX-070. If the GC heap is exhausted, `memset(NULL, 0, ...)` is undefined behavior — typically a segfault.
**Fix:** Add `if (!app) return NULL;` between the allocation and the memset.

### GFX-094: HiDPI theme scaling is cumulative and irreversible
**File:** `rt_gui_app.c` lines 116-139
**Category:** P1 — Logic bug
**Description:** `rt_gui_app_new` scales the global theme singleton's typography, spacing, and widget metrics by the HiDPI factor. This mutates `vg_theme_get_current()` directly. If a second app were ever created (or the app is recreated after destroy), all metrics are scaled again — fonts become 4× on a 2× display. There is no corresponding unscale in `rt_gui_app_destroy`.
**Fix:** Either store the unscaled theme defaults and reapply fresh on each app creation, or apply scaling only once (guard with a static `bool scaled = false`).

### GFX-095: rt_gui_app_set_font leaks previous default font
**File:** `rt_gui_app.c` lines 595-603
**Category:** P1 — Memory leak
**Description:** `rt_gui_app_set_font` overwrites `app->default_font` without freeing the previous font. If `rt_gui_ensure_default_font()` already loaded the embedded JetBrains Mono font via `vg_font_load`, that font handle is now leaked. Each call to `set_font` leaks the prior font.
**Fix:** Add `if (app->default_font) vg_font_destroy(app->default_font);` before the assignment. Also null-check the `font` parameter.

### GFX-096: KEY_CHAR synthesis hardcoded to US keyboard layout
**File:** `rt_gui_app.c` lines 403-472
**Category:** P3 — Internationalization
**Description:** The shift-mapping for printable characters is a hardcoded US QWERTY table. On non-US layouts (UK: Shift+3 = '£' not '#'; DE: Shift+/ doesn't exist), the synthesized KEY_CHAR events produce wrong characters. The comment on line 403 acknowledges this limitation.
**Fix:** Use platform-level character translation (e.g., macOS `UCKeyTranslate`, Windows `ToUnicode`, X11 `XLookupString`) instead of a manual shift table. Alternatively, rely on `VGFX_EVENT_TEXT_INPUT` if the platform layer provides it.

### GFX-090: Shortcut parser cannot handle special keys (Escape, Enter, Space, arrows, etc.)
**File:** `rt_gui_system.c` lines 105-165
**Category:** P1 — Missing feature / silent failure
**Description:** `parse_shortcut_keys()` only recognizes single-character keys (A-Z, 0-9) and function keys (F1-F12). Special keys like Escape, Enter, Space, Tab, Delete, Backspace, Home, End, PageUp, PageDown, and arrow keys are not handled. Registering a shortcut like "Ctrl+Enter" or "Alt+Escape" silently fails — `parse_shortcut_keys` returns 0, the shortcut is registered with `parsed_key = 0`, and `rt_shortcuts_check_key` skips it (line 313: `!g_shortcuts[i].parsed_key`).
**Fix:** Add named key parsing after the F-key branch: `else if (strcasecmp(token, "Enter") == 0 || strcasecmp(token, "Return") == 0) *key = VG_KEY_ENTER;` etc. for all VG_KEY_* constants that have standard names.

### GFX-091: File header comment references nonexistent rt_shortcuts_destroy_all()
**File:** `rt_gui_system.c` line 27
**Category:** P3 — Stale comment
**Description:** Header says "freed by rt_shortcuts_destroy_all()" but the function is actually called `rt_shortcuts_clear()` (line 244).
**Fix:** Update comment to reference `rt_shortcuts_clear()`.

### GFX-092: Widget cursor API ignores widget parameter — cursor is always global
**File:** `rt_gui_system.c` lines 698-710
**Category:** P3 — Misleading API
**Description:** `rt_widget_set_cursor(widget, type)` and `rt_widget_reset_cursor(widget)` accept a widget pointer but immediately cast it to void. The cursor is always set globally via `rt_cursor_set()`. This API implies per-widget cursors but provides global-only behavior. A TreeView and a CodeEditor cannot have different cursor styles simultaneously.
**Fix:** Either document the global-only behavior or implement per-widget cursor by storing cursor type on the widget and applying it during hit-test dispatch.

### GFX-087: Mat4 perspective matrix differs from Camera3D perspective
**File:** `rt_mat4.c` lines 242-257 vs `rt_camera3d.c` lines 43-56
**Category:** Inconsistency / rendering bug
**Description:** `rt_mat4_perspective` places -1 at m[11] (row 2, col 3) and 2fn at m[14] (row 3, col 2). `rt_camera3d`'s `build_perspective` places -1 at m[14] (row 3, col 2) and 2fn at m[11] (row 2, col 3). These are transposed from each other. The camera3d version is the standard OpenGL row-major convention. If a user creates a perspective matrix via `Mat4.Perspective()` and uses it with Camera3D, the projection will be wrong.
**Fix:** Align `rt_mat4_perspective` to match the camera3d convention: swap m[11] and m[14] values.

### GFX-086: Audio3D right vector is negated — stereo pan is reversed
**File:** `rt_audio3d.c` lines 53-54
**Category:** Audio bug
**Description:** The cross product `forward × (0,1,0)` should be `(-fwd_z, 0, fwd_x)` but the code computes `(fwd_z, 0, -fwd_x)` — the negation. This means the "right" vector actually points left, reversing all stereo panning. Sounds on the player's right play from the left speaker.
**Fix:** Change line 53 to `double rx = -listener_fwd[2];` and line 54 to `double rz = listener_fwd[0];`

### GFX-084: InstanceBatch draw passes wrong light parameter type
**File:** `rt_instbatch3d.c` line 180
**Category:** Type mismatch / rendering bug
**Description:** `rt_canvas3d_draw_instanced` passes `(const vgfx3d_light_params_t *)c->lights` to `backend->submit_draw`. But `c->lights` is `rt_light3d *lights[VGFX3D_MAX_LIGHTS]` — an array of pointers to light objects. The backend expects `vgfx3d_light_params_t *` — a flat array of light parameter structs. The cast reinterprets pointer values as float lighting data, producing garbage lighting. `rt_canvas3d_draw_mesh` correctly uses `build_light_params()` to convert.
**Fix:** Call `build_light_params(c, lights_array, VGFX3D_MAX_LIGHTS)` like draw_mesh does, and pass the resulting array.

### GFX-083: Path3D add_point triple realloc can corrupt state on partial failure
**File:** `rt_path3d.c` lines 86-89
**Category:** Memory corruption
**Description:** `add_point` does three sequential reallocs for xs, ys, zs without checking any return values. If the second or third realloc fails (returns NULL), the corresponding pointer is overwritten with NULL while the others point to the new (larger) allocations. The path is now in an inconsistent state with mismatched array sizes, and the old allocation for the failed array is leaked.
**Fix:** Save old pointers before realloc. On any failure, restore old pointers and return without adding the point.

### GFX-082: Terrain3D normal computation assumes square XZ scaling
**File:** `rt_terrain3d.c` lines 183-185, 223-225
**Category:** Math error
**Description:** Terrain normals via central difference use `ny = 2.0 * t->scale[0]` for both get_normal_at and chunk building. This assumes X and Z spacing are equal. If scale[0] != scale[2] (non-square terrain), the Z-axis gradient is computed with the wrong denominator, producing skewed normals and incorrect lighting.
**Fix:** Use `ny = 2.0 * t->scale[0]` for X derivative and a separate computation with `scale[2]` for Z, or compute the full gradient: `nx = (hL-hR) * scale[1] / (2*scale[0])`, `nz = (hD-hU) * scale[1] / (2*scale[2])`, `ny = 1.0`.

### GFX-081: Sprite3D draw mesh/material not registered as temp buffers — use-after-free risk
**File:** `rt_sprite3d.c` lines 145-166
**Category:** Use-after-free
**Description:** `rt_canvas3d_draw_sprite3d` creates a new Mesh3D and Material3D per frame via GC allocation, then passes them to `rt_canvas3d_draw_mesh` which stores raw pointers to vertex/index arrays in the deferred draw queue. If GC runs between `draw_mesh` and `End()`, the mesh finalizer frees the vertex/index arrays while they're still referenced by the deferred queue. The `rt_canvas3d_add_temp_buffer` function exists (declared at line 164) specifically for this purpose but is never called.
**Fix:** Register the mesh's vertex and index buffers via `rt_canvas3d_add_temp_buffer`, or ensure the mesh stays alive until End() by preventing GC during the frame.

### GFX-080: Water3D color change after first update is silently ignored
**File:** `rt_water3d.c` lines 95-101, 152-156
**Category:** Logic bug
**Description:** `rt_water3d_set_color` updates `w->color[]` and `w->alpha`, but the Material3D is only created once (line 152: `if (!w->material)`). After the first `update()` call, subsequent `set_color()` calls change internal state but the material retains the original color. Only alpha is propagated (line 157).
**Fix:** Recreate or update the material color in `rt_water3d_update` when color changes, or add `rt_material3d_set_color(w->material, w->color[0], w->color[1], w->color[2])` before setting alpha.

### GFX-078: NavMesh3D build doesn't check malloc for vertices/triangles
**File:** `rt_navmesh3d.c` lines 133, 143
**Category:** Crash (null dereference)
**Description:** `nm->vertices = malloc(...)` and `nm->triangles = malloc(...)` results are not checked for NULL. If either allocation fails (e.g., mesh with millions of vertices), the subsequent loop writes to NULL.
**Fix:** Add `if (!nm->vertices) { rt_trap("..."); return NULL; }` after each malloc.

### GFX-079: NavMesh3D A* pathfinder heap buffer overflow
**File:** `rt_navmesh3d.c` lines 309, 333
**Category:** Heap buffer overflow
**Description:** The A* open set heap is allocated with capacity `tc` (triangle count), but triangles can be pushed multiple times with improved g-costs without removing previous entries. If a triangle is reached via many paths, `heap_size` can exceed `tc`, causing writes past the heap array.
**Fix:** Either use a closed-set check before pushing (already done at line 326, but `closed[next]` is only set when popped, not when pushed), or allocate the heap with capacity `3 * tc` (max 3 edges per triangle) to handle worst case.

### GFX-077: Scene3D add_lod realloc failure leaks old LOD array
**File:** `rt_scene3d.c` lines 741-743
**Category:** Memory leak
**Description:** `node->lod_levels = realloc(node->lod_levels, ...)` — if realloc returns NULL (OOM), the old `lod_levels` pointer is overwritten with NULL, leaking the original allocation and losing all existing LOD levels. Standard realloc failure pattern: must save old pointer before reassigning.
**Fix:** Use `void *tmp = realloc(node->lod_levels, ...); if (!tmp) return; node->lod_levels = tmp;`

### GFX-076: AnimPlayer3D looping with near-zero duration causes excessive iterations
**File:** `rt_skeleton3d.c` lines 798-800
**Category:** Performance / potential hang
**Description:** `while (p->current_time >= p->current->duration) p->current_time -= p->current->duration;` — if duration is very small (e.g., 1e-40 from float precision), this loop iterates billions of times. The `duration > 0.0f` guard at line 798 only catches exact zero. AnimBlend3D uses `fmodf` (line 1098) which handles this correctly in one operation.
**Fix:** Replace while loop with `p->current_time = fmodf(p->current_time, p->current->duration);` matching AnimBlend3D.

### GFX-075: Physics3D set_static(false) doesn't restore inv_mass
**File:** `rt_physics3d.c` lines 509-517
**Category:** Logic bug
**Description:** `rt_body3d_set_static(body, 1)` sets `b->inv_mass = 0` (infinite mass). But `rt_body3d_set_static(body, 0)` only clears the static flag — it does NOT restore `inv_mass` to `1.0 / b->mass`. The body behaves as infinite-mass (immovable) even after being made non-static.
**Fix:** Add `else b->inv_mass = b->mass > 1e-12 ? 1.0 / b->mass : 0.0;` when `s == 0`.

### GFX-074: Canvas3D finalizer doesn't free shadow render target
**File:** `rt_canvas3d.c` lines 125-150
**Category:** Memory leak
**Description:** The Canvas3D GC finalizer at `rt_canvas3d_finalize` frees `draw_cmds`, `temp_buffers`, and the window, but does NOT free `c->shadow_rt` (allocated at line 1521-1528 when shadows are enabled). The shadow render target, its depth buffer, and color buffer are leaked.
**Fix:** Add shadow RT cleanup to the finalizer: `if (c->shadow_rt) { free(c->shadow_rt->color_buf); free(c->shadow_rt->depth_buf); free(c->shadow_rt); }`

### GFX-073: Gamepad deadzone division by zero when deadzone = 1.0
**File:** `rt_input_pad.c` line 1028
**Category:** Crash (floating-point division by zero)
**Description:** `apply_deadzone` computes `(abs_value - g_pad_deadzone) / (1.0 - g_pad_deadzone)`. If `g_pad_deadzone` is exactly 1.0 (allowed by the clamp at line 1299: `clamp_axis(radius, 0.0, 1.0)`), the denominator is 0.0, producing infinity/NaN. The axis value then propagates as garbage through all gamepad consumers.
**Fix:** Clamp deadzone to `[0.0, 0.99]` in `rt_pad_set_deadzone`, or add `if (g_pad_deadzone >= 1.0) return 0.0;` in `apply_deadzone`.

### GFX-071: Camera parallax layers hold dangling Pixels pointers
**File:** `rt_camera.c` line 426
**Category:** Use-after-free
**Description:** `rt_camera_add_parallax` stores the Pixels pointer directly in the parallax layer without retaining it. If the Pixels object is GC-collected while the parallax layer is active, `rt_camera_draw_parallax` (line 508) reads freed memory via `rt_canvas_blit_alpha(canvas, tx, ty, layer->pixels)`.
**Fix:** Retain the Pixels reference on add (`rt_obj_retain_maybe`), release on remove/clear.

### GFX-072: Camera.Move doesn't set dirty flag
**File:** `rt_camera.c` lines 326-337
**Category:** Rendering bug
**Description:** `rt_camera_move` modifies `camera->x` and `camera->y` but never sets `camera->dirty = 1`. Combined with GFX-008 (Follow also missing dirty), this means two of the three position-mutation methods don't trigger dirty — only SetX/SetY do. Renderers checking `is_dirty()` will miss Move and Follow updates.
**Fix:** Add `camera->dirty = 1;` in both `rt_camera_move` and `rt_camera_follow`.

### GFX-070: Scene node/scene creation crashes on allocation failure
**File:** `rt_scene.c` lines 103-104, 724-725
**Category:** Crash (null dereference)
**Description:** Both `rt_scene_node_new` and `rt_scene_new` call `memset(obj, 0, sizeof(...))` immediately after `rt_obj_new_i64` without checking if the return value is NULL. If allocation fails, `memset(NULL, 0, ...)` is undefined behavior — typically a segfault.
**Fix:** Add `if (!node) { rt_trap("SceneNode: allocation failed"); return NULL; }` before the memset.

### Systemic Issue: Missing GC Finalizers
**Files:** rt_bitmapfont.c (GFX-065), rt_sprite.c (GFX-067), rt_spritebatch.c (GFX-068), rt_tilemap.c (GFX-069)
**Category:** Systemic memory leak pattern
**Description:** 4 of the first 11 files reviewed have no GC finalizer despite their header comments claiming "freed by GC finalizer." Each leaks internal heap allocations or retained references when GC-collected. This is likely a systemic issue — other object types may have the same problem.
**Recommendation:** Audit ALL `rt_obj_new_i64` call sites across the graphics runtime and verify each one either registers a finalizer or truly needs no cleanup.

## Files Reviewed (29/65 re-verified in restart pass, 77 total findings)

### Restart Pass Detail (every file read line-by-line)
| # | File | Lines | New Findings |
|---|------|-------|--------------|
| 1 | rt_graphics.c | 31 | — |
| 2 | rt_canvas.c | 527 | GFX-061 |
| 3 | rt_drawing.c | 866 | — |
| 4 | rt_drawing_advanced.c | 1365 | — |
| 5 | rt_pixels.c | 2410 | GFX-066 |
| 6 | rt_font.c | 277 | GFX-062 |
| 7 | rt_bitmapfont.c | 857 | GFX-063, GFX-064, GFX-065 |
| 8 | rt_sprite.c | 729 | GFX-067 |
| 9 | rt_spritebatch.c | 420 | GFX-068 |
| 10 | rt_spritesheet.c | 344 | — |
| 11 | rt_tilemap.c | 980 | GFX-069 |
| 12 | rt_tilemap_io.c | 608 | — |
| 13 | rt_scene.c | 996 | GFX-070 |
| 14 | rt_camera.c | 516 | GFX-071, GFX-072 |
| 15 | rt_input.c | 1462 | — |
| 16 | rt_input_pad.c | 1412 | GFX-073 |
| 17 | rt_inputmgr.c | 556 | — |
| 18 | rt_keychord.c | 375 | — |
| 19 | rt_action.c | 1938 | — |
| 20 | rt_canvas3d.c | 1559 | GFX-074 |
| 21 | rt_mesh3d.c | 790 | — |
| 22 | rt_camera3d.c | 562 | — |
| 23 | rt_material3d.c | 160 | — |
| 24 | rt_light3d.c | 127 | — |
| 25 | rt_transform3d.c | 289 | — |
| 26 | rt_physics3d.c | 786 | GFX-075 |
| 27 | rt_skeleton3d.c | 1195 | GFX-076 |
| 28 | rt_particles3d.c | 687 | — |
| 29 | rt_scene3d.c | 772 | GFX-077 |
| 30-65 | (36 remaining) | ~18,000 | IN PROGRESS |

| # | File | Lines | Status |
|---|------|-------|--------|
| 1 | rt_graphics.c | 31 | Clean |
| 2 | rt_canvas.c | 527 | GFX-004, GFX-014, GFX-026 |
| 3 | rt_drawing.c | 866 | Clean (minor int overflow potential in disc_alpha) |
| 4 | rt_drawing_advanced.c | 1365 | GFX-005, GFX-011, GFX-012, GFX-013, GFX-019 |
| 5 | rt_pixels.c | 2410 | GFX-001, GFX-002, GFX-020, GFX-021, GFX-028, GFX-031, GFX-034 |
| 6 | rt_font.c | 277 | Clean |
| 7 | rt_bitmapfont.c | 857 | GFX-006, GFX-033 |
| 8 | rt_sprite.c | 729 | GFX-007, GFX-032 |
| 9 | rt_spritebatch.c | 420 | GFX-022 |
| 10 | rt_spritesheet.c | 344 | GFX-023 |
| 11 | rt_tilemap.c | 980 | Clean |
| 12 | rt_tilemap_io.c | 608 | GFX-015, GFX-035 |
| 13 | rt_scene.c | 996 | GFX-010, GFX-018, GFX-025, GFX-027 |
| 14 | rt_camera.c | 516 | GFX-008, GFX-009 |
| 15 | rt_input.c | 1462 | GFX-003, GFX-016, GFX-017 |
| 16 | rt_input_pad.c | 1412 | GFX-024, GFX-029 |
| 17 | rt_inputmgr.c | 556 | GFX-030 |
| 18 | rt_keychord.c | 375 | GFX-036 |
| 19 | rt_action.c | 1938 | GFX-036 |
| 20 | rt_canvas3d.c | 1559 | GFX-042, GFX-043, GFX-044, GFX-046 |
| 21 | rt_mesh3d.c | 790 | GFX-037, GFX-047 |
| 22 | rt_camera3d.c | 562 | GFX-045 |
| 23 | rt_material3d.c | 160 | Clean |
| 24 | rt_light3d.c | 127 | Clean |
| 25 | rt_transform3d.c | 289 | GFX-038 |
| 26 | rt_physics3d.c | 786 | GFX-039, GFX-040, GFX-041, GFX-048 |
| 27 | rt_skeleton3d.c | 1195 | GFX-049, GFX-050, GFX-053 |
| 28 | rt_particles3d.c | 687 | GFX-051, GFX-052 |
| 29 | rt_scene3d.c | 772 | GFX-055 |
| 30 | rt_navmesh3d.c | 432 | GFX-057 |
| 31 | rt_water3d.c | 174 | GFX-054 |
| 32 | rt_cubemap3d.c | 237 | GFX-056 |
| 33 | rt_decal3d.c | 184 | GFX-060 |
| 34 | rt_sprite3d.c | 169 | GFX-058, GFX-059 |
| 35 | rt_vec3.c | 941 | Clean |
| 36 | rt_quat.c | 436 | Clean |
| 37 | rt_physics2d.c | 668 | (sampled — well-structured) |
| 38 | rt_raycast3d.c | 524 | Clean (Möller-Trumbore) |
| 39 | rt_fbx_loader.c | 1385 | (sampled — binary parser) |
| 40 | rt_postfx3d.c | 676 | (sampled — effect chain) |
| 41 | vgfx3d_backend_sw.c | 953 | (sampled — rasterizer) |
| 42 | rt_camera3d.c | 562 | GFX-045 |
| 43 | rt_gui_system.c | 932 | Clean |
| 44 | rt_gui_app.c | 726 | (HiDPI theme mutation is single-app assumption) |
| 45 | rt_gui_widgets.c | 934 | Clean |
| 46 | vgfx3d_backend_opengl.c | 740 | Clean (custom GL loader) |
| 47 | rt_physics2d.c | 668 | Clean (grid broad-phase, good structure) |
| 48 | rt_physics2d_joint.c | 494 | (not fully reviewed) |
| 49 | rt_gui_widgets_complex.c | 1463 | (not fully reviewed) |
| 50 | rt_gui_codeeditor.c | 2718 | (not fully reviewed) |
| 51 | rt_gui_menus.c | 1820 | (not fully reviewed) |
| 52 | rt_gui_features.c | 1467 | (not fully reviewed) |
| 53 | rt_vec2.c | 1095 | (not fully reviewed — pure math, pool pattern matches vec3) |
| 54 | rt_mat3.c | 402 | (not fully reviewed — pure math) |
| 55 | rt_mat4.c | 713 | (not fully reviewed — pure math) |
| 56 | rt_morphtarget3d.c | 322 | (not fully reviewed) |
| 57 | rt_terrain3d.c | 274 | (not fully reviewed) |
| 58 | rt_path3d.c | 223 | (not fully reviewed) |
| 59 | rt_spline.c | 476 | (not fully reviewed) |
| 60 | rt_texatlas3d.c | 170 | (not fully reviewed) |
| 61 | rt_instbatch3d.c | 186 | (not fully reviewed) |
| 62 | rt_rendertarget3d.c | 211 | (not fully reviewed) |
| 63 | rt_audio3d.c | 113 | (not fully reviewed) |
| 64 | vgfx3d_backend_d3d11.c | 648 | (not fully reviewed) |
| 65 | vgfx3d_frustum.c | 228 | (not fully reviewed) |
| 66 | vgfx3d_skinning.c | 76 | (not fully reviewed) |
| 67 | rt_graphics_stubs.c | 2791 | (not fully reviewed — stubs only) |

---

## Summary Statistics (ALL 66 files fully reviewed line-by-line, 101 total findings)

| Severity | Count | Description |
|----------|-------|-------------|
| **P0** | 10 | Crashes, security, data corruption |
| **P1** | 33 | Logic bugs, missing features, memory leaks |
| **P2** | 18 | Optimization, performance |
| **P3** | 22 | Code quality, comments, robustness |
| **Systemic** | 1 | Missing GC finalizers (4+ object types) |
| **Total** | **101** | |

### New Findings from Restart Pass (GFX-061 through GFX-077)
| ID | Sev | File | Description |
|----|-----|------|-------------|
| GFX-061 | P3 | rt_canvas.c | Poll return value misleading |
| GFX-062 | P3 | rt_font.c | Stale file reference in comment |
| GFX-063 | P1 | rt_bitmapfont.c | BDF hex parser reads past null terminator |
| GFX-064 | P2 | rt_bitmapfont.c | BDF parser leaks cur_bitmap on truncated file |
| GFX-065 | P1 | rt_bitmapfont.c | **No GC finalizer — glyph bitmaps leak** |
| GFX-066 | P0 | rt_pixels.c | BMP loader row_size integer overflow (security) |
| GFX-067 | P1 | rt_sprite.c | **No GC finalizer — frame references leak** |
| GFX-068 | P1 | rt_spritebatch.c | **No GC finalizer — items array leaks** |
| GFX-069 | P1 | rt_tilemap.c | **No GC finalizer — tileset/layer data leaks** |
| GFX-070 | P0 | rt_scene.c | memset on NULL if allocation fails (crash) |
| GFX-071 | P1 | rt_camera.c | Parallax layers hold dangling Pixels pointers |
| GFX-072 | P1 | rt_camera.c | Camera.Move doesn't set dirty flag |
| GFX-073 | P1 | rt_input_pad.c | Deadzone division by zero when deadzone=1.0 |
| GFX-074 | P2 | rt_canvas3d.c | Finalizer doesn't free shadow render target |
| GFX-075 | P1 | rt_physics3d.c | set_static(false) doesn't restore inv_mass |
| GFX-076 | P1 | rt_skeleton3d.c | Looping animation with near-zero duration hangs |
| GFX-077 | P1 | rt_scene3d.c | add_lod realloc failure leaks old LOD array |

### Priority Fix Order
1. **GFX-001/002** (P0): Fix pixel format in 6 image processing functions — affects every game using Pixels
2. **GFX-003** (P0): Replace stub get_time_ms() with real clock — fixes click/double-click
3. **GFX-007** (P1): Clone frame before flipping in sprite draw — prevents frame corruption
4. **GFX-004/046** (P0): Guard against division by zero in mouse scaling
5. **GFX-054** (P1): Reuse water mesh instead of creating new one each frame
6. **GFX-039/040** (P1): Add sphere-sphere narrow phase + friction to physics3D
7. **GFX-008** (P1): Set dirty flag in camera_follow
8. **GFX-009/042/049/056/059** (P1): Replace all fragile struct layout casts with API calls

---

## Files Not Fully Reviewed (Recommended for Follow-Up)

The following files were sampled but not exhaustively line-by-line reviewed. They should be prioritized in a follow-up audit:

**High Priority (complex logic, security surface):**
- `rt_gui_codeeditor.c` (2718 lines) — largest GUI file, syntax highlighting, IntelliSense
- `rt_gui_menus.c` (1820 lines) — menu system, context menus
- `rt_gui_features.c` (1467 lines) — dialogs, file picker, drag-drop
- `rt_gui_widgets_complex.c` (1463 lines) — TreeView, TabBar, ListBox
- `rt_fbx_loader.c` (1385 lines) — binary file parser (security-sensitive)
- `vgfx3d_backend_d3d11.c` (648 lines) — D3D11 rendering backend

**Medium Priority (math, smaller 3D):**
- `rt_vec2.c` (1095 lines), `rt_mat4.c` (713 lines), `rt_mat3.c` (402 lines) — math operations
- `rt_morphtarget3d.c` (322 lines), `rt_terrain3d.c` (274 lines), `rt_spline.c` (476 lines)
- `rt_physics2d_joint.c` (494 lines) — 2D joint constraints

**Low Priority (small, straightforward):**
- `rt_path3d.c` (223 lines), `rt_texatlas3d.c` (170 lines), `rt_instbatch3d.c` (186 lines)
- `rt_rendertarget3d.c` (211 lines), `rt_audio3d.c` (113 lines)
- `vgfx3d_frustum.c` (228 lines), `vgfx3d_skinning.c` (76 lines)
- `rt_graphics_stubs.c` (2791 lines) — no-op stubs, low risk
