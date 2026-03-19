# Phase 14: Skeletal Animation & Skinning

## Goal

Support bone hierarchies, keyframe animation with SLERP interpolation (leveraging the existing `Quat.Slerp`), vertex skinning (4 bones per vertex), and animation blending/crossfade. This is the largest runtime addition and is required before FBX loading (Phase 15).

## Dependencies

- Phase 9 complete (multi-texture materials, tangent calculation)
- Existing math: `rt_quat_slerp()`, `rt_quat_to_mat4()`, `rt_vec3_lerp()` in `src/runtime/graphics/`

## Vertex Format

Uses the `bone_indices[4]` and `bone_weights[4]` fields already present in `vgfx3d_vertex_t` (80 bytes, defined in Phase 1). The `SetBoneWeights` method populates these fields per vertex — no vertex struct change is needed.

## New Files

#### Library Level (`src/lib/graphics/src/`)

**`vgfx3d_skinning.h`** (~30 LOC)
```c
// Apply skeletal skinning on CPU (software path or pre-transform for GPU upload)
void vgfx3d_skin_vertices(const vgfx3d_vertex_t *src, vgfx3d_vertex_t *dst,
                           uint32_t vertex_count,
                           const float *bone_palette,  // bone_count * 16 floats
                           int32_t bone_count);
```

**`vgfx3d_skinning.c`** (~250 LOC)
```c
void vgfx3d_skin_vertices(const vgfx3d_vertex_t *src, vgfx3d_vertex_t *dst,
                           uint32_t vertex_count,
                           const float *palette, int32_t bone_count) {
    for (uint32_t v = 0; v < vertex_count; v++) {
        float pos[3] = {0, 0, 0};
        float nrm[3] = {0, 0, 0};
        for (int b = 0; b < 4; b++) {
            float w = src[v].bone_weights[b];
            if (w < 1e-6f) continue;
            int idx = src[v].bone_indices[b];
            if (idx >= bone_count) continue;
            const float *m = &palette[idx * 16];
            // pos += w * (m * src_pos)
            for (int i = 0; i < 3; i++) {
                pos[i] += w * (m[i*4+0]*src[v].pos[0] + m[i*4+1]*src[v].pos[1] +
                               m[i*4+2]*src[v].pos[2] + m[i*4+3]);
                nrm[i] += w * (m[i*4+0]*src[v].normal[0] + m[i*4+1]*src[v].normal[1] +
                               m[i*4+2]*src[v].normal[2]);
            }
        }
        dst[v] = src[v];  // copy all attributes
        memcpy(dst[v].pos, pos, sizeof(float) * 3);
        // Normalize skinned normal
        float len = sqrtf(nrm[0]*nrm[0] + nrm[1]*nrm[1] + nrm[2]*nrm[2]);
        if (len > 1e-8f) { nrm[0] /= len; nrm[1] /= len; nrm[2] /= len; }
        memcpy(dst[v].normal, nrm, sizeof(float) * 3);
    }
}
```

#### Runtime Level (`src/runtime/graphics/`)

**`rt_skeleton3d.h` / `rt_skeleton3d.c`** (~460 LOC)

```c
#define VGFX3D_MAX_BONES 128

typedef struct {
    char name[64];
    int32_t parent_index;       // -1 for root bone
    float bind_pose_local[16];  // local transform at bind pose (row-major)
    float inverse_bind[16];     // inverse of global bind pose (for skinning)
} vgfx3d_bone_t;

typedef struct {
    void *vptr;
    vgfx3d_bone_t *bones;
    int32_t bone_count;
} rt_skeleton3d;

// Construction
void *rt_skeleton3d_new(void);
int64_t rt_skeleton3d_add_bone(void *skel, rt_string name, int64_t parent_index, void *bind_mat4);
// INVARIANT: Bones must be added in topological order (parent before child).
// parent_index must be < current bone count (or -1 for root).
// The bone palette computation iterates 0..N-1 and reads parent_global by index,
// so this ordering is required for correctness.
// Traps on violation: "AddBone: parent_index must be less than bone count"
// FBX stores bones in parent-first order by convention; the FBX loader (Phase 15)
// should verify this invariant during extraction.
void rt_skeleton3d_compute_inverse_bind(void *skel);  // compute inverse bind matrices

// Query
int64_t rt_skeleton3d_get_bone_count(void *skel);
int64_t rt_skeleton3d_find_bone(void *skel, rt_string name);
rt_string rt_skeleton3d_get_bone_name(void *skel, int64_t index);
void *rt_skeleton3d_get_bone_bind_pose(void *skel, int64_t index);  // returns Mat4
```

**`rt_animation3d.h` / `rt_animation3d.c`** (~550 LOC)

```c
// Keyframe: position + rotation (quaternion) + scale at a specific time
typedef struct {
    float time;          // seconds
    float position[3];
    float rotation[4];   // quaternion (x, y, z, w)
    float scale_xyz[3];
} vgfx3d_keyframe_t;

// Channel: keyframes for one bone
typedef struct {
    int32_t bone_index;
    vgfx3d_keyframe_t *keyframes;
    int32_t keyframe_count;
    int32_t keyframe_capacity;
} vgfx3d_anim_channel_t;

// Animation clip
typedef struct {
    void *vptr;
    char name[64];
    vgfx3d_anim_channel_t *channels;
    int32_t channel_count;
    int32_t channel_capacity;
    float duration;       // total duration in seconds
    int8_t looping;
} rt_animation3d;

// AnimationPlayer — drives playback, computes bone palette
typedef struct {
    void *vptr;
    rt_skeleton3d *skeleton;
    rt_animation3d *current;
    rt_animation3d *crossfade_from;
    float current_time;
    float crossfade_time;
    float crossfade_duration;
    float speed;              // playback speed multiplier (default 1.0)
    int8_t playing;
    // Output: computed bone palette for skinning
    float *bone_palette;      // bone_count * 16 floats (world * inverse_bind per bone)
    float *local_transforms;  // bone_count * 16 floats (workspace)
} rt_anim_player3d;

// Animation3D API
void *rt_animation3d_new(rt_string name, double duration);
void  rt_animation3d_add_keyframe(void *anim, int64_t bone_index, double time,
                                   void *position, void *rotation, void *scale);
void  rt_animation3d_set_looping(void *anim, int8_t loop);
int8_t rt_animation3d_get_looping(void *anim);
double rt_animation3d_get_duration(void *anim);
rt_string rt_animation3d_get_name(void *anim);

// AnimationPlayer3D API
void *rt_anim_player3d_new(void *skeleton);
void  rt_anim_player3d_play(void *player, void *animation);
void  rt_anim_player3d_crossfade(void *player, void *animation, double duration);
void  rt_anim_player3d_stop(void *player);
void  rt_anim_player3d_update(void *player, double delta_time);
void  rt_anim_player3d_set_speed(void *player, double speed);
double rt_anim_player3d_get_speed(void *player);
int8_t rt_anim_player3d_is_playing(void *player);
double rt_anim_player3d_get_time(void *player);
void  rt_anim_player3d_set_time(void *player, double time);
```

## Keyframe Sampling Algorithm

For each bone channel at time `t`:

```c
// 1. Find bracketing keyframes
int k0 = 0, k1 = 0;
for (int i = 0; i < channel->keyframe_count - 1; i++) {
    if (channel->keyframes[i + 1].time >= t) { k0 = i; k1 = i + 1; break; }
}

// 2. Compute interpolation factor
float t0 = channel->keyframes[k0].time;
float t1 = channel->keyframes[k1].time;
float alpha = (t - t0) / (t1 - t0);  // 0..1

// 3. Interpolate
// Position: linear interpolation
float pos[3];
for (int i = 0; i < 3; i++)
    pos[i] = lerp(key0->position[i], key1->position[i], alpha);

// Rotation: SLERP (using existing rt_quat_slerp)
// Convert float[4] to ViperQuat, call slerp, convert back
ViperQuat q0 = {key0->rotation[0], key0->rotation[1], key0->rotation[2], key0->rotation[3]};
ViperQuat q1 = {key1->rotation[0], key1->rotation[1], key1->rotation[2], key1->rotation[3]};
ViperQuat qr = quat_slerp_internal(q0, q1, alpha);  // reuse SLERP algorithm

// Scale: linear interpolation
float scl[3];
for (int i = 0; i < 3; i++)
    scl[i] = lerp(key0->scale_xyz[i], key1->scale_xyz[i], alpha);

// 4. Build local bone transform from interpolated TRS
float local[16];
build_trs_matrix_float(pos, qr, scl, local);
```

## Bone Palette Computation

After sampling all channels:

```c
void compute_bone_palette(rt_anim_player3d *player) {
    rt_skeleton3d *skel = player->skeleton;

    // 1. Start with bind pose for any un-animated bones
    for (int i = 0; i < skel->bone_count; i++)
        memcpy(&player->local_transforms[i*16], skel->bones[i].bind_pose_local, 64);

    // 2. Override with animated transforms
    if (player->current) {
        for (int c = 0; c < player->current->channel_count; c++) {
            int bone = player->current->channels[c].bone_index;
            sample_channel(&player->current->channels[c], player->current_time,
                           &player->local_transforms[bone * 16]);
        }
    }

    // 3. Compute global transforms (multiply up hierarchy)
    for (int i = 0; i < skel->bone_count; i++) {
        float global[16];
        if (skel->bones[i].parent_index >= 0) {
            // global = parent_global * local
            mat4_mul_float(&player->bone_palette[skel->bones[i].parent_index * 16],
                           &player->local_transforms[i * 16], global);
        } else {
            memcpy(global, &player->local_transforms[i * 16], 64);
        }
        // 4. Apply inverse bind pose: palette[i] = global * inverse_bind[i]
        mat4_mul_float(global, skel->bones[i].inverse_bind,
                       &player->bone_palette[i * 16]);
    }
}
```

## Crossfade

When `crossfade(newAnim, duration)` is called:
- Store current animation as `crossfade_from` with its current time
- Set `current = newAnim`, reset `current_time = 0`
- During `update()`, sample both animations, blend results per-bone:
  `blended = lerp(from_local, to_local, crossfade_time / crossfade_duration)`
- For rotation: `blended_rot = slerp(from_rot, to_rot, factor)`
- When crossfade completes: clear `crossfade_from`

## Canvas3D Extension

```c
// Draw a skinned mesh with animation
void rt_canvas3d_draw_mesh_skinned(void *canvas, void *mesh, void *transform,
                                    void *material, void *anim_player);
```

This calls `vgfx3d_skin_vertices()` on the CPU path (software renderer), then submits the skinned vertices to the normal draw pipeline. On GPU path, it uploads the bone palette as a uniform buffer and the vertex shader performs skinning.

## Mesh3D Extensions

```c
void rt_mesh3d_set_skeleton(void *mesh, void *skeleton);
void rt_mesh3d_set_bone_weights(void *mesh, int64_t vertex_index,
                                 int64_t b0, double w0, int64_t b1, double w1,
                                 int64_t b2, double w2, int64_t b3, double w3);
```

## Shader Changes (All 3 Languages)

**Vertex shader additions:**

```
// New attributes
in uint4  aBoneIndices;    // 4 bone indices (0-127)
in float4 aBoneWeights;    // 4 blend weights

// New uniform
uniform mat4 bones[128];   // bone palette
uniform bool skinned;      // is this a skinned mesh?

// Skinning computation
if (skinned) {
    mat4 skinMatrix = bones[aBoneIndices.x] * aBoneWeights.x
                    + bones[aBoneIndices.y] * aBoneWeights.y
                    + bones[aBoneIndices.z] * aBoneWeights.z
                    + bones[aBoneIndices.w] * aBoneWeights.w;
    float4 skinnedPos = skinMatrix * float4(aPosition, 1.0);
    float3 skinnedNormal = (skinMatrix * float4(aNormal, 0.0)).xyz;
    // Use skinnedPos/skinnedNormal instead of raw attributes
}
```

## GC Finalizers

```c
static void rt_skeleton3d_finalize(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    free(s->bones); s->bones = NULL;
}

static void rt_animation3d_finalize(void *obj) {
    rt_animation3d *a = (rt_animation3d *)obj;
    for (int i = 0; i < a->channel_count; i++)
        free(a->channels[i].keyframes);
    free(a->channels); a->channels = NULL;
}

static void rt_anim_player3d_finalize(void *obj) {
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    free(p->bone_palette); p->bone_palette = NULL;
    free(p->local_transforms); p->local_transforms = NULL;
}
```

## runtime.def Entries

```c
// Skeleton3D
RT_FUNC(Skeleton3DNew,               rt_skeleton3d_new,               "Viper.Graphics3D.Skeleton3D.New",               "obj()")
RT_FUNC(Skeleton3DAddBone,           rt_skeleton3d_add_bone,          "Viper.Graphics3D.Skeleton3D.AddBone",           "i64(obj,str,i64,obj)")
RT_FUNC(Skeleton3DComputeInverseBind,rt_skeleton3d_compute_inverse_bind,"Viper.Graphics3D.Skeleton3D.ComputeInverseBind","void(obj)")
RT_FUNC(Skeleton3DBoneCount,         rt_skeleton3d_get_bone_count,    "Viper.Graphics3D.Skeleton3D.get_BoneCount",     "i64(obj)")
RT_FUNC(Skeleton3DFindBone,          rt_skeleton3d_find_bone,         "Viper.Graphics3D.Skeleton3D.FindBone",          "i64(obj,str)")
RT_FUNC(Skeleton3DGetBoneName,       rt_skeleton3d_get_bone_name,     "Viper.Graphics3D.Skeleton3D.GetBoneName",       "str(obj,i64)")

RT_CLASS_BEGIN("Viper.Graphics3D.Skeleton3D", Skeleton3D, "obj", Skeleton3DNew)
    RT_PROP("BoneCount", "i64", Skeleton3DBoneCount, none)
    RT_METHOD("AddBone", "i64(str,i64,obj)", Skeleton3DAddBone)
    RT_METHOD("ComputeInverseBind", "void()", Skeleton3DComputeInverseBind)
    RT_METHOD("FindBone", "i64(str)", Skeleton3DFindBone)
    RT_METHOD("GetBoneName", "str(i64)", Skeleton3DGetBoneName)
RT_CLASS_END()

// Animation3D
RT_FUNC(Animation3DNew,         rt_animation3d_new,         "Viper.Graphics3D.Animation3D.New",         "obj(str,f64)")
RT_FUNC(Animation3DAddKeyframe, rt_animation3d_add_keyframe,"Viper.Graphics3D.Animation3D.AddKeyframe", "void(obj,i64,f64,obj,obj,obj)")
RT_FUNC(Animation3DSetLooping,  rt_animation3d_set_looping, "Viper.Graphics3D.Animation3D.set_Looping", "void(obj,i1)")
RT_FUNC(Animation3DGetLooping,  rt_animation3d_get_looping, "Viper.Graphics3D.Animation3D.get_Looping", "i1(obj)")
RT_FUNC(Animation3DGetDuration, rt_animation3d_get_duration,"Viper.Graphics3D.Animation3D.get_Duration","f64(obj)")
RT_FUNC(Animation3DGetName,     rt_animation3d_get_name,    "Viper.Graphics3D.Animation3D.get_Name",    "str(obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.Animation3D", Animation3D, "obj", Animation3DNew)
    RT_PROP("Looping", "i1", Animation3DGetLooping, Animation3DSetLooping)
    RT_PROP("Duration", "f64", Animation3DGetDuration, none)
    RT_PROP("Name", "str", Animation3DGetName, none)
    RT_METHOD("AddKeyframe", "void(i64,f64,obj,obj,obj)", Animation3DAddKeyframe)
RT_CLASS_END()

// AnimationPlayer3D
RT_FUNC(AnimPlayer3DNew,       rt_anim_player3d_new,       "Viper.Graphics3D.AnimPlayer3D.New",       "obj(obj)")
RT_FUNC(AnimPlayer3DPlay,      rt_anim_player3d_play,      "Viper.Graphics3D.AnimPlayer3D.Play",      "void(obj,obj)")
RT_FUNC(AnimPlayer3DCrossfade, rt_anim_player3d_crossfade, "Viper.Graphics3D.AnimPlayer3D.Crossfade", "void(obj,obj,f64)")
RT_FUNC(AnimPlayer3DStop,      rt_anim_player3d_stop,      "Viper.Graphics3D.AnimPlayer3D.Stop",      "void(obj)")
RT_FUNC(AnimPlayer3DUpdate,    rt_anim_player3d_update,    "Viper.Graphics3D.AnimPlayer3D.Update",    "void(obj,f64)")
RT_FUNC(AnimPlayer3DSetSpeed,  rt_anim_player3d_set_speed, "Viper.Graphics3D.AnimPlayer3D.set_Speed", "void(obj,f64)")
RT_FUNC(AnimPlayer3DGetSpeed,  rt_anim_player3d_get_speed, "Viper.Graphics3D.AnimPlayer3D.get_Speed", "f64(obj)")
RT_FUNC(AnimPlayer3DIsPlaying, rt_anim_player3d_is_playing,"Viper.Graphics3D.AnimPlayer3D.get_IsPlaying","i1(obj)")
RT_FUNC(AnimPlayer3DGetTime,   rt_anim_player3d_get_time,  "Viper.Graphics3D.AnimPlayer3D.get_Time",  "f64(obj)")
RT_FUNC(AnimPlayer3DSetTime,   rt_anim_player3d_set_time,  "Viper.Graphics3D.AnimPlayer3D.set_Time",  "void(obj,f64)")
RT_FUNC(AnimPlayer3DGetBoneMatrix, rt_anim_player3d_get_bone_matrix, "Viper.Graphics3D.AnimPlayer3D.GetBoneMatrix", "obj(obj,i64)")

RT_CLASS_BEGIN("Viper.Graphics3D.AnimPlayer3D", AnimPlayer3D, "obj", AnimPlayer3DNew)
    RT_PROP("Speed", "f64", AnimPlayer3DGetSpeed, AnimPlayer3DSetSpeed)
    RT_PROP("IsPlaying", "i1", AnimPlayer3DIsPlaying, none)
    RT_PROP("Time", "f64", AnimPlayer3DGetTime, AnimPlayer3DSetTime)
    RT_METHOD("Play", "void(obj)", AnimPlayer3DPlay)
    RT_METHOD("Crossfade", "void(obj,f64)", AnimPlayer3DCrossfade)
    RT_METHOD("Stop", "void()", AnimPlayer3DStop)
    RT_METHOD("Update", "void(f64)", AnimPlayer3DUpdate)
    RT_METHOD("GetBoneMatrix", "obj(i64)", AnimPlayer3DGetBoneMatrix)
RT_CLASS_END()

// Mesh3D extensions (add to existing Mesh3D class)
RT_FUNC(Mesh3DSetSkeleton,    rt_mesh3d_set_skeleton,    "Viper.Graphics3D.Mesh3D.SetSkeleton",    "void(obj,obj)")
RT_FUNC(Mesh3DSetBoneWeights,  rt_mesh3d_set_bone_weights,"Viper.Graphics3D.Mesh3D.SetBoneWeights", "void(obj,i64,i64,f64,i64,f64,i64,f64,i64,f64)")

// Canvas3D extension (add to existing Canvas3D class)
RT_FUNC(Canvas3DDrawMeshSkinned,rt_canvas3d_draw_mesh_skinned,"Viper.Graphics3D.Canvas3D.DrawMeshSkinned","void(obj,obj,obj,obj,obj)")
```

## Stubs

Skeleton3D.New, Animation3D.New, AnimPlayer3D.New trap. All methods are no-ops. Getters return 0/NULL/false.

## Tests (20)

| Test | Description |
|------|-------------|
| Single bone rotation | 1 bone, rotate 90° → vertices transform correctly |
| Two-bone chain | Parent + child bone, verify child follows parent |
| Identity bind pose | All bones identity → no deformation |
| SLERP at t=0.0 | Returns first keyframe exactly |
| SLERP at t=1.0 | Returns second keyframe exactly |
| SLERP at t=0.5 | Midpoint rotation correct (verify against Quat.Slerp) |
| Position interpolation | Linear position between keyframes |
| Scale interpolation | Linear scale between keyframes |
| Crossfade blend | Two animations → blended result |
| Crossfade completion | After duration, only new animation plays |
| Speed multiplier | Speed 2.0 → animation plays twice as fast |
| Loop wrap-around | Looping animation wraps past duration |
| Non-looping stop | Non-looping animation stops at end |
| CPU skinning | 4-bone vertex, verify weighted sum |
| Weight normalization | Weights not summing to 1.0 → still correct (normalize internally) |
| Zero weight | Bone with weight 0.0 → no contribution |
| 128-bone limit | Add 128 bones → works; 129th → trap |
| Bone palette output | Verify palette = global * inverse_bind |
| GPU vs CPU parity | Same skeleton, compare skinned positions |
| Find bone by name | FindBone("LeftArm") returns correct index |
